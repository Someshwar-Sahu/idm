#include <curl/curl.h>
#include "DownloadQueue.h"
#include <iostream>
#include <cassert>
#include <memory>

using namespace std;

int main() {
    cout << "==========================================================" << endl;
    cout << "  Unit Test: Queue Power & Thread Allocation (Pos + Neg)  " << endl;
    cout << "==========================================================" << endl;

    curl_global_init(CURL_GLOBAL_DEFAULT);

    DownloadQueue queue("Test Power Queue", 3);

    // =========================================================================
    // 1. UNCONSTRAINED MODE (m_max_total_threads = 0)
    // =========================================================================
    cout << "\n[1/6] Testing Unconstrained Mode (Preserve Configured Threads)..." << endl;
    queue.set_max_total_threads(0);
    assert(queue.get_max_total_threads() == 0);
    assert(queue.calculate_allocated_threads(1, 8) == 8);
    assert(queue.calculate_allocated_threads(3, 4) == 4);
    assert(queue.calculate_allocated_threads(5, 16) == 16);
    cout << "  -> Unconstrained allocation preserves configured thread counts: PASSED" << endl;

    // =========================================================================
    // 2. AC POWER UNTHROTTLED (m_max_total_threads = 32)
    // =========================================================================
    cout << "\n[2/6] Testing AC Power Mode (m_max_total_threads = 32)..." << endl;
    queue.set_max_total_threads(32);
    assert(queue.get_max_total_threads() == 32);

    // Single active task with 8 configured threads gets full 8 (not artificially inflated or deflated)
    assert(queue.calculate_allocated_threads(1, 8) == 8);

    // 4 active tasks share 32 threads: 32 / 4 = 8 each
    assert(queue.calculate_allocated_threads(4, 8) == 8);

    // 8 active tasks share 32 threads: 32 / 8 = 4 each
    assert(queue.calculate_allocated_threads(8, 8) == 4);
    cout << "  -> AC mode fair-sharing within power ceiling: PASSED" << endl;

    // =========================================================================
    // 3. BATTERY POWER ECO CLAMP (m_max_total_threads = 4)
    // =========================================================================
    cout << "\n[3/6] Testing Battery Eco Mode (m_max_total_threads = 4)..." << endl;
    queue.set_max_total_threads(4);
    assert(queue.get_max_total_threads() == 4);

    // CRITICAL BUG VERIFICATION: Single active task on battery MUST get 4 threads (not divided down to 1!)
    int single_battery_alloc = queue.calculate_allocated_threads(1, 8);
    cout << "  -> Single task on battery allocation: " << single_battery_alloc << " threads (expected 4)" << endl;
    assert(single_battery_alloc == 4);

    // 2 active tasks share 4 threads: 4 / 2 = 2 each
    assert(queue.calculate_allocated_threads(2, 8) == 2);

    // 4 active tasks share 4 threads: 4 / 4 = 1 each
    assert(queue.calculate_allocated_threads(4, 8) == 1);
    cout << "  -> Battery mode single-task allocation & multi-task sharing: PASSED" << endl;

    // =========================================================================
    // 4. CRITICAL BATTERY EMERGENCY CLAMP (m_max_total_threads = 2)
    // =========================================================================
    cout << "\n[4/6] Testing Critical Battery Emergency Mode (m_max_total_threads = 2)..." << endl;
    queue.set_max_total_threads(2);
    assert(queue.calculate_allocated_threads(1, 8) == 2);
    assert(queue.calculate_allocated_threads(2, 8) == 1);
    cout << "  -> Critical battery clamps to maximum 2 threads: PASSED" << endl;

    // =========================================================================
    // 5. NEGATIVE & BOUNDARY INPUTS
    // =========================================================================
    cout << "\n[5/6] Testing Negative & Boundary Inputs..." << endl;
    // Negative max_total_threads clamped to 0 (unconstrained)
    queue.set_max_total_threads(-10);
    assert(queue.get_max_total_threads() == 0);

    // 0 or negative active downloads count handled gracefully (treated as 1)
    queue.set_max_total_threads(4);
    assert(queue.calculate_allocated_threads(0, 8) == 4);
    assert(queue.calculate_allocated_threads(-3, 8) == 4);

    // 0 or negative configured task threads enforced to minimum floor 1
    assert(queue.calculate_allocated_threads(1, 0) == 4); // 0 defaults to 8, capped at 4
    assert(queue.calculate_allocated_threads(1, -5) == 4);

    // Excessive active downloads (e.g. 10 on ceiling 4 -> 4/10 = 0 integer division clamped to 1)
    assert(queue.calculate_allocated_threads(10, 8) == 1);
    cout << "  -> Negative thread counts and zero-division guards: PASSED" << endl;

    // =========================================================================
    // 6. DOWNLOADTASK INTEGRATION & CONFIGURED THREAD RETENTION
    // =========================================================================
    cout << "\n[6/6] Testing Task Thread Count Preservation..." << endl;
    auto task = make_shared<DownloadTask>("https://example.com/test.iso", "test_power_dest.iso", 10);
    assert(task->get_configured_thread_count() == 10);
    assert(task->get_thread_count() == 10);

    // Simulate queue power allocation under battery constraint (4 threads)
    int allocated = queue.calculate_allocated_threads(1, task->get_configured_thread_count());
    task->set_allocated_thread_count(allocated);

    // Task active threads should be 4, but configured count remains 10
    assert(task->get_thread_count() == 4);
    assert(task->get_configured_thread_count() == 10);

    // Explicit user reconfiguration updates both
    task->set_thread_count(16);
    assert(task->get_thread_count() == 16);
    assert(task->get_configured_thread_count() == 16);
    cout << "  -> Configured vs allocated thread separation verified: PASSED" << endl;

    curl_global_cleanup();

    cout << "\n==========================================================" << endl;
    cout << "  [PASSED] ALL 6/6 POWER & THREAD ALLOCATION TESTS PASSED! " << endl;
    cout << "==========================================================" << endl;
    return 0;
}

