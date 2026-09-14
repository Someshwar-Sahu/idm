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

    explicit DownloadQueue(const std::string &queue_name = "Main Queue", int max_concurrent_tasks = 3);
    ~DownloadQueue();

    DownloadQueue(const DownloadQueue&) = delete;
    DownloadQueue& operator=(const DownloadQueue&) = delete;

    void add_task(std::shared_ptr<DownloadTask> task);
    bool remove_task(const std::string &destination_filepath);

    void start(QueueCompletionCallback completion_callback = nullptr);
    void pause();
    void stop();

    void set_max_concurrent_tasks(int max_concurrent);
    int get_max_concurrent_tasks() const; 

    void set_post_queue_action(PostQueueAction action);
    PostQueueAction get_post_queue_action() const;
    std::string get_queue_name() const; 
    QueueState get_state() const;

    std::vector<std::shared_ptr<DownloadTask>> get_tasks() const;
    size_t get_task_count() const;

private:

    std::thread m_queue_thread;
    void process_queue_loop();
    std::unordered_set<DownloadTask*> m_dispatched_tasks;

    std::string m_queue_name;
    int m_max_concurrent_tasks{3};
    PostQueueAction m_post_action{PostQueueAction::None};

    std::vector<std::shared_ptr<DownloadTask>> m_tasks;
    std::atomic<QueueState> m_state{QueueState::Stopped};
    std::atomic<bool> m_stop_thread{false};

    mutable std::mutex m_mutex;
    QueueCompletionCallback m_completion_callback;
};

#endif