#include "ThreadPool.h"
#include <iostream>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <string>
#include <stdexcept>

using namespace std;

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);

    cout << "==========================================================" << endl;
    cout << "  Comprehensive Unit Test: ThreadPool (Pos + Neg)         " << endl;
    cout << "==========================================================" << endl;

    // =========================================================================
    // 1. CONSTRUCTION & THREAD SIZING (Positive & Negative/Edge)
    // =========================================================================
    cout << "\n[1/5] Testing ThreadPool Construction & Sizing..." << endl;

    // 1.1 Positive: Explicit sizing
    {
        ThreadPool pool4(4);
        assert(pool4.size() == 4);
    }

    // 1.2 Positive: Default sizing (hardware concurrency)
    {
        ThreadPool pool_default;
        assert(pool_default.size() >= 1);
    }

    // 1.3 Negative / Edge: Sizing with 0 threads must be clamped to 1
    {
        ThreadPool pool_zero(0);
        assert(pool_zero.size() == 1);
    }
    cout << "  -> Construction sizing & 0-thread clamp: PASSED" << endl;

    // =========================================================================
    // 2. CONCURRENT ENQUEUE STRESS TEST (Positive)
    // =========================================================================
    cout << "\n[2/5] Testing Concurrent enqueue() Stress Test..." << endl;

    constexpr int NUM_TASKS = 100;
    atomic<int> counter{0};

    {
        ThreadPool pool(4);
        for (int i = 0; i < NUM_TASKS; ++i) {
            pool.enqueue([&counter]() {
                this_thread::sleep_for(chrono::milliseconds(1));
                counter++;
            });
        }
    } // Pool destructor waits for all 100 tasks to finish

    assert(counter.load() == NUM_TASKS);
    cout << "  -> 100 concurrent tasks executed correctly: PASSED" << endl;

    // =========================================================================
    // 3. ENQUEUE_FUTURE & EXCEPTION MARSHALING (Positive & Negative)
    // =========================================================================
    cout << "\n[3/5] Testing enqueue_future() & Exception Marshaling..." << endl;

    {
        ThreadPool pool(2);

        // 3.1 Positive: Primitive computation
        auto fut_add = pool.enqueue_future([](int a, int b) {
            return a + b;
        }, 20, 22);
        assert(fut_add.get() == 42);

        // 3.2 Positive: String return type
        auto fut_str = pool.enqueue_future([](const string &prefix) {
            return prefix + "_IDM_ENGINE";
        }, "NEXTGEN");
        assert(fut_str.get() == "NEXTGEN_IDM_ENGINE");

        // 3.3 Negative / Exception: Marshaling exceptions thrown in worker threads
        auto fut_err = pool.enqueue_future([]() -> int {
            throw runtime_error("Simulated task exception inside ThreadPool");
        });

        bool caught_task_exception = false;
        try {
            fut_err.get(); // Must re-throw the runtime_error from worker thread
        } catch (const runtime_error &e) {
            caught_task_exception = true;
        }
        assert(caught_task_exception == true);
    }
    cout << "  -> enqueue_future() values & exception propagation: PASSED" << endl;

    // =========================================================================
    // 4. STOP LIFECYCLE & IDEMPOTENCE (Positive)
    // =========================================================================
    cout << "\n[4/5] Testing stop() Lifecycle & Idempotence..." << endl;

    ThreadPool pool_stop(3);
    assert(pool_stop.size() == 3);

    // Explicit stop joins all workers and clears them
    pool_stop.stop();
    assert(pool_stop.size() == 0);

    // Consecutive second call to stop() must be safe and idempotent
    pool_stop.stop();
    assert(pool_stop.size() == 0);
    cout << "  -> Explicit stop() & idempotence: PASSED" << endl;

    // =========================================================================
    // 5. ENQUEUE ON STOPPED POOL GUARDS (Negative)
    // =========================================================================
    cout << "\n[5/5] Testing Enqueue Rejection on Stopped Pool (Negative)..." << endl;

    // 5.1 Negative: enqueue() on stopped pool throws runtime_error
    bool caught_enqueue = false;
    try {
        pool_stop.enqueue([]() {});
    } catch (const runtime_error &e) {
        caught_enqueue = true;
    }
    assert(caught_enqueue == true);

    // 5.2 Negative: enqueue_future() on stopped pool throws runtime_error
    bool caught_future = false;
    try {
        pool_stop.enqueue_future([]() { return 1; });
    } catch (const runtime_error &e) {
        caught_future = true;
    }
    assert(caught_future == true);
    cout << "  -> Enqueue rejection on stopped pool: PASSED" << endl;

    cout << "\n==========================================================" << endl;
    cout << "  [PASSED] 100% OF THREADPOOL FUNCTIONS VERIFIED!         " << endl;
    cout << "==========================================================" << endl;

    return 0;
}
