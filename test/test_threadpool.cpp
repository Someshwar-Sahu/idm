#include "ThreadPool.h"
#include <iostream>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio> // For setvbuf

using namespace std;

int main() {
    // Disable stdout buffering so output appears immediately in PowerShell
    setvbuf(stdout, NULL, _IONBF, 0);

    cout << "[Test] Starting ThreadPool verification test..." << endl;

    constexpr int NUM_TASKS = 100;
    atomic<int> counter{0};

    {
        ThreadPool pool(4);
        cout << "[Test] Created ThreadPool with " << pool.size() << " worker threads." << endl;

        for (int i = 0; i < NUM_TASKS; ++i) {
            pool.enqueue([&counter]() {
                this_thread::sleep_for(chrono::milliseconds(2));
                counter++;
            });
        }
        cout << "[Test] Enqueued " << NUM_TASKS << " tasks." << endl;
        
        // Future test
        auto fut = pool.enqueue_future([](int a, int b) {
            return a + b;
        }, 10, 32);

        int sum = fut.get();
        cout << "[Test] Future result evaluated: " << sum << " (expected 42)" << endl;
        assert(sum == 42);
    } // ThreadPool destructor runs here, joining all threads

    cout << "[Test] Counter final value: " << counter.load() << " / " << NUM_TASKS << endl;
    assert(counter.load() == NUM_TASKS);

    cout << "[PASSED] ThreadPool test completed successfully!" << endl;
    return 0;
}
