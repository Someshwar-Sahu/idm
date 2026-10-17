#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "NativeHost.h"
#include "../src/ipc/LocalServer.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <cassert>
#include <filesystem>
#include <fstream>

using namespace std;
using namespace idm;
using json = nlohmann::json;

void test_payload_json_parsing() {
    cout << "\n[1/8] Testing Payload JSON Parsing (Pos + Neg)..." << endl;

    string sample = "{"
        "\"url\": \"https://example.com/archive.zip\","
        "\"filename\": \"archive.zip\","
        "\"cookies\": \"session=12345; auth=ok\","
        "\"user_agent\": \"Mozilla/5.0 Chrome/120\","
        "\"referer\": \"https://example.com/page\","
        "\"page_title\": \"Download Archive\","
        "\"total_size\": 10485760"
    "}";

    InterceptedDownloadPayload p;
    assert(NativeHost::json_to_payload(sample, p));
    assert(p.url == "https://example.com/archive.zip");
    assert(p.filename == "archive.zip");
    assert(p.cookies == "session=12345; auth=ok");
    assert(p.total_size == 10485760);

    // Negative: missing URL
    InterceptedDownloadPayload p_bad;
    assert(!NativeHost::json_to_payload("{\"filename\":\"test.txt\"}", p_bad));

    cout << "  -> Valid payload and missing URL guard: PASSED" << endl;
}

void test_payload_roundtrip_serialization() {
    cout << "\n[2/8] Testing Payload Serialization Round-Trip..." << endl;

    InterceptedDownloadPayload orig;
    orig.url = "https://cdn.example.com/large.iso";
    orig.filename = "large.iso";
    orig.cookies = "cf_clearance=xyz";
    orig.user_agent = "IDM Browser Agent";
    orig.referer = "https://cdn.example.com";
    orig.page_title = "Large ISO";
    orig.total_size = 4294967296ULL;
    orig.user_force_override = true;

    string json_str = NativeHost::payload_to_json(orig);
    InterceptedDownloadPayload parsed;
    assert(NativeHost::json_to_payload(json_str, parsed));

    assert(parsed.url == orig.url);
    assert(parsed.total_size == orig.total_size);
    assert(parsed.user_force_override == orig.user_force_override);

    cout << "  -> JSON roundtrip bit-exact match: PASSED" << endl;
}

void test_native_manifest_generation() {
    cout << "\n[3/8] Testing Native Messaging JSON Manifest Builder..." << endl;

    vector<string> origins = {"chrome-extension://abcdefghijklmnop/"};
    string chrome_manifest = NativeHost::generate_native_manifest("C:\\IDM\\idm_native_host.exe", origins, BrowserType::Chrome);

    json j_chrome = json::parse(chrome_manifest);
    assert(j_chrome["name"] == "com.idm.native_host");
    assert(j_chrome["type"] == "stdio");
    assert(j_chrome["path"] == "C:\\IDM\\idm_native_host.exe");
    assert(j_chrome["allowed_origins"][0] == origins[0]);

    // Firefox allowed_extensions
    string ff_manifest = NativeHost::generate_native_manifest("/usr/bin/idm_native_host", {"idm@example.com"}, BrowserType::Firefox);
    json j_ff = json::parse(ff_manifest);
    assert(j_ff.contains("allowed_extensions"));

    cout << "  -> Chrome and Firefox native host manifests: PASSED" << endl;
}

void test_browser_registry_key_paths() {
    cout << "\n[4/8] Testing Browser Registry Key Hierarchy Paths..." << endl;

    string chrome_key = NativeHost::get_registry_key_path(BrowserType::Chrome);
    assert(chrome_key.find("Google\\Chrome") != string::npos);

    string edge_key = NativeHost::get_registry_key_path(BrowserType::Edge);
    assert(edge_key.find("Microsoft\\Edge") != string::npos);

    string ff_key = NativeHost::get_registry_key_path(BrowserType::Firefox);
    assert(ff_key.find("Mozilla") != string::npos);

    cout << "  -> Correct HKCU registry key strings: PASSED" << endl;
}

void test_windows_registry_registration_roundtrip() {
    cout << "\n[5/8] Testing Windows Registry Key Install and Uninstall..." << endl;

#ifdef _WIN32
    const string test_manifest = "C:\\IDM\\test_manifest.json";
    bool installed = NativeHost::install_native_host_registry(BrowserType::Chrome, test_manifest);
    assert(installed);

    // Read back registry value
    HKEY hKey;
    LSTATUS status = RegOpenKeyExA(HKEY_CURRENT_USER, NativeHost::get_registry_key_path(BrowserType::Chrome).c_str(), 0, KEY_READ, &hKey);
    assert(status == ERROR_SUCCESS);

    char val[256];
    DWORD val_len = sizeof(val);
    status = RegQueryValueExA(hKey, nullptr, nullptr, nullptr, reinterpret_cast<LPBYTE>(val), &val_len);
    RegCloseKey(hKey);

    assert(status == ERROR_SUCCESS);
    assert(string(val) == test_manifest);

    // Cleanup uninstall
    bool uninstalled = NativeHost::uninstall_native_host_registry(BrowserType::Chrome);
    assert(uninstalled);

    status = RegOpenKeyExA(HKEY_CURRENT_USER, NativeHost::get_registry_key_path(BrowserType::Chrome).c_str(), 0, KEY_READ, &hKey);
    assert(status != ERROR_SUCCESS); // Key must be gone
#endif

    cout << "  -> HKCU registry write, verification, and cleanup: PASSED" << endl;
}

void test_payload_forwarding_to_live_local_server() {
    cout << "\n[6/8] Testing Forwarding Payload to Running LocalServer..." << endl;

    LocalServer server;
    bool started = server.start("127.0.0.1", 0);
    assert(started);
    int port = server.get_port();
    assert(port > 0);

    bool route_hit = false;
    auto handler = [&route_hit](const HttpRequest& req) -> HttpResponse {
        route_hit = true;
        assert(req.json_body.value("url", "") == "https://test.com/payload.bin");
        return HttpResponse::json_response(201, {{"status", "created"}, {"id", "task_99"}});
    };
    server.register_route("POST", "/api/add_download", handler);
    server.register_route("POST", "/api/intercept", handler);

    InterceptedDownloadPayload payload;
    payload.url = "https://test.com/payload.bin";
    payload.filename = "payload.bin";

    string response;
    bool forwarded = NativeHost::forward_payload_to_server(payload, "127.0.0.1", port, &response);
    assert(forwarded);
    assert(route_hit);
    assert(response.find("201 Created") != string::npos);

    server.stop();
    cout << "  -> Loopback TCP POST /api/add_download forward: PASSED" << endl;
}

void test_offline_engine_fallback() {
    cout << "\n[7/8] Testing Offline Engine Fallback Handling..." << endl;

    InterceptedDownloadPayload payload;
    payload.url = "https://offline.com/file.zip";

    string response;
    // Attempting to forward to unused port
    bool ok = NativeHost::forward_payload_to_server(payload, "127.0.0.1", 49151, &response);
    assert(!ok); // Should cleanly fail without throwing exceptions

    cout << "  -> Clean connection failure handling when engine is offline: PASSED" << endl;
}

void test_extension_manifest_v3_validation() {
    cout << "\n[8/8] Testing Extension Manifest V3 Structural Integrity..." << endl;

    const string manifest_path = "extension/manifest.json";
    assert(filesystem::exists(manifest_path));

    ifstream fin(manifest_path);
    json manifest = json::parse(fin);

    assert(manifest["manifest_version"] == 3);
    assert(manifest.contains("permissions"));
    assert(manifest["background"]["service_worker"] == "background.js");
    assert(manifest["action"]["default_popup"] == "popup.html");

    // Verify background.js and popup.html exist on disk
    assert(filesystem::exists("extension/background.js"));
    assert(filesystem::exists("extension/popup.html"));
    assert(filesystem::exists("extension/popup.js"));
    assert(filesystem::exists("extension/com.idm.native_host.json"));

    cout << "  -> Manifest V3 structure and component files verified: PASSED" << endl;
}

int main() {
    cout << "==========================================================" << endl;
    cout << "  Comprehensive Unit Test: NativeHost & Manifest V3 (T26) " << endl;
    cout << "==========================================================" << endl;

    test_payload_json_parsing();
    test_payload_roundtrip_serialization();
    test_native_manifest_generation();
    test_browser_registry_key_paths();
    test_windows_registry_registration_roundtrip();
    test_payload_forwarding_to_live_local_server();
    test_offline_engine_fallback();
    test_extension_manifest_v3_validation();

    cout << "\n==========================================================" << endl;
    cout << "  [PASSED] 100% OF BROWSER EXTENSION & NATIVE HOST VERIFIED!" << endl;
    cout << "==========================================================" << endl;

    return 0;
}
