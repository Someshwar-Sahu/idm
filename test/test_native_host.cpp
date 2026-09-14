#include "NativeHost.h"
#include <iostream>
#include <cassert>

using namespace std;

int main() {
    cout << "[Test] Starting NativeHost unit test..." << endl;

    // Sample JSON string coming from Chrome / Brave Extension
        string sample_chrome_json = "{"
        "\"url\": \"https://example.com/files/document.pdf\","
        "\"filename\": \"document.pdf\","
        "\"cookies\": \"session_id=abc123xyz; cf_clearance=987\","
        "\"user_agent\": \"Mozilla/5.0 (Windows NT 10.0; Win64; x64)\","
        "\"referer\": \"https://example.com/download-page\","
        "\"page_title\": \"Download Document\","
        "\"total_size\": 10485760"
    "}";


    InterceptedDownloadPayload payload;
    bool parsed = NativeHost::json_to_payload(sample_chrome_json, payload);

    assert(parsed);
    assert(payload.url == "https://example.com/files/document.pdf");
    assert(payload.filename == "document.pdf");
    assert(payload.cookies == "session_id=abc123xyz; cf_clearance=987");
    assert(payload.total_size == 10485760);

    cout << "[Test] Successfully parsed Chrome extension JSON payload." << endl;
    cout << "[Test] Intercepted URL: " << payload.url << endl;
    cout << "[Test] Session Cookies: " << payload.cookies << endl;

    // Test serialization back to JSON
    string serialized = NativeHost::payload_to_json(payload);
    assert(!serialized.empty());

    InterceptedDownloadPayload payload2;
    bool parsed2 = NativeHost::json_to_payload(serialized, payload2);
    assert(parsed2);
    assert(payload2.url == payload.url);

    cout << "[PASSED] NativeHost unit test completed successfully!" << endl;
    return 0;
}
