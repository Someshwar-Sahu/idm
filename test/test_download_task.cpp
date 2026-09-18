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
    cout << "  Comprehensive Unit Test: DownloadTask (Pos + Neg)       " << endl;
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
    cout << "\n[1/5] Testing Initial State & Accessors..." << endl;

    DownloadTask task_init("https://httpbin.org/bytes/2048", DEST_FILE, 4);
    assert(task_init.get_status() == DownloadStatus::NotStarted);
    assert(task_init.get_last_error().empty() == true);
    cout << "  -> Initial state (NotStarted) & empty error: PASSED" << endl;

    // =========================================================================
    // 2. PRE-FLIGHT NETWORK FAILURE MODES (Negative)
    // =========================================================================
    cout << "\n[2/5] Testing Pre-Flight Probe Failure Handling (Negative)..." << endl;

    DownloadTask task_unreachable("http://127.0.0.1:59999/unreachable_port", DEST_FILE, 2);
    bool start_unreach = task_unreachable.start();
    assert(start_unreach == false);
    assert(task_unreachable.get_status() == DownloadStatus::Failed);
    assert(!task_unreachable.get_last_error().empty());
    assert(task_unreachable.get_last_error().find("HTTP HEAD probe failed") != string::npos);
    cout << "  -> Unreachable host detected & status set to Failed: PASSED" << endl;

    // =========================================================================
    // 3. INVALID DESTINATION FILE PATH GUARDS (Negative)
    // =========================================================================
    cout << "\n[3/5] Testing Destination File Creation Guards (Negative)..." << endl;

    // URL is valid, but destination path is on a non-existent drive
    DownloadTask task_bad_dest("https://httpbin.org/bytes/2048", DEST_BAD_PATH, 2);
    bool start_bad_dest = task_bad_dest.start();
    assert(start_bad_dest == false);
    assert(task_bad_dest.get_status() == DownloadStatus::Failed);
    assert(task_bad_dest.get_last_error().find("Failed to create/open destination file") != string::npos);
    cout << "  -> Invalid disk path rejected gracefully: PASSED" << endl;

    // =========================================================================
    // 4. LIVE MULTI-THREADED DOWNLOAD EXECUTION (Positive)
    // =========================================================================
    cout << "\n[4/5] Testing Live Multi-Threaded Download Execution (Positive)..." << endl;

    DownloadTask task_live("https://httpbin.org/bytes/2048", DEST_FILE, 4);

    bool callback_called = false;
    auto callback = [&callback_called](const ProgressTracker &tracker, const string &status_msg) {
        callback_called = true;
    };

    bool live_ok = task_live.start(callback);
    assert(live_ok == true);
    assert(task_live.get_status() == DownloadStatus::Completed);
    assert(callback_called == true);

    // Verify destination file size on disk matches
    assert(get_file_size_on_disk(DEST_FILE) == 2048);

    // Verify sidecar progress JSON was automatically removed upon completion
    string sidecar = ResumeStore::get_sidecar_filepath(DEST_FILE);
    assert(ResumeStore::exists(sidecar) == false);
    cout << "  -> Multi-threaded download completed, file verified & sidecar cleaned: PASSED" << endl;

    remove(DEST_FILE.c_str());

    // =========================================================================
    // 5. PAUSE & CANCEL LIFECYCLE (Positive)
    // =========================================================================
    cout << "\n[5/5] Testing pause() and cancel() Lifecycle..." << endl;

    DownloadTask task_lifecycle("https://httpbin.org/bytes/2048", DEST_FILE, 2);

    // 5.1 Test pause sets m_pause_requested flag
    task_lifecycle.pause();
    assert(task_lifecycle.m_pause_requested == true);

    // 5.2 Test cancel removes sidecar file
    // Create dummy sidecar to ensure cancel deletes it
    ofstream dummy_sc(sidecar);
    dummy_sc << "{}";
    dummy_sc.close();
    assert(ResumeStore::exists(sidecar) == true);

    task_lifecycle.cancel();
    assert(task_lifecycle.m_pause_requested == true);
    assert(ResumeStore::exists(sidecar) == false);
    cout << "  -> pause flag and cancel sidecar cleanup: PASSED" << endl;

    curl_global_cleanup();

    cout << "\n==========================================================" << endl;
    cout << "  [PASSED] 100% OF DOWNLOADTASK FUNCTIONS VERIFIED!       " << endl;
    cout << "==========================================================" << endl;

    return 0;
}
