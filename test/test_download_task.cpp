#include <curl/curl.h>

#define private public
#include "DownloadTask.h"
#undef private

#include <iostream>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iomanip>

using namespace std;

static uint64_t get_file_size_on_disk(const string &path) {
    ifstream in(path, ios::binary | ios::ate);
    if (!in.is_open()) return 0;
    return static_cast<uint64_t>(in.tellg());
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);

    cout << "==========================================================" << endl;
    cout << "  Comprehensive Unit Test: DownloadTask (Tasks 1 - 6)     " << endl;
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
    cout << "\n[1/8] Testing Initial State & Accessors..." << endl;

    DownloadTask task_init("https://httpbin.org/bytes/2048", DEST_FILE, 4);
    assert(task_init.get_status() == DownloadStatus::NotStarted);
    assert(task_init.get_last_error().empty() == true);
    assert(task_init.get_speed_limit() == 0);
    cout << "  -> Initial state (NotStarted), empty error & default unlimited speed: PASSED" << endl;

    // =========================================================================
    // 2. TASK 2: SPEED LIMITER CONTROLS & DYNAMIC UPDATES (Positive)
    // =========================================================================
    cout << "\n[2/8] Testing Speed Limiter Initialization & Dynamic Updates (Task 2)..." << endl;

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
    cout << "\n[3/8] Testing Pre-Flight Disk Space Verification (Task 4)..." << endl;

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
    cout << "\n[4/8] Testing Pre-Flight Probe Failure Handling (Negative)..." << endl;

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
    cout << "\n[5/8] Testing Destination File Creation Guards (Negative)..." << endl;

    DownloadTask task_bad_dest("https://httpbin.org/bytes/2048", DEST_BAD_PATH, 2);
    bool start_bad_dest = task_bad_dest.start();
    assert(start_bad_dest == false);
    assert(task_bad_dest.get_status() == DownloadStatus::Failed);
    cout << "  -> Invalid disk path rejected gracefully: PASSED" << endl;

    // =========================================================================
    // 6. LIVE MULTI-THREADED DOWNLOAD WITH SPEED LIMIT (Positive)
    // =========================================================================
    cout << "\n[6/8] Testing Live Multi-Threaded Download with Speed Limit (Positive)..." << endl;

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
    cout << "\n[7/8] Testing pause() and cancel() Lifecycle..." << endl;

    DownloadTask task_lifecycle("https://httpbin.org/bytes/2048", DEST_FILE, 2);

    task_lifecycle.pause();
    assert(task_lifecycle.m_pause_requested == true);

    ofstream dummy_sc(sidecar);
    dummy_sc << "{}";
    dummy_sc.close();
    assert(ResumeStore::exists(sidecar) == true);

    task_lifecycle.cancel();
    assert(task_lifecycle.m_pause_requested == true);
    assert(ResumeStore::exists(sidecar) == false);
    cout << "  -> pause flag and cancel sidecar cleanup: PASSED" << endl;

    // =========================================================================
    // 8. TASK 6: EXPIRED LINK REFRESH & SESSION CONTEXT (Pos + Neg)
    // =========================================================================
    cout << "\n[8/8] Testing Expired Link Refresh & Session Context (Task 6)..." << endl;

    const string TEST_REF = "https://example.com/download_page";
    const string TEST_COOKIE = "session_id=secret123; auth=token456";
    const string TEST_UA = "IDM_Custom_Browser/2.0";

    // 8.1 Initialize task with session context
    DownloadTask task_context("https://httpbin.org/bytes/2048", DEST_FILE, 2, 0, TEST_REF, TEST_COOKIE, TEST_UA);
    assert(task_context.get_referer() == TEST_REF);
    assert(task_context.get_cookies() == TEST_COOKIE);
    assert(task_context.get_user_agent() == TEST_UA);

    // 8.2 Dynamic update of session context
    task_context.set_http_context("https://example.com/updated_page", "session_id=new999", "IDM_Updated_UA/3.0");
    assert(task_context.get_referer() == "https://example.com/updated_page");
    assert(task_context.get_cookies() == "session_id=new999");
    assert(task_context.get_user_agent() == "IDM_Updated_UA/3.0");

    // 8.3 Update URL with compatible fresh link (Positive)
    DownloadTask task_refresh("https://httpbin.org/bytes/2048", DEST_FILE, 2);
    task_refresh.m_total_size = 2048; // Existing established download size

    bool refresh_ok = task_refresh.update_url("https://httpbin.org/bytes/2048");
    assert(refresh_ok == true);
    assert(task_refresh.get_url() == "https://httpbin.org/bytes/2048");

    // 8.4 Negative: Reject refreshed URL when size does not match existing file
    bool mismatch_ok = task_refresh.update_url("https://httpbin.org/bytes/4096");
    assert(mismatch_ok == false);
    assert(task_refresh.get_last_error().find("size mismatch") != string::npos);

    // 8.5 Negative: Reject empty URL
    bool empty_ok = task_refresh.update_url("");
    assert(empty_ok == false);

    // 8.6 Negative: Reject unreachable refreshed URL
    bool unreach_ok = task_refresh.update_url("http://127.0.0.1:59999/unreachable_port");
    assert(unreach_ok == false);
    assert(task_refresh.get_last_error().find("Validation of refreshed URL failed") != string::npos);

    cout << "  -> Link refresh validation, size matching & session headers: PASSED" << endl;

    curl_global_cleanup();

    cout << "\n==========================================================" << endl;
    cout << "  [PASSED] 100% OF DOWNLOADTASK & TASK 6 FUNCTIONS VERIFIED!" << endl;
    cout << "==========================================================" << endl;

    return 0;
}
