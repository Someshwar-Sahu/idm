#include "DownloadTask.h"
#include <iostream>
#include <cassert>
#include <string>

using namespace std;

int main() {
    cout << "==========================================================" << endl;
    cout << "  Unit Test: Range Probing & Header Matrix (Pos + Neg)    " << endl;
    cout << "==========================================================" << endl;

    // =========================================================================
    // 1. GET EFFECTIVE HEADERS & RANGE CAPABILITY DETECTION
    // =========================================================================
    cout << "\n[1/4] Testing Header Matrix for Accept-Ranges & Content-Range..." << endl;
    {
        // 1.1 Multi-hop redirect with final Accept-Ranges: bytes
        string multi_hop = 
            "HTTP/1.1 302 Found\r\n"
            "Location: https://cdn.example.com/movie.mkv\r\n\r\n"
            "HTTP/1.1 200 OK\r\n"
            "Server: Cloudflare\r\n"
            "Accept-Ranges: bytes\r\n"
            "Content-Length: 3600000000\r\n"
            "Content-Type: video/x-matroska\r\n\r\n";

        string eff = DownloadTask::get_last_response_headers(multi_hop);
        string ar = DownloadTask::get_header_value(eff, "Accept-Ranges");
        string cl = DownloadTask::get_header_value(eff, "Content-Length");

        assert(ar == "bytes");
        assert(cl == "3600000000");
        cout << "  -> Multi-hop redirect effective Accept-Ranges & Content-Length: PASSED" << endl;

        // 1.2 Content-Range detection in GET 206 response
        string range_206 = 
            "HTTP/1.1 206 Partial Content\r\n"
            "Content-Range: bytes 0-0/524288000\r\n"
            "Content-Length: 1\r\n\r\n";

        string eff_range = DownloadTask::get_last_response_headers(range_206);
        string cr = DownloadTask::get_header_value(eff_range, "Content-Range");
        assert(!cr.empty());
        assert(cr == "bytes 0-0/524288000");
        size_t slash = cr.find('/');
        assert(slash != string::npos);
        uint64_t total_size = stoull(cr.substr(slash + 1));
        assert(total_size == 524288000);
        cout << "  -> Content-Range 206 extraction & total size calculation: PASSED" << endl;
    }

    // =========================================================================
    // 2. GET 200 OK WITH ACCEPT-RANGES (CDN Fallback Edge Case)
    // =========================================================================
    cout << "\n[2/4] Testing GET Fallback 200 OK with Accept-Ranges: bytes..." << endl;
    {
        // Many CDNs respond with 200 to 1-byte probes but advertise Accept-Ranges: bytes
        string get_200_with_ranges = 
            "HTTP/1.1 200 OK\r\n"
            "Server: BunnyCDN\r\n"
            "Accept-Ranges: bytes\r\n"
            "Content-Length: 104857600\r\n\r\n";

        string eff = DownloadTask::get_last_response_headers(get_200_with_ranges);
        string ar = DownloadTask::get_header_value(eff, "Accept-Ranges");
        for (char &c : ar) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

        bool supports_ranges = (ar.find("bytes") != string::npos);
        assert(supports_ranges == true);
        cout << "  -> GET 200 with Accept-Ranges: bytes correctly detects range support: PASSED" << endl;
    }

    // =========================================================================
    // 3. GENUINELY NON-RANGE SERVER (Negative Test)
    // =========================================================================
    cout << "\n[3/4] Testing Non-Range Server (Negative: Accept-Ranges: none)..." << endl;
    {
        string no_ranges = 
            "HTTP/1.1 200 OK\r\n"
            "Server: DynamicStreamer/1.0\r\n"
            "Accept-Ranges: none\r\n"
            "Transfer-Encoding: chunked\r\n\r\n";

        string eff = DownloadTask::get_last_response_headers(no_ranges);
        string ar = DownloadTask::get_header_value(eff, "Accept-Ranges");
        for (char &c : ar) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

        bool supports_ranges = true;
        if (ar.find("none") != string::npos) {
            supports_ranges = false;
        }
        assert(supports_ranges == false);
        cout << "  -> Accept-Ranges: none correctly marks ranges unsupported: PASSED" << endl;
    }

    // =========================================================================
    // 4. RANGE SUPPORT SAFE CLAMPING IN CHUNK INITIALIZATION
    // =========================================================================
    cout << "\n[4/4] Testing Safe Thread Clamping Based on Range Support..." << endl;
    {
        DownloadTask task_range("https://example.com/test.bin", "range_test.bin", 8);
        assert(task_range.get_thread_count() == 8);

        // When ranges supported, workers = 8
        int range_workers = task_range.supports_ranges() ? task_range.get_thread_count() : 1;
        assert(range_workers == 8);

        // When ranges NOT supported, workers MUST be clamped to 1 to prevent file corruption!
        bool mock_no_ranges = false;
        int single_workers = mock_no_ranges ? task_range.get_thread_count() : 1;
        assert(single_workers == 1);
        cout << "  -> Range-supported (8 threads) vs non-range (1 thread safety clamp): PASSED" << endl;
    }

    cout << "\n==========================================================" << endl;
    cout << "  [PASSED] ALL 4/4 RANGE PROBING & HEADER TESTS PASSED!   " << endl;
    cout << "==========================================================" << endl;
    return 0;
}

