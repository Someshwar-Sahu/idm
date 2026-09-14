#include "DownloadQueue.h"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>

using namespace std;

int main() {
    remove("test_queue_file1.bin");
    remove("test_queue_file1.bin.progress.json");
    remove("test_queue_file2.bin");
    remove("test_queue_file2.bin.progress.json");

    cout << "[Test] Starting DownloadQueue unit test..." << endl;

    DownloadQueue queue("Test Queue", 2);
    queue.set_post_queue_action(PostQueueAction::PlaySound);

    assert(queue.get_queue_name() == "Test Queue");
    assert(queue.get_max_concurrent_tasks() == 2);
    assert(queue.get_post_queue_action() == PostQueueAction::PlaySound);

    string url1 = "https://httpbin.org/bytes/2048";
    string dest1 = "test_queue_file1.bin";

    string url2 = "https://httpbin.org/bytes/4096";
    string dest2 = "test_queue_file2.bin";

    auto task1 = make_shared<DownloadTask>(url1, dest1, 2);
    auto task2 = make_shared<DownloadTask>(url2, dest2, 2);

    queue.add_task(task1);
    queue.add_task(task2);

    assert(queue.get_task_count() == 2);
    cout << "[Test] Successfully added 2 tasks to queue." << endl;

    bool callback_triggered = false;
    PostQueueAction triggered_action = PostQueueAction::None;

    auto callback = [&](PostQueueAction action) {
        callback_triggered = true;
        triggered_action = action;
        cout << "[Test Callback] Queue execution completed with post-action enum: " 
             << static_cast<int>(action) << endl;
    };

    cout << "[Test] Starting queue processing loop..." << endl;
    queue.start(callback);

    // Wait for tasks to finish downloading (up to 10 seconds max)
    int attempts = 0;
    while (queue.get_state() == QueueState::Running && attempts < 100) {
        this_thread::sleep_for(chrono::milliseconds(200));
        attempts++;
    }

    queue.stop();

    assert(callback_triggered);
    assert(triggered_action == PostQueueAction::PlaySound);

    // Clean up downloaded files
    remove(dest1.c_str());
    remove(dest2.c_str());

    cout << "[PASSED] DownloadQueue unit test completed successfully!" << endl;
    return 0;
}
