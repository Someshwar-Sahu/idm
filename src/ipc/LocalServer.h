#ifndef LOCAL_SERVER_H
#define LOCAL_SERVER_H

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <nlohmann/json.hpp>

// Forward declaration in global namespace
class DownloadQueue;
class SettingsManager;

namespace idm {

using json = nlohmann::json;

struct HttpRequest {
    std::string method;                                          // "GET", "POST", "OPTIONS", "DELETE"
    std::string path;                                            // "/api/tasks"
    std::string raw_query;                                       // "id=task_1&filter=active"
    std::unordered_map<std::string, std::string> query_params;   // ["id"] = "task_1"
    std::unordered_map<std::string, std::string> headers;        // ["content-type"] = "application/json"
    std::string body;                                            // Raw HTTP payload body
    json json_body;                                              // Parsed JSON payload (if valid)
};

struct HttpResponse {
    int status_code{200};                                        // 200, 204, 400, 404, 500
    std::string status_message{"OK"};
    std::unordered_map<std::string, std::string> headers;
    std::string body;

    static HttpResponse json_response(int code, const json& j);
    static HttpResponse error_response(int code, const std::string& error_msg);
    static HttpResponse cors_preflight_response();
};

using HttpHandler = std::function<HttpResponse(const HttpRequest&)>;

class LocalServer {
public:
    LocalServer();
    ~LocalServer();

    // Lifecycle
    bool start(const std::string& host = "127.0.0.1", int port = 8080);
    void stop();
    bool is_running() const;

    std::string get_host() const;
    int get_port() const;

    // Pluggable Routing
    void register_route(const std::string& method, const std::string& path, HttpHandler handler);

    // Core Integration
    void attach_queue(DownloadQueue* queue);
    void attach_settings(SettingsManager* settings) { m_settings = settings; }

    struct InterceptRequest {
        std::string id;
        std::string url;
        std::string filename;
        std::string cookies;
        std::string referer;
        std::string user_agent;
        uint64_t total_size{0};
    };

    using TrayNotifier = std::function<void(const std::string& title, const std::string& message)>;
    using AppTerminator = std::function<void()>;
    using WindowRestorer = std::function<void()>;
    using InterceptNotifier = std::function<void(const InterceptRequest&)>;

    void set_tray_notifier(TrayNotifier notifier) { m_tray_notifier = notifier; }
    void set_app_terminator(AppTerminator terminator) { m_app_terminator = terminator; }
    void set_window_restorer(WindowRestorer restorer) { m_window_restorer = restorer; }
    void set_intercept_notifier(InterceptNotifier notifier) { m_intercept_notifier = notifier; }
    void notify_events();

    // Request Dispatch & Static Protocol Helpers
    HttpResponse dispatch_request(const HttpRequest& req);
    static HttpRequest parse_raw_request(const std::string& raw_http);
    static std::string format_raw_response(const HttpResponse& res);

    // URL decode helper
    static std::string url_decode(const std::string& in);

    // Direct Intercept & Clipboard Monitoring (Phase E)
    void queue_intercept(const std::string& url, const std::string& filename = "", const std::string& cookies = "");
    void start_clipboard_monitor();
    void stop_clipboard_monitor();
    bool is_clipboard_monitor_running() const;

private:
    void init_default_routes();
    void server_loop();
    void handle_client(uintptr_t client_socket);
    void clipboard_monitor_loop();

    std::string m_host{"127.0.0.1"};
    int m_port{8080};
    std::atomic<bool> m_running{false};
    std::thread m_listener_thread;

    uintptr_t m_server_socket{0};
    std::mutex m_routes_mutex;
    std::unordered_map<std::string, HttpHandler> m_routes;

    std::atomic<int> m_active_clients{0};
    std::condition_variable m_clients_cv;
    std::mutex m_clients_mutex;

    DownloadQueue* m_queue{nullptr};
    SettingsManager* m_settings{nullptr};
    TrayNotifier m_tray_notifier{nullptr};
    AppTerminator m_app_terminator{nullptr};
    WindowRestorer m_window_restorer{nullptr};
    InterceptNotifier m_intercept_notifier{nullptr};

    std::atomic<bool> m_clipboard_monitor_running{false};
    std::thread m_clipboard_thread;
    std::string m_last_clipboard_url;
    std::mutex m_intercept_mutex;
    std::vector<InterceptRequest> m_pending_intercepts;

    std::mutex m_event_mutex;
    std::condition_variable m_event_cv;
    std::atomic<uint64_t> m_event_counter{0};

    // Internal standalone task registry for testing without active DownloadQueue
    struct StandaloneTaskInfo {
        std::string id;
        std::string url;
        std::string save_path;
        int threads{8};
        uint64_t speed_limit_bps{0};
        std::string status{"Downloading"};
        uint64_t total_bytes{104857600};
        uint64_t downloaded_bytes{0};
        bool av_override{false};
    };
    std::mutex m_standalone_mutex;
    std::vector<StandaloneTaskInfo> m_standalone_tasks;
};

} // namespace idm

#endif // LOCAL_SERVER_H
