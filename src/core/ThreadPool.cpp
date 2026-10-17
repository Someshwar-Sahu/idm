#include "ThreadPool.h"

using namespace std;

ThreadPool::ThreadPool(size_t num_threads) {
    if (num_threads == 0) {
        num_threads = 1;
    }
    m_workers.reserve(num_threads);
    for (size_t i = 0; i < num_threads; ++i) {
        m_workers.emplace_back(&ThreadPool::worker_loop, this);
    }
}

ThreadPool::~ThreadPool() {
    stop();
}

void ThreadPool::enqueue(function<void()> task) {
    {
        unique_lock<mutex> lock(m_queue_mutex);
        if (m_stop) {
            throw runtime_error("Cannot enqueue on stopped ThreadPool");
        }
        m_tasks.push(move(task));
    }
    m_cv.notify_one();
}

void ThreadPool::stop() {
    bool expected = false;
    if (m_stop.compare_exchange_strong(expected, true)) {
        m_cv.notify_all();
        for (thread &worker : m_workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        m_workers.clear();
    }
}

size_t ThreadPool::size() const {
    return m_workers.size();
}

void ThreadPool::worker_loop() {
    while (true) {
        function<void()> task;
        {
            unique_lock<mutex> lock(m_queue_mutex);
            m_cv.wait(lock, [this]() {
                return m_stop || !m_tasks.empty();
            });

            if (m_stop && m_tasks.empty()) {
                return;
            }

            task = move(m_tasks.front());
            m_tasks.pop();
        }

        if (task) {
            task();
        }
    }
}
