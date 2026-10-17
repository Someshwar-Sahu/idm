#include <curl/curl.h>

#include "DownloadQueue.h"
#include <iostream>
#include <fstream>
#include <cassert>
#include <thread>
#include <chrono>
#include <cstdio>
#include <memory>

using namespace std;

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);

    cout << "==========================================================" << endl;
    cout << "  Comprehensive Unit Test: DownloadQueue (Pos + Neg)      " << endl;
    cout << "==========================================================" << endl;

    curl_global_init(CURL_GLOBAL_DEFAULT);

    const string DEST1 = "test_queue_file1.bin";
    const string DEST2 = "test_queue_file2.bin";

    // Clean up stale files
    remove(DEST1.c_str());
    remove(DEST2.c_str());
    ResumeStore::remove(ResumeStore::get_sidecar_filepath(DEST1));
    ResumeStore::remove(ResumeStore::get_sidecar_filepath(DEST2));

    // =========================================================================
    // 1. INITIALIZATION & CONCURRENCY CLAMPING (Positive & Negative)
    // =========================================================================
    cout << "\n[1/5] Testing Initialization & Concurrency Bounds..." << endl;

    DownloadQueue queue("Test Primary Queue", 3);
    assert(queue.get_queue_name() == "Test Primary Queue");
    assert(queue.get_max_concurrent_tasks() == 3);
    assert(queue.get_state() == QueueState::Stopped);
    assert(queue.get_task_count() == 0);

    // 1.1 Negative / Edge: Concurrency clamped to at least 1
    queue.set_max_concurrent_tasks(0);
    assert(queue.get_max_concurrent_tasks() == 1);

    queue.set_max_concurrent_tasks(-10);
    assert(queue.get_max_concurrent_tasks() == 1);

    // Reset to valid concurrency
    queue.set_max_concurrent_tasks(2);
    assert(queue.get_max_concurrent_tasks() == 2);
    cout << "  -> Queue properties and concurrency clamping: PASSED" << endl;

    // =========================================================================
    // 2. POST-QUEUE ACTION & NULL TASK GUARDS (Positive & Negative)
    // =========================================================================
    cout << "\n[2/5] Testing PostQueueAction & Null Guards (Pos + Neg)..." << endl;

    // 2.1 Positive: Test all PostQueueAction options
    queue.set_post_queue_action(PostQueueAction::PlaySound);
    assert(queue.get_post_queue_action() == PostQueueAction::PlaySound);

    queue.set_post_queue_action(PostQueueAction::ShutdownPC);
    assert(queue.get_post_queue_action() == PostQueueAction::ShutdownPC);

    queue.set_post_queue_action(PostQueueAction::None);
    assert(queue.get_post_queue_action() == PostQueueAction::None);

    // 2.2 Negative: add_task(nullptr) must be rejected
    queue.add_task(nullptr);
    assert(queue.get_task_count() == 0);
    cout << "  -> PostQueueAction values and nullptr guard: PASSED" << endl;

    // =========================================================================
    // 3. TASK REMOVAL & EMPTY QUEUE GUARDS (Positive & Negative)
    // =========================================================================
    cout << "\n[3/5] Testing remove_task() Guards (Pos + Neg)..." << endl;

    // 3.1 Negative: remove_task on empty queue returns false
    bool remove_empty = queue.remove_task("non_existent_path.bin");
    assert(remove_empty == false);

    // 3.2 Positive: add dummy task and remove it
    auto dummy_task = make_shared<DownloadTask>("https://httpbin.org/bytes/100", "dummy.bin", 1);
    queue.add_task(dummy_task);
    assert(queue.get_task_count() == 1);

    bool remove_ok = queue.remove_task("dummy.bin");
    assert(remove_ok == true);
    assert(queue.get_task_count() == 0);
    cout << "  -> remove_task() on empty and pending items: PASSED" << endl;

    // =========================================================================
    // 4. LIFECYCLE TRANSITIONS: START, PAUSE, STOP (Positive)
    // =========================================================================
    cout << "\n[4/5] Testing Queue State Lifecycle Transitions..." << endl;

    DownloadQueue lifecycle_queue("Lifecycle Queue", 1);
    assert(lifecycle_queue.get_state() == QueueState::Stopped);

    lifecycle_queue.start();
    assert(lifecycle_queue.get_state() == QueueState::Running);

    // Double start is safe
    lifecycle_queue.start();
    assert(lifecycle_queue.get_state() == QueueState::Running);

    lifecycle_queue.pause();
    assert(lifecycle_queue.get_state() == QueueState::Paused);

    lifecycle_queue.stop();
    assert(lifecycle_queue.get_state() == QueueState::Stopped);

    // Double stop is safe and idempotent
    lifecycle_queue.stop();
    assert(lifecycle_queue.get_state() == QueueState::Stopped);
    cout << "  -> Stopped -> Running -> Paused -> Stopped transitions: PASSED" << endl;

    // =========================================================================
    // 5. LIVE EXECUTION & COMPLETION CALLBACK (Positive)
    // =========================================================================
    cout << "\n[5/5] Testing Live Queue Download & Completion Callback..." << endl;

    DownloadQueue live_queue("Live Batch", 2);
    live_queue.set_post_queue_action(PostQueueAction::PlaySound);

    auto task1 = make_shared<DownloadTask>("https://httpbin.org/bytes/2048", DEST1, 2);
    auto task2 = make_shared<DownloadTask>("https://httpbin.org/bytes/2048", DEST2, 2);

    live_queue.add_task(task1);
    live_queue.add_task(task2);
    assert(live_queue.get_task_count() == 2);

    bool completion_called = false;
    PostQueueAction received_action = PostQueueAction::None;

    live_queue.start([&](PostQueueAction action) {
        completion_called = true;
        received_action = action;
    });

    // Wait for queue processing (up to 10 seconds)
    int wait_iterations = 0;
    while (live_queue.get_state() == QueueState::Running && wait_iterations < 50) {
        this_thread::sleep_for(chrono::milliseconds(200));
        wait_iterations++;
    }

    live_queue.stop();

    assert(completion_called == true);
    assert(received_action == PostQueueAction::PlaySound);
    assert(live_queue.get_state() == QueueState::Stopped);

    // Verify both files downloaded
    ifstream f1(DEST1, ios::binary | ios::ate);
    ifstream f2(DEST2, ios::binary | ios::ate);
    assert(f1.is_open() && f1.tellg() == 2048);
    assert(f2.is_open() && f2.tellg() == 2048);
    f1.close();
    f2.close();

    // Clean up
    remove(DEST1.c_str());
    remove(DEST2.c_str());
    curl_global_cleanup();
    cout << "  -> Live multi-task batch execution & completion callback: PASSED" << endl;

        // =========================================================================
    // 6. POST-QUEUE NATIVE DISPATCH VIA MOCK EXECUTOR (Task 18)
    // =========================================================================
    cout << "\n[6/7] Testing Post-Queue Action Dispatch via Mock (Task 18)..." << endl;
    DownloadQueue action_queue("Action Queue", 1);

    PostQueueAction last_dispatched_action = PostQueueAction::None;
    action_queue.set_mock_native_action_executor([&](PostQueueAction action) {
        last_dispatched_action = action;
        return true;
    });

    // Test ShutdownPC dispatch
    action_queue.set_post_queue_action(PostQueueAction::ShutdownPC);
    assert(action_queue.execute_post_queue_action(PostQueueAction::ShutdownPC) == true);
    assert(last_dispatched_action == PostQueueAction::ShutdownPC);
    cout << "  -> ShutdownPC action safely intercepted by executor: PASSED" << endl;

    // Test SleepPC dispatch
    action_queue.set_post_queue_action(PostQueueAction::SleepPC);
    assert(action_queue.execute_post_queue_action(PostQueueAction::SleepPC) == true);
    assert(last_dispatched_action == PostQueueAction::SleepPC);
    cout << "  -> SleepPC action safely intercepted by executor: PASSED" << endl;

    // Test HibernatePC dispatch
    action_queue.set_post_queue_action(PostQueueAction::HibernatePC);
    assert(action_queue.execute_post_queue_action(PostQueueAction::HibernatePC) == true);
    assert(last_dispatched_action == PostQueueAction::HibernatePC);
    cout << "  -> HibernatePC action safely intercepted by executor: PASSED" << endl;

    // =========================================================================
    // 7. NATIVE DRY-RUN VALIDATION & LIVE SOUND ALERT (Task 18)
    // =========================================================================
    cout << "\n[7/7] Testing Native Dry-Run Actions & Live Sound Alert (Task 18)..." << endl;
    assert(DownloadQueue::execute_native_post_action(PostQueueAction::ShutdownPC, /*dry_run=*/true) == true);
    assert(DownloadQueue::execute_native_post_action(PostQueueAction::SleepPC, /*dry_run=*/true) == true);
    assert(DownloadQueue::execute_native_post_action(PostQueueAction::HibernatePC, /*dry_run=*/true) == true);
    assert(DownloadQueue::execute_native_post_action(PostQueueAction::None, /*dry_run=*/false) == true);

    // Test live sound playback (harmless chime notification)
    bool sound_played = DownloadQueue::execute_native_post_action(PostQueueAction::PlaySound, /*dry_run=*/false);
    assert(sound_played == true);
    // =========================================================================
    // 8. SCHEDULER SETTINGS & POST-QUEUE COMPLETION WITH PAUSED TASKS (Phase C)
    // =========================================================================
    cout << "\n[8/8] Testing Scheduler Settings & Post-Queue Paused Task Completion (Phase C)..." << endl;
    DownloadQueue sched_queue("Sched Queue", 2);

    DownloadQueue::QueueSchedule sched;
    sched.start_scheduled = true;
    sched.start_time = "02:30";
    sched.stop_scheduled = true;
    sched.stop_time = "06:00";
    sched.max_retries = 5;

    sched_queue.set_schedule(sched);
    auto retrieved_sched = sched_queue.get_schedule();
    assert(retrieved_sched.start_scheduled == true);
    assert(retrieved_sched.start_time == "02:30");
    assert(retrieved_sched.stop_scheduled == true);
    assert(retrieved_sched.stop_time == "06:00");
    assert(retrieved_sched.max_retries == 5);
    cout << "  -> QueueSchedule persistence and retrieval: PASSED" << endl;

    // Test queue completion when active runnable task completes while paused task remains
    const string DEST3 = "test_queue_file3.bin";
    remove(DEST3.c_str());
    ResumeStore::remove(ResumeStore::get_sidecar_filepath(DEST3));

    auto runnable_task = make_shared<DownloadTask>("https://httpbin.org/bytes/1024", DEST3, 2);
    auto paused_task = make_shared<DownloadTask>("https://httpbin.org/bytes/5000", "paused_dummy.bin", 1);
    paused_task->pause(); // Explicitly user-paused / "Download Later"

    sched_queue.set_post_queue_action(PostQueueAction::PlaySound);
    sched_queue.add_task(runnable_task);
    sched_queue.add_task(paused_task);

    bool partial_completion_called = false;
    sched_queue.start([&](PostQueueAction) {
        partial_completion_called = true;
    });

    int wait_cycles = 0;
    while (sched_queue.get_state() == QueueState::Running && wait_cycles < 50) {
        this_thread::sleep_for(chrono::milliseconds(200));
        wait_cycles++;
    }
    sched_queue.stop();

    assert(partial_completion_called == true);
    assert(runnable_task->get_status() == DownloadStatus::Completed);
    assert(paused_task->get_status() == DownloadStatus::Paused);
    remove(DEST3.c_str());
    cout << "  -> Post-queue completion with paused tasks in queue: PASSED" << endl;

    // =========================================================================
    // 9. LIVE SCHEDULER MINUTE TRIGGER VALIDATION
    // =========================================================================
    cout << "\n[9/9] Testing Live Scheduler Minute-Triggered Queue Start..." << endl;
    DownloadQueue live_sched_queue("Live Sched Queue", 2);
    auto pending_task = make_shared<DownloadTask>("https://httpbin.org/bytes/1024", "test_pending.bin", 2);
    pending_task->pause();
    live_sched_queue.add_task(pending_task);
    assert(live_sched_queue.get_state() != QueueState::Running);

    auto now = chrono::system_clock::now();
    time_t now_time = chrono::system_clock::to_time_t(now);
    tm local_tm{};
#ifdef _WIN32
    localtime_s(&local_tm, &now_time);
#else
    localtime_r(&now_time, &local_tm);
#endif
    char time_buf[16];
    snprintf(time_buf, sizeof(time_buf), "%02d:%02d", local_tm.tm_hour, local_tm.tm_min);

    DownloadQueue::QueueSchedule live_sched;
    live_sched.start_scheduled = true;
    live_sched.start_time = time_buf;
    live_sched_queue.set_schedule(live_sched);

    // Wait up to 3 seconds for the scheduler loop to evaluate the current minute
    int sched_wait = 0;
    while (live_sched_queue.get_state() != QueueState::Running && sched_wait < 30) {
        this_thread::sleep_for(chrono::milliseconds(100));
        sched_wait++;
    }
    assert(live_sched_queue.get_state() == QueueState::Running);
    assert(pending_task->get_status() != DownloadStatus::Paused);
    live_sched_queue.stop();
    remove("test_pending.bin");
    ResumeStore::remove(ResumeStore::get_sidecar_filepath("test_pending.bin"));
    cout << "  -> Live scheduler minute-triggered queue auto-start: PASSED" << endl;

    cout << "\n==========================================================" << endl;
    cout << "  [PASSED] 100% OF DOWNLOADQUEUE FUNCTIONS VERIFIED!      " << endl;
    cout << "==========================================================" << endl;

    return 0;
}
