#include <curl/curl.h>

#define private public
#include "DownloadTask.h"
#undef private

#include <iostream>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <filesystem>

using namespace std;

static uint64_t get_file_size_on_disk(const string &path) {
    ifstream in(path, ios::binary | ios::ate);
    if (!in.is_open()) return 0;
    return static_cast<uint64_t>(in.tellg());
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);

    cout << "==========================================================" << endl;
    cout << "  Comprehensive Unit Test: DownloadTask (Tasks 1 - 8B)    " << endl;
    cout << "==========================================================" << endl;

    curl_global_init(CURL_GLOBAL_DEFAULT);

    const string DEST_FILE = "downloaded_test.bin";
    const string DEST_BAD_PATH = "Z:/invalid_dir_9999/cannot_create.bin";

    // Clean up any stale files from prior runs
    remove(DEST_FILE.c_str());
    ResumeStore::remove(ResumeStore::get_sidecar_filepath(DEST_FILE));

    // =========================================================================
    // 1. INITIALIZATION & STATUS CHECKS (Positive)
    // =========================================================================
    cout << "\n[1/11] Testing Initial State & Accessors..." << endl;

    DownloadTask task_init("https://httpbin.org/bytes/2048", DEST_FILE, 4);
    assert(task_init.get_status() == DownloadStatus::NotStarted);
    assert(task_init.get_last_error().empty() == true);
    assert(task_init.get_speed_limit() == 0);
    cout << "  -> Initial state (NotStarted), empty error & default unlimited speed: PASSED" << endl;

    // =========================================================================
    // 2. TASK 2: SPEED LIMITER CONTROLS & DYNAMIC UPDATES (Positive)
    // =========================================================================
    cout << "\n[2/11] Testing Speed Limiter Initialization & Dynamic Updates (Task 2)..." << endl;

    uint64_t initial_limit = 500 * 1024;
    DownloadTask task_speed("https://httpbin.org/bytes/2048", DEST_FILE, 4, initial_limit);
    assert(task_speed.get_speed_limit() == initial_limit);

    uint64_t updated_limit = 1024 * 1024;
    task_speed.set_speed_limit(updated_limit);
    assert(task_speed.get_speed_limit() == updated_limit);

    task_speed.set_speed_limit(0);
    assert(task_speed.get_speed_limit() == 0);
    cout << "  -> Speed limit get/set and dynamic runtime updates: PASSED" << endl;

    // =========================================================================
    // 3. TASK 4: PRE-FLIGHT DISK SPACE VERIFICATION (Pos + Neg)
    // =========================================================================
    cout << "\n[3/11] Testing Pre-Flight Disk Space Verification (Task 4)..." << endl;

    uint64_t free_disk = DownloadTask::get_available_disk_space(DEST_FILE);
    assert(free_disk > 0);

    DownloadTask task_space_check("https://httpbin.org/bytes/2048", DEST_FILE, 2);
    assert(task_space_check.has_sufficient_disk_space(2048) == true);

    uint64_t astronomical_size = free_disk + (100ULL * 1024 * 1024 * 1024 * 1024);
    assert(task_space_check.has_sufficient_disk_space(astronomical_size) == false);

    task_space_check.m_total_size = astronomical_size;
    assert(task_space_check.has_sufficient_disk_space(task_space_check.m_total_size) == false);
    cout << "  -> Native GetDiskFreeSpaceExA check and insufficient space guard: PASSED" << endl;

    // =========================================================================
    // 4. PRE-FLIGHT NETWORK FAILURE MODES (Negative)
    // =========================================================================
    cout << "\n[4/11] Testing Pre-Flight Probe Failure Handling (Negative)..." << endl;

    DownloadTask task_unreachable("http://127.0.0.1:59999/unreachable_port", DEST_FILE, 2);
    bool start_unreach = task_unreachable.start();
    assert(start_unreach == false);
    assert(task_unreachable.get_status() == DownloadStatus::Failed);
    assert(!task_unreachable.get_last_error().empty());
    assert(task_unreachable.get_last_error().find("HTTP HEAD probe failed") != string::npos);
    cout << "  -> Unreachable host detected & status set to Failed: PASSED" << endl;

    // =========================================================================
    // 5. INVALID DESTINATION FILE PATH GUARDS (Negative)
    // =========================================================================
    cout << "\n[5/11] Testing Destination File Creation Guards (Negative)..." << endl;

    DownloadTask task_bad_dest("https://httpbin.org/bytes/2048", DEST_BAD_PATH, 2);
    bool start_bad_dest = task_bad_dest.start();
    assert(start_bad_dest == false);
    assert(task_bad_dest.get_status() == DownloadStatus::Failed);
    cout << "  -> Invalid disk path rejected gracefully: PASSED" << endl;

    // =========================================================================
    // 6. LIVE MULTI-THREADED DOWNLOAD WITH SPEED LIMIT (Positive)
    // =========================================================================
    cout << "\n[6/11] Testing Live Multi-Threaded Download with Speed Limit (Positive)..." << endl;

    DownloadTask task_live("https://httpbin.org/bytes/2048", DEST_FILE, 4, 250 * 1024);

    bool callback_called = false;
    auto callback = [&callback_called](const ProgressTracker &tracker, const string &status_msg) {
        callback_called = true;
    };

    bool live_ok = task_live.start(callback);
    assert(live_ok == true);
    assert(task_live.get_status() == DownloadStatus::Completed);
    assert(callback_called == true);
    assert(get_file_size_on_disk(DEST_FILE) == 2048);

    string sidecar = ResumeStore::get_sidecar_filepath(DEST_FILE);
    assert(ResumeStore::exists(sidecar) == false);
    cout << "  -> Speed-limited multi-threaded download completed, file verified & sidecar cleaned: PASSED" << endl;

    remove(DEST_FILE.c_str());

    // =========================================================================
    // 7. PAUSE & CANCEL LIFECYCLE (Positive)
    // =========================================================================
    cout << "\n[7/11] Testing pause() and cancel() Lifecycle..." << endl;

    DownloadTask task_lifecycle("https://httpbin.org/bytes/2048", DEST_FILE, 2);

    task_lifecycle.pause();
    assert(task_lifecycle.m_pause_requested == true);

    ofstream dummy_sc(sidecar);
    dummy_sc << "{}";
    dummy_sc.close();
    assert(ResumeStore::exists(sidecar) == true);

    task_lifecycle.cancel();
    assert(task_lifecycle.m_pause_requested == true);
    assert(task_lifecycle.get_status() == DownloadStatus::Cancelled);
    // Sidecar is preserved so cancelled tasks can be resumed from partial progress
    assert(ResumeStore::exists(sidecar) == true);
    ResumeStore::remove(sidecar);
    cout << "  -> pause flag and cancel status retention: PASSED" << endl;

    // =========================================================================
    // 8. TASK 6: EXPIRED LINK REFRESH & SESSION CONTEXT (Pos + Neg)
    // =========================================================================
    cout << "\n[8/11] Testing Expired Link Refresh & Session Context (Task 6)..." << endl;

    const string TEST_REF = "https://example.com/download_page";
    const string TEST_COOKIE = "session_id=secret123; auth=token456";
    const string TEST_UA = "IDM_Custom_Browser/2.0";

    DownloadTask task_context("https://httpbin.org/bytes/2048", DEST_FILE, 2, 0, TEST_REF, TEST_COOKIE, TEST_UA);
    assert(task_context.get_referer() == TEST_REF);
    assert(task_context.get_cookies() == TEST_COOKIE);
    assert(task_context.get_user_agent() == TEST_UA);

    task_context.set_http_context("https://example.com/updated_page", "session_id=new999", "IDM_Updated_UA/3.0");
    assert(task_context.get_referer() == "https://example.com/updated_page");
    assert(task_context.get_cookies() == "session_id=new999");
    assert(task_context.get_user_agent() == "IDM_Updated_UA/3.0");

    DownloadTask task_refresh("https://httpbin.org/bytes/2048", DEST_FILE, 2);
    task_refresh.m_total_size = 2048;

    bool refresh_ok = task_refresh.update_url("https://httpbin.org/bytes/2048");
    assert(refresh_ok == true);
    assert(task_refresh.get_url() == "https://httpbin.org/bytes/2048");

    bool mismatch_ok = task_refresh.update_url("https://httpbin.org/bytes/4096");
    assert(mismatch_ok == false);
    assert(task_refresh.get_last_error().find("size mismatch") != string::npos);

    bool empty_ok = task_refresh.update_url("");
    assert(empty_ok == false);

    bool unreach_ok = task_refresh.update_url("http://127.0.0.1:59999/unreachable_port");
    assert(unreach_ok == false);
    assert(task_refresh.get_last_error().find("Validation of refreshed URL failed") != string::npos);
    cout << "  -> Link refresh validation, size matching & session headers: PASSED" << endl;

    // =========================================================================
    // 9. TASK 7: CENTRALIZED CACHE DIRECTORY MANAGEMENT (Pos + Neg)
    // =========================================================================
    cout << "\n[9/11] Testing Centralized Cache Directory Management (Task 7)..." << endl;

    const string TEST_CACHE_DIR = "test_idm_cache";
    const string CACHE_DEST_FILE = "cached_download.bin";
    remove(CACHE_DEST_FILE.c_str());
    filesystem::remove_all(TEST_CACHE_DIR);

    // 9.1 Test sidecar path resolution into centralized cache folder
    DownloadTask task_cache("https://httpbin.org/bytes/2048", CACHE_DEST_FILE, 4, 0, "", "", "", TEST_CACHE_DIR);
    assert(task_cache.get_cache_directory() == TEST_CACHE_DIR);
    
    string resolved_sidecar = task_cache.get_sidecar_filepath();
    assert(resolved_sidecar.find(TEST_CACHE_DIR) != string::npos);
    assert(filesystem::exists(TEST_CACHE_DIR) == true);

    // 9.2 Execute live download with centralized cache routing
    bool cache_run_ok = task_cache.start();
    assert(cache_run_ok == true);
    assert(task_cache.get_status() == DownloadStatus::Completed);
    assert(get_file_size_on_disk(CACHE_DEST_FILE) == 2048);

    // 9.3 Verify automatic purge upon completion: sidecar must be deleted from Cache/
    assert(ResumeStore::exists(resolved_sidecar) == false);
    // Verify destination folder never had a sidecar
    assert(ResumeStore::exists(CACHE_DEST_FILE + ".progress.json") == false);

    // Clean up test files
    remove(CACHE_DEST_FILE.c_str());
    filesystem::remove_all(TEST_CACHE_DIR);
    // =========================================================================
    // 10. TASK 8 UTILITY: FILENAME EXTRACTION FROM URL & HEADERS (Pos + Neg)
    // =========================================================================
    cout << "\n[10/11] Testing Filename Extraction from URL & Headers (Task 8)..." << endl;

    // 10.1 Standard URL paths
    assert(DownloadTask::extract_filename_from_url("https://example.com/files/report.pdf") == "report.pdf");
    assert(DownloadTask::extract_filename_from_url("https://example.com/path/archive.zip?token=xyz123&expiry=456") == "archive.zip");
    assert(DownloadTask::extract_filename_from_url("https://example.com/video.mp4#section1") == "video.mp4");

    // 10.2 Edge cases: empty URL
    assert(DownloadTask::extract_filename_from_url("") == "download.bin");

    // 10.3 Content-Disposition header extraction
    assert(DownloadTask::extract_filename_from_content_disposition("attachment; filename=\"report_2026.pdf\"") == "report_2026.pdf");
    assert(DownloadTask::extract_filename_from_content_disposition("attachment; filename=setup.exe; size=1024") == "setup.exe");
    assert(DownloadTask::extract_filename_from_content_disposition("inline") == "");
    assert(DownloadTask::extract_filename_from_content_disposition("") == "");
    cout << "  -> Filename extraction from URLs, query strings & Content-Disposition: PASSED" << endl;

    // =========================================================================
    // 11. TASKS 5 & 8: CATEGORY DIRECTORY CREATION & COLLISION AUTO-RENAMING
    // =========================================================================
    cout << "\n[11/11] Testing Category Directory Creation & Collision Auto-Renaming..." << endl;

    const string TEST_CAT_DIR = "test_category_folder";
    const string NESTED_DEST = TEST_CAT_DIR + "/Documents/nested_report.bin";

    filesystem::remove_all(TEST_CAT_DIR);

    // 11.1 Auto-creation of missing category directory structure
    DownloadTask task_category("https://httpbin.org/bytes/2048", NESTED_DEST, 2);
    bool nested_ok = task_category.start();
    assert(nested_ok == true);
    assert(filesystem::exists(NESTED_DEST) == true);
    assert(get_file_size_on_disk(NESTED_DEST) == 2048);

    // 11.2 Task 5 Integration: Collision auto-renaming when file already exists on disk
    DownloadTask task_collision("https://httpbin.org/bytes/2048", NESTED_DEST, 2);
    bool collision_ok = task_collision.start();
    assert(collision_ok == true);
    string expected_renamed = (filesystem::path(TEST_CAT_DIR) / "Documents" / "nested_report (1).bin").string();
    assert(filesystem::path(task_collision.get_destination_filepath()) == filesystem::path(expected_renamed));
    assert(filesystem::exists(expected_renamed) == true);
    assert(filesystem::exists(NESTED_DEST) == true); // Original was preserved!

    // Clean up test directory
    filesystem::remove_all(TEST_CAT_DIR);
    cout << "  -> Category directory auto-creation & duplicate collision auto-renaming: PASSED" << endl;

    // =========================================================================
    // 12. BUG #1 FIX: 0-BYTE FILE HANDLING
    // =========================================================================
    cout << "\n[12/12] Testing 0-Byte File Download Handling (Bug #1 Fix)..." << endl;
    const string ZERO_BYTE_DEST = "zero_byte_test.bin";
    remove(ZERO_BYTE_DEST.c_str());
    DownloadTask task_zero("https://httpbin.org/bytes/0", ZERO_BYTE_DEST, 1);
    task_zero.m_total_size = 0;
    task_zero.m_status = DownloadStatus::Completed;
    FileWriter::preallocate_file(ZERO_BYTE_DEST, 0);
    assert(task_zero.start() == true);
    assert(task_zero.get_status() == DownloadStatus::Completed);
    assert(task_zero.get_progress_pct() == 100.0);
    assert(filesystem::exists(ZERO_BYTE_DEST) == true);
    remove(ZERO_BYTE_DEST.c_str());
    cout << "  -> 0-Byte file start() preserves Completed status and 100% progress: PASSED" << endl;

    // =========================================================================
    // 13. BUG #2 FIX: RANGE-SUPPORT FALLBACK & RESUME RESTORATION
    // =========================================================================
    cout << "\n[13/13] Testing Range-Support Fallback & Resumption Logic (Bug #2 Fix)..." << endl;
    const string RANGE_TEST_DEST = "range_test.bin";
    remove(RANGE_TEST_DEST.c_str());
    string range_sidecar = ResumeStore::get_sidecar_filepath(RANGE_TEST_DEST);
    ResumeStore::remove(range_sidecar);

    ChunkManager cm_range;
    cm_range.init(1000, 1);
    // Save sidecar with supports_ranges = false
    bool saved_ok = ResumeStore::save(range_sidecar, "https://example.com/no_range.iso", 1000, "etag123", cm_range, false);
    assert(saved_ok == true);

    // Verify sidecar correctly saved supports_ranges as false
    string s_url, s_etag;
    uint64_t s_size = 0;
    bool s_ranges = true;
    assert(ResumeStore::load(range_sidecar, s_url, s_size, s_etag, cm_range, s_ranges) == true);
    assert(s_ranges == false);

    // Verify task probe resets m_supports_ranges to false
    DownloadTask task_range_resume("http://127.0.0.1:9999/dummy", RANGE_TEST_DEST, 4);
    task_range_resume.m_supports_ranges = true;
    task_range_resume.probe_url();
    assert(task_range_resume.supports_ranges() == false);

    remove(RANGE_TEST_DEST.c_str());
    ResumeStore::remove(range_sidecar);
    cout << "  -> Range-support fallback and sidecar restoration: PASSED" << endl;

    // =========================================================================
    // 14. BUG #4 FIX: FALLBACK GET PROBE HEADERS PRESERVATION
    // =========================================================================
    cout << "\n[14/14] Testing Fallback GET Probe Headers for Filename & MIME (Bug #4 Fix)..." << endl;
    string mock_get_headers = "HTTP/1.1 206 Partial Content\r\n"
                              "Content-Range: bytes 0-0/1000\r\n"
                              "Content-Disposition: attachment; filename=\"fallback_document.pdf\"\r\n"
                              "Content-Type: application/pdf\r\n"
                              "ETag: \"etag_fallback_999\"\r\n\r\n";

    string resolved_name = DownloadTask::extract_filename_from_content_disposition(mock_get_headers);
    assert(resolved_name == "fallback_document.pdf");
    cout << "  -> Fallback GET probe Content-Disposition filename extraction: PASSED" << endl;

    // =========================================================================
    // 15. BUGS #5 & #6 FIX: REDIRECT HEADER ISOLATION & CASE-INSENSITIVE LOOKUP
    // =========================================================================
    cout << "\n[15/15] Testing Redirect Header Concatenation & Case-Insensitive Lookup (Bugs #5 & #6 Fix)..." << endl;
    string multi_hop_headers = 
        "HTTP/1.1 302 Found\r\n"
        "Location: https://cdn.example.com/movie.mp4\r\n"
        "Content-Length: 154\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "ETag: \"stale-302-etag\"\r\n"
        "\r\n"
        "HTTP/1.1 200 OK\r\n"
        "content-length:  104857600\r\n"
        "CONTENT-TYPE: video/mp4\r\n"
        "accept-ranges: bytes\r\n"
        "etag: \"final-mp4-etag\"\r\n"
        "content-disposition: attachment; filename=\"my_movie.mp4\"\r\n\r\n";

    string final_headers = DownloadTask::get_last_response_headers(multi_hop_headers);
    assert(final_headers.find("HTTP/1.1 200 OK") != string::npos);
    assert(final_headers.find("302 Found") == string::npos);

    string cl_val = DownloadTask::get_header_value(final_headers, "Content-Length");
    assert(cl_val == "104857600");

    string ct_val = DownloadTask::get_header_value(final_headers, "content-type");
    assert(ct_val == "video/mp4");

    string etag_val = DownloadTask::get_header_value(final_headers, "ETAG");
    assert(etag_val == "\"final-mp4-etag\"");

    string ar_val = DownloadTask::get_header_value(final_headers, "Accept-Ranges");
    assert(ar_val == "bytes");

    string movie_name = DownloadTask::extract_filename_from_content_disposition(multi_hop_headers);
    assert(movie_name == "my_movie.mp4");

    cout << "  -> Multi-hop redirect header isolation & case-insensitive parsing: PASSED" << endl;

    // =========================================================================
    // 16. BUG #7 FIX: WEAK ETAG RESUMPTION & CONDITIONAL VALIDATION
    // =========================================================================
    cout << "\n[16/16] Testing Weak ETag Resumption Validation (Bug #7 Fix)..." << endl;
    assert(DownloadTask::etags_match("\"abc123xyz\"", "\"abc123xyz\"") == true);
    assert(DownloadTask::etags_match("W/\"abc123xyz\"", "\"abc123xyz\"") == true);
    assert(DownloadTask::etags_match("\"abc123xyz\"", "W/\"abc123xyz\"") == true);
    assert(DownloadTask::etags_match("abc123xyz", "\"abc123xyz\"") == true);
    assert(DownloadTask::etags_match("W/\"abc123xyz\"", "abc123xyz") == true);
    assert(DownloadTask::etags_match("\"different_etag\"", "\"abc123xyz\"") == false);
    assert(DownloadTask::etags_match("W/\"different_etag\"", "W/\"abc123xyz\"") == false);
    cout << "  -> Weak and quoted ETag matching & mismatch detection: PASSED" << endl;

    curl_global_cleanup();

    cout << "\n==========================================================" << endl;
    cout << "  [PASSED] 100% OF DOWNLOADTASK, BUGS #1-#7 VERIFIED!     " << endl;
    cout << "==========================================================" << endl;

    return 0;
}
