#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "gui/MainWindow.h"
#include "queue/DownloadQueue.h"
#include "config/SettingsManager.h"
#include "ipc/LocalServer.h"
#include "net/RemoteDashboard.h"
#include "media/StreamPreviewer.h"
#include "net/PowerMonitor.h"
#include "net/ChannelBonding.h"
#include "net/NetworkMonitor.h"
#include "security/Vault.h"
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
    server.attach_settings(&settings);

    RemoteDashboard remote_dashboard;
    remote_dashboard.attach_to_server(server, &queue);

    // Advanced Backend Modules Integration (Phase D)
    StreamPreviewer::attach_to_server(server, &queue);

    PowerMonitor power_monitor;
    power_monitor.set_on_power_state_changed_callback([&queue](const PowerStatus& status, uint32_t recommended_threads) {
        if (status.is_critical) {
            std::cout << "[PowerMonitor] Critical battery alert (" << status.battery_percent << "%). Auto-pausing queue." << std::endl;
            queue.pause();
        } else {
            queue.set_max_total_threads(recommended_threads);
        }
    });
    power_monitor.start();

    auto channel_bonding = std::make_shared<ChannelBonding>();
    server.register_route("GET", "/api/bonding/adapters", [channel_bonding](const HttpRequest&) -> HttpResponse {
        json res;
        res["status"] = "SUCCESS";
        auto adapters = channel_bonding->discover_adapters();
        json arr = json::array();
        for (const auto& a : adapters) {
            arr.push_back({
                {"name", a.name},
                {"description", a.description},
                {"ip", a.ip_address},
                {"is_up", a.is_up},
                {"is_enabled", a.is_enabled},
                {"weight", a.weight}
            });
        }
        res["adapters"] = arr;
        res["bonding_mode"] = static_cast<int>(channel_bonding->get_bonding_mode());
        return HttpResponse::json_response(200, res);
    });

    server.register_route("POST", "/api/bonding/settings", [channel_bonding](const HttpRequest& req) -> HttpResponse {
        if (req.json_body.is_object()) {
            int mode = req.json_body.value("mode", 0);
            channel_bonding->set_bonding_mode(static_cast<BondingMode>(mode));
        }
        json res;
        res["status"] = "SUCCESS";
        return HttpResponse::json_response(200, res);
    });

    auto vault = std::make_shared<Vault>();
    vault->load_from_file("vault.dat");
    server.register_route("GET", "/api/vault/credentials", [vault](const HttpRequest&) -> HttpResponse {
        json res;
        res["status"] = "SUCCESS";
        res["domains"] = vault->list_domains();
        return HttpResponse::json_response(200, res);
    });
    server.register_route("POST", "/api/vault/credentials", [vault](const HttpRequest& req) -> HttpResponse {
        if (req.json_body.is_object()) {
            SiteCredential cred;
            cred.domain = req.json_body.value("domain", "");
            cred.username = req.json_body.value("username", "");
            cred.password = req.json_body.value("password", "");
            cred.cookies = req.json_body.value("cookies", "");
            if (!cred.domain.empty()) {
                vault->save_credential(cred);
                vault->save_to_file("vault.dat");
            }
        }
        json res;
        res["status"] = "SUCCESS";
        return HttpResponse::json_response(200, res);
    });
    server.register_route("POST", "/api/vault/remove", [vault](const HttpRequest& req) -> HttpResponse {
        if (req.json_body.is_object()) {
            std::string dom = req.json_body.value("domain", "");
            if (!dom.empty()) {
                vault->remove_credential(dom);
                vault->save_to_file("vault.dat");
            }
        }
        json res;
        res["status"] = "SUCCESS";
        return HttpResponse::json_response(200, res);
    });

    auto network_monitor = std::make_shared<NetworkMonitor>();
    network_monitor->start();
    server.register_route("GET", "/api/network/qos", [network_monitor](const HttpRequest&) -> HttpResponse {
        json res;
        res["status"] = "SUCCESS";
        auto stats = network_monitor->get_stats();
        res["current_rtt_ms"] = stats.current_rtt_ms;
        res["baseline_rtt_ms"] = stats.baseline_rtt_ms;
        res["gaming_active"] = stats.gaming_active;
        res["active_game"] = stats.active_game_name;
        res["qos_state"] = static_cast<int>(stats.qos_state);
        res["metered"] = stats.metered_connection_active;
        return HttpResponse::json_response(200, res);
    });

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
    window.attach_queue(&queue);

    server.set_tray_notifier([&window](const std::string& title, const std::string& message) {
        window.show_tray_notification(title, message);
    });
    server.set_app_terminator([&window]() {
        window.close();
    });
    server.set_window_restorer([&window]() {
        window.restore_or_launch_window();
    });

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
        } else if (action == "STOP_ALL") {
            queue.stop();
            res["status"] = "SUCCESS";
            res["message"] = "All downloads stopped.";
        } else if (action == "SET_SPEED_LIMIT") {
            bool enabled = req.value("enabled", false);
            uint64_t limit = enabled ? (500 * 1024) : 0;
            auto tasks = queue.get_tasks();
            for (auto& t : tasks) {
                if (t) t->set_speed_limit(limit);
            }
            res["status"] = "SUCCESS";
            res["message"] = enabled ? "Speed limiter enabled (500 KB/s)." : "Speed limiter disabled.";
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
    power_monitor.stop();
    network_monitor->stop();
    server.stop();
    queue.stop();
    return 0;
}
