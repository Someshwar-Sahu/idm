#include "gui/MainWindow.h"
#include <iostream>
#include <cassert>
#include <cstdio>
#include <string>

using namespace std;
using namespace idm;

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);

    cout << "==========================================================" << endl;
    cout << "  Comprehensive Unit Test: MainWindow & IPC (Pos + Neg)   " << endl;
    cout << "==========================================================" << endl;

    // =========================================================================
    // 1. DEFAULT INITIALIZATION & ACCESSORS (Positive)
    // =========================================================================
    cout << "\n[1/5] Testing Default Window Construction & Accessors..." << endl;

    MainWindow window_default;
    assert(window_default.is_running() == false);
    assert(window_default.get_width() == 1100);
    assert(window_default.get_height() == 720);
    assert(window_default.get_title() == "IDM Native Engine Dashboard");
    assert(window_default.get_ui_path().empty() == true);
    cout << "  -> Default construction and dimension accessors: PASSED" << endl;

    // =========================================================================
    // 2. WINDOW INIT & MISSING UI FILE GUARDS (Positive & Negative)
    // =========================================================================
    cout << "\n[2/5] Testing init() With Valid & Missing UI Paths (Pos + Neg)..." << endl;

    // 2.1 Negative: Non-existent UI path must return false
    MainWindow window_bad_path;
    bool bad_init = window_bad_path.init("Invalid Path Test", 800, 600, "ui/non_existent_file_xyz_123.html");
    assert(bad_init == false);
    assert(window_bad_path.is_running() == false);

    // 2.2 Positive: Valid existing UI path ("ui/index.html")
    MainWindow window;
    bool init_ok = window.init("IDM Ultra Engine", 1280, 800, "ui/index.html");
    assert(init_ok == true);
    assert(window.is_running() == true);
    assert(window.get_title() == "IDM Ultra Engine");
    assert(window.get_width() == 1280);
    assert(window.get_height() == 800);
    assert(window.get_ui_path() == "ui/index.html");
    cout << "  -> Missing UI file rejection and valid UI initialization: PASSED" << endl;

    // =========================================================================
    // 3. BUILT-IN IPC DISPATCHER (Positive & Negative)
    // =========================================================================
    cout << "\n[3/5] Testing Default Built-in IPC Dispatcher (Pos + Neg)..." << endl;

    // 3.1 Positive: Built-in PING
    string ping_req = R"({"action": "PING"})";
    string ping_res_str = window.dispatch_ipc_message(ping_req);
    json ping_res = json::parse(ping_res_str);
    assert(ping_res["status"] == "OK");
    assert(ping_res["message"] == "PONG from IDM C++ MainWindow Backend");

    // 3.2 Negative: Unregistered action with default handler returns ERROR
    string unhandled_req = R"({"action": "UNREGISTERED_ACTION_TEST"})";
    string unhandled_res_str = window.dispatch_ipc_message(unhandled_req);
    json unhandled_res = json::parse(unhandled_res_str);
    assert(unhandled_res["status"] == "ERROR");
    cout << "  -> Built-in PING and unregistered action handling: PASSED" << endl;

    // =========================================================================
    // 4. CUSTOM IPC HANDLER & CORRUPT JSON GUARDS (Positive & Negative)
    // =========================================================================
    cout << "\n[4/5] Testing Custom IPC Handler & Corrupt JSON Guards..." << endl;

    // 4.1 Positive: Register custom backend IPC handler
    window.set_ipc_handler([](const json& req) -> json {
        json res;
        string action = req.value("action", "");
        if (action == "ADD_DOWNLOAD") {
            res["status"] = "SUCCESS";
            res["task_id"] = "task_42";
            res["filename"] = req.value("filename", "unknown");
        } else {
            res["status"] = "UNRECOGNIZED_ACTION";
        }
        return res;
    });

    string add_req = R"({"action": "ADD_DOWNLOAD", "filename": "test_video.mp4"})";
    string add_res_str = window.dispatch_ipc_message(add_req);
    json add_res = json::parse(add_res_str);
    assert(add_res["status"] == "SUCCESS");
    assert(add_res["task_id"] == "task_42");
    assert(add_res["filename"] == "test_video.mp4");

    // 4.2 Negative: Malformed / Corrupt JSON string
    string corrupt_req = "{ action: 'no quotes', broken [ ";
    string corrupt_res_str = window.dispatch_ipc_message(corrupt_req);
    json corrupt_res = json::parse(corrupt_res_str);
    assert(corrupt_res["status"] == "ERROR");
    assert(corrupt_res["message"].get<string>().find("JSON Parse Failure") != string::npos);

    // 4.3 Negative: Empty string input
    string empty_res_str = window.dispatch_ipc_message("");
    json empty_res = json::parse(empty_res_str);
    assert(empty_res["status"] == "ERROR");
    cout << "  -> Custom IPC commands and malformed JSON parse guards: PASSED" << endl;

    // =========================================================================
    // 5. WINDOW CLOSURE & IDEMPOTENCE (Positive)
    // =========================================================================
    cout << "\n[5/5] Testing Window Closure Lifecycle & Idempotence..." << endl;

    assert(window.is_running() == true);
    window.close();
    assert(window.is_running() == false);

    // Consecutive second call to close() must be safe
    window.close();
    assert(window.is_running() == false);
    cout << "  -> Window close and idempotence: PASSED" << endl;

    cout << "\n==========================================================" << endl;
    cout << "  [PASSED] 100% OF MAINWINDOW & IPC FUNCTIONS VERIFIED!   " << endl;
    cout << "==========================================================" << endl;

    return 0;
}
