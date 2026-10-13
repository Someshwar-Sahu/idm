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
}

DownloadQueue::~DownloadQueue() {
    stop();
}

void DownloadQueue::add_task(shared_ptr<DownloadTask> task){
    if(!task) return;
    lock_guard<mutex> lock(m_mutex);
    m_tasks.push_back(task);
}

bool DownloadQueue::remove_task(const string &destination_filepath){
    lock_guard<mutex> lock(m_mutex);
    auto it = remove_if(m_tasks.begin(), m_tasks.end(), [&](const shared_ptr<DownloadTask> &t) {
        return t != nullptr;
    });

    for(auto iter = m_tasks.begin(); iter != m_tasks.end(); ++iter){
        if(*iter && (*iter)->get_status() != DownloadStatus::Downloading){
            m_tasks.erase(iter);
            return true;
        }
    }
    return false;
}

void DownloadQueue::start(QueueCompletionCallback completion_callback){
    if(m_state == QueueState::Running) return;

    m_completion_callback = completion_callback;
    m_stop_thread = false;
    m_state = QueueState::Running;

    if(m_queue_thread.joinable()){
        m_queue_thread.join();
    }

    m_queue_thread = thread(&DownloadQueue::process_queue_loop, this);
}

void DownloadQueue::pause() {
    m_state = QueueState::Paused;
    lock_guard<mutex> lock(m_mutex);
    for(auto &t: m_tasks){
        if(t && t->get_status() == DownloadStatus::Downloading){
            t->pause();
        }
    }
}

void DownloadQueue::stop() {
    m_stop_thread = true;
    m_state = QueueState::Stopped;

    lock_guard<mutex> lock(m_mutex);
    for(auto &t: m_tasks){
        if(t&& (t->get_status() == DownloadStatus::Downloading || t->get_status() == DownloadStatus::Probing)){
            t->pause();
        }
    }

    if(m_queue_thread.joinable()){
        m_queue_thread.join();
    }
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
            bool all_finished = true;

            for (auto &t : m_tasks) {
                if (!t) continue;
                auto status = t->get_status();
                if (status == DownloadStatus::Downloading || status == DownloadStatus::Probing || m_dispatched_tasks.count(t.get()) > 0) {
                    active_downloads++;
                    all_finished = false;
                } else if (status == DownloadStatus::NotStarted || status == DownloadStatus::Paused) {
                    all_finished = false;
                }
            }

            if (active_downloads < m_max_concurrent_tasks) {
                for (auto &t : m_tasks) {
                    if (!t) continue;
                    auto status = t->get_status();
                    
                    if ((status == DownloadStatus::NotStarted || status == DownloadStatus::Paused) &&
                        m_dispatched_tasks.find(t.get()) == m_dispatched_tasks.end()) {
                        
                        m_dispatched_tasks.insert(t.get());

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

            if (all_finished && !m_tasks.empty()) {
                cout << "[DownloadQueue] All tasks in '" << m_queue_name << "' have completed!" << endl;
                m_state = QueueState::Stopped;
                action_to_run = m_post_action;
                cb_to_run = m_completion_callback;
                finished = true;
            }
        }

        if (finished) {
            execute_post_queue_action(action_to_run);
            if (cb_to_run) {
                cb_to_run(action_to_run);
            }
            break;
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
