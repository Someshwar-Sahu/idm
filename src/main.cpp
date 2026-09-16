#include "gui/MainWindow.h"
#include "queue/DownloadQueue.h"
#include "config/SettingsManager.h"
#include <iostream>

using namespace idm;

int main(int argc, char* argv[]) {
    std::cout << "=====================================================" << std::endl;
    std::cout << "          IDM Internet Download Manager              " << std::endl;
    std::cout << "               C++ Core Engine v1.0                  " << std::endl;
    std::cout << "=====================================================" << std::endl;

    // 1. Initialize Settings Manager
    SettingsManager settings;
    settings.load("config.json");
    int max_concurrent = settings.get_settings().max_concurrent_download;
    std::cout << "[Main] Settings loaded. Max concurrent downloads: " 
              << max_concurrent << std::endl;

    // 2. Initialize Download Queue
    DownloadQueue queue("Main Queue", max_concurrent);
    queue.start();

    // 3. Initialize Native GUI Window
    MainWindow window;
    if (!window.init("Internet Download Manager Pro", 1150, 750, "ui/index.html")) {
        std::cerr << "[Main] Failed to initialize GUI Window." << std::endl;
        return 1;
    }

    // 4. Bind C++ Backend IPC Message Dispatcher to GUI
    window.set_ipc_handler([&queue](const json& req) -> json {
        json res;
        std::string action = req.value("action", "");
        
        if (action == "ADD_DOWNLOAD") {
            std::string url = req.value("url", "");
            int threads = req.value("threads", 8);
            if (!url.empty()) {
                auto task = std::make_shared<DownloadTask>(url, "./downloads", threads);
                queue.add_task(task);
                res["status"] = "SUCCESS";
                res["message"] = "Task added to queue successfully.";
            } else {
                res["status"] = "ERROR";
                res["message"] = "URL cannot be empty.";
            }
        } else if (action == "PAUSE_ALL") {
            queue.pause();
            res["status"] = "SUCCESS";
            res["message"] = "All downloads paused.";
        } else if (action == "RESUME_ALL") {
            queue.start();
            res["status"] = "SUCCESS";
            res["message"] = "All downloads resumed.";
        } else {
            res["status"] = "OK";
            res["message"] = "IDM C++ Engine Backend Listening.";
        }

        return res;
    });

    // 5. Open Desktop Application Window
    window.run_event_loop();

    std::cout << "[Main] IDM Application closing..." << std::endl;
    queue.stop();
    return 0;
}
