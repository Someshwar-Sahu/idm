#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <string>
#include <atomic>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <nlohmann/json.hpp>

class DownloadQueue;

namespace idm {

class LocalServer;

using json = nlohmann::json;
using IPCHandler = std::function<json(const json&)>;
using WindowCloseCallback = std::function<void()>;

class MainWindow {
public: 
    MainWindow();
    ~MainWindow();

    bool init(const std::string& title, int width, int height, const std::string& ui_index_path);

    void run_event_loop(bool headless = false);

    void close();

    bool is_running() const;
    std::string get_title() const;
    std::string get_ui_path() const;
    int get_width() const;
    int get_height() const;

    void set_ipc_handler(IPCHandler handler);
    void set_close_callback(WindowCloseCallback callback);
    void attach_server(LocalServer* server);
    void attach_queue(DownloadQueue* queue);

    std::string dispatch_ipc_message(const std::string& raw_json_message);

    // Phase 7: Windows Desktop Integration & System Tray
    bool setup_system_tray();
    void remove_system_tray();
    void show_tray_notification(const std::string& title, const std::string& message);
    void restore_or_launch_window();

private:
    std::string m_title;
    int m_width;
    int m_height;
    std::string m_ui_path;
    std::atomic<bool> m_is_running{false};
    IPCHandler m_ipc_handler{nullptr};
    WindowCloseCallback m_close_callback{nullptr};
    LocalServer* m_server{nullptr};
    DownloadQueue* m_queue{nullptr};

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    void* m_process_handler{nullptr};

#ifdef _WIN32
    void* m_tray_hwnd{nullptr};
    bool m_tray_icon_added{false};
#endif
};

}

#endif