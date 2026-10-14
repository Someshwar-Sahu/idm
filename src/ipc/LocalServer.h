#ifndef LOCAL_SERVER_H
#define LOCAL_SERVER_H

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <nlohmann/json.hpp>

// Forward declaration in global namespace
class DownloadQueue;

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
    bool start(const std::string& host = "127.0.0.1", int port = 9090);
    void stop();
    bool is_running() const;

    std::string get_host() const;
    int get_port() const;

    // Pluggable Routing
    void register_route(const std::string& method, const std::string& path, HttpHandler handler);

    // Core Integration
    void attach_queue(DownloadQueue* queue);

    // Request Dispatch & Static Protocol Helpers
    HttpResponse dispatch_request(const HttpRequest& req);
    static HttpRequest parse_raw_request(const std::string& raw_http);
    static std::string format_raw_response(const HttpResponse& res);

    // URL decode helper
    static std::string url_decode(const std::string& in);

private:
    void init_default_routes();
    void server_loop();
    void handle_client(uintptr_t client_socket);

    std::string m_host{"127.0.0.1"};
    int m_port{9090};
    std::atomic<bool> m_running{false};
    std::thread m_listener_thread;

    uintptr_t m_server_socket{0};
    std::mutex m_routes_mutex;
    std::unordered_map<std::string, HttpHandler> m_routes;

    DownloadQueue* m_queue{nullptr};

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
