#ifndef DOWNLOAD_QUEUE_H
#define DOWNLOAD_QUEUE_H

#include "DownloadTask.h"
#include <thread>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <functional>
#include <unordered_set>

enum class PostQueueAction{
    None,
    PlaySound,
    ShutdownPC,
    SleepPC,
    HibernatePC
};

enum class QueueState{
    Stopped,
    Running,
    Paused
};

class DownloadQueue{
public:
    using QueueCompletionCallback = std::function<void(PostQueueAction action)>;
    using NativeActionExecutor = std::function<bool(PostQueueAction action)>;

    explicit DownloadQueue(const std::string &queue_name = "Main Queue", int max_concurrent_tasks = 3);
    ~DownloadQueue();

    DownloadQueue(const DownloadQueue&) = delete;
    DownloadQueue& operator=(const DownloadQueue&) = delete;

    void add_task(std::shared_ptr<DownloadTask> task);
    bool remove_task(const std::string &destination_filepath);
    bool move_task_up(const std::string &destination_filepath);
    bool move_task_down(const std::string &destination_filepath);

    void start(QueueCompletionCallback completion_callback = nullptr);
    void pause();
    void stop();
    bool resume_task(const std::string &destination_filepath);
    bool pause_task(const std::string &destination_filepath);

    void set_max_concurrent_tasks(int max_concurrent);
    int get_max_concurrent_tasks() const; 
    void set_max_total_threads(int max_threads) { m_max_total_threads = max_threads; }
    int get_max_total_threads() const { return m_max_total_threads; }

    void set_post_queue_action(PostQueueAction action);
    PostQueueAction get_post_queue_action() const;
    std::string get_queue_name() const; 
    QueueState get_state() const;

    std::vector<std::shared_ptr<DownloadTask>> get_tasks() const;
    size_t get_task_count() const;
    
    bool execute_post_queue_action(PostQueueAction action);
    void set_mock_native_action_executor(NativeActionExecutor executor);
    static bool execute_native_post_action(PostQueueAction action, bool dry_run = false);

    struct QueueSchedule {
        bool start_scheduled{false};
        std::string start_time{""}; // "HH:MM"
        bool stop_scheduled{false};
        std::string stop_time{""};  // "HH:MM"
        int max_retries{10};
    };

    void set_schedule(const QueueSchedule &sched);
    QueueSchedule get_schedule() const;
    void start_scheduler();
    void stop_scheduler();

private:

    std::thread m_queue_thread;
    void process_queue_loop();
    std::unordered_set<DownloadTask*> m_dispatched_tasks;

    std::thread m_scheduler_thread;
    std::atomic<bool> m_stop_scheduler{false};
    void schedule_evaluator_loop();
    QueueSchedule m_schedule;
    std::string m_last_started_min{""};
    std::string m_last_stopped_min{""};

    std::string m_queue_name;
    int m_max_concurrent_tasks{3};
    int m_max_total_threads{16};
    PostQueueAction m_post_action{PostQueueAction::None};

    std::vector<std::shared_ptr<DownloadTask>> m_tasks;
    std::atomic<QueueState> m_state{QueueState::Stopped};
    std::atomic<bool> m_stop_thread{false};

    mutable std::mutex m_mutex;
    QueueCompletionCallback m_completion_callback;
    NativeActionExecutor m_mock_action_executor{nullptr};
};

#endif