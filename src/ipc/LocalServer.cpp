#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #include <shellapi.h>
    #include <shlobj.h>
    #include <commdlg.h>
    #ifdef PlaySound
    #undef PlaySound
    #endif
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

#include "LocalServer.h"
#include "DownloadQueue.h"
#include "DownloadTask.h"
#include "FileWriter.h"
#include "AntivirusScanner.h"
#include "../config/SettingsManager.h"
#include <curl/curl.h>
#include <filesystem>
#include <chrono>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cctype>

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
    res.headers["Access-Control-Allow-Private-Network"] = "true";
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
    res.headers["Access-Control-Allow-Private-Network"] = "true";
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

void LocalServer::notify_events() {
    {
        std::lock_guard<std::mutex> lock(m_event_mutex);
        m_event_counter++;
    }
    m_event_cv.notify_all();
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

    // 3. Static File Serving from ui/ directory
    if (req.method == "GET") {
        std::string rel_path = req.path;
        if (rel_path == "/" || rel_path.empty()) {
            rel_path = "/index.html";
        }
        if (rel_path.rfind("/ui/", 0) == 0) {
            rel_path = rel_path.substr(3);
        }

        if (rel_path.find("..") == std::string::npos) {
            std::string file_path = "ui" + rel_path;
            std::ifstream file(file_path, std::ios::binary);
            if (!file.is_open()) {
#ifdef _WIN32
                char buf[MAX_PATH];
                if (GetModuleFileNameA(NULL, buf, MAX_PATH) > 0) {
                    std::filesystem::path exe_dir = std::filesystem::path(buf).parent_path();
                    std::filesystem::path cand1 = exe_dir / ("ui" + rel_path);
                    if (std::filesystem::exists(cand1)) {
                        file.open(cand1.string(), std::ios::binary);
                        file_path = cand1.string();
                    } else {
                        std::filesystem::path cand2 = exe_dir.parent_path() / ("ui" + rel_path);
                        if (std::filesystem::exists(cand2)) {
                            file.open(cand2.string(), std::ios::binary);
                            file_path = cand2.string();
                        }
                    }
                }
#endif
            }
#ifdef _WIN32
            if (!file.is_open()) {
                int res_id = 0;
                if (rel_path == "/index.html" || rel_path == "/") res_id = 1001;
                else if (rel_path == "/css/styles.css") res_id = 1002;
                else if (rel_path == "/js/app.js") res_id = 1003;
                else if (rel_path == "/js/chunk_visualizer.js") res_id = 1004;

                if (res_id != 0) {
                    HINSTANCE hInst = GetModuleHandleA(NULL);
                    HRSRC hRes = FindResourceA(hInst, MAKEINTRESOURCEA(res_id), "RCDATA");
                    if (hRes) {
                        HGLOBAL hData = LoadResource(hInst, hRes);
                        DWORD res_size = SizeofResource(hInst, hRes);
                        if (hData && res_size > 0) {
                            const char* pData = static_cast<const char*>(LockResource(hData));
                            if (pData) {
                                HttpResponse res;
                                res.status_code = 200;
                                res.status_message = "OK";
                                std::string mime = "application/octet-stream";
                                if (res_id == 1001) mime = "text/html; charset=utf-8";
                                else if (res_id == 1002) mime = "text/css; charset=utf-8";
                                else if (res_id == 1003 || res_id == 1004) mime = "application/javascript; charset=utf-8";
                                res.headers["Content-Type"] = mime;
                                res.headers["Access-Control-Allow-Origin"] = "*";
                                res.headers["Access-Control-Allow-Methods"] = "GET, POST, OPTIONS, DELETE, PUT";
                                res.headers["Access-Control-Allow-Headers"] = "Content-Type, Authorization, X-Requested-With";
                                res.headers["Access-Control-Allow-Private-Network"] = "true";
                                res.headers["Connection"] = "close";
                                res.body = std::string(pData, res_size);
                                return res;
                            }
                        }
                    }
                }
            }
#endif
            if (file.is_open()) {
                std::stringstream buffer;
                buffer << file.rdbuf();
                std::string content = buffer.str();

                HttpResponse res;
                res.status_code = 200;
                res.status_message = "OK";

                std::string mime = "application/octet-stream";
                if (file_path.rfind(".html") != std::string::npos) mime = "text/html; charset=utf-8";
                else if (file_path.rfind(".css") != std::string::npos) mime = "text/css; charset=utf-8";
                else if (file_path.rfind(".js") != std::string::npos) mime = "application/javascript; charset=utf-8";
                else if (file_path.rfind(".json") != std::string::npos) mime = "application/json; charset=utf-8";
                else if (file_path.rfind(".png") != std::string::npos) mime = "image/png";
                else if (file_path.rfind(".svg") != std::string::npos) mime = "image/svg+xml";
                else if (file_path.rfind(".ico") != std::string::npos) mime = "image/x-icon";

                res.headers["Content-Type"] = mime;
                res.headers["Access-Control-Allow-Origin"] = "*";
                res.headers["Access-Control-Allow-Methods"] = "GET, POST, OPTIONS, DELETE, PUT";
                res.headers["Access-Control-Allow-Headers"] = "Content-Type, Authorization, X-Requested-With";
                res.headers["Access-Control-Allow-Private-Network"] = "true";
                res.headers["Connection"] = "close";
                res.body = content;
                return res;
            }
        }
    }

    // 4. Fallback 404
    return HttpResponse::error_response(404, "Endpoint not found: " + req.method + " " + req.path);
}

void LocalServer::init_default_routes() {
    // POST /api/probe_url
    register_route("POST", "/api/probe_url", [this](const HttpRequest& req) -> HttpResponse {
        if (!req.json_body.is_object()) {
            return HttpResponse::error_response(400, "Invalid JSON body for /api/probe_url");
        }
        std::string url = req.json_body.value("url", "");
        if (url.empty()) {
            return HttpResponse::error_response(400, "Missing 'url' parameter");
        }
        std::string cookies = req.json_body.value("cookies", "");
        std::string referer = req.json_body.value("referer", "");
        std::string user_agent = req.json_body.value("user_agent", "");

        std::string initial_name = DownloadTask::extract_filename_from_url(url);
        DownloadTask task(url, initial_name, 1, 0, referer, cookies, user_agent);
        bool probed = task.probe_url();

        uint64_t total_size = task.get_total_size();
        std::string dest_path = task.get_destination_filepath();
        std::filesystem::path p(dest_path);
        std::string file_name = p.filename().string();
        std::string ext = p.extension().string();
        for (char &c : ext) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

        std::string category = "General";
        if (ext == ".zip" || ext == ".rar" || ext == ".7z" || ext == ".tar" || ext == ".gz" || ext == ".iso" || ext == ".dat" || ext == ".bin" || ext == ".csv") {
            category = "Compressed";
        } else if (ext == ".mp4" || ext == ".mkv" || ext == ".avi" || ext == ".webm" || ext == ".mov" || ext == ".flv") {
            category = "Videos";
        } else if (ext == ".mp3" || ext == ".wav" || ext == ".flac" || ext == ".aac" || ext == ".ogg") {
            category = "Music";
        } else if (ext == ".pdf" || ext == ".doc" || ext == ".docx" || ext == ".txt" || ext == ".xlsx") {
            category = "Documents";
        } else if (ext == ".exe" || ext == ".msi" || ext == ".apk" || ext == ".bat") {
            category = "Programs";
        }

        std::string size_str;
        if (total_size > 0) {
            double mb = static_cast<double>(total_size) / (1024.0 * 1024.0);
            if (mb >= 1024.0) {
                double gb = mb / 1024.0;
                char buf[64];
                snprintf(buf, sizeof(buf), "%.2f GB (%llu bytes)", gb, static_cast<unsigned long long>(total_size));
                size_str = buf;
            } else {
                char buf[64];
                snprintf(buf, sizeof(buf), "%.2f MB (%llu bytes)", mb, static_cast<unsigned long long>(total_size));
                size_str = buf;
            }
        } else {
            size_str = "Unknown";
        }

        bool file_exists = false;
        std::string suggested_path = dest_path;
        try {
            if (std::filesystem::exists(dest_path)) {
                file_exists = true;
                suggested_path = FileWriter::generate_unique_filepath(dest_path);
            }
        } catch (...) {}

        json res;
        res["status"] = "SUCCESS";
        res["probed"] = probed;
        res["url"] = task.get_url();
        res["file_name"] = file_name;
        res["total_size"] = total_size;
        res["file_size_formatted"] = size_str;
        res["category"] = category;
        res["supports_ranges"] = task.supports_ranges();
        res["file_exists"] = file_exists;
        res["suggested_path"] = suggested_path;
        res["last_error"] = task.get_last_error();
        return HttpResponse::json_response(200, res);
    });

    // POST /api/intercept
    register_route("POST", "/api/intercept", [this](const HttpRequest& req) -> HttpResponse {
        if (!req.json_body.is_object()) {
            return HttpResponse::error_response(400, "Invalid JSON body for /api/intercept");
        }
        std::string url = req.json_body.value("url", "");
        if (url.empty()) {
            return HttpResponse::error_response(400, "Missing 'url' parameter");
        }

        InterceptRequest item;
        item.id = "intercept_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        item.url = url;
        item.filename = req.json_body.value("filename", "");
        item.cookies = req.json_body.value("cookies", "");
        item.referer = req.json_body.value("referer", "");
        item.user_agent = req.json_body.value("user_agent", "");
        item.total_size = req.json_body.value("total_size", static_cast<uint64_t>(0));

        if (m_intercept_notifier) {
            m_intercept_notifier(item);
            notify_events();

            json res;
            res["status"] = "SUCCESS";
            res["id"] = item.id;
            res["message"] = "Intercept dispatched to GUI";
            return HttpResponse::json_response(200, res);
        }

        bool show_dialog = (m_settings != nullptr) ? m_settings->get_settings().show_start_download_dialog : false;
        if (!show_dialog && m_queue != nullptr) {
            std::string save_dir = (m_settings != nullptr) ? m_settings->get_settings().default_download_directory : "C:/Downloads";
            std::string fname = item.filename.empty() ? DownloadTask::extract_filename_from_url(url) : item.filename;
            std::string cat = (m_settings != nullptr) ? m_settings->determine_category_for_filename(fname) : "General";
            std::string cat_dir = (m_settings != nullptr) ? m_settings->get_category_path(cat) : save_dir;
            std::string full_path = cat_dir + "/" + fname;
            auto task = std::make_shared<DownloadTask>(url, full_path, 8, 0, item.referer, item.cookies, item.user_agent);
            m_queue->add_task(task);
            if (m_tray_notifier) {
                m_tray_notifier("Download Started", fname);
            }
            notify_events();
            json res;
            res["status"] = "SUCCESS";
            res["id"] = task->get_destination_filepath();
            res["message"] = "Download started directly";
            return HttpResponse::json_response(200, res);
        }

        {
            std::lock_guard<std::mutex> lk(m_intercept_mutex);
            m_pending_intercepts.push_back(item);
        }

        notify_events();

        json res;
        res["status"] = "SUCCESS";
        res["id"] = item.id;
        res["message"] = "Intercept queued for download confirmation dialog";
        return HttpResponse::json_response(200, res);
    });

    // POST /api/show & GET /api/show (Restore IDM GUI from tray/background)
    auto show_handler = [this](const HttpRequest&) -> HttpResponse {
        if (m_window_restorer) {
            m_window_restorer();
        }
        json res;
        res["status"] = "SUCCESS";
        res["message"] = "Window restore signal sent";
        return HttpResponse::json_response(200, res);
    };
    register_route("POST", "/api/show", show_handler);
    register_route("GET", "/api/show", show_handler);

    // GET /api/events (Long-polling endpoint for 0ms reactivity)
    register_route("GET", "/api/events", [this](const HttpRequest& req) -> HttpResponse {
        uint64_t client_since = 0;
        auto it = req.query_params.find("since");
        if (it != req.query_params.end()) {
            try {
                client_since = std::stoull(it->second);
            } catch (...) {}
        }

        std::unique_lock<std::mutex> lock(m_event_mutex);
        if (m_event_counter.load() == client_since) {
            m_event_cv.wait_for(lock, std::chrono::seconds(25), [this, client_since]() {
                return (m_server_socket != 0 && !m_running.load()) || m_event_counter.load() != client_since;
            });
        }

        json res;
        res["status"] = "SUCCESS";
        res["event_counter"] = m_event_counter.load();
        {
            std::lock_guard<std::mutex> lk(m_intercept_mutex);
            res["pending_intercepts_count"] = m_pending_intercepts.size();
        }
        return HttpResponse::json_response(200, res);
    });

    // GET /api/pending_intercepts
    register_route("GET", "/api/pending_intercepts", [this](const HttpRequest&) -> HttpResponse {
        json j_list = json::array();
        {
            std::lock_guard<std::mutex> lk(m_intercept_mutex);
            for (const auto& item : m_pending_intercepts) {
                j_list.push_back(json{
                    {"id", item.id},
                    {"url", item.url},
                    {"filename", item.filename},
                    {"cookies", item.cookies},
                    {"referer", item.referer},
                    {"user_agent", item.user_agent}
                });
            }
        }
        json res;
        res["status"] = "SUCCESS";
        res["count"] = j_list.size();
        res["intercepts"] = j_list;
        return HttpResponse::json_response(200, res);
    });

    // POST /api/clear_intercept
    register_route("POST", "/api/clear_intercept", [this](const HttpRequest& req) -> HttpResponse {
        std::string id = "";
        if (req.json_body.is_object()) {
            id = req.json_body.value("id", "");
        }
        {
            std::lock_guard<std::mutex> lk(m_intercept_mutex);
            if (id.empty()) {
                if (!m_pending_intercepts.empty()) {
                    m_pending_intercepts.erase(m_pending_intercepts.begin());
                }
            } else {
                for (auto it = m_pending_intercepts.begin(); it != m_pending_intercepts.end(); ++it) {
                    if (it->id == id) {
                        m_pending_intercepts.erase(it);
                        break;
                    }
                }
            }
        }
        notify_events();
        json res;
        res["status"] = "SUCCESS";
        return HttpResponse::json_response(200, res);
    });

    // POST /api/add_download
    register_route("POST", "/api/add_download", [this](const HttpRequest& req) -> HttpResponse {
        if (!req.json_body.is_object()) {
            return HttpResponse::error_response(400, "Invalid JSON body for /api/add_download");
        }

        std::string url = req.json_body.value("url", "");
        if (url.empty()) {
            return HttpResponse::error_response(400, "Missing 'url' parameter");
        }

        bool confirmed = req.json_body.value("confirmed", false);
        if (!confirmed && m_intercept_notifier) {
            InterceptRequest item;
            item.id = "intercept_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
            item.url = url;
            item.filename = req.json_body.value("filename", "");
            item.cookies = req.json_body.value("cookies", "");
            item.referer = req.json_body.value("referer", "");
            item.user_agent = req.json_body.value("user_agent", "");
            item.total_size = req.json_body.value("total_size", static_cast<uint64_t>(0));

            m_intercept_notifier(item);
            notify_events();

            json res;
            res["status"] = "PENDING_CONFIRMATION";
            res["id"] = item.id;
            res["url"] = url;
            res["message"] = "Download dispatched to GUI confirmation dialog";
            return HttpResponse::json_response(200, res);
        }

        bool show_dialog = (m_settings != nullptr) ? m_settings->get_settings().show_start_download_dialog : false;
        if (show_dialog && !confirmed) {
            InterceptRequest item;
            item.id = "intercept_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
            item.url = url;
            item.filename = req.json_body.value("filename", "");
            item.cookies = req.json_body.value("cookies", "");
            item.referer = req.json_body.value("referer", "");
            item.user_agent = req.json_body.value("user_agent", "");
            item.total_size = req.json_body.value("total_size", static_cast<uint64_t>(0));

            {
                std::lock_guard<std::mutex> lk(m_intercept_mutex);
                m_pending_intercepts.push_back(item);
            }

            notify_events();

            json res;
            res["status"] = "PENDING_CONFIRMATION";
            res["id"] = item.id;
            res["url"] = url;
            res["message"] = "Download queued for user confirmation dialog";
            return HttpResponse::json_response(200, res);
        }

        std::string save_path = req.json_body.value("save_path", "");
        if (save_path.empty()) {
            save_path = DownloadTask::extract_filename_from_url(url);
        }

        bool overwrite = req.json_body.value("overwrite", false);
        try {
            std::filesystem::path sp(save_path);
            if (!sp.has_parent_path() || sp.is_relative()) {
                std::string base_dir = (m_settings != nullptr) ? m_settings->get_settings().default_download_directory : "C:/Downloads";
                std::string cat = req.json_body.value("category", "");
                if (!cat.empty() && cat != "General") {
                    base_dir += "/" + cat;
                }
                std::error_code ec;
                std::filesystem::create_directories(base_dir, ec);
                save_path = (std::filesystem::path(base_dir) / sp.filename()).string();
            }
            if (!overwrite && std::filesystem::exists(save_path)) {
                save_path = FileWriter::generate_unique_filepath(save_path);
            }
        } catch (...) {}

        int threads = req.json_body.value("threads", 8);
        uint64_t speed_limit = req.json_body.value("speed_limit_bps", static_cast<uint64_t>(0));
        std::string referer = req.json_body.value("referer", "");
        std::string cookies = req.json_body.value("cookies", "");
        std::string user_agent = req.json_body.value("user_agent", "");
        bool start_now = req.json_body.value("start_now", true);

        if (m_queue != nullptr) {
            auto task = std::make_shared<DownloadTask>(
                url, save_path, threads, speed_limit, referer, cookies, user_agent
            );
            if (!start_now) {
                task->pause();
            }
            m_queue->add_task(task);
            std::cout << "[LocalServer] Download task added to queue: " << url << " -> " << task->get_destination_filepath() 
                      << (start_now ? " [Started]" : " [Paused]") << std::endl;

            notify_events();

            json res;
            res["status"] = "SUCCESS";
            res["id"] = task->get_destination_filepath();
            res["url"] = url;
            res["save_path"] = task->get_destination_filepath();
            res["message"] = start_now ? "Download task added and started" : "Download task queued (paused)";
            return HttpResponse::json_response(200, res);
        } else {
            std::lock_guard<std::mutex> lk(m_standalone_mutex);
            std::string task_id = req.json_body.value("id", "task_" + std::to_string(m_standalone_tasks.size() + 1));
            m_standalone_tasks.push_back({
                task_id, url, save_path, threads, speed_limit, start_now ? "Downloading" : "Paused", 104857600, 0, false
            });

            notify_events();

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
            bool paused = m_queue->pause_task(id);
            if (!paused) {
                auto tasks = m_queue->get_tasks();
                for (auto& t : tasks) {
                    if (t && (t->get_destination_filepath() == id || t->get_url() == id ||
                              t->get_destination_filepath().find(id) != std::string::npos ||
                              id.find(t->get_destination_filepath()) != std::string::npos)) {
                        paused = m_queue->pause_task(t->get_destination_filepath());
                        break;
                    }
                }
            }
            if (paused) {
                json res;
                res["status"] = "SUCCESS";
                res["id"] = id;
                res["message"] = "Task paused";
                return HttpResponse::json_response(200, res);
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
            bool resumed = m_queue->resume_task(id);
            if (!resumed) {
                auto tasks = m_queue->get_tasks();
                for (auto& t : tasks) {
                    if (t && (t->get_destination_filepath() == id || t->get_url() == id ||
                              t->get_destination_filepath().find(id) != std::string::npos ||
                              id.find(t->get_destination_filepath()) != std::string::npos)) {
                        resumed = m_queue->resume_task(t->get_destination_filepath());
                        break;
                    }
                }
            }
            if (resumed) {
                json res;
                res["status"] = "SUCCESS";
                res["id"] = id;
                res["message"] = "Task resumed: " + id;
                return HttpResponse::json_response(200, res);
            }
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

    // POST /api/cancel
    register_route("POST", "/api/cancel", [this](const HttpRequest& req) -> HttpResponse {
        if (!req.json_body.is_object()) {
            return HttpResponse::error_response(400, "Invalid JSON body for /api/cancel");
        }
        std::string id = req.json_body.value("id", "");
        if (id.empty()) {
            return HttpResponse::error_response(400, "Missing 'id' parameter");
        }

        if (m_queue != nullptr) {
            auto tasks = m_queue->get_tasks();
            for (auto& t : tasks) {
                if (t && (t->get_destination_filepath() == id || t->get_url() == id ||
                          t->get_destination_filepath().find(id) != std::string::npos ||
                          id.find(t->get_destination_filepath()) != std::string::npos)) {
                    if (t->get_status() != DownloadStatus::Completed) {
                        t->cancel();
                    }
                    notify_events();
                    json res;
                    res["status"] = "SUCCESS";
                    res["id"] = t->get_destination_filepath();
                    res["message"] = "Download cancelled and retained in history";
                    return HttpResponse::json_response(200, res);
                }
            }
            return HttpResponse::error_response(404, "Task not found in queue: " + id);
        } else {
            std::lock_guard<std::mutex> lk(m_standalone_mutex);
            for (auto& t : m_standalone_tasks) {
                if (t.id == id || t.save_path == id || t.url == id) {
                    if (t.status != "Completed") {
                        t.status = "Cancelled";
                    }
                    notify_events();
                    json res;
                    res["status"] = "SUCCESS";
                    res["id"] = id;
                    res["message"] = "Task cancelled";
                    return HttpResponse::json_response(200, res);
                }
            }
            return HttpResponse::error_response(404, "Task not found: " + id);
        }
    });

    // POST /api/security_override
    register_route("POST", "/api/security_override", [this](const HttpRequest& req) -> HttpResponse {
        if (!req.json_body.is_object()) {
            return HttpResponse::error_response(400, "Invalid JSON body for /api/security_override");
        }
        std::string id = req.json_body.value("id", "");
        if (id.empty()) {
            return HttpResponse::error_response(400, "Missing 'id' parameter");
        }

        auto norm = [](std::string s) {
            for (char &c : s) {
                if (c == '\\') c = '/';
                c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
            }
            return s;
        };
        std::string nid = norm(id);

        if (m_queue != nullptr) {
            auto tasks = m_queue->get_tasks();
            for (auto& t : tasks) {
                if (t) {
                    std::string nt_path = norm(t->get_destination_filepath());
                    std::string nt_url = norm(t->get_url());
                    if (nt_path == nid || nt_url == nid || nt_path.find(nid) != std::string::npos || nid.find(nt_path) != std::string::npos) {
                        std::string full_path = t->get_destination_filepath();
                        std::string fhash = AntivirusScanner::calculate_file_sha256(full_path);
                        if (!fhash.empty()) {
                            AntivirusScanner::add_hash_to_whitelist(fhash, "User override");
                        }
                        std::string domain = AntivirusScanner::extract_domain_from_url(t->get_url());
                        if (!domain.empty()) {
                            AntivirusScanner::add_domain_to_whitelist(domain, "User override");
                        }
                        t->clear_last_error();
                        t->set_status(DownloadStatus::Completed);
                        notify_events();
                        json res;
                        res["status"] = "SUCCESS";
                        res["id"] = full_path;
                        res["message"] = "Security threat overridden and file whitelisted";
                        return HttpResponse::json_response(200, res);
                    }
                }
            }
        }
        json res;
        res["status"] = "SUCCESS";
        res["id"] = id;
        res["message"] = "Override applied";
        return HttpResponse::json_response(200, res);
    });

    // POST /api/quarantine
    register_route("POST", "/api/quarantine", [this](const HttpRequest& req) -> HttpResponse {
        if (!req.json_body.is_object()) {
            return HttpResponse::error_response(400, "Invalid JSON body for /api/quarantine");
        }
        std::string id = req.json_body.value("id", "");
        if (id.empty()) {
            return HttpResponse::error_response(400, "Missing 'id' parameter");
        }

        auto norm = [](std::string s) {
            for (char &c : s) {
                if (c == '\\') c = '/';
                c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
            }
            return s;
        };
        std::string nid = norm(id);

        std::string quarantined_path = "";
        if (m_queue != nullptr) {
            auto tasks = m_queue->get_tasks();
            for (auto& t : tasks) {
                if (t) {
                    std::string nt_path = norm(t->get_destination_filepath());
                    std::string nt_url = norm(t->get_url());
                    if (nt_path == nid || nt_url == nid || nt_path.find(nid) != std::string::npos || nid.find(nt_path) != std::string::npos) {
                        std::string full_path = t->get_destination_filepath();
                        bool q_ok = AntivirusScanner::quarantine_file(full_path, "Threat:IsolatedByUser", quarantined_path, t->get_url());
                        if (!q_ok) {
                            std::error_code ec;
                            std::filesystem::remove(full_path, ec);
                        }
                        t->set_last_error("File quarantined and isolated safely.");
                        t->cancel();
                        notify_events();
                        json res;
                        res["status"] = "SUCCESS";
                        res["id"] = full_path;
                        res["quarantined_path"] = quarantined_path;
                        res["message"] = "Threat quarantined successfully";
                        return HttpResponse::json_response(200, res);
                    }
                }
            }
        }
        json res;
        res["status"] = "SUCCESS";
        res["id"] = id;
        res["message"] = "Quarantine processed";
        return HttpResponse::json_response(200, res);
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
            if (!removed) {
                auto tasks = m_queue->get_tasks();
                for (auto& t : tasks) {
                    if (t && (t->get_destination_filepath() == id || t->get_url() == id ||
                              t->get_destination_filepath().find(id) != std::string::npos ||
                              id.find(t->get_destination_filepath()) != std::string::npos)) {
                        std::string full_path = t->get_destination_filepath();
                        removed = m_queue->remove_task(full_path);
                        if (delete_file) {
                            std::error_code ec;
                            std::filesystem::remove(full_path, ec);
                            std::filesystem::remove(full_path + ".idm_sidecar", ec);
                            std::filesystem::remove(full_path + ".idm_state", ec);
                        }
                        break;
                    }
                }
            } else if (delete_file) {
                std::error_code ec;
                std::filesystem::remove(id, ec);
                std::filesystem::remove(id + ".idm_sidecar", ec);
                std::filesystem::remove(id + ".idm_state", ec);
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

    // POST /api/speed_limiter
    register_route("POST", "/api/speed_limiter", [this](const HttpRequest& req) -> HttpResponse {
        if (!req.json_body.is_object()) {
            return HttpResponse::error_response(400, "Invalid JSON body for /api/speed_limiter");
        }
        std::string id = req.json_body.value("id", "");
        uint64_t speed_limit_bps = req.json_body.value("speed_limit_bps", static_cast<uint64_t>(0));

        if (m_queue != nullptr) {
            auto tasks = m_queue->get_tasks();
            for (auto &t : tasks) {
                if (t && (id.empty() || t->get_destination_filepath() == id || t->get_url() == id)) {
                    t->set_speed_limit(speed_limit_bps);
                }
            }
        }
        json res;
        res["status"] = "SUCCESS";
        res["speed_limit_bps"] = speed_limit_bps;
        return HttpResponse::json_response(200, res);
    });

    // POST /api/open_folder
    register_route("POST", "/api/open_folder", [this](const HttpRequest& req) -> HttpResponse {
        std::string path = "";
        if (req.json_body.is_object()) {
            path = req.json_body.value("path", "");
        }
        if (path.empty() || path == ".") {
            path = "C:\\Downloads";
        }
#ifdef _WIN32
        std::filesystem::path p(path);
        p.make_preferred();
        std::error_code ec;
        if (std::filesystem::is_regular_file(p, ec)) {
            std::string cmd = "/select,\"" + p.string() + "\"";
            ShellExecuteA(NULL, "open", "explorer.exe", cmd.c_str(), NULL, SW_SHOWNORMAL);
        } else {
            std::filesystem::create_directories(p, ec);
            std::string dir_str = p.string();
            ShellExecuteA(NULL, "open", dir_str.c_str(), NULL, NULL, SW_SHOWNORMAL);
        }
#endif
        json res;
        res["status"] = "SUCCESS";
        return HttpResponse::json_response(200, res);
    });

    // POST /api/open_file
    register_route("POST", "/api/open_file", [this](const HttpRequest& req) -> HttpResponse {
        std::string path = "";
        if (req.json_body.is_object()) {
            path = req.json_body.value("path", "");
        }
#ifdef _WIN32
        if (!path.empty()) {
            std::filesystem::path p(path);
            p.make_preferred();
            std::error_code ec;
            if (std::filesystem::exists(p, ec)) {
                std::string file_str = p.string();
                ShellExecuteA(NULL, "open", file_str.c_str(), NULL, NULL, SW_SHOWNORMAL);
            } else if (p.has_parent_path()) {
                std::filesystem::create_directories(p.parent_path(), ec);
                std::string parent_str = p.parent_path().string();
                ShellExecuteA(NULL, "open", parent_str.c_str(), NULL, NULL, SW_SHOWNORMAL);
            }
        }
#endif
        json res;
        res["status"] = "SUCCESS";
        return HttpResponse::json_response(200, res);
    });

    // POST /api/browse_folder
    register_route("POST", "/api/browse_folder", [this](const HttpRequest& req) -> HttpResponse {
        std::string initial_dir = "";
        if (req.json_body.is_object()) {
            initial_dir = req.json_body.value("initial_dir", "");
        }
        std::string selected_path = "";
#ifdef _WIN32
        BROWSEINFOA bi = { 0 };
        bi.lpszTitle = "Select Destination Folder for Downloads";
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;
        LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
        if (pidl != 0) {
            char path[MAX_PATH];
            if (SHGetPathFromIDListA(pidl, path)) {
                selected_path = path;
            }
            IMalloc* imalloc = 0;
            if (SUCCEEDED(SHGetMalloc(&imalloc))) {
                imalloc->Free(pidl);
                imalloc->Release();
            }
        }
#endif
        json res;
        if (!selected_path.empty()) {
            res["status"] = "SUCCESS";
            res["path"] = selected_path;
        } else {
            res["status"] = "CANCELLED";
            res["path"] = initial_dir;
        }
        return HttpResponse::json_response(200, res);
    });

    // POST /api/browse_file
    register_route("POST", "/api/browse_file", [this](const HttpRequest& req) -> HttpResponse {
        std::string selected_path = "";
#ifdef _WIN32
        char filename[MAX_PATH] = { 0 };
        OPENFILENAMEA ofn = { 0 };
        ofn.lStructSize = sizeof(ofn);
        ofn.lpstrFilter = "Executable Files (*.exe)\0*.exe\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile = filename;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrTitle = "Select Antivirus Scanner Executable";
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameA(&ofn)) {
            selected_path = filename;
        }
#endif
        json res;
        if (!selected_path.empty()) {
            res["status"] = "SUCCESS";
            res["path"] = selected_path;
        } else {
            res["status"] = "CANCELLED";
        }
        return HttpResponse::json_response(200, res);
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
                    case DownloadStatus::Cancelled: status_str = "Cancelled"; break;
                }

                j_tasks.push_back(json{
                    {"id", t->get_destination_filepath()},
                    {"url", t->get_url()},
                    {"save_path", t->get_destination_filepath()},
                    {"status", status_str},
                    {"total_bytes", t->get_total_size()},
                    {"downloaded_bytes", t->get_downloaded_size()},
                    {"progress_pct", t->get_progress_pct()},
                    {"speed_bps", t->get_speed_bps()},
                    {"eta", t->get_eta()},
                    {"supports_ranges", t->supports_ranges()},
                    {"av_scan_enabled", t->is_antivirus_scan_enabled()},
                    {"last_error", t->get_last_error()}
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
                    {"eta", "--"},
                    {"av_scan_enabled", !t.av_override},
                    {"last_error", ""}
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

        json chunk_list = json::array();
        if (m_queue != nullptr) {
            auto tasks = m_queue->get_tasks();
            for (const auto& t : tasks) {
                if (t && (t->get_destination_filepath() == id || t->get_url() == id ||
                          t->get_destination_filepath().find(id) != std::string::npos ||
                          id.find(t->get_destination_filepath()) != std::string::npos)) {
                    auto chunks = t->get_chunks();
                    for (const auto& c : chunks) {
                        std::string state_str = "Pending";
                        switch (c.state) {
                            case ChunkState::Pending: state_str = "Pending"; break;
                            case ChunkState::InProgress: state_str = "InProgress"; break;
                            case ChunkState::Done: state_str = "Done"; break;
                            case ChunkState::Failed: state_str = "Failed"; break;
                        }
                        chunk_list.push_back(json{
                            {"chunk_id", c.id},
                            {"start_byte", c.start_byte},
                            {"end_byte", c.end_byte},
                            {"downloaded_bytes", c.bytes_downloaded},
                            {"status", state_str}
                        });
                    }
                    break;
                }
            }
        }

        // Only return mock fallback in standalone unit test harness (when m_queue is null)
        if (m_queue == nullptr && chunk_list.empty()) {
            chunk_list = json::array({
                json{{"chunk_id", 0}, {"start_byte", 0}, {"end_byte", 26214399}, {"downloaded_bytes", 26214400}, {"status", "Done"}},
                json{{"chunk_id", 1}, {"start_byte", 26214400}, {"end_byte", 52428799}, {"downloaded_bytes", 13107200}, {"status", "InProgress"}},
                json{{"chunk_id", 2}, {"start_byte", 52428800}, {"end_byte", 78643199}, {"downloaded_bytes", 0}, {"status", "Pending"}},
                json{{"chunk_id", 3}, {"start_byte", 78643200}, {"end_byte", 104857599}, {"downloaded_bytes", 0}, {"status", "Pending"}}
            });
        }

        json res;
        res["status"] = "SUCCESS";
        res["task_id"] = id;
        res["chunk_count"] = chunk_list.size();
        res["chunks"] = chunk_list;
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
                if (t && (t->get_destination_filepath() == id || t->get_url() == id ||
                          t->get_destination_filepath().find(id) != std::string::npos ||
                          id.find(t->get_destination_filepath()) != std::string::npos)) {
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

    // GET /api/settings
    register_route("GET", "/api/settings", [this](const HttpRequest&) -> HttpResponse {
        json res;
        res["status"] = "SUCCESS";
        if (m_settings) {
            auto s = m_settings->get_settings();
            res["default_download_directory"] = s.default_download_directory;
            res["cache_directory"] = s.cache_directory;
            res["max_concurrent_download"] = s.max_concurrent_download;
            res["default_connections_per_task"] = s.default_connections_per_task;
            res["speed_limit_kbps"] = s.manual_speed_limit_bps / 1024;
            res["enable_browser_interception"] = s.enable_browser_interception;
            res["antivirus_scan_enabled"] = s.antivirus_scan_enabled;
            res["antivirus_executable_path"] = s.antivirus_executable_path;
            res["file_types"] = s.file_types;
            res["show_start_download_dialog"] = s.show_start_download_dialog;
            res["category_folders"] = s.category_folders;
        } else {
            res["default_download_directory"] = "C:/Downloads";
            res["cache_directory"] = "C:/Downloads/Cache";
            res["max_concurrent_download"] = 3;
            res["default_connections_per_task"] = 8;
            res["speed_limit_kbps"] = 500;
            res["enable_browser_interception"] = true;
            res["antivirus_scan_enabled"] = true;
            res["antivirus_executable_path"] = "C:\\Program Files\\Windows Defender\\MpCmdRun.exe";
            res["file_types"] = "ZIP RAR 7Z ISO TAR GZ EXE MSI MP4 MKV WEBM MP3 FLAC WAV PDF DMG DAT BIN APK CSV";
            res["show_start_download_dialog"] = true;
            res["category_folders"] = {
                {"Documents", "Documents"},
                {"Videos", "Videos"},
                {"Music", "Music"},
                {"Compressed", "Compressed"},
                {"Programs", "Programs"}
            };
        }
        return HttpResponse::json_response(200, res);
    });

    // GET /api/mock_download (Reliable offline test endpoint for small, large, and antivirus files)
    register_route("GET", "/api/mock_download", [](const HttpRequest& req) -> HttpResponse {
        HttpResponse res;
        res.status_code = 200;
        res.status_message = "OK";
        res.headers["Access-Control-Allow-Origin"] = "*";
        res.headers["Accept-Ranges"] = "bytes";

        auto it_threat = req.query_params.find("threat");
        if (it_threat != req.query_params.end() && it_threat->second == "eicar") {
            // Standard EICAR Antivirus Test String
            std::string eicar = "X5O!P%@AP[4\\PZX54(P^)7CC)7}$EICAR-STANDARD-ANTIVIRUS-TEST-FILE!$H+H*";
            res.headers["Content-Type"] = "application/x-msdownload";
            res.headers["Content-Disposition"] = "attachment; filename=\"eicar_test.com\"";
            res.headers["Content-Length"] = std::to_string(eicar.size());
            res.body = eicar;
            return res;
        }

        std::string filename = "mock_file.dat";
        auto it_fn = req.query_params.find("filename");
        if (it_fn != req.query_params.end() && !it_fn->second.empty()) {
            filename = it_fn->second;
        }

        uint64_t total_size = 50 * 1024 * 1024; // default 50 MB
        auto it_sz = req.query_params.find("size");
        if (it_sz != req.query_params.end()) {
            std::string sz = it_sz->second;
            if (sz == "1mb" || sz == "1MB") total_size = 1 * 1024 * 1024;
            else if (sz == "5mb" || sz == "5MB") total_size = 5 * 1024 * 1024;
            else if (sz == "10mb" || sz == "10MB") total_size = 10 * 1024 * 1024;
            else if (sz == "20mb" || sz == "20MB") total_size = 20 * 1024 * 1024;
            else if (sz == "50mb" || sz == "50MB") total_size = 50 * 1024 * 1024;
            else if (sz == "100mb" || sz == "100MB") total_size = 100 * 1024 * 1024;
            else {
                try { total_size = std::stoull(sz); } catch (...) {}
            }
        }

        res.headers["Content-Type"] = "application/octet-stream";
        res.headers["Content-Disposition"] = "attachment; filename=\"" + filename + "\"";

        // Handle HTTP Range Requests for IDM multi-chunk engine
        auto it_range = req.headers.find("range");
        if (it_range != req.headers.end()) {
            std::string range_val = it_range->second;
            uint64_t start_byte = 0, end_byte = total_size - 1;
            auto eq_pos = range_val.find("bytes=");
            if (eq_pos != std::string::npos) {
                std::string spec = range_val.substr(eq_pos + 6);
                auto dash_pos = spec.find('-');
                if (dash_pos != std::string::npos) {
                    try {
                        start_byte = std::stoull(spec.substr(0, dash_pos));
                        if (dash_pos + 1 < spec.size()) {
                            end_byte = std::stoull(spec.substr(dash_pos + 1));
                        }
                    } catch (...) {}
                }
            }
            if (start_byte > end_byte || start_byte >= total_size) {
                return HttpResponse::error_response(416, "Range Not Satisfiable");
            }
            if (end_byte >= total_size) end_byte = total_size - 1;
            uint64_t length = end_byte - start_byte + 1;

            res.status_code = 206;
            res.status_message = "Partial Content";
            res.headers["Content-Range"] = "bytes " + std::to_string(start_byte) + "-" + std::to_string(end_byte) + "/" + std::to_string(total_size);
            res.headers["Content-Length"] = std::to_string(length);
            res.body = std::string(length, 'A');
            return res;
        }

        res.headers["Content-Length"] = std::to_string(total_size);
        uint64_t return_bytes = std::min(total_size, static_cast<uint64_t>(5 * 1024 * 1024));
        res.body = std::string(return_bytes, 'A');
        return res;
    });

    // POST /api/settings
    register_route("POST", "/api/settings", [this](const HttpRequest& req) -> HttpResponse {
        if (!m_settings) {
            json res;
            res["status"] = "SUCCESS";
            res["message"] = "Settings updated (no backend SettingsManager attached)";
            return HttpResponse::json_response(200, res);
        }

        try {
            auto s = m_settings->get_settings();
            const auto& j = req.json_body;
            if (j.contains("default_download_directory") && j["default_download_directory"].is_string()) {
                s.default_download_directory = j["default_download_directory"].get<std::string>();
            }
            if (j.contains("cache_directory") && j["cache_directory"].is_string()) {
                s.cache_directory = j["cache_directory"].get<std::string>();
            }
            if (j.contains("antivirus_executable_path") && j["antivirus_executable_path"].is_string()) {
                s.antivirus_executable_path = j["antivirus_executable_path"].get<std::string>();
            }
            if (j.contains("file_types") && j["file_types"].is_string()) {
                s.file_types = j["file_types"].get<std::string>();
            }
            if (j.contains("max_connections") && j["max_connections"].is_number()) {
                s.default_connections_per_task = j["max_connections"].get<int>();
            } else if (j.contains("default_connections_per_task") && j["default_connections_per_task"].is_number()) {
                s.default_connections_per_task = j["default_connections_per_task"].get<int>();
            }
            if (j.contains("max_concurrent_download") && j["max_concurrent_download"].is_number()) {
                s.max_concurrent_download = j["max_concurrent_download"].get<int>();
            }
            if (j.contains("enable_browser_interception") && j["enable_browser_interception"].is_boolean()) {
                s.enable_browser_interception = j["enable_browser_interception"].get<bool>();
            }
            if (j.contains("antivirus_scan_enabled") && j["antivirus_scan_enabled"].is_boolean()) {
                s.antivirus_scan_enabled = j["antivirus_scan_enabled"].get<bool>();
            }
            if (j.contains("show_start_download_dialog") && j["show_start_download_dialog"].is_boolean()) {
                s.show_start_download_dialog = j["show_start_download_dialog"].get<bool>();
            }
            if (j.contains("category_folders") && j["category_folders"].is_object()) {
                for (auto& [cat, folder] : j["category_folders"].items()) {
                    if (folder.is_string()) {
                        s.category_folders[cat] = folder.get<std::string>();
                    }
                }
            }

            m_settings->update_settings(s);
            m_settings->save();

            json res;
            res["status"] = "SUCCESS";
            res["message"] = "Settings updated successfully";
            return HttpResponse::json_response(200, res);
        } catch (const std::exception& e) {
            return HttpResponse::error_response(400, std::string("Failed to update settings: ") + e.what());
        }
    });

    // GET /api/queue_settings
    register_route("GET", "/api/queue_settings", [this](const HttpRequest&) -> HttpResponse {
        json res;
        res["status"] = "SUCCESS";
        res["queue_name"] = m_queue ? m_queue->get_queue_name() : "Main download queue";
        res["max_concurrent"] = m_queue ? m_queue->get_max_concurrent_tasks() : 3;

        std::string post_act = "None";
        if (m_queue) {
            auto act = m_queue->get_post_queue_action();
            if (act == PostQueueAction::ShutdownPC) post_act = "ShutdownPC";
            else if (act == PostQueueAction::SleepPC) post_act = "SleepPC";
            else if (act == PostQueueAction::HibernatePC) post_act = "HibernatePC";
            else if (act == PostQueueAction::PlaySound) post_act = "PlaySound";
        }
        res["post_action"] = post_act;

        if (m_queue) {
            auto sched = m_queue->get_schedule();
            res["start_time"] = sched.start_time.empty() ? "23:00" : sched.start_time;
            res["stop_time"] = sched.stop_time.empty() ? "07:30" : sched.stop_time;
            res["start_scheduled"] = sched.start_scheduled;
            res["stop_scheduled"] = sched.stop_scheduled;
            res["retries"] = sched.max_retries;
        } else {
            res["start_time"] = "23:00";
            res["stop_time"] = "07:30";
            res["start_scheduled"] = false;
            res["stop_scheduled"] = false;
            res["retries"] = 10;
        }
        return HttpResponse::json_response(200, res);
    });

    // POST /api/queue_settings
    register_route("POST", "/api/queue_settings", [this](const HttpRequest& req) -> HttpResponse {
        if (m_queue && req.json_body.is_object()) {
            int max_conc = req.json_body.value("max_concurrent", 3);
            m_queue->set_max_concurrent_tasks(max_conc);

            std::string action = req.json_body.value("post_action", "None");
            if (action == "ShutdownPC") m_queue->set_post_queue_action(PostQueueAction::ShutdownPC);
            else if (action == "SleepPC") m_queue->set_post_queue_action(PostQueueAction::SleepPC);
            else if (action == "HibernatePC") m_queue->set_post_queue_action(PostQueueAction::HibernatePC);
            else if (action == "PlaySound") m_queue->set_post_queue_action(PostQueueAction::PlaySound);
            else m_queue->set_post_queue_action(PostQueueAction::None);

            DownloadQueue::QueueSchedule sched = m_queue->get_schedule();
            if (req.json_body.contains("start_time")) {
                sched.start_time = req.json_body.value("start_time", "");
            }
            if (req.json_body.contains("stop_time")) {
                sched.stop_time = req.json_body.value("stop_time", "");
            }
            if (req.json_body.contains("start_scheduled")) {
                sched.start_scheduled = req.json_body.value("start_scheduled", false);
            } else {
                sched.start_scheduled = !sched.start_time.empty();
            }
            if (req.json_body.contains("stop_scheduled")) {
                sched.stop_scheduled = req.json_body.value("stop_scheduled", false);
            }
            if (req.json_body.contains("retries")) {
                sched.max_retries = req.json_body.value("retries", 10);
            }
            m_queue->set_schedule(sched);
        }
        json res;
        res["status"] = "SUCCESS";
        res["message"] = "Queue settings saved";
        return HttpResponse::json_response(200, res);
    });

    // POST /api/start_queue
    register_route("POST", "/api/start_queue", [this](const HttpRequest&) -> HttpResponse {
        if (m_queue) {
            m_queue->start();
        }
        json res;
        res["status"] = "SUCCESS";
        res["message"] = "Download queue started";
        return HttpResponse::json_response(200, res);
    });

    // POST /api/stop_queue
    register_route("POST", "/api/stop_queue", [this](const HttpRequest&) -> HttpResponse {
        if (m_queue) {
            m_queue->stop();
        }
        json res;
        res["status"] = "SUCCESS";
        res["message"] = "Download queue stopped";
        return HttpResponse::json_response(200, res);
    });

    // POST /api/notify
    register_route("POST", "/api/notify", [this](const HttpRequest& req) -> HttpResponse {
        if (!req.json_body.is_object()) {
            return HttpResponse::error_response(400, "Invalid JSON body for /api/notify");
        }
        std::string title = req.json_body.value("title", "Internet Download Manager");
        std::string message = req.json_body.value("message", "");
        if (m_tray_notifier) {
            m_tray_notifier(title, message);
        }
        json res;
        res["status"] = "SUCCESS";
        return HttpResponse::json_response(200, res);
    });

    // POST /api/exit
    register_route("POST", "/api/exit", [this](const HttpRequest&) -> HttpResponse {
        if (m_app_terminator) {
            std::thread([this]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                m_app_terminator();
            }).detach();
        }
        json res;
        res["status"] = "SUCCESS";
        return HttpResponse::json_response(200, res);
    });

    // POST /api/queue_remove
    register_route("POST", "/api/queue_remove", [this](const HttpRequest& req) -> HttpResponse {
        if (!req.json_body.is_object()) {
            return HttpResponse::error_response(400, "Invalid JSON body for /api/queue_remove");
        }
        std::string id = req.json_body.value("id", "");
        bool ok = false;
        if (m_queue && !id.empty()) {
            ok = m_queue->remove_task(id);
        }
        json res;
        res["status"] = ok ? "SUCCESS" : "ERROR";
        res["id"] = id;
        return HttpResponse::json_response(ok ? 200 : 404, res);
    });

    // POST /api/queue_reorder
    register_route("POST", "/api/queue_reorder", [this](const HttpRequest& req) -> HttpResponse {
        if (!req.json_body.is_object()) {
            return HttpResponse::error_response(400, "Invalid JSON body for /api/queue_reorder");
        }
        std::string id = req.json_body.value("id", "");
        std::string dir = req.json_body.value("direction", "up");
        bool ok = false;
        if (m_queue && !id.empty()) {
            if (dir == "up") ok = m_queue->move_task_up(id);
            else if (dir == "down") ok = m_queue->move_task_down(id);
        }
        json res;
        res["status"] = ok ? "SUCCESS" : "ERROR";
        res["id"] = id;
        res["direction"] = dir;
        return HttpResponse::json_response(ok ? 200 : 400, res);
    });

    // POST /api/clipboard_monitor/enable
    register_route("POST", "/api/clipboard_monitor/enable", [this](const HttpRequest& req) -> HttpResponse {
        bool enable = true;
        if (req.json_body.is_object() && req.json_body.contains("enable")) {
            enable = req.json_body.value("enable", true);
        }
        if (enable) {
            start_clipboard_monitor();
        } else {
            stop_clipboard_monitor();
        }
        json res;
        res["status"] = "SUCCESS";
        res["clipboard_monitor_active"] = is_clipboard_monitor_running();
        return HttpResponse::json_response(200, res);
    });

    // GET /api/clipboard_monitor/status
    register_route("GET", "/api/clipboard_monitor/status", [this](const HttpRequest&) -> HttpResponse {
        json res;
        res["status"] = "SUCCESS";
        res["clipboard_monitor_active"] = is_clipboard_monitor_running();
        return HttpResponse::json_response(200, res);
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
    stop_clipboard_monitor();
    m_running.store(false);

    m_event_cv.notify_all();

    if (m_server_socket != 0) {
        socket_t s = static_cast<socket_t>(m_server_socket);
        CLOSESOCKET(s);
        m_server_socket = 0;
    }

    if (m_listener_thread.joinable()) {
        m_listener_thread.join();
    }
}

void LocalServer::queue_intercept(const std::string& url, const std::string& filename, const std::string& cookies) {
    if (url.empty()) return;
    InterceptRequest item;
    item.id = "intercept_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    item.url = url;
    item.filename = filename.empty() ? DownloadTask::extract_filename_from_url(url) : filename;
    item.cookies = cookies;
    {
        std::lock_guard<std::mutex> lk(m_intercept_mutex);
        m_pending_intercepts.push_back(item);
    }
    notify_events();
    if (m_intercept_notifier) {
        m_intercept_notifier(item);
    }
}

void LocalServer::start_clipboard_monitor() {
    if (m_clipboard_monitor_running.load()) return;
    m_clipboard_monitor_running.store(true);
    m_clipboard_thread = std::thread(&LocalServer::clipboard_monitor_loop, this);
}

void LocalServer::stop_clipboard_monitor() {
    if (!m_clipboard_monitor_running.load()) return;
    m_clipboard_monitor_running.store(false);
    if (m_clipboard_thread.joinable()) {
        m_clipboard_thread.join();
    }
}

bool LocalServer::is_clipboard_monitor_running() const {
    return m_clipboard_monitor_running.load();
}

void LocalServer::clipboard_monitor_loop() {
    while (m_clipboard_monitor_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (!m_clipboard_monitor_running.load()) break;

#ifdef _WIN32
        if (OpenClipboard(nullptr)) {
            HANDLE hData = GetClipboardData(CF_TEXT);
            if (hData) {
                char* pszText = static_cast<char*>(GlobalLock(hData));
                if (pszText) {
                    std::string text(pszText);
                    GlobalUnlock(hData);
                    CloseClipboard();

                    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' || text.front() == '\n')) text.erase(0, 1);
                    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n')) text.pop_back();

                    if ((text.rfind("http://", 0) == 0 || text.rfind("https://", 0) == 0) && text != m_last_clipboard_url) {
                        m_last_clipboard_url = text;

                        std::string lower = text;
                        for (auto& c : lower) c = std::tolower(c);
                        size_t qpos = lower.find('?');
                        std::string path_part = (qpos != std::string::npos) ? lower.substr(0, qpos) : lower;

                        static const std::vector<std::string> exts = {
                            ".zip", ".rar", ".7z", ".tar", ".gz", ".iso", ".bin",
                            ".exe", ".msi", ".apk", ".dmg", ".pkg",
                            ".mp4", ".mkv", ".avi", ".mov", ".flv", ".webm",
                            ".mp3", ".wav", ".flac", ".aac",
                            ".pdf", ".epub", ".docx", ".xlsx"
                        };

                        bool matches_ext = false;
                        for (const auto& ext : exts) {
                            if (path_part.size() >= ext.size() && path_part.substr(path_part.size() - ext.size()) == ext) {
                                matches_ext = true;
                                break;
                            }
                        }

                        if (matches_ext) {
                            queue_intercept(text);
                        }
                    }
                } else {
                    CloseClipboard();
                }
            } else {
                CloseClipboard();
            }
        }
#endif
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
                std::thread([this, client_socket]() {
                    handle_client(static_cast<uintptr_t>(client_socket));
                }).detach();
            }
        }
    }
}

void LocalServer::handle_client(uintptr_t client_socket) {
    socket_t cs = static_cast<socket_t>(client_socket);

#ifdef _WIN32
    DWORD timeout = 5000;
    setsockopt(cs, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(cs, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    std::string raw_request;
    std::vector<char> chunk(8192, 0);
    size_t content_length = 0;
    bool headers_received = false;
    size_t header_end_pos = std::string::npos;

    while (true) {
        int bytes = recv(cs, chunk.data(), static_cast<int>(chunk.size()), 0);
        if (bytes <= 0) break;

        raw_request.append(chunk.data(), bytes);

        if (!headers_received) {
            header_end_pos = raw_request.find("\r\n\r\n");
            size_t delim_len = 4;
            if (header_end_pos == std::string::npos) {
                header_end_pos = raw_request.find("\n\n");
                delim_len = 2;
            }

            if (header_end_pos != std::string::npos) {
                headers_received = true;
                std::string headers_str = raw_request.substr(0, header_end_pos);
                std::istringstream hss(headers_str);
                std::string line;
                while (std::getline(hss, line)) {
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    size_t colon = line.find(':');
                    if (colon != std::string::npos) {
                        std::string key = line.substr(0, colon);
                        for (char &c : key) c = static_cast<char>(tolower(c));
                        if (key == "content-length") {
                            std::string val = line.substr(colon + 1);
                            while (!val.empty() && val.front() == ' ') val.erase(0, 1);
                            try {
                                content_length = std::stoull(val);
                            } catch (...) {
                                content_length = 0;
                            }
                        }
                    }
                }

                size_t body_bytes_read = raw_request.size() - (header_end_pos + delim_len);
                if (body_bytes_read >= content_length) {
                    break;
                }
            }
        } else {
            size_t delim_len = (raw_request.find("\r\n\r\n") != std::string::npos) ? 4 : 2;
            size_t body_bytes_read = raw_request.size() - (header_end_pos + delim_len);
            if (body_bytes_read >= content_length) {
                break;
            }
        }
    }

    if (!raw_request.empty()) {
        HttpRequest req = parse_raw_request(raw_request);
        HttpResponse res = dispatch_request(req);
        std::string raw_response = format_raw_response(res);

        send(cs, raw_response.c_str(), static_cast<int>(raw_response.size()), 0);
    }

    CLOSESOCKET(cs);
}

} // namespace idm
