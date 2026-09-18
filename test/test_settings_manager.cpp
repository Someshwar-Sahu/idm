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
    cout << "  Comprehensive Unit Test: SettingsManager (Pos + Neg)    " << endl;
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
    cout << "\n[1/5] Testing SpeedLimiterMode String Conversion (Pos + Neg)..." << endl;

    // 1.1 Positive: Valid enum to string
    assert(AppSettings::speed_mode_to_string(SpeedLimiterMode::Uncapped) == "Uncapped");
    assert(AppSettings::speed_mode_to_string(SpeedLimiterMode::AutoQoS) == "AutoQoS");
    assert(AppSettings::speed_mode_to_string(SpeedLimiterMode::ManualCap) == "ManualCap");

    // 1.2 Positive: Valid string to enum
    assert(AppSettings::string_to_speed_mode("Uncapped") == SpeedLimiterMode::Uncapped);
    assert(AppSettings::string_to_speed_mode("AutoQoS") == SpeedLimiterMode::AutoQoS);
    assert(AppSettings::string_to_speed_mode("ManualCap") == SpeedLimiterMode::ManualCap);

    // 1.3 Negative: Unknown string falls back to Uncapped
    assert(AppSettings::string_to_speed_mode("Corrupt_Speed_Mode_123") == SpeedLimiterMode::Uncapped);
    assert(AppSettings::string_to_speed_mode("") == SpeedLimiterMode::Uncapped);

    // 1.4 Negative: Unknown enum value falls back to "Uncapped"
    assert(AppSettings::speed_mode_to_string(static_cast<SpeedLimiterMode>(999)) == "Uncapped");
    cout << "  -> SpeedLimiterMode conversions and fallback defaults: PASSED" << endl;

    // =========================================================================
    // 2. DEFAULT SETTINGS & RESET (Positive)
    // =========================================================================
    cout << "\n[2/5] Testing Default Settings & reset_to_defaults()..." << endl;

    SettingsManager manager;
    AppSettings defaults = manager.get_settings();

    // Verify initial values
    assert(!defaults.default_download_directory.empty());
    assert(!defaults.cache_directory.empty());
    assert(defaults.max_concurrent_download == 3);
    assert(defaults.default_connections_per_task == 8);
    assert(defaults.speed_mode == SpeedLimiterMode::Uncapped);
    assert(defaults.enable_browser_interception == true);
    assert(defaults.antivirus_scan_enabled == true);

    // Mutate settings
    AppSettings modified = defaults;
    modified.max_concurrent_download = 10;
    modified.speed_mode = SpeedLimiterMode::ManualCap;
    modified.manual_speed_limit_bps = 500000;
    manager.update_settings(modified);
    assert(manager.get_settings().max_concurrent_download == 10);

        // Reset to defaults and verify restoration
    manager.reset_to_defaults();
    defaults = manager.get_settings();
    assert(defaults.max_concurrent_download == 3);
    assert(defaults.speed_mode == SpeedLimiterMode::Uncapped);
    assert(defaults.manual_speed_limit_bps == 0);
    cout << "  -> Default initialization & reset_to_defaults(): PASSED" << endl;

    // =========================================================================
    // 3. CATEGORY PATH RESOLUTION (Positive & Negative)
    // =========================================================================
    cout << "\n[3/5] Testing Category Path Resolution (Pos + Neg)..." << endl;

    // 3.1 Positive: Known category
    string video_path = manager.get_category_path("Videos");
    assert(video_path == defaults.default_download_directory + "/Videos");

    string doc_path = manager.get_category_path("Documents");
    assert(doc_path == defaults.default_download_directory + "/Documents");

    // 3.2 Negative: Unknown category falls back to base download directory
    string unknown_path = manager.get_category_path("NonExistentCategory_XYZ");
    assert(unknown_path == defaults.default_download_directory);
    cout << "  -> Category path resolution and unknown category fallback: PASSED" << endl;

    // =========================================================================
    // 4. PERSISTENCE: SAVE & LOAD (Positive)
    // =========================================================================
    cout << "\n[4/5] Testing save() and load() Persistence (Positive)..." << endl;

    AppSettings custom = defaults;
    custom.max_concurrent_download = 6;
    custom.default_connections_per_task = 16;
    custom.speed_mode = SpeedLimiterMode::ManualCap;
    custom.manual_speed_limit_bps = 1048576; // 1 MB/s
    custom.antivirus_scan_enabled = false;
    custom.category_folders["Videos"] = "CustomMovies";

    manager.update_settings(custom);
    bool save_ok = manager.save(TEST_CONFIG);
    assert(save_ok == true);

    // Load back on fresh manager instance
    SettingsManager loaded_mgr(TEST_CONFIG);
    AppSettings loaded = loaded_mgr.get_settings();

    assert(loaded.max_concurrent_download == 6);
    assert(loaded.default_connections_per_task == 16);
    assert(loaded.speed_mode == SpeedLimiterMode::ManualCap);
    assert(loaded.manual_speed_limit_bps == 1048576);
    assert(loaded.antivirus_scan_enabled == false);
    assert(loaded.category_folders["Videos"] == "CustomMovies");
    assert(loaded_mgr.get_category_path("Videos") == defaults.default_download_directory + "/CustomMovies");
    cout << "  -> Full settings serialization and deserialization: PASSED" << endl;

    // =========================================================================
    // 5. ERROR HANDLING & CORRUPT JSON GUARDS (Negative)
    // =========================================================================
    cout << "\n[5/5] Testing Error Handling & Corrupt JSON Guards (Negative)..." << endl;

    // 5.1 Negative: Loading non-existent file returns false
    SettingsManager non_existent_mgr;
    bool load_non_existent = non_existent_mgr.load("non_existent_settings_xyz_123.json");
    assert(load_non_existent == false);

    // 5.2 Negative: Saving to non-existent directory returns false
    bool save_bad = manager.save("Z:/invalid_dir_9999/settings.json");
    assert(save_bad == false);

    // 5.3 Negative: Loading corrupt/malformed JSON returns false
    {
        ofstream corrupt_file(TEST_CORRUPT_CONFIG);
        corrupt_file << "{ this is not valid json : [ 123 ";
        corrupt_file.close();
    }
    SettingsManager corrupt_mgr;
    bool load_corrupt = corrupt_mgr.load(TEST_CORRUPT_CONFIG);
    assert(load_corrupt == false);

    // 5.4 Positive / Partial JSON: Loading JSON with missing keys populates defaults
    {
        ofstream partial_file(TEST_PARTIAL_CONFIG);
        partial_file << "{\"max_concurrent_download\": 7}";
        partial_file.close();
    }
    SettingsManager partial_mgr;
    bool load_partial = partial_mgr.load(TEST_PARTIAL_CONFIG);
    assert(load_partial == true);
    assert(partial_mgr.get_settings().max_concurrent_download == 7);
    assert(partial_mgr.get_settings().default_connections_per_task == 8); // Default fallback!
    cout << "  -> Non-existent, corrupt syntax, and partial JSON fallbacks: PASSED" << endl;

    // Clean up temporary test files
    remove(TEST_CONFIG.c_str());
    remove(TEST_CORRUPT_CONFIG.c_str());
    remove(TEST_PARTIAL_CONFIG.c_str());

    cout << "\n==========================================================" << endl;
    cout << "  [PASSED] 100% OF SETTINGSMANAGER FUNCTIONS VERIFIED!    " << endl;
    cout << "==========================================================" << endl;

    return 0;
}
