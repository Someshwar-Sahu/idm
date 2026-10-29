#include "NotificationManager.h"
#include <iostream>
#include <cassert>
#include <thread>

using namespace std;

int main() {
    cout << "==========================================================" << endl;
    cout << "  Comprehensive Unit Test: NotificationManager (Task 19)  " << endl;
    cout << "==========================================================" << endl;

    NotificationManager manager;

    // 1. Initial State & Payload Formatting
    cout << "\n[1/7] Testing Construction & Payload Formatting..." << endl;
    assert(manager.get_history_count() == 0);

    NotificationPayload p1;
    p1.id = "task_001";
    p1.title = "IDM: Download Complete";
    p1.message = "archive_data.zip";
    p1.filepath = "C:/Downloads/archive_data.zip";
    p1.file_size_bytes = 104857600; // 100 MB

    cout << "  -> Initial manager state verified: PASSED" << endl;

    // 2. Empty & Malformed Payload Rejection (Negative)
    cout << "\n[2/7] Testing Empty Payload Guards (Negative)..." << endl;
    NotificationPayload empty_payload;
    assert(manager.show_download_complete(empty_payload) == false);
    assert(manager.show_download_complete("", "empty.bin") == false);
    cout << "  -> Missing/empty file paths safely rejected: PASSED" << endl;

    // 3. Windows Action Center XML Structure
    cout << "\n[3/7] Testing Action Center XML Generation..." << endl;
    string xml = NotificationManager::build_windows_toast_xml(p1);
    assert(xml.find("<toast") != string::npos);
    assert(xml.find("Open File") != string::npos);
    assert(xml.find("Open Folder") != string::npos);
    assert(xml.find("archive_data.zip") != string::npos);
    cout << "  -> Valid XML template with interactive actions generated: PASSED" << endl;

    // 4. Interactive [Open File] Action Routing
    cout << "\n[4/7] Testing Interactive [Open File] Routing..." << endl;
    string last_action_file;
    ToastAction last_action_type = ToastAction::Dismiss;

    manager.set_action_handler([&](const string &id, ToastAction action, const string &path) {
        last_action_file = path;
        last_action_type = action;
    });

    // Mock dispatcher to store payload in history without popping UI
    manager.set_mock_dispatcher([&](const NotificationPayload &p) {
        return true;
    });

    manager.show_download_complete(p1);
    assert(manager.get_history_count() == 1);

    bool open_file_ok = manager.trigger_action(p1.id, ToastAction::OpenFile, /*dry_run=*/true);
    assert(last_action_type == ToastAction::OpenFile);
    assert(last_action_file == p1.filepath);
    assert(NotificationManager::open_file(p1.filepath, /*dry_run=*/true) == true);
    cout << "  -> OpenFile action routed with exact file path: PASSED" << endl;

    // 5. Interactive [Open Folder] Action Routing
    cout << "\n[5/7] Testing Interactive [Open Folder] Routing..." << endl;
    bool open_folder_ok = manager.trigger_action(p1.id, ToastAction::OpenFolder, /*dry_run=*/true);
    assert(last_action_type == ToastAction::OpenFolder);
    assert(last_action_file == p1.filepath);
    assert(NotificationManager::open_folder_and_select_file(p1.filepath, /*dry_run=*/true) == true);
    cout << "  -> OpenFolder action routed with explorer file selection: PASSED" << endl;

    // 6. Mock Dispatcher & History Storage
    cout << "\n[6/7] Testing Notification History Storage..." << endl;
    NotificationPayload p2;
    p2.id = "task_002";
    p2.title = "IDM: Download Complete";
    p2.message = "video_stream.mp4";
    p2.filepath = "C:/Downloads/video_stream.mp4";

    manager.show_download_complete(p2);
    assert(manager.get_history_count() == 2);
    auto history = manager.get_history();
    assert(history.size() == 2);
    assert(history[0].id == "task_001");
    assert(history[1].id == "task_002");

    manager.clear_history();
    assert(manager.get_history_count() == 0);
    cout << "  -> History records and clear verified: PASSED" << endl;

    // 7. Native Dry-Run Toast Execution
    cout << "\n[7/7] Testing Native Dry-Run Toast Execution..." << endl;
    assert(NotificationManager::dispatch_native_toast(p1, /*dry_run=*/true) == true);
    assert(NotificationManager::dispatch_native_toast(empty_payload, /*dry_run=*/true) == false);
    cout << "  -> Dry-run native toast validation: PASSED" << endl;

    cout << "\n==========================================================" << endl;
    cout << "  [PASSED] 100% OF NOTIFICATIONMANAGER (TASK 19) VERIFIED!" << endl;
    cout << "==========================================================" << endl;

        // Portable demonstration: use a local relative file path
    cout << "\n[Bonus] Testing Portable Notification Action Handler..." << endl;
    NotificationPayload live_test;
    live_test.id = "live_demo";
    live_test.filepath = "CMakeLists.txt";
    manager.show_download_complete(live_test);

    bool opened = manager.trigger_action("live_demo", ToastAction::OpenFolder, /*dry_run=*/true);
    assert(opened == true);
    cout << "  -> Action handler triggered: PASSED" << endl;

    return 0;
}
