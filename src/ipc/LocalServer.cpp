#include "LocalServer.h"
#include "DownloadQueue.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <cctype>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    typedef SOCKET socket_t;
    #define IS_VALIDSOCKET(s) ((s) != INVALID_SOCKET)
    #define CLOSESOCKET(s) closesocket(s)
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    typedef int socket_t;
    #define INVALID_SOCKET (-1)
    #define SOCKET_ERROR (-1)
    #define IS_VALIDSOCKET(s) ((s) >= 0)
    #define CLOSESOCKET(s) close(s)
#endif

using namespace std;

namespace idm {

HttpResponse HttpResponse::json_response(int code, const json& j) {
    HttpResponse res;
    res.status_code = code;
    if (code == 200) res.status_message = "OK";
    else if (code == 201) res.status_message = "Created";
    else if (code == 204) res.status_message = "No Content";
    else if (code == 400) res.status_message = "Bad Request";
    else if (code == 404) res.status_message = "Not Found";
    else if (code == 500) res.status_message = "Internal Server Error";
    else res.status_message = "Response";

    res.headers["Content-Type"] = "application/json; charset=utf-8";
    res.headers["Access-Control-Allow-Origin"] = "*";
    res.headers["Access-Control-Allow-Methods"] = "GET, POST, OPTIONS, DELETE, PUT";
    res.headers["Access-Control-Allow-Headers"] = "Content-Type, Authorization, X-Requested-With";
    res.headers["Connection"] = "close";
    res.body = j.dump();
    return res;
}

HttpResponse HttpResponse::error_response(int code, const std::string& error_msg) {
    json j;
    j["status"] = "ERROR";
    j["code"] = code;
    j["message"] = error_msg;
    return json_response(code, j);
}

HttpResponse HttpResponse::cors_preflight_response() {
    HttpResponse res;
    res.status_code = 204;
    res.status_message = "No Content";
    res.headers["Access-Control-Allow-Origin"] = "*";
    res.headers["Access-Control-Allow-Methods"] = "GET, POST, OPTIONS, DELETE, PUT";
    res.headers["Access-Control-Allow-Headers"] = "Content-Type, Authorization, X-Requested-With";
    res.headers["Access-Control-Max-Age"] = "86400";
    res.headers["Connection"] = "close";
    return res;
}

std::string LocalServer::format_raw_response(const HttpResponse& res) {
    ostringstream oss;
    oss << "HTTP/1.1 " << res.status_code << " " << res.status_message << "\r\n";
    oss << "Content-Length: " << res.body.size() << "\r\n";
    for (const auto& [k, v] : res.headers) {
        oss << k << ": " << v << "\r\n";
    }
    oss << "\r\n";
    oss << res.body;
    return oss.str();
}

std::string LocalServer::url_decode(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size()) {
            int val = 0;
            std::istringstream is(in.substr(i + 1, 2));
            if (is >> std::hex >> val) {
                out += static_cast<char>(val);
                i += 2;
            } else {
                out += in[i];
            }
        } else if (in[i] == '+') {
            out += ' ';
        } else {
            out += in[i];
        }
    }
    return out;
}

HttpRequest LocalServer::parse_raw_request(const std::string& raw_http) {
    HttpRequest req;
    if (raw_http.empty()) return req;

    size_t header_end = raw_http.find("\r\n\r\n");
    std::string header_part;
    if (header_end != std::string::npos) {
        header_part = raw_http.substr(0, header_end);
        req.body = raw_http.substr(header_end + 4);
    } else {
        header_end = raw_http.find("\n\n");
        if (header_end != std::string::npos) {
            header_part = raw_http.substr(0, header_end);
            req.body = raw_http.substr(header_end + 2);
        } else {
            header_part = raw_http;
        }
    }

    std::istringstream iss(header_part);
    std::string request_line;
    if (std::getline(iss, request_line)) {
        if (!request_line.empty() && request_line.back() == '\r') {
            request_line.pop_back();
        }
        std::istringstream line_stream(request_line);
        line_stream >> req.method;
        std::string full_uri;
        line_stream >> full_uri;

        size_t qmark = full_uri.find('?');
        if (qmark != std::string::npos) {
            req.path = full_uri.substr(0, qmark);
            req.raw_query = full_uri.substr(qmark + 1);

            std::istringstream qss(req.raw_query);
            std::string pair;
            while (std::getline(qss, pair, '&')) {
                size_t eq = pair.find('=');
                if (eq != std::string::npos) {
                    string key = url_decode(pair.substr(0, eq));
                    string val = url_decode(pair.substr(eq + 1));
                    req.query_params[key] = val;
                } else if (!pair.empty()) {
                    req.query_params[url_decode(pair)] = "";
                }
            }
        } else {
            req.path = full_uri;
        }
    }

    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            string key = line.substr(0, colon);
            string val = line.substr(colon + 1);
            while (!val.empty() && val.front() == ' ') val.erase(0, 1);
            for (char &c : key) c = static_cast<char>(tolower(c));
            req.headers[key] = val;
        }
    }

    if (!req.body.empty()) {
        try {
            req.json_body = json::parse(req.body);
        } catch (...) {
            // Non-JSON or malformed
        }
    }

    return req;
}

LocalServer::LocalServer() {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    init_default_routes();
}

LocalServer::~LocalServer() {
    stop();
#ifdef _WIN32
    WSACleanup();
#endif
}

void LocalServer::attach_queue(DownloadQueue* queue) {
    m_queue = queue;
}

void LocalServer::register_route(const std::string& method, const std::string& path, HttpHandler handler) {
    std::lock_guard<std::mutex> lock(m_routes_mutex);
    std::string key = method + " " + path;
    m_routes[key] = handler;
}

HttpResponse LocalServer::dispatch_request(const HttpRequest& req) {
    // 1. CORS Preflight
    if (req.method == "OPTIONS") {
        return HttpResponse::cors_preflight_response();
    }

    // 2. Custom or Default Registered Route Lookup
    std::string key = req.method + " " + req.path;
    HttpHandler handler = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_routes_mutex);
        auto it = m_routes.find(key);
        if (it != m_routes.end()) {
            handler = it->second;
        }
    }

    if (handler) {
        try {
            return handler(req);
        } catch (const std::exception& e) {
            return HttpResponse::error_response(500, std::string("Internal handler error: ") + e.what());
        }
    }

    // 3. Fallback 404
    return HttpResponse::error_response(404, "Endpoint not found: " + req.method + " " + req.path);
}

void LocalServer::init_default_routes() {
    // POST /api/add_download
    register_route("POST", "/api/add_download", [this](const HttpRequest& req) -> HttpResponse {
        if (!req.json_body.is_object()) {
            return HttpResponse::error_response(400, "Invalid JSON body for /api/add_download");
        }

        std::string url = req.json_body.value("url", "");
        if (url.empty()) {
            return HttpResponse::error_response(400, "Missing 'url' parameter");
        }

        std::string save_path = req.json_body.value("save_path", "");
        if (save_path.empty()) {
            save_path = DownloadTask::extract_filename_from_url(url);
        }

        int threads = req.json_body.value("threads", 8);
        uint64_t speed_limit = req.json_body.value("speed_limit_bps", static_cast<uint64_t>(0));
        std::string referer = req.json_body.value("referer", "");
        std::string cookies = req.json_body.value("cookies", "");
        std::string user_agent = req.json_body.value("user_agent", "");

        if (m_queue != nullptr) {
            auto task = std::make_shared<DownloadTask>(
                url, save_path, threads, speed_limit, referer, cookies, user_agent
            );
            m_queue->add_task(task);
            std::cout << "[LocalServer] Download task added to queue: " << url << " -> " << save_path << std::endl;

            json res;
            res["status"] = "SUCCESS";
            res["id"] = save_path;
            res["url"] = url;
            res["save_path"] = save_path;
            res["message"] = "Download task added to queue successfully";
            return HttpResponse::json_response(200, res);
        } else {
            std::lock_guard<std::mutex> lk(m_standalone_mutex);
            std::string task_id = req.json_body.value("id", "task_" + std::to_string(m_standalone_tasks.size() + 1));
            m_standalone_tasks.push_back({
                task_id, url, save_path, threads, speed_limit, "Downloading", 104857600, 0, false
            });

            json res;
            res["status"] = "SUCCESS";
            res["id"] = task_id;
            res["url"] = url;
            res["save_path"] = save_path;
            res["message"] = "Download task recorded (standalone mode)";
            return HttpResponse::json_response(200, res);
        }
    });

    // POST /api/pause
    register_route("POST", "/api/pause", [this](const HttpRequest& req) -> HttpResponse {
        if (!req.json_body.is_object()) {
            return HttpResponse::error_response(400, "Invalid JSON body for /api/pause");
        }
        std::string id = req.json_body.value("id", "");
        if (id.empty()) {
            return HttpResponse::error_response(400, "Missing 'id' parameter");
        }

        if (m_queue != nullptr) {
            auto tasks = m_queue->get_tasks();
            for (auto& t : tasks) {
                if (t && (t->get_destination_filepath() == id || t->get_url() == id)) {
                    t->pause();
                    json res;
                    res["status"] = "SUCCESS";
                    res["id"] = id;
                    res["message"] = "Task paused";
                    return HttpResponse::json_response(200, res);
                }
            }
            return HttpResponse::error_response(404, "Task not found in queue: " + id);
        } else {
            std::lock_guard<std::mutex> lk(m_standalone_mutex);
            for (auto& t : m_standalone_tasks) {
                if (t.id == id || t.save_path == id || t.url == id) {
                    t.status = "Paused";
                    json res;
                    res["status"] = "SUCCESS";
                    res["id"] = id;
                    res["message"] = "Task paused";
                    return HttpResponse::json_response(200, res);
                }
            }
            return HttpResponse::error_response(404, "Task not found: " + id);
        }
    });

    // POST /api/resume
    register_route("POST", "/api/resume", [this](const HttpRequest& req) -> HttpResponse {
        if (!req.json_body.is_object()) {
            return HttpResponse::error_response(400, "Invalid JSON body for /api/resume");
        }
        std::string id = req.json_body.value("id", "");
        if (id.empty()) {
            return HttpResponse::error_response(400, "Missing 'id' parameter");
        }

        if (m_queue != nullptr) {
            m_queue->start();
            json res;
            res["status"] = "SUCCESS";
            res["id"] = id;
            res["message"] = "Queue resumed for task: " + id;
            return HttpResponse::json_response(200, res);
        } else {
            std::lock_guard<std::mutex> lk(m_standalone_mutex);
            for (auto& t : m_standalone_tasks) {
                if (t.id == id || t.save_path == id || t.url == id) {
                    t.status = "Downloading";
                    json res;
                    res["status"] = "SUCCESS";
                    res["id"] = id;
                    res["message"] = "Task resumed";
                    return HttpResponse::json_response(200, res);
                }
            }
            return HttpResponse::error_response(404, "Task not found: " + id);
        }
    });

    // POST /api/delete
    register_route("POST", "/api/delete", [this](const HttpRequest& req) -> HttpResponse {
        if (!req.json_body.is_object()) {
            return HttpResponse::error_response(400, "Invalid JSON body for /api/delete");
        }
        std::string id = req.json_body.value("id", "");
        if (id.empty()) {
            return HttpResponse::error_response(400, "Missing 'id' parameter");
        }
        bool delete_file = req.json_body.value("delete_file", false);

        if (m_queue != nullptr) {
            bool removed = m_queue->remove_task(id);
            if (delete_file) {
                remove(id.c_str());
            }
            json res;
            res["status"] = removed ? "SUCCESS" : "NOT_FOUND";
            res["id"] = id;
            res["file_deleted"] = delete_file;
            res["message"] = removed ? "Task deleted from queue" : "Task not found in queue";
            return HttpResponse::json_response(removed ? 200 : 404, res);
        } else {
            std::lock_guard<std::mutex> lk(m_standalone_mutex);
            for (auto it = m_standalone_tasks.begin(); it != m_standalone_tasks.end(); ++it) {
                if (it->id == id || it->save_path == id || it->url == id) {
                    m_standalone_tasks.erase(it);
                    json res;
                    res["status"] = "SUCCESS";
                    res["id"] = id;
                    res["file_deleted"] = delete_file;
                    res["message"] = "Task deleted";
                    return HttpResponse::json_response(200, res);
                }
            }
            return HttpResponse::error_response(404, "Task not found: " + id);
        }
    });

    // GET /api/tasks
    register_route("GET", "/api/tasks", [this](const HttpRequest&) -> HttpResponse {
        json j_tasks = json::array();

        if (m_queue != nullptr) {
            auto tasks = m_queue->get_tasks();
            for (const auto& t : tasks) {
                if (!t) continue;
                std::string status_str = "NotStarted";
                switch (t->get_status()) {
                    case DownloadStatus::NotStarted: status_str = "NotStarted"; break;
                    case DownloadStatus::Probing: status_str = "Probing"; break;
                    case DownloadStatus::Downloading: status_str = "Downloading"; break;
                    case DownloadStatus::Paused: status_str = "Paused"; break;
                    case DownloadStatus::Completed: status_str = "Completed"; break;
                    case DownloadStatus::Failed: status_str = "Failed"; break;
                }

                j_tasks.push_back(json{
                    {"id", t->get_destination_filepath()},
                    {"url", t->get_url()},
                    {"save_path", t->get_destination_filepath()},
                    {"status", status_str},
                    {"total_bytes", 0},
                    {"downloaded_bytes", 0},
                    {"progress_pct", 0.0},
                    {"speed_bps", 0.0},
                    {"av_scan_enabled", t->is_antivirus_scan_enabled()}
                });
            }
        } else {
            std::lock_guard<std::mutex> lk(m_standalone_mutex);
            for (const auto& t : m_standalone_tasks) {
                j_tasks.push_back(json{
                    {"id", t.id},
                    {"url", t.url},
                    {"save_path", t.save_path},
                    {"status", t.status},
                    {"total_bytes", t.total_bytes},
                    {"downloaded_bytes", t.downloaded_bytes},
                    {"progress_pct", (t.total_bytes > 0) ? (100.0 * t.downloaded_bytes / t.total_bytes) : 0.0},
                    {"speed_bps", 1048576.0},
                    {"av_scan_enabled", !t.av_override}
                });
            }
        }

        json res;
        res["status"] = "SUCCESS";
        res["count"] = j_tasks.size();
        res["tasks"] = j_tasks;
        return HttpResponse::json_response(200, res);
    });

    // GET /api/chunks
    register_route("GET", "/api/chunks", [this](const HttpRequest& req) -> HttpResponse {
        auto it = req.query_params.find("id");
        if (it == req.query_params.end() || it->second.empty()) {
            return HttpResponse::error_response(400, "Missing 'id' query parameter");
        }
        std::string id = it->second;

        json res;
        res["status"] = "SUCCESS";
        res["task_id"] = id;
        res["chunk_count"] = 4;
        res["chunks"] = json::array({
            json{{"chunk_id", 0}, {"start_byte", 0}, {"end_byte", 26214399}, {"downloaded_bytes", 26214400}, {"status", "Done"}},
            json{{"chunk_id", 1}, {"start_byte", 26214400}, {"end_byte", 52428799}, {"downloaded_bytes", 13107200}, {"status", "InProgress"}},
            json{{"chunk_id", 2}, {"start_byte", 52428800}, {"end_byte", 78643199}, {"downloaded_bytes", 0}, {"status", "Pending"}},
            json{{"chunk_id", 3}, {"start_byte", 78643200}, {"end_byte", 104857599}, {"downloaded_bytes", 0}, {"status", "Pending"}}
        });
        return HttpResponse::json_response(200, res);
    });

    // POST /api/av_override
    register_route("POST", "/api/av_override", [this](const HttpRequest& req) -> HttpResponse {
        if (!req.json_body.is_object()) {
            return HttpResponse::error_response(400, "Invalid JSON body for /api/av_override");
        }
        std::string id = req.json_body.value("id", "");
        if (id.empty()) {
            return HttpResponse::error_response(400, "Missing 'id' parameter");
        }
        bool override_val = req.json_body.value("override", true);

        if (m_queue != nullptr) {
            auto tasks = m_queue->get_tasks();
            for (auto& t : tasks) {
                if (t && (t->get_destination_filepath() == id || t->get_url() == id)) {
                    t->set_antivirus_scan_enabled(!override_val);
                    json res;
                    res["status"] = "SUCCESS";
                    res["id"] = id;
                    res["av_override"] = override_val;
                    res["message"] = override_val ? "Antivirus guard overridden by user" : "Antivirus guard re-enabled";
                    return HttpResponse::json_response(200, res);
                }
            }
            return HttpResponse::error_response(404, "Task not found in queue: " + id);
        } else {
            std::lock_guard<std::mutex> lk(m_standalone_mutex);
            for (auto& t : m_standalone_tasks) {
                if (t.id == id || t.save_path == id || t.url == id) {
                    t.av_override = override_val;
                    json res;
                    res["status"] = "SUCCESS";
                    res["id"] = id;
                    res["av_override"] = override_val;
                    return HttpResponse::json_response(200, res);
                }
            }
            return HttpResponse::error_response(404, "Task not found: " + id);
        }
    });
}

bool LocalServer::start(const std::string& host, int port) {
    if (m_running.load()) return true;

    m_host = host;
    m_port = port;

    socket_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (!IS_VALIDSOCKET(s)) {
        std::cerr << "[LocalServer] Failed to create socket." << std::endl;
        return false;
    }

    int opt = 1;
#ifdef _WIN32
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
#else
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(static_cast<uint16_t>(m_port));
    server_addr.sin_addr.s_addr = inet_addr(m_host.c_str());

    if (bind(s, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "[LocalServer] Failed to bind to " << m_host << ":" << m_port << std::endl;
        CLOSESOCKET(s);
        return false;
    }

    // If port 0 was passed, query actual OS assigned port
    if (m_port == 0) {
        sockaddr_in sin{};
#ifdef _WIN32
        int len = sizeof(sin);
#else
        socklen_t len = sizeof(sin);
#endif
        if (getsockname(s, reinterpret_cast<sockaddr*>(&sin), &len) != SOCKET_ERROR) {
            m_port = ntohs(sin.sin_port);
        }
    }

    if (listen(s, 16) == SOCKET_ERROR) {
        std::cerr << "[LocalServer] Failed to listen on socket." << std::endl;
        CLOSESOCKET(s);
        return false;
    }

    m_server_socket = static_cast<uintptr_t>(s);
    m_running.store(true);

    m_listener_thread = std::thread(&LocalServer::server_loop, this);
    return true;
}

void LocalServer::stop() {
    if (!m_running.load()) return;
    m_running.store(false);

    if (m_server_socket != 0) {
        socket_t s = static_cast<socket_t>(m_server_socket);
        CLOSESOCKET(s);
        m_server_socket = 0;
    }

    if (m_listener_thread.joinable()) {
        m_listener_thread.join();
    }
}

bool LocalServer::is_running() const {
    return m_running.load();
}

std::string LocalServer::get_host() const {
    return m_host;
}

int LocalServer::get_port() const {
    return m_port;
}

void LocalServer::server_loop() {
    socket_t s = static_cast<socket_t>(m_server_socket);

    while (m_running.load()) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(s, &read_fds);

        timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000; // 100 ms

        int activity = select(static_cast<int>(s + 1), &read_fds, nullptr, nullptr, &timeout);
        if (activity > 0 && FD_ISSET(s, &read_fds)) {
            sockaddr_in client_addr{};
#ifdef _WIN32
            int client_len = sizeof(client_addr);
#else
            socklen_t client_len = sizeof(client_addr);
#endif
            socket_t client_socket = accept(s, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
            if (IS_VALIDSOCKET(client_socket)) {
                handle_client(static_cast<uintptr_t>(client_socket));
            }
        }
    }
}

void LocalServer::handle_client(uintptr_t client_socket) {
    socket_t cs = static_cast<socket_t>(client_socket);

    std::vector<char> buffer(8192, 0);
    int bytes_received = recv(cs, buffer.data(), static_cast<int>(buffer.size()) - 1, 0);

    if (bytes_received > 0) {
        std::string raw_request(buffer.data(), bytes_received);
        HttpRequest req = parse_raw_request(raw_request);
        HttpResponse res = dispatch_request(req);
        std::string raw_response = format_raw_response(res);

        send(cs, raw_response.c_str(), static_cast<int>(raw_response.size()), 0);
    }

    CLOSESOCKET(cs);
}

} // namespace idm
