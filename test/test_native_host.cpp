#include "NativeHost.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <sstream>
#include <cassert>
#include <cstdio>
#include <string>

using namespace std;
using json = nlohmann::json;

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);

    cout << "==========================================================" << endl;
    cout << "  Comprehensive Unit Test: NativeHost (Pos + Neg)         " << endl;
    cout << "==========================================================" << endl;

    // =========================================================================
    // 1. EXTENSION PAYLOAD PARSING (Positive)
    // =========================================================================
    cout << "\n[1/5] Testing json_to_payload() Parsing (Positive)..." << endl;

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

    assert(parsed == true);
    assert(payload.url == "https://example.com/files/document.pdf");
    assert(payload.filename == "document.pdf");
    assert(payload.cookies == "session_id=abc123xyz; cf_clearance=987");
    assert(payload.user_agent == "Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
    assert(payload.referer == "https://example.com/download-page");
    assert(payload.page_title == "Download Document");
    assert(payload.total_size == 10485760);

    // Minimal valid payload
    InterceptedDownloadPayload min_payload;
    bool min_parsed = NativeHost::json_to_payload("{\"url\":\"https://example.com/file.zip\"}", min_payload);
    assert(min_parsed == true);
    assert(min_payload.url == "https://example.com/file.zip");
    assert(min_payload.filename.empty());
    cout << "  -> Full and minimal payload parsing: PASSED" << endl;

    // =========================================================================
    // 2. PAYLOAD TO JSON SERIALIZATION & ROUNDTRIP (Positive)
    // =========================================================================
    cout << "\n[2/5] Testing payload_to_json() Roundtrip (Positive)..." << endl;

    string serialized = NativeHost::payload_to_json(payload);
    assert(!serialized.empty());

    InterceptedDownloadPayload roundtrip_payload;
    bool roundtrip_ok = NativeHost::json_to_payload(serialized, roundtrip_payload);
    assert(roundtrip_ok == true);
    assert(roundtrip_payload.url == payload.url);
    assert(roundtrip_payload.filename == payload.filename);
    assert(roundtrip_payload.cookies == payload.cookies);
    assert(roundtrip_payload.total_size == payload.total_size);
    cout << "  -> Serialization and lossless roundtrip: PASSED" << endl;

    // =========================================================================
    // 3. INVALID & CORRUPT PAYLOAD GUARDS (Negative)
    // =========================================================================
    cout << "\n[3/5] Testing Corrupt & Missing Data Guards (Negative)..." << endl;

    InterceptedDownloadPayload err_payload;

    // 3.1 Negative: Empty input string
    assert(NativeHost::json_to_payload("", err_payload) == false);

    // 3.2 Negative: Missing mandatory URL field
    string missing_url_json = "{\"filename\":\"test.pdf\",\"total_size\":1000}";
    assert(NativeHost::json_to_payload(missing_url_json, err_payload) == false);

    // 3.3 Negative: Empty URL string
    string empty_url_json = "{\"url\":\"\",\"filename\":\"test.pdf\"}";
    assert(NativeHost::json_to_payload(empty_url_json, err_payload) == false);

    // 3.4 Negative: Malformed/corrupt JSON syntax
    string corrupt_json = "{ url: 'no quotes', unclosed bracket [";
    assert(NativeHost::json_to_payload(corrupt_json, err_payload) == false);
    cout << "  -> Empty, missing URL, and syntax error guards: PASSED" << endl;

    // =========================================================================
    // 4. CHROME NATIVE MESSAGING STDIN PROTOCOL (Positive & Negative)
    // =========================================================================
    cout << "\n[4/5] Testing 4-Byte Native Protocol Stdin Parsing (Pos + Neg)..." << endl;

    // 4.1 Positive: Valid 4-byte length prefix + valid JSON body
    {
        stringstream fake_stdin;
        string json_msg = NativeHost::payload_to_json(payload);
        uint32_t len = static_cast<uint32_t>(json_msg.size());
        fake_stdin.write(reinterpret_cast<const char*>(&len), 4);
        fake_stdin.write(json_msg.data(), len);

        auto cin_buf = cin.rdbuf(fake_stdin.rdbuf());
        InterceptedDownloadPayload stream_payload;
        bool read_ok = NativeHost::read_message_from_stdin(stream_payload);
        cin.rdbuf(cin_buf); // Restore cin

        assert(read_ok == true);
        assert(stream_payload.url == payload.url);
    }

    // 4.2 Negative: Truncated header (< 4 bytes)
    {
        stringstream fake_stdin;
        char short_buf[2] = {1, 0};
        fake_stdin.write(short_buf, 2);

        auto cin_buf = cin.rdbuf(fake_stdin.rdbuf());
        InterceptedDownloadPayload stream_payload;
        bool read_fail = NativeHost::read_message_from_stdin(stream_payload);
        cin.rdbuf(cin_buf);

        assert(read_fail == false);
    }

    // 4.3 Negative: 0-byte message length
    {
        stringstream fake_stdin;
        uint32_t zero_len = 0;
        fake_stdin.write(reinterpret_cast<const char*>(&zero_len), 4);

        auto cin_buf = cin.rdbuf(fake_stdin.rdbuf());
        InterceptedDownloadPayload stream_payload;
        bool read_fail = NativeHost::read_message_from_stdin(stream_payload);
        cin.rdbuf(cin_buf);

        assert(read_fail == false);
    }

    // 4.4 Negative: Message length exceeding 10 MB cap (> 10485760 bytes)
    {
        stringstream fake_stdin;
        uint32_t oversized = 15 * 1024 * 1024; // 15 MB
        fake_stdin.write(reinterpret_cast<const char*>(&oversized), 4);

        auto cin_buf = cin.rdbuf(fake_stdin.rdbuf());
        InterceptedDownloadPayload stream_payload;
        bool read_fail = NativeHost::read_message_from_stdin(stream_payload);
        cin.rdbuf(cin_buf);

        assert(read_fail == false);
    }

    // 4.5 Negative: Truncated payload body
    {
        stringstream fake_stdin;
        uint32_t claimed_len = 50;
        fake_stdin.write(reinterpret_cast<const char*>(&claimed_len), 4);
        fake_stdin.write("{\"url\":", 7); // Only 7 bytes provided instead of 50

        auto cin_buf = cin.rdbuf(fake_stdin.rdbuf());
        InterceptedDownloadPayload stream_payload;
        bool read_fail = NativeHost::read_message_from_stdin(stream_payload);
        cin.rdbuf(cin_buf);

        assert(read_fail == false);
    }
    cout << "  -> Native 4-byte protocol parsing and truncation guards: PASSED" << endl;

    // =========================================================================
    // 5. CHROME NATIVE MESSAGING STDOUT PROTOCOL (Positive)
    // =========================================================================
    cout << "\n[5/5] Testing 4-Byte Native Protocol Stdout Response (Positive)..." << endl;

    {
        stringstream fake_stdout;
        auto cout_buf = cout.rdbuf(fake_stdout.rdbuf());

        bool write_ok = NativeHost::write_response_to_stdout(true, "Download successfully intercepted");
        cout.rdbuf(cout_buf); // Restore cout

        assert(write_ok == true);

        // Verify the 4-byte header and JSON body written to fake_stdout
        uint32_t response_len = 0;
        fake_stdout.read(reinterpret_cast<char*>(&response_len), 4);
        assert(response_len > 0);

        string body(response_len, '\0');
        fake_stdout.read(&body[0], response_len);

        json j = json::parse(body);
        assert(j["success"] == true);
        assert(j["message"] == "Download successfully intercepted");
    }
    cout << "  -> Native 4-byte protocol response serialization: PASSED" << endl;

    cout << "\n==========================================================" << endl;
    cout << "  [PASSED] 100% OF NATIVEHOST FUNCTIONS VERIFIED!         " << endl;
    cout << "==========================================================" << endl;

    return 0;
}
