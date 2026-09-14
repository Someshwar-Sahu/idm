#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <vector>
#include <thread>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <future>
#include <type_traits> 

using namespace std;

class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads = thread::hardware_concurrency());
    ~ThreadPool();
    
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    void enqueue(function<void()> task);

    template<class F, class... Args>
    auto enqueue_future(F&& f, Args&&... args) -> future<typename invoke_result<F, Args...>::type> {
        using return_type = typename invoke_result<F, Args...>::type;

        auto task = make_shared<packaged_task<return_type()>>(
            bind(forward<F>(f), forward<Args>(args)...)
        ); 

        future<return_type> res = task->get_future();
        {
            unique_lock<mutex> lock(m_queue_mutex);
            if(m_stop){
                throw runtime_error("enqueue on stopped ThreadPool");
            }
            m_tasks.emplace([task]() { (*task)(); }); 
        }
        m_cv.notify_one();
        return res;
    }

    void stop();
    size_t size() const;

private:
    void worker_loop();

    vector<thread> m_workers;
    queue<function<void()>> m_tasks;

    mutable mutex m_queue_mutex;
    condition_variable m_cv;
    atomic<bool> m_stop{false};
};

#endif
