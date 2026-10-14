#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <string>
#include <atomic>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <nlohmann/json.hpp>
#include <wincontypes.h>

namespace idm {

class LocalServer;

using json = nlohmann::json;
using IPCHandler = std::function<json(const json&)>;
using WindowCloseCallback = std::function<void()>;

class MainWindow{
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

    std::string dispatch_ipc_message(const std::string& raw_json_message);

private:
    std::string m_title;
    int m_width;
    int m_height;
    std::string m_ui_path;
    std::atomic<bool> m_is_running{false};
    IPCHandler m_ipc_handler{nullptr};
    WindowCloseCallback m_close_callback{nullptr};
    LocalServer* m_server{nullptr};

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    void* m_process_handler{nullptr};

};
}

#endif