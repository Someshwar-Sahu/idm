#include "DownloadQueue.h"
#include <iostream>
#include <algorithm>
#include <thread>
#include <chrono>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>
#include <powrprof.h>
#ifdef PlaySound
#undef PlaySound
#endif
#endif

using namespace std;

DownloadQueue::DownloadQueue(const string &queue_name, int max_concurrent_tasks): m_queue_name(queue_name), m_max_concurrent_tasks(max_concurrent_tasks){
    start_scheduler();
}

DownloadQueue::~DownloadQueue() {
    stop_scheduler();
    stop();
}

void DownloadQueue::add_task(shared_ptr<DownloadTask> task){
    if(!task) return;
    lock_guard<mutex> lock(m_mutex);
    m_tasks.push_back(task);
}

bool DownloadQueue::remove_task(const string &destination_filepath){
    lock_guard<mutex> lock(m_mutex);
    for(auto iter = m_tasks.begin(); iter != m_tasks.end(); ++iter){
        if(*iter && ((*iter)->get_destination_filepath() == destination_filepath || (*iter)->get_url() == destination_filepath)){
            if((*iter)->get_status() == DownloadStatus::Downloading){
                (*iter)->pause();
            }
            m_tasks.erase(iter);
            return true;
        }
    }
    return false;
}

bool DownloadQueue::move_task_up(const string &destination_filepath){
    lock_guard<mutex> lock(m_mutex);
    for(size_t i = 1; i < m_tasks.size(); ++i){
        if(m_tasks[i] && (m_tasks[i]->get_destination_filepath() == destination_filepath || m_tasks[i]->get_url() == destination_filepath)){
            std::swap(m_tasks[i], m_tasks[i - 1]);
            return true;
        }
    }
    return false;
}

bool DownloadQueue::move_task_down(const string &destination_filepath){
    lock_guard<mutex> lock(m_mutex);
    for(size_t i = 0; i + 1 < m_tasks.size(); ++i){
        if(m_tasks[i] && (m_tasks[i]->get_destination_filepath() == destination_filepath || m_tasks[i]->get_url() == destination_filepath)){
            std::swap(m_tasks[i], m_tasks[i + 1]);
            return true;
        }
    }
    return false;
}

void DownloadQueue::start(QueueCompletionCallback completion_callback){
    if (completion_callback) {
        m_completion_callback = completion_callback;
    }

    bool was_running = (m_state == QueueState::Running);
    m_stop_thread = false;
    m_state = QueueState::Running;

    if (!was_running || !m_queue_thread.joinable()) {
        if (m_queue_thread.joinable()) {
            m_queue_thread.join();
        }
        m_queue_thread = thread(&DownloadQueue::process_queue_loop, this);
    }
}

void DownloadQueue::pause() {
    m_state = QueueState::Paused;
    lock_guard<mutex> lock(m_mutex);
    for(auto &t: m_tasks){
        if(t && (t->get_status() == DownloadStatus::Downloading || t->get_status() == DownloadStatus::Probing)){
            t->pause();
        }
    }
}

void DownloadQueue::stop() {
    m_stop_thread = true;
    m_state = QueueState::Stopped;

    {
        lock_guard<mutex> lock(m_mutex);
        for(auto &t: m_tasks){
            if(t&& (t->get_status() == DownloadStatus::Downloading || t->get_status() == DownloadStatus::Probing)){
                t->pause();
            }
        }
    }

    if(m_queue_thread.joinable()){
        m_queue_thread.join();
    }
}

bool DownloadQueue::resume_task(const string &destination_filepath) {
    bool found = false;
    bool need_restart = false;
    {
        lock_guard<mutex> lock(m_mutex);
        for (auto &t : m_tasks) {
            if (t && (t->get_destination_filepath() == destination_filepath || t->get_url() == destination_filepath)) {
                t->resume();
                m_dispatched_tasks.erase(t.get());
                if (m_state != QueueState::Running) {
                    m_state = QueueState::Running;
                    m_stop_thread = false;
                    need_restart = true;
                }
                found = true;
                break;
            }
        }
    }
    if (need_restart) {
        if (m_queue_thread.joinable()) {
            m_queue_thread.join();
        }
        m_queue_thread = thread(&DownloadQueue::process_queue_loop, this);
    }
    return found;
}

bool DownloadQueue::pause_task(const string &destination_filepath) {
    lock_guard<mutex> lock(m_mutex);
    for (auto &t : m_tasks) {
        if (t && (t->get_destination_filepath() == destination_filepath || t->get_url() == destination_filepath)) {
            t->pause();
            return true;
        }
    }
    return false;
}

void DownloadQueue::set_max_concurrent_tasks(int max_concurrent){
    lock_guard<mutex> lock(m_mutex);
    m_max_concurrent_tasks = max(1, max_concurrent);
}

int DownloadQueue::get_max_concurrent_tasks() const{
    lock_guard<mutex> lock(m_mutex);
    return m_max_concurrent_tasks;
}

void DownloadQueue::set_post_queue_action(PostQueueAction action){
    lock_guard<mutex> lock(m_mutex);
    m_post_action = action;
}

PostQueueAction DownloadQueue::get_post_queue_action() const {
    lock_guard<mutex> lock(m_mutex);
    return m_post_action;
}

string DownloadQueue::get_queue_name() const {
    lock_guard<mutex> lock(m_mutex);
    return m_queue_name;
}

QueueState DownloadQueue::get_state() const {
    return m_state.load();
}

vector<shared_ptr<DownloadTask>> DownloadQueue::get_tasks() const {
    lock_guard<mutex> lock(m_mutex);
    return m_tasks;
}

size_t DownloadQueue::get_task_count() const {
    lock_guard<mutex> lock(m_mutex);
    return m_tasks.size();
}

void DownloadQueue::process_queue_loop() {
    while (!m_stop_thread && m_state == QueueState::Running) {
        this_thread::sleep_for(chrono::milliseconds(200));

        PostQueueAction action_to_run = PostQueueAction::None;
        QueueCompletionCallback cb_to_run = nullptr;
        bool finished = false;

        {
            lock_guard<mutex> lock(m_mutex);
            if (m_tasks.empty()) continue;

            int active_downloads = 0;
            int runnable_count = 0;
            int completed_count = 0;
            bool all_finished = true;

            for (auto &t : m_tasks) {
                if (!t) continue;
                auto status = t->get_status();
                if (status == DownloadStatus::Downloading || status == DownloadStatus::Probing || m_dispatched_tasks.count(t.get()) > 0) {
                    active_downloads++;
                    all_finished = false;
                } else if (status == DownloadStatus::NotStarted) {
                    runnable_count++;
                    all_finished = false;
                } else if (status == DownloadStatus::Completed) {
                    completed_count++;
                } else {
                    all_finished = false;
                }
            }

            if (active_downloads < m_max_concurrent_tasks) {
                for (auto &t : m_tasks) {
                    if (!t) continue;
                    auto status = t->get_status();
                    
                    if (status == DownloadStatus::NotStarted &&
                        m_dispatched_tasks.find(t.get()) == m_dispatched_tasks.end()) {
                        
                        m_dispatched_tasks.insert(t.get());

                        // Bug 19 Fix: Dynamically allocate worker threads within global queue thread ceiling
                        if (m_max_total_threads > 0 && m_max_concurrent_tasks > 0) {
                            int allocated_threads = std::max(1, m_max_total_threads / m_max_concurrent_tasks);
                            t->set_thread_count(allocated_threads);
                        }

                        thread([this, t]() {
                            t->start(nullptr);

                            lock_guard<mutex> lk(m_mutex);
                            m_dispatched_tasks.erase(t.get());
                        }).detach();

                        active_downloads++;
                        if (active_downloads >= m_max_concurrent_tasks) break;
                    }
                }
            }

            // Post-queue completion: executes when all tasks completed or all runnable tasks completed
            if ((all_finished || (active_downloads == 0 && runnable_count == 0 && completed_count > 0)) && !m_tasks.empty()) {
                if (m_completion_callback) {
                    cb_to_run = m_completion_callback;
                    m_completion_callback = nullptr;
                    finished = true;
                }
                if (m_post_action != PostQueueAction::None) {
                    cout << "[DownloadQueue] All active tasks in '" << m_queue_name << "' have completed!" << endl;
                    m_state = QueueState::Stopped;
                    action_to_run = m_post_action;
                    finished = true;
                }
            }
        }

        if (finished) {
            execute_post_queue_action(action_to_run);
            if (cb_to_run) {
                cb_to_run(action_to_run);
            }
            if (m_state == QueueState::Stopped) {
                break;
            }
        }
    }
}

bool DownloadQueue::execute_post_queue_action(PostQueueAction action) {
    if (action == PostQueueAction::None) {
        return true;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_mock_action_executor) {
            return m_mock_action_executor(action);
        }
    }
    return execute_native_post_action(action, false);
}

void DownloadQueue::set_mock_native_action_executor(NativeActionExecutor executor) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_mock_action_executor = executor;
}

bool DownloadQueue::execute_native_post_action(PostQueueAction action, bool dry_run) {
    if (action == PostQueueAction::None) {
        return true;
    }

#ifdef _WIN32
    if (action == PostQueueAction::PlaySound) {
        if (!dry_run) {
            PlaySoundA("SystemNotification", NULL, SND_ALIAS | SND_ASYNC);
        }
        return true;
    }

    if (action == PostQueueAction::ShutdownPC) {
        if (dry_run) return true;

        HANDLE hToken;
        TOKEN_PRIVILEGES tkp;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
            LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &tkp.Privileges[0].Luid);
            tkp.PrivilegeCount = 1;
            tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, (PTOKEN_PRIVILEGES)NULL, 0);
            CloseHandle(hToken);
        }
        return ExitWindowsEx(EWX_SHUTDOWN | EWX_FORCEIFHUNG, SHTDN_REASON_MAJOR_APPLICATION | SHTDN_REASON_MINOR_OTHER) != 0;
    }

    if (action == PostQueueAction::SleepPC) {
        if (dry_run) return true;
        return SetSystemPowerState(TRUE, FALSE) != 0;
    }

    if (action == PostQueueAction::HibernatePC) {
        if (dry_run) return true;
        return SetSuspendState(TRUE, FALSE, FALSE) != 0;
    }
#elif defined(__APPLE__)
    if (action == PostQueueAction::PlaySound) {
        if (!dry_run) system("afplay /System/Library/Sounds/Glass.aiff &");
        return true;
    }
    if (action == PostQueueAction::ShutdownPC) {
        if (!dry_run) system("osascript -e 'tell app \"System Events\" to shut down'");
        return true;
    }
    if (action == PostQueueAction::SleepPC || action == PostQueueAction::HibernatePC) {
        if (!dry_run) system("pmset sleepnow");
        return true;
    }
#elif defined(__linux__)
    if (action == PostQueueAction::PlaySound) {
        if (!dry_run) std::cout << "\a" << std::flush;
        return true;
    }
    if (action == PostQueueAction::ShutdownPC) {
        if (!dry_run) system("shutdown -h now");
        return true;
    }
    if (action == PostQueueAction::SleepPC || action == PostQueueAction::HibernatePC) {
        if (!dry_run) system("systemctl suspend");
        return true;
    }
#endif
    return false;
}

void DownloadQueue::set_schedule(const QueueSchedule &sched) {
    lock_guard<mutex> lock(m_mutex);
    m_schedule = sched;
    m_last_started_min.clear();
    m_last_stopped_min.clear();
}

DownloadQueue::QueueSchedule DownloadQueue::get_schedule() const {
    lock_guard<mutex> lock(m_mutex);
    return m_schedule;
}

void DownloadQueue::start_scheduler() {
    if (m_scheduler_thread.joinable()) return;
    m_stop_scheduler = false;
    m_scheduler_thread = thread(&DownloadQueue::schedule_evaluator_loop, this);
}

void DownloadQueue::stop_scheduler() {
    m_stop_scheduler = true;
    if (m_scheduler_thread.joinable()) {
        m_scheduler_thread.join();
    }
}

void DownloadQueue::schedule_evaluator_loop() {
    while (!m_stop_scheduler) {
        this_thread::sleep_for(chrono::seconds(1));
        if (m_stop_scheduler) break;

        QueueSchedule sched;
        {
            lock_guard<mutex> lock(m_mutex);
            sched = m_schedule;
        }

        if (!sched.start_scheduled && !sched.stop_scheduled) {
            continue;
        }

        auto now = chrono::system_clock::now();
        time_t now_time = chrono::system_clock::to_time_t(now);
        tm local_tm{};
#ifdef _WIN32
        localtime_s(&local_tm, &now_time);
#else
        localtime_r(&now_time, &local_tm);
#endif

        char buf[16];
        snprintf(buf, sizeof(buf), "%02d:%02d", local_tm.tm_hour, local_tm.tm_min);
        string cur_min(buf);

        int sched_h = -1, sched_m = -1;
        if (sched.start_scheduled && !sched.start_time.empty() &&
            sscanf(sched.start_time.c_str(), "%d:%d", &sched_h, &sched_m) == 2) {
            if (local_tm.tm_hour == sched_h && local_tm.tm_min == sched_m && m_last_started_min != cur_min) {
                m_last_started_min = cur_min;
                cout << "[DownloadQueue] Scheduled start triggered for queue '" << m_queue_name << "' at " << cur_min << endl;
                {
                    lock_guard<mutex> lk(m_mutex);
                    for (auto &t : m_tasks) {
                        if (t && (t->get_status() == DownloadStatus::Paused || t->get_status() == DownloadStatus::NotStarted)) {
                            t->resume();
                            t->set_status(DownloadStatus::NotStarted);
                        }
                    }
                    m_dispatched_tasks.clear();
                }
                start();
            }
        }

        int stop_h = -1, stop_m = -1;
        if (sched.stop_scheduled && !sched.stop_time.empty() &&
            sscanf(sched.stop_time.c_str(), "%d:%d", &stop_h, &stop_m) == 2) {
            if (local_tm.tm_hour == stop_h && local_tm.tm_min == stop_m && m_last_stopped_min != cur_min) {
                m_last_stopped_min = cur_min;
                cout << "[DownloadQueue] Scheduled stop triggered for queue '" << m_queue_name << "' at " << cur_min << endl;
                pause();
            }
        }
    }
}

