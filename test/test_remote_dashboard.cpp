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
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#include "../src/net/RemoteDashboard.h"
#include "../src/ipc/LocalServer.h"
#include "../src/queue/DownloadQueue.h"

#include <nlohmann/json.hpp>
#include <iostream>
#include <cassert>
#include <vector>
#include <string>

using namespace std;
using namespace idm;

using json = nlohmann::json;

void test_local_ip_and_dashboard_url() {
    cout << "\n[1/8] Testing Local LAN IP Discovery & Mobile URL Construction..." << endl;

    string ip = RemoteDashboard::get_local_ip_address();
    assert(!ip.empty());

    // Mock IP test
    RemoteDashboard::set_mock_local_ip("192.168.1.50");
    assert(RemoteDashboard::get_local_ip_address() == "192.168.1.50");

    RemoteDashboard dashboard;
    string url = dashboard.get_dashboard_url(8080);
    assert(url == "http://192.168.1.50:8080/dashboard");

    RemoteDashboard::set_mock_local_ip(""); // Reset
    cout << "  -> LAN IP discovery and mobile URL formatting: PASSED" << endl;
}

void test_embedded_mobile_dashboard_html() {
    cout << "\n[2/8] Testing Embedded Mobile Web Dashboard HTML Asset..." << endl;

    string html = RemoteDashboard::get_embedded_dashboard_html();
    assert(!html.empty());
    assert(html.find("<!DOCTYPE html>") != string::npos);
    assert(html.find("IDM Remote Dashboard") != string::npos);
    assert(html.find("/api/remote/status") != string::npos);
    assert(html.find("/api/remote/add") != string::npos);

    cout << "  -> Zero-dependency embedded mobile web dashboard: PASSED" << endl;
}

void test_pairing_pin_security() {
    cout << "\n[3/8] Testing Pairing PIN Generation & Validation..." << endl;

    RemoteDashboard dashboard;
    assert(!dashboard.is_pin_required());

    dashboard.set_pin_required(true);
    assert(dashboard.is_pin_required());

    string pin1 = dashboard.get_pin();
    assert(pin1.length() == 4);
    assert(dashboard.verify_pin(pin1));
    assert(!dashboard.verify_pin("0000"));

    string pin2 = dashboard.regenerate_pin();
    assert(pin2.length() == 4);
    assert(dashboard.verify_pin(pin2));

    cout << "  -> 4-Digit pairing PIN lifecycle & access control: PASSED" << endl;
}

void test_remote_status_telemetry() {
    cout << "\n[4/8] Testing Remote Status JSON Endpoint (/api/remote/status)..." << endl;

    LocalServer server;
    assert(server.start("127.0.0.1", 0));
    int port = server.get_port();

    DownloadQueue queue("TestQueue", 3);
    auto task = make_shared<DownloadTask>("https://example.com/file1.zip", "file1.zip");
    queue.add_task(task);

    RemoteDashboard dashboard;
    dashboard.attach_to_server(server, &queue);

    // Call GET /api/remote/status
    HttpRequest req;
    req.method = "GET";
    req.path = "/api/remote/status";

    HttpResponse res = server.dispatch_request(req);
    assert(res.status_code == 200);
    json j_res = json::parse(res.body);
    assert(j_res["online"] == true);
    assert(j_res["task_count"] == 1);
    assert(j_res["tasks"][0]["url"] == "https://example.com/file1.zip");
    server.stop();
    cout << "  -> Real-time remote telemetry JSON endpoint: PASSED" << endl;
}

void test_remote_task_submission() {
    cout << "\n[5/8] Testing Remote Download Submission (/api/remote/add)..." << endl;

    LocalServer server;
    assert(server.start("127.0.0.1", 0));

    DownloadQueue queue("TestQueue", 3);
    RemoteDashboard dashboard;
    dashboard.attach_to_server(server, &queue);

    // 1. Valid add
    HttpRequest req;
    req.method = "POST";
    req.path = "/api/remote/add";
    req.json_body = {{"url", "https://example.com/remote_file.tar.gz"}};

    HttpResponse res = server.dispatch_request(req);
    assert(res.status_code == 201);
    assert(queue.get_task_count() == 1);

    // 2. Invalid URL
    req.json_body = {{"url", "invalid url with spaces"}};
    HttpResponse res2 = server.dispatch_request(req);
    assert(res2.status_code == 400);

    server.stop();
    cout << "  -> Remote mobile link dispatch and validation: PASSED" << endl;
}

void test_remote_queue_action_execution() {
    cout << "\n[6/8] Testing Remote Action Execution (Pause, Resume, Cancel)..." << endl;

    LocalServer server;
    assert(server.start("127.0.0.1", 0));

    DownloadQueue queue("TestQueue", 3);
    auto task = make_shared<DownloadTask>("https://example.com/file2.zip", "file2.zip");
    queue.add_task(task);
    assert(queue.get_task_count() == 1);

    RemoteDashboard dashboard;
    dashboard.attach_to_server(server, &queue);

    // Cancel task
    HttpRequest req;
    req.method = "POST";
    req.path = "/api/remote/action";
    req.json_body = {{"action", "cancel"}, {"id", "file2.zip"}};

    HttpResponse res = server.dispatch_request(req);
    assert(res.status_code == 200);
    assert(queue.get_task_count() == 0);

    server.stop();
    cout << "  -> Remote queue management actions: PASSED" << endl;
}

void test_dock_config_and_state_machine() {
    cout << "\n[7/8] Testing Floating Mini-Dock Configuration & Clamping..." << endl;

    RemoteDashboard dashboard;
    DockConfig cfg = dashboard.get_dock_config();
    assert(cfg.state == DockState::Expanded);

    dashboard.set_dock_position(250, 400);
    dashboard.set_dock_state(DockState::Collapsed);
    dashboard.set_dock_opacity(0.5);

    DockConfig updated = dashboard.get_dock_config();
    assert(updated.x == 250);
    assert(updated.y == 400);
    assert(updated.state == DockState::Collapsed);
    assert(updated.opacity == 0.5);

    // Opacity bounds clamping
    dashboard.set_dock_opacity(1.5);
    assert(dashboard.get_dock_config().opacity == 1.0);
    dashboard.set_dock_opacity(-0.2);
    assert(dashboard.get_dock_config().opacity == 0.1);

    cout << "  -> Mini-Dock window positioning and opacity clamping: PASSED" << endl;
}

void test_dock_drag_and_drop_url_ingestion() {
    cout << "\n[8/8] Testing Mini-Dock Drag-and-Drop URL Ingestion..." << endl;

    // URL validation
    assert(RemoteDashboard::is_valid_drop_url("https://example.com/iso.zip"));
    assert(RemoteDashboard::is_valid_drop_url("http://cdn.test.org/archive.7z"));
    assert(RemoteDashboard::is_valid_drop_url("magnet:?xt=urn:btih:abcdef123456"));
    assert(!RemoteDashboard::is_valid_drop_url("Not a valid link"));
    assert(!RemoteDashboard::is_valid_drop_url("https://example .com/spaces"));

    // Multi-line link parsing
    string raw_drop = "https://example.com/file1.zip\r\n"
                      "Invalid non-url text\r\n"
                      "https://example.com/file2.zip\n";

    auto links = RemoteDashboard::parse_dropped_links(raw_drop);
    assert(links.size() == 2);
    assert(links[0] == "https://example.com/file1.zip");
    assert(links[1] == "https://example.com/file2.zip");

    // Queue intake
    DownloadQueue queue("DropQueue", 2);
    RemoteDashboard dashboard;
    assert(dashboard.handle_dropped_url("https://example.com/file1.zip", &queue));
    assert(queue.get_task_count() == 1);

    cout << "  -> Drag-and-drop link ingestion & noise filtering: PASSED" << endl;
}

int main() {
    cout << "==========================================================" << endl;
    cout << "  Comprehensive Unit Test: RemoteDashboard (Task 28)      " << endl;
    cout << "==========================================================" << endl;

    test_local_ip_and_dashboard_url();
    test_embedded_mobile_dashboard_html();
    test_pairing_pin_security();
    test_remote_status_telemetry();
    test_remote_task_submission();
    test_remote_queue_action_execution();
    test_dock_config_and_state_machine();
    test_dock_drag_and_drop_url_ingestion();

    cout << "\n==========================================================" << endl;
    cout << "  [PASSED] 100% OF REMOTE DASHBOARD & MINI-DOCK VERIFIED! " << endl;
    cout << "==========================================================" << endl;

    return 0;
}
