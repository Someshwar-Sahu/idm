#include "gui/MainWindow.h"
#include <iostream>
#include <cassert>

using namespace idm;

int main() {
    std::cout << "[Test] Starting MainWindow unit test..." << std::endl;

    MainWindow window;

    // Test 1: Window Initialization
    bool init_success = window.init("IDM Ultra Engine", 1280, 800, "ui/index.html");
    assert(init_success);
    assert(window.is_running() == true);
    assert(window.get_title() == "IDM Ultra Engine");
    assert(window.get_width() == 1280);
    assert(window.get_height() == 800);
    assert(window.get_ui_path() == "ui/index.html");
    std::cout << "[Test] Window initialization test passed." << std::endl;

    // Test 2: Default Built-in PING IPC Handler
    std::string ping_req = R"({"action": "PING"})";
    std::string ping_res_str = window.dispatch_ipc_message(ping_req);
    json ping_res = json::parse(ping_res_str);
    assert(ping_res["status"] == "OK");
    assert(ping_res["message"] == "PONG from IDM C++ MainWindow Backend");
    std::cout << "[Test] Built-in PING IPC dispatcher test passed." << std::endl;

    // Test 3: Custom C++ Backend IPC Handler
    window.set_ipc_handler([](const json& req) -> json {
        json res;
        std::string action = req.value("action", "");
        if (action == "ADD_TASK") {
            res["status"] = "SUCCESS";
            res["task_id"] = "task_999";
            res["filename"] = req.value("filename", "unknown");
        } else {
            res["status"] = "UNKNOWN_ACTION";
        }
        return res;
    });

    std::string add_req = R"({"action": "ADD_TASK", "filename": "Ubuntu_24_04.iso", "url": "https://releases.ubuntu.com/24.04/iso"})";
    std::string add_res_str = window.dispatch_ipc_message(add_req);
    json add_res = json::parse(add_res_str);
    assert(add_res["status"] == "SUCCESS");
    assert(add_res["task_id"] == "task_999");
    assert(add_res["filename"] == "Ubuntu_24_04.iso");
    std::cout << "[Test] Custom IPC command dispatcher test passed." << std::endl;

    // Test 4: Window Closure
    window.run_event_loop();
    window.close();
    assert(window.is_running() == false);
    std::cout << "[Test] Window close & teardown test passed." << std::endl;

    std::cout << "[PASSED] MainWindow unit test completed successfully!" << std::endl;
    return 0;
}
