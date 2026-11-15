#include "../src/config/SettingsManager.h"
#include "../src/core/ChunkWorker.h"
#include <iostream>
#include <cassert>
#include <filesystem>
#include <fstream>

using namespace std;
namespace fs = std::filesystem;

int main() {
    cout << "=== RUNNING EXTENSIVE CATEGORY EDITOR & DOWNLOADS OPTIONS TESTS (POSITIVE + NEGATIVE) ===" << endl;

    fs::path temp_config = "test_category_settings_tmp.json";
    if (fs::exists(temp_config)) fs::remove(temp_config);

    // TEST 1: Default Category Lookup & Extension Mapping (Positive)
    {
        cout << "\n[TEST 1] Testing Default Extension to Category Mapping..." << endl;
        SettingsManager sm;
        AppSettings s = sm.get_settings();

        assert(sm.determine_category_for_filename("video.mp4") == "Videos");
        assert(sm.determine_category_for_filename("movie.mkv") == "Videos");
        assert(sm.determine_category_for_filename("document.pdf") == "Documents");
        assert(sm.determine_category_for_filename("archive.zip") == "Compressed");
        assert(sm.determine_category_for_filename("setup.exe") == "Programs");
        assert(sm.determine_category_for_filename("song.mp3") == "Music");

        cout << "  -> Default extension categorization: PASSED" << endl;
    }

    // TEST 2: Custom Category Addition & Overrides (Positive)
    {
        cout << "\n[TEST 2] Testing Custom Category Addition and Extension Overrides..." << endl;
        SettingsManager sm(temp_config.string());
        AppSettings s = sm.get_settings();

        // Add custom extension mapping
        s.extension_to_category[".blend"] = "Documents";
        s.extension_to_category[".cbz"] = "Compressed";
        s.extension_to_category[".flac"] = "Music";
        // Override .mkv to General
        s.extension_to_category[".mkv"] = "General";

        sm.update_settings(s);
        sm.save();

        // Reload fresh from disk to verify persistence
        SettingsManager sm2(temp_config.string());
        assert(sm2.determine_category_for_filename("model.blend") == "Documents");
        assert(sm2.determine_category_for_filename("comic.cbz") == "Compressed");
        assert(sm2.determine_category_for_filename("film.mkv") == "General");

        cout << "  -> Custom extension persistence and lookup: PASSED" << endl;
    }

    // TEST 3: Edge Cases: Uppercase, Multi-dots, and Extensionless Files (Negative + Boundary)
    {
        cout << "\n[TEST 3] Testing Extension Edge Cases (Uppercase, Multi-dots, Extensionless)..." << endl;
        SettingsManager sm;

        // Uppercase extension
        assert(sm.determine_category_for_filename("RAMAYANA_2026.MKV") == "Videos");
        assert(sm.determine_category_for_filename("REPORT.PDF") == "Documents");
        assert(sm.determine_category_for_filename("PACKAGE.ZIP") == "Compressed");

        // Multi-dot filename
        assert(sm.determine_category_for_filename("Ramayana.2026.Hindi.Movie.1080p.HDTC.mkv") == "Videos");
        assert(sm.determine_category_for_filename("backup.2026.11.06.tar.gz") == "Compressed");

        // Extensionless or dotfile (unmatched returns empty string; caller defaults to "General")
        assert(sm.determine_category_for_filename("README") == "");
        assert(sm.determine_category_for_filename(".gitignore") == "");
        assert(sm.determine_category_for_filename("") == "");

        cout << "  -> Edge cases & case insensitivity: PASSED" << endl;
    }

    // TEST 4: Tab 5 (Downloads) Settings Serialization & Persistence (Positive)
    {
        cout << "\n[TEST 4] Testing Tab 5 (Downloads) Settings Serialization & Persistence..." << endl;
        SettingsManager sm(temp_config.string());
        AppSettings s = sm.get_settings();

        // Defaults check
        assert(s.show_download_complete_dialog == true);
        assert(s.play_sound_on_completion == true);
        assert(s.auto_open_completed_file == false);
        assert(s.max_chunk_retries == 5);

        // Mutate Tab 5 options
        s.show_download_complete_dialog = false;
        s.play_sound_on_completion = false;
        s.auto_open_completed_file = true;
        s.max_chunk_retries = 12;

        sm.update_settings(s);
        sm.save();

        // Reload fresh from disk
        SettingsManager sm2(temp_config.string());
        AppSettings s2 = sm2.get_settings();

        assert(s2.show_download_complete_dialog == false);
        assert(s2.play_sound_on_completion == false);
        assert(s2.auto_open_completed_file == true);
        assert(s2.max_chunk_retries == 12);

        cout << "  -> Tab 5 Settings full round-trip serialization: PASSED" << endl;
    }

    // TEST 5: ChunkWorker Max Retries Configuration & Boundary Clamping (Fault-Injection)
    {
        cout << "\n[TEST 5] Testing ChunkWorker Max Retries Configuration & Boundary Clamping..." << endl;
        
        ChunkWorker::set_default_max_retries(10);
        assert(ChunkWorker::get_default_max_retries() == 10);

        // Negative input: 0 or negative retry count must clamp to safe default 5
        ChunkWorker::set_default_max_retries(0);
        assert(ChunkWorker::get_default_max_retries() == 5);

        ChunkWorker::set_default_max_retries(-8);
        assert(ChunkWorker::get_default_max_retries() == 5);

        // Reset to normal
        ChunkWorker::set_default_max_retries(5);
        assert(ChunkWorker::get_default_max_retries() == 5);

        cout << "  -> ChunkWorker max retry bounds and fault tolerance: PASSED" << endl;
    }

    // Cleanup
    if (fs::exists(temp_config)) fs::remove(temp_config);

    cout << "\n=== ALL 5 CATEGORY & DOWNLOADS OPTIONS TESTS PASSED SUCCESSFULLY! ===" << endl;
    return 0;
}
