#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <string>
#include <atomic>
#include <memory>
#include <functional>
#include <nlohmann/json.hpp>

namespace idm {

using json = nlohmann::json;

using IPCHandler = std::function<json(const json&)>;

class MainWindow{
public: 
    MainWindow();
    ~MainWindow();

    bool init(const std::string& title, int width, int height, const std::string& ui_index_path);

    void run_event_loop();

    void close();

    bool is_running() const;
    std::string get_title() const;
    std::string get_ui_path() const;
    int get_width() const;
    int get_height() const;

    void set_ipc_handler(IPCHandler handler);

    std::string dispatch_ipc_message(const std::string& raw_json_message);

private:
    std::string m_title;
    int m_width;
    int m_height;
    std::string m_ui_path;
    std::atomic<bool> m_is_running;
    IPCHandler m_ipc_handler;
};
}

#endif