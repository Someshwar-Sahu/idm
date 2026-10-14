#include "gui/MainWindow.h"
#include "queue/DownloadQueue.h"
#include "config/SettingsManager.h"
#include "ipc/LocalServer.h"
#include "net/RemoteDashboard.h"
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

    // 3. Initialize Local REST API Server (Port 8080) & Remote Dashboard
    LocalServer server;
    server.attach_queue(&queue);

    RemoteDashboard remote_dashboard;
    remote_dashboard.attach_to_server(server, &queue);

    int port = 8080;
    if (server.start("127.0.0.1", port)) {
        std::cout << "[Main] Local REST Server listening on: http://127.0.0.1:" << port << std::endl;
        std::cout << "[Main] Remote Web Dashboard: " << remote_dashboard.get_dashboard_url(port) << std::endl;
        if (remote_dashboard.is_pin_required()) {
            std::cout << "[Main] Remote Pairing PIN: " << remote_dashboard.get_pin() << std::endl;
        }
    } else {
        std::cerr << "[Main] Warning: Failed to bind Local Server to port " << port << std::endl;
    }

    // 4. Initialize Native GUI Window
    MainWindow window;
    window.attach_server(&server);
    if (!window.init("Internet Download Manager Pro", 1150, 750, "ui/index.html")) {
        std::cerr << "[Main] Failed to initialize GUI Window." << std::endl;
        server.stop();
        queue.stop();
        return 1;
    }

    // 4. Bind C++ Backend IPC Message Dispatcher to GUI
    window.set_ipc_handler([&queue, &settings](const json& req) -> json {
        json res;
        std::string action = req.value("action", "");
        
        if (action == "ADD_DOWNLOAD") {
            std::string url = req.value("url", "");
            int threads = req.value("threads", settings.get_settings().default_connections_per_task);
            uint64_t file_size = req.value("file_size", static_cast<uint64_t>(0));
            bool force_override = req.value("force_override", false);

            if (url.empty()) {
                res["status"] = "ERROR";
                res["message"] = "URL cannot be empty.";
                return res;
            }

            // Task 8B: Small File Bypass Check
            if (!settings.should_intercept_download(file_size, force_override)) {
                res["status"] = "BYPASS";
                res["message"] = "File size below threshold, bypassing to browser.";
                return res;
            }

            // Task 8: Category Target Filepath Resolution
            std::string filename = req.value("filename", "");
            if (filename.empty()) {
                filename = DownloadTask::extract_filename_from_url(url);
            }
            std::string target_filepath = settings.get_target_filepath_for_filename(filename);

            auto task = std::make_shared<DownloadTask>(
                url,
                target_filepath,
                threads,
                settings.get_settings().manual_speed_limit_bps,
                req.value("referer", ""),
                req.value("cookies", ""),
                req.value("user_agent", ""),
                settings.get_settings().cache_directory
            );

            queue.add_task(task);
            res["status"] = "SUCCESS";
            res["target_filepath"] = target_filepath;
            res["message"] = "Task added to queue successfully.";
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
    bool headless = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--headless" || std::string(argv[i]) == "-h") {
            headless = true;
        }
    }
    window.run_event_loop(headless);

    std::cout << "[Main] IDM Application closing..." << std::endl;
    server.stop();
    queue.stop();
    return 0;
}
