#include "SettingsManager.h"
#include <iostream>
#include <fstream>
#include <cassert>
#include <cstdio>
#include <string>

using namespace std;

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);

    cout << "==========================================================" << endl;
    cout << "  Comprehensive Unit Test: SettingsManager (Tasks 1 - 8B) " << endl;
    cout << "==========================================================" << endl;

    const string TEST_CONFIG = "test_settings.json";
    const string TEST_CORRUPT_CONFIG = "test_corrupt_settings.json";
    const string TEST_PARTIAL_CONFIG = "test_partial_settings.json";

    // Clean up any stale files from previous runs
    remove(TEST_CONFIG.c_str());
    remove(TEST_CORRUPT_CONFIG.c_str());
    remove(TEST_PARTIAL_CONFIG.c_str());

    // =========================================================================
    // 1. ENUM STRING CONVERSION (Positive & Negative)
    // =========================================================================
    cout << "\n[1/7] Testing SpeedLimiterMode String Conversion (Pos + Neg)..." << endl;

    assert(AppSettings::speed_mode_to_string(SpeedLimiterMode::Uncapped) == "Uncapped");
    assert(AppSettings::speed_mode_to_string(SpeedLimiterMode::AutoQoS) == "AutoQoS");
    assert(AppSettings::speed_mode_to_string(SpeedLimiterMode::ManualCap) == "ManualCap");

    assert(AppSettings::string_to_speed_mode("Uncapped") == SpeedLimiterMode::Uncapped);
    assert(AppSettings::string_to_speed_mode("AutoQoS") == SpeedLimiterMode::AutoQoS);
    assert(AppSettings::string_to_speed_mode("ManualCap") == SpeedLimiterMode::ManualCap);

    assert(AppSettings::string_to_speed_mode("Corrupt_Speed_Mode_123") == SpeedLimiterMode::Uncapped);
    assert(AppSettings::string_to_speed_mode("") == SpeedLimiterMode::Uncapped);
    cout << "  -> SpeedLimiterMode conversions and fallback defaults: PASSED" << endl;

    // =========================================================================
    // 2. DEFAULT SETTINGS & RESET (Positive)
    // =========================================================================
    cout << "\n[2/7] Testing Default Settings & reset_to_defaults()..." << endl;

    SettingsManager manager;
    AppSettings defaults = manager.get_settings();

    assert(!defaults.default_download_directory.empty());
    assert(!defaults.cache_directory.empty());
    assert(defaults.max_concurrent_download == 3);
    assert(defaults.default_connections_per_task == 8);
    assert(defaults.speed_mode == SpeedLimiterMode::Uncapped);
    assert(defaults.enable_browser_interception == true);
    assert(defaults.antivirus_scan_enabled == true);
    assert(defaults.enable_small_file_bypass == true);
    assert(defaults.min_capture_size_bytes == 10 * 1024 * 1024);

    AppSettings modified = defaults;
    modified.max_concurrent_download = 10;
    modified.speed_mode = SpeedLimiterMode::ManualCap;
    modified.manual_speed_limit_bps = 500000;
    manager.update_settings(modified);
    assert(manager.get_settings().max_concurrent_download == 10);

    manager.reset_to_defaults();
    defaults = manager.get_settings();
    assert(defaults.max_concurrent_download == 3);
    assert(defaults.speed_mode == SpeedLimiterMode::Uncapped);
    assert(defaults.manual_speed_limit_bps == 0);
    cout << "  -> Default initialization & reset_to_defaults(): PASSED" << endl;

    // =========================================================================
    // 3. CATEGORY PATH RESOLUTION (Positive & Negative)
    // =========================================================================
    cout << "\n[3/7] Testing Category Path Resolution (Pos + Neg)..." << endl;

    string video_path = manager.get_category_path("Videos");
    assert(video_path == defaults.default_download_directory + "/Videos");

    string doc_path = manager.get_category_path("Documents");
    assert(doc_path == defaults.default_download_directory + "/Documents");

    string unknown_path = manager.get_category_path("NonExistentCategory_XYZ");
    assert(unknown_path == defaults.default_download_directory);
    cout << "  -> Category path resolution and unknown category fallback: PASSED" << endl;

    // =========================================================================
    // 4. PERSISTENCE: SAVE & LOAD (Positive)
    // =========================================================================
    cout << "\n[4/7] Testing save() and load() Persistence (Positive)..." << endl;

    AppSettings custom = defaults;
    custom.max_concurrent_download = 6;
    custom.default_connections_per_task = 16;
    custom.speed_mode = SpeedLimiterMode::ManualCap;
    custom.manual_speed_limit_bps = 1048576;
    custom.min_capture_size_bytes = 20 * 1024 * 1024; // 20 MB
    custom.category_folders["Videos"] = "CustomMovies";

    manager.update_settings(custom);
    bool save_ok = manager.save(TEST_CONFIG);
    assert(save_ok == true);

    SettingsManager loaded_mgr(TEST_CONFIG);
    AppSettings loaded = loaded_mgr.get_settings();

    assert(loaded.max_concurrent_download == 6);
    assert(loaded.default_connections_per_task == 16);
    assert(loaded.manual_speed_limit_bps == 1048576);
    assert(loaded.min_capture_size_bytes == 20 * 1024 * 1024);
    assert(loaded.category_folders["Videos"] == "CustomMovies");
    cout << "  -> Full settings serialization and deserialization: PASSED" << endl;

    // =========================================================================
    // 5. ERROR HANDLING & CORRUPT JSON GUARDS (Negative)
    // =========================================================================
    cout << "\n[5/7] Testing Error Handling & Corrupt JSON Guards (Negative)..." << endl;

    SettingsManager non_existent_mgr;
    assert(non_existent_mgr.load("non_existent_settings_xyz_123.json") == false);

    assert(manager.save("Z:/invalid_dir_9999/settings.json") == false);

    {
        ofstream corrupt_file(TEST_CORRUPT_CONFIG);
        corrupt_file << "{ this is not valid json : [ 123 ";
        corrupt_file.close();
    }
    SettingsManager corrupt_mgr;
    assert(corrupt_mgr.load(TEST_CORRUPT_CONFIG) == false);

    {
        ofstream partial_file(TEST_PARTIAL_CONFIG);
        partial_file << "{\"max_concurrent_download\": 7}";
        partial_file.close();
    }
    SettingsManager partial_mgr;
    assert(partial_mgr.load(TEST_PARTIAL_CONFIG) == true);
    assert(partial_mgr.get_settings().max_concurrent_download == 7);
    cout << "  -> Non-existent, corrupt syntax, and partial JSON fallbacks: PASSED" << endl;

    // =========================================================================
    // 6. TASK 8: AUTOMATIC FILE CATEGORY ROUTING (Positive & Edge Cases)
    // =========================================================================
    cout << "\n[6/7] Testing Automatic File Category Routing (Task 8)..." << endl;

    manager.reset_to_defaults();

    // 6.1 All standard categories
    assert(manager.determine_category_for_filename("report.pdf") == "Documents");
    assert(manager.determine_category_for_filename("lecture.mp4") == "Videos");
    assert(manager.determine_category_for_filename("archive.zip") == "Compressed");
    assert(manager.determine_category_for_filename("song.mp3") == "Music");
    assert(manager.determine_category_for_filename("setup.exe") == "Programs");



    // 6.2 Case-insensitive matching (.PDF, .MKV, .ZIP)
    assert(manager.determine_category_for_filename("EBOOK.PDF") == "Documents");
    assert(manager.determine_category_for_filename("MOVIE.MKV") == "Videos");
    assert(manager.determine_category_for_filename("BACKUP.7Z") == "Compressed");

    // 6.3 Unknown extensions or extension-less files
    assert(manager.determine_category_for_filename("README") == "");
    assert(manager.determine_category_for_filename("data.unknownext999") == "");

    // 6.4 Full target filepath routing
    string expected_video = defaults.default_download_directory + "/Videos/clip.mp4";
    assert(manager.get_target_filepath_for_filename("clip.mp4") == expected_video);

    string expected_unknown = defaults.default_download_directory + "/LICENSE";
    assert(manager.get_target_filepath_for_filename("LICENSE") == expected_unknown);
    cout << "  -> Category determination, case-insensitivity & filepath routing: PASSED" << endl;

    // =========================================================================
    // 7. TASK 8B: SMALL FILE BYPASS THRESHOLD & OVERRIDES (Pos + Neg)
    // =========================================================================
    cout << "\n[7/7] Testing Small File Bypass Threshold (Task 8B)..." << endl;

    constexpr uint64_t MB = 1024 * 1024;

    // Default threshold is 10 MB
    // 7.1 Small file (< 10 MB) bypasses IDM to browser
    assert(manager.should_intercept_download(2 * MB) == false);  // 2 MB -> Browser
    assert(manager.should_intercept_download(500 * 1024) == false); // 500 KB -> Browser

    // 7.2 Large file (>= 10 MB) is captured by IDM
    assert(manager.should_intercept_download(10 * MB) == true);
    assert(manager.should_intercept_download(500 * MB) == true);

    // 7.3 Unknown size (0 bytes) is always captured
    assert(manager.should_intercept_download(0) == true);

    // 7.4 User Force Override (Alt-click or context menu) captures even 1 byte!
    assert(manager.should_intercept_download(100, true) == true);
    assert(manager.should_intercept_download(2 * MB, true) == true);

    // 7.5 When bypass is disabled in settings, all files are intercepted
    AppSettings bypass_disabled = defaults;
    bypass_disabled.enable_small_file_bypass = false;
    manager.update_settings(bypass_disabled);
    assert(manager.should_intercept_download(100) == true);
    assert(manager.should_intercept_download(2 * MB) == true);

    cout << "  -> Small file threshold (10 MB), browser bypass & user force override: PASSED" << endl;

    // Clean up temporary test files
    remove(TEST_CONFIG.c_str());
    remove(TEST_CORRUPT_CONFIG.c_str());
    remove(TEST_PARTIAL_CONFIG.c_str());

    cout << "\n==========================================================" << endl;
    cout << "  [PASSED] 100% OF SETTINGSMANAGER & TASKS 8/8B VERIFIED! " << endl;
    cout << "==========================================================" << endl;

    return 0;
}
