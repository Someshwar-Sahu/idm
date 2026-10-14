#include "LocalServer.h"
#include "DownloadQueue.h"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include <stdexcept>

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
    #define CLOSESOCKET(s) closesocket(s)
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    typedef int socket_t;
    #define CLOSESOCKET(s) close(s)
#endif

using namespace std;
using namespace idm;

string send_http_request(int port, const string& request_str) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    socket_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == (socket_t)(-1)) {
        cerr << "[Client] socket() failed with error: " << WSAGetLastError() << endl;
        return "";
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    int conn = connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (conn != 0) {
        cerr << "[Client] connect() failed with error: " << WSAGetLastError() << endl;
        CLOSESOCKET(s);
        return "";
    }

    send(s, request_str.c_str(), static_cast<int>(request_str.size()), 0);

    string response;
    vector<char> buf(4096, 0);
    int n = 0;
    while ((n = recv(s, buf.data(), static_cast<int>(buf.size()) - 1, 0)) > 0) {
        response.append(buf.data(), n);
    }
    CLOSESOCKET(s);
    return response;
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    cout << "==========================================================" << endl;
    cout << "    Comprehensive Unit Test: LocalServer (Pos + Neg)      " << endl;
    cout << "==========================================================" << endl;

    // =========================================================================
    // 1. URL DECODING & STRING UTILITIES (Positive & Negative)
    // =========================================================================
    cout << "\n[1/8] Testing URL Decoding Utilities (Pos + Neg)..." << endl;
    {
        // Positive: space as %20, space as +, special characters
        assert(LocalServer::url_decode("hello%20world") == "hello world");
        assert(LocalServer::url_decode("hello+world") == "hello world");
        assert(LocalServer::url_decode("id%3D42%26name%3Dtest") == "id=42&name=test");
        assert(LocalServer::url_decode("pure_ascii_123") == "pure_ascii_123");

        // Negative / Edge Cases: Empty string, incomplete hex, invalid hex characters
        assert(LocalServer::url_decode("").empty());
        assert(LocalServer::url_decode("trailing%2") == "trailing%2");
        assert(LocalServer::url_decode("invalid%ZZchar") == "invalid%ZZchar");
        assert(LocalServer::url_decode("percent%") == "percent%");
        cout << "  -> url_decode() positive and edge cases: PASSED" << endl;
    }

    // =========================================================================
    // 2. RAW HTTP REQUEST & QUERY PARSING (Positive & Negative)
    // =========================================================================
    cout << "\n[2/8] Testing Raw HTTP Request & Query Parsing (Pos + Neg)..." << endl;
    {
        // Positive: Valid GET with query params & headers
        string raw_get = "GET /api/chunks?id=task_99&detail=true HTTP/1.1\r\n"
                         "Host: 127.0.0.1:9090\r\n"
                         "Content-Type: application/json\r\n\r\n";
        HttpRequest parsed_get = LocalServer::parse_raw_request(raw_get);
        assert(parsed_get.method == "GET");
        assert(parsed_get.path == "/api/chunks");
        assert(parsed_get.query_params["id"] == "task_99");
        assert(parsed_get.query_params["detail"] == "true");
        assert(parsed_get.headers["content-type"] == "application/json");

        // Positive: Valid POST with JSON payload
        string raw_post = "POST /api/add_download HTTP/1.1\r\n"
                          "Content-Type: application/json\r\n\r\n"
                          "{\"url\":\"https://example.com/test.zip\",\"threads\":16}";
        HttpRequest parsed_post = LocalServer::parse_raw_request(raw_post);
        assert(parsed_post.method == "POST");
        assert(parsed_post.path == "/api/add_download");
        assert(parsed_post.json_body["url"] == "https://example.com/test.zip");
        assert(parsed_post.json_body["threads"] == 16);

        // Negative: Completely empty HTTP payload
        HttpRequest empty_req = LocalServer::parse_raw_request("");
        assert(empty_req.method.empty());
        assert(empty_req.path.empty());
        assert(empty_req.headers.empty());

        // Negative: Non-JSON payload body in request
        HttpRequest non_json_req = LocalServer::parse_raw_request("POST /api/test HTTP/1.1\r\n\r\nNot Valid JSON");
        assert(non_json_req.method == "POST");
        assert(non_json_req.json_body.is_null());
        assert(non_json_req.body == "Not Valid JSON");
        cout << "  -> Raw HTTP request and query parameter parser: PASSED" << endl;
    }

    // =========================================================================
    // 3. CORS PREFLIGHT, RESPONSE BUILDERS & 500 GUARD (Positive & Negative)
    // =========================================================================
    cout << "\n[3/8] Testing CORS Preflight, Response Formatting & 500 Guard..." << endl;
    {
        LocalServer server;

        // Positive: OPTIONS CORS preflight returns 204 No Content with CORS headers
        HttpRequest opt_req;
        opt_req.method = "OPTIONS";
        opt_req.path = "/api/tasks";
        HttpResponse opt_res = server.dispatch_request(opt_req);
        assert(opt_res.status_code == 204);
        assert(opt_res.headers["Access-Control-Allow-Origin"] == "*");
        assert(opt_res.headers["Access-Control-Allow-Methods"].find("GET") != string::npos);

        string raw_opt_str = LocalServer::format_raw_response(opt_res);
        assert(raw_opt_str.find("HTTP/1.1 204 No Content") != string::npos);
        assert(raw_opt_str.find("Access-Control-Allow-Origin: *") != string::npos);

        // Positive: Custom error_response formatting
        HttpResponse err_res = HttpResponse::error_response(400, "Bad Request Test");
        assert(err_res.status_code == 400);
        json err_j = json::parse(err_res.body);
        assert(err_j["status"] == "ERROR");
        assert(err_j["code"] == 400);

        // Negative: Exception in route handler caught and converted to 500 Internal Error
        server.register_route("GET", "/api/crash_test", [](const HttpRequest&) -> HttpResponse {
            throw runtime_error("Simulated internal exception");
        });
        HttpRequest crash_req;
        crash_req.method = "GET";
        crash_req.path = "/api/crash_test";
        HttpResponse crash_res = server.dispatch_request(crash_req);
        assert(crash_res.status_code == 500);
        assert(crash_res.body.find("Simulated internal exception") != string::npos);

        cout << "  -> CORS preflight, error responses, and 500 exception guard: PASSED" << endl;
    }

    // =========================================================================
    // 4. REST API STANDALONE MODE (Complete Pos + Neg for All 7 Endpoints)
    // =========================================================================
    cout << "\n[4/8] Testing REST API Endpoints (All 7 Endpoints Pos + Neg)..." << endl;
    {
        LocalServer server;

        // 4.1 POST /api/add_download (Neg: empty json, Neg: missing url, Pos: valid url)
        HttpRequest bad_add_empty;
        bad_add_empty.method = "POST";
        bad_add_empty.path = "/api/add_download";
        bad_add_empty.json_body = json();
        assert(server.dispatch_request(bad_add_empty).status_code == 400);

        HttpRequest bad_add_nourl;
        bad_add_nourl.method = "POST";
        bad_add_nourl.path = "/api/add_download";
        bad_add_nourl.json_body = json::object();
        assert(server.dispatch_request(bad_add_nourl).status_code == 400);

        HttpRequest good_add;
        good_add.method = "POST";
        good_add.path = "/api/add_download";
        good_add.json_body = {
            {"url", "https://releases.ubuntu.com/jammy/ubuntu-22.04.iso"},
            {"save_path", "ubuntu-22.04.iso"},
            {"threads", 8}
        };
        HttpResponse res_good_add = server.dispatch_request(good_add);
        assert(res_good_add.status_code == 200);
        json add_j = json::parse(res_good_add.body);
        assert(add_j["status"] == "SUCCESS");
        string task_id = add_j["id"];
        assert(!task_id.empty());

        // 4.2 GET /api/tasks (Pos: lists added task)
        HttpRequest get_tasks;
        get_tasks.method = "GET";
        get_tasks.path = "/api/tasks";
        HttpResponse res_tasks = server.dispatch_request(get_tasks);
        assert(res_tasks.status_code == 200);
        json tasks_j = json::parse(res_tasks.body);
        assert(tasks_j["status"] == "SUCCESS");
        assert(tasks_j["count"] == 1);
        assert(tasks_j["tasks"][0]["url"] == "https://releases.ubuntu.com/jammy/ubuntu-22.04.iso");

        // 4.3 GET /api/chunks (Neg: missing id query param, Pos: valid task id)
        HttpRequest get_chunks_noid;
        get_chunks_noid.method = "GET";
        get_chunks_noid.path = "/api/chunks";
        assert(server.dispatch_request(get_chunks_noid).status_code == 400);

        HttpRequest get_chunks;
        get_chunks.method = "GET";
        get_chunks.path = "/api/chunks";
        get_chunks.query_params["id"] = task_id;
        HttpResponse res_chunks = server.dispatch_request(get_chunks);
        assert(res_chunks.status_code == 200);
        json chunks_j = json::parse(res_chunks.body);
        assert(chunks_j["status"] == "SUCCESS");
        assert(chunks_j["chunks"].size() == 4);

        // 4.4 POST /api/pause (Neg: missing id param, Neg: unknown id, Pos: valid id)
        HttpRequest pause_noid;
        pause_noid.method = "POST";
        pause_noid.path = "/api/pause";
        pause_noid.json_body = json::object();
        assert(server.dispatch_request(pause_noid).status_code == 400);

        HttpRequest pause_unknown;
        pause_unknown.method = "POST";
        pause_unknown.path = "/api/pause";
        pause_unknown.json_body = {{"id", "non_existent_id_xyz"}};
        assert(server.dispatch_request(pause_unknown).status_code == 404);

        HttpRequest pause_valid;
        pause_valid.method = "POST";
        pause_valid.path = "/api/pause";
        pause_valid.json_body = {{"id", task_id}};
        assert(server.dispatch_request(pause_valid).status_code == 200);

        // 4.5 POST /api/resume (Neg: missing id param, Neg: unknown id, Pos: valid id)
        HttpRequest resume_noid;
        resume_noid.method = "POST";
        resume_noid.path = "/api/resume";
        resume_noid.json_body = json::object();
        assert(server.dispatch_request(resume_noid).status_code == 400);

        HttpRequest resume_unknown;
        resume_unknown.method = "POST";
        resume_unknown.path = "/api/resume";
        resume_unknown.json_body = {{"id", "non_existent_id_xyz"}};
        assert(server.dispatch_request(resume_unknown).status_code == 404);

        HttpRequest resume_valid;
        resume_valid.method = "POST";
        resume_valid.path = "/api/resume";
        resume_valid.json_body = {{"id", task_id}};
        assert(server.dispatch_request(resume_valid).status_code == 200);

        // 4.6 POST /api/av_override (Neg: missing id param, Neg: unknown id, Pos: valid id)
        HttpRequest av_noid;
        av_noid.method = "POST";
        av_noid.path = "/api/av_override";
        av_noid.json_body = json::object();
        assert(server.dispatch_request(av_noid).status_code == 400);

        HttpRequest av_unknown;
        av_unknown.method = "POST";
        av_unknown.path = "/api/av_override";
        av_unknown.json_body = {{"id", "non_existent_id_xyz"}, {"override", true}};
        assert(server.dispatch_request(av_unknown).status_code == 404);

        HttpRequest av_valid;
        av_valid.method = "POST";
        av_valid.path = "/api/av_override";
        av_valid.json_body = {{"id", task_id}, {"override", true}};
        HttpResponse res_av = server.dispatch_request(av_valid);
        assert(res_av.status_code == 200);
        json av_j = json::parse(res_av.body);
        assert(av_j["av_override"] == true);

        // 4.7 POST /api/delete (Neg: missing id param, Neg: unknown id, Pos: valid id)
        HttpRequest del_noid;
        del_noid.method = "POST";
        del_noid.path = "/api/delete";
        del_noid.json_body = json::object();
        assert(server.dispatch_request(del_noid).status_code == 400);

        HttpRequest del_unknown;
        del_unknown.method = "POST";
        del_unknown.path = "/api/delete";
        del_unknown.json_body = {{"id", "non_existent_id_xyz"}};
        assert(server.dispatch_request(del_unknown).status_code == 404);

        HttpRequest del_valid;
        del_valid.method = "POST";
        del_valid.path = "/api/delete";
        del_valid.json_body = {{"id", task_id}};
        assert(server.dispatch_request(del_valid).status_code == 200);

        // Verify count is 0 after deletion
        HttpResponse res_tasks_empty = server.dispatch_request(get_tasks);
        json tasks_empty_j = json::parse(res_tasks_empty.body);
        assert(tasks_empty_j["count"] == 0);

        // Neg: Non-existent route returns 404
        HttpRequest not_found_req;
        not_found_req.method = "GET";
        not_found_req.path = "/api/does_not_exist";
        assert(server.dispatch_request(not_found_req).status_code == 404);

        cout << "  -> Positive and negative guards for all 7 REST endpoints: PASSED" << endl;
    }

    // =========================================================================
    // 5. DYNAMIC ROUTE REGISTRATION & ACCESSORS (Positive)
    // =========================================================================
    cout << "\n[5/8] Testing Dynamic Route Registration & Accessors..." << endl;
    {
        LocalServer server;
        assert(server.get_host() == "127.0.0.1");
        assert(server.get_port() == 9090);
        assert(server.is_running() == false);

        server.register_route("GET", "/api/status", [](const HttpRequest&) -> HttpResponse {
            json j;
            j["status"] = "OK";
            j["version"] = "1.0.0";
            return HttpResponse::json_response(200, j);
        });

        HttpRequest status_req;
        status_req.method = "GET";
        status_req.path = "/api/status";
        HttpResponse status_res = server.dispatch_request(status_req);
        assert(status_res.status_code == 200);
        json status_j = json::parse(status_res.body);
        assert(status_j["status"] == "OK");
        assert(status_j["version"] == "1.0.0");
        cout << "  -> Custom route registration and host/port accessors: PASSED" << endl;
    }

    // =========================================================================
    // 6. LIVE TCP SOCKET SERVER & NEGATIVE BIND GUARD (Positive & Negative)
    // =========================================================================
    cout << "\n[6/8] Testing Live TCP Socket Server & Negative Bind Guard..." << endl;
    {
        LocalServer server;

        // Negative: Invalid host IP fails to bind
        assert(server.start("999.999.999.999", 9090) == false);
        assert(server.is_running() == false);

        // Positive: Valid bind to 127.0.0.1 on ephemeral port (port 0)
        assert(server.start("127.0.0.1", 0) == true);
        assert(server.is_running() == true);
        int live_port = server.get_port();
        assert(live_port > 0);
        assert(server.get_host() == "127.0.0.1");

        // Real TCP socket HTTP GET over loopback
        string http_get = "GET /api/tasks HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
        string response_get = send_http_request(live_port, http_get);
        assert(response_get.find("HTTP/1.1 200 OK") != string::npos);
        assert(response_get.find("\"status\":\"SUCCESS\"") != string::npos);

        // Real TCP socket HTTP POST with JSON body over loopback
        string post_body = "{\"url\":\"https://example.com/network_test.iso\"}";
        string http_post = "POST /api/add_download HTTP/1.1\r\n"
                           "Host: 127.0.0.1\r\n"
                           "Content-Type: application/json\r\n"
                           "Content-Length: " + to_string(post_body.size()) + "\r\n"
                           "Connection: close\r\n\r\n" + post_body;
        string response_post = send_http_request(live_port, http_post);
        assert(response_post.find("HTTP/1.1 200 OK") != string::npos);
        assert(response_post.find("Download task recorded") != string::npos);

        server.stop();
        assert(server.is_running() == false);
        cout << "  -> Live TCP loopback round-trip and invalid bind guard: PASSED" << endl;
    }

    // =========================================================================
    // 7. SERVER LIFECYCLE IDEMPOTENCE (Positive & Negative)
    // =========================================================================
    cout << "\n[7/8] Testing Server Lifecycle Idempotence (Start/Stop/Restart)..." << endl;
    {
        LocalServer server;
        assert(server.start("127.0.0.1", 0) == true);
        assert(server.start("127.0.0.1", 0) == true); // Consecutive start is safe and idempotent
        assert(server.is_running() == true);

        server.stop();
        server.stop(); // Consecutive stop is safe and idempotent
        assert(server.is_running() == false);

        // Can cleanly restart after being stopped
        assert(server.start("127.0.0.1", 0) == true);
        assert(server.is_running() == true);
        server.stop();
        assert(server.is_running() == false);
        cout << "  -> Idempotent start/stop/restart lifecycle: PASSED" << endl;
    }

    // =========================================================================
    // 8. CORE DOWNLOADQUEUE ATTACHMENT & REST CONTROL (Positive & Negative)
    // =========================================================================
    cout << "\n[8/8] Testing Core DownloadQueue Attachment & REST Control (Pos + Neg)..." << endl;
    {
        DownloadQueue queue("Test Live Queue", 4);
        LocalServer server;
        server.attach_queue(&queue);

        // Positive: Add task directly to DownloadQueue
        HttpRequest add_req;
        add_req.method = "POST";
        add_req.path = "/api/add_download";
        add_req.json_body = {
            {"url", "https://example.com/queue_test.zip"},
            {"save_path", "queue_test.zip"},
            {"threads", 4}
        };
        HttpResponse add_res = server.dispatch_request(add_req);
        assert(add_res.status_code == 200);
        assert(queue.get_task_count() == 1);

        // Positive: Query tasks from real DownloadQueue
        HttpRequest get_tasks;
        get_tasks.method = "GET";
        get_tasks.path = "/api/tasks";
        HttpResponse tasks_res = server.dispatch_request(get_tasks);
        assert(tasks_res.status_code == 200);
        json tasks_j = json::parse(tasks_res.body);
        assert(tasks_j["count"] == 1);
        assert(tasks_j["tasks"][0]["url"] == "https://example.com/queue_test.zip");

        // Negative: Unknown task in real DownloadQueue returns 404
        HttpRequest pause_bad;
        pause_bad.method = "POST";
        pause_bad.path = "/api/pause";
        pause_bad.json_body = {{"id", "non_existent_queue_task"}};
        assert(server.dispatch_request(pause_bad).status_code == 404);

        // Positive: Pause task in real DownloadQueue
        HttpRequest pause_good;
        pause_good.method = "POST";
        pause_good.path = "/api/pause";
        pause_good.json_body = {{"id", "queue_test.zip"}};
        assert(server.dispatch_request(pause_good).status_code == 200);

        // Positive: Antivirus override toggle on real task
        HttpRequest av_req;
        av_req.method = "POST";
        av_req.path = "/api/av_override";
        av_req.json_body = {{"id", "queue_test.zip"}, {"override", true}};
        HttpResponse av_res = server.dispatch_request(av_req);
        assert(av_res.status_code == 200);
        json av_j = json::parse(av_res.body);
        assert(av_j["av_override"] == true);

        // Positive: Delete task from real DownloadQueue
        HttpRequest del_req;
        del_req.method = "POST";
        del_req.path = "/api/delete";
        del_req.json_body = {{"id", "queue_test.zip"}};
        HttpResponse del_res = server.dispatch_request(del_req);
        assert(del_res.status_code == 200);
        assert(queue.get_task_count() == 0);

        // Negative: Delete again returns 404
        assert(server.dispatch_request(del_req).status_code == 404);

        cout << "  -> Attached DownloadQueue live REST control & error guards: PASSED" << endl;
    }

    cout << "\n==========================================================" << endl;
    cout << "  [PASSED] 100% OF LOCALSERVER (TASK 20) VERIFIED!        " << endl;
    cout << "==========================================================" << endl;

    return 0;
}
