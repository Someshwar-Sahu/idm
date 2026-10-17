#include "SettingsManager.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <cctype>
#include <sstream>
#include <algorithm>

using namespace std;
using json = nlohmann::json;

static string get_default_user_downloads_dir() {
#ifdef _WIN32
    const char *user_profile = std::getenv("USERPROFILE");
    if (user_profile != nullptr) {
        return string(user_profile) + "/Downloads";
    }
    return "./Downloads";
#else
    const char *home = std::getenv("HOME");
    if (home != nullptr) {
        return string(home) + "/Downloads";
    }
    return "./Downloads";
#endif
}

string AppSettings::speed_mode_to_string(SpeedLimiterMode mode) {
    switch (mode) {
        case SpeedLimiterMode::AutoQoS: return "AutoQoS";
        case SpeedLimiterMode::ManualCap: return "ManualCap";
        case SpeedLimiterMode::Uncapped:
        default: return "Uncapped";
    }
}

SpeedLimiterMode AppSettings::string_to_speed_mode(const string &str) {
    if (str == "AutoQoS") return SpeedLimiterMode::AutoQoS;
    if (str == "ManualCap") return SpeedLimiterMode::ManualCap;
    return SpeedLimiterMode::Uncapped;
}

SettingsManager::SettingsManager() {
    reset_to_defaults();
}

SettingsManager::SettingsManager(const string &config_filepath)
    : m_config_filepath(config_filepath) {
    if (!load()) {
        reset_to_defaults();
    }
}

bool SettingsManager::load(const string &config_filepath) {
    lock_guard<mutex> lock(m_mutex);

    string target_path = config_filepath.empty() ? m_config_filepath : config_filepath;
    ifstream file(target_path);

    if (!file.is_open()) {
        m_settings.default_download_directory = get_default_user_downloads_dir();
        m_settings.cache_directory = m_settings.default_download_directory + "/Cache";
        return false;
    }

    try {
        json j;
        file >> j;
        file.close();

        string default_dir = get_default_user_downloads_dir();

        m_settings.default_download_directory = j.value("default_download_directory", default_dir);
        m_settings.cache_directory = j.value("cache_directory", m_settings.default_download_directory + "/Cache");
        m_settings.max_concurrent_download = j.value("max_concurrent_download", 3);
        m_settings.default_connections_per_task = j.value("default_connections_per_task", 8);

        string mode_str = j.value("speed_mode", "Uncapped");
        m_settings.speed_mode = AppSettings::string_to_speed_mode(mode_str);
        m_settings.manual_speed_limit_bps = j.value("manual_speed_limit_bps", static_cast<uint64_t>(0));

        m_settings.enable_browser_interception = j.value("enable_browser_interception", true);
        m_settings.launch_on_startup = j.value("launch_on_startup", true);
        m_settings.integrate_chrome = j.value("integrate_chrome", true);
        m_settings.integrate_edge = j.value("integrate_edge", true);
        m_settings.integrate_firefox = j.value("integrate_firefox", true);
        m_settings.integrate_brave = j.value("integrate_brave", true);
        m_settings.monitor_clipboard = j.value("monitor_clipboard", true);
        m_settings.exception_sites = j.value("exception_sites", "");
        m_settings.antivirus_scan_enabled = j.value("antivirus_scan_enabled", true);
        m_settings.antivirus_executable_path = j.value("antivirus_executable_path", "");

        m_settings.enable_small_file_bypass = j.value("enable_small_file_bypass", true);
        m_settings.min_capture_size_bytes = j.value("min_capture_size_bytes", static_cast<uint64_t>(10 * 1024 * 1024));
        m_settings.show_start_download_dialog = j.value("show_start_download_dialog", false);
        m_settings.file_types = j.value("file_types", "ZIP RAR 7Z ISO TAR GZ EXE MSI MP4 MKV WEBM MP3 FLAC WAV PDF DMG DAT BIN APK CSV");

        if (j.contains("category_folders") && j["category_folders"].is_object()) {
            m_settings.category_folders.clear();
            for (auto &[cat, subfolder] : j["category_folders"].items()) {
                m_settings.category_folders[cat] = subfolder.get<string>();
            }
        }

        if (j.contains("extension_to_category") && j["extension_to_category"].is_object()) {
            m_settings.extension_to_category.clear();
            for (auto &[ext, cat] : j["extension_to_category"].items()) {
                m_settings.extension_to_category[ext] = cat.get<string>();
            }
        }

        m_config_filepath = target_path;
        return true;
    } catch (const exception &e) {
        cerr << "[SettingsManager Error] Failed to parse settings JSON: " << e.what() << endl;
        return false;
    }
}

bool SettingsManager::save(const string &config_filepath) const {
    lock_guard<mutex> lock(m_mutex);

    string target_path = config_filepath.empty() ? m_config_filepath : config_filepath;

    try {
        json j;
        j["default_download_directory"] = m_settings.default_download_directory;
        j["cache_directory"] = m_settings.cache_directory;
        j["max_concurrent_download"] = m_settings.max_concurrent_download;
        j["default_connections_per_task"] = m_settings.default_connections_per_task;

        j["speed_mode"] = AppSettings::speed_mode_to_string(m_settings.speed_mode);
        j["manual_speed_limit_bps"] = m_settings.manual_speed_limit_bps;

        j["enable_browser_interception"] = m_settings.enable_browser_interception;
        j["launch_on_startup"] = m_settings.launch_on_startup;
        j["integrate_chrome"] = m_settings.integrate_chrome;
        j["integrate_edge"] = m_settings.integrate_edge;
        j["integrate_firefox"] = m_settings.integrate_firefox;
        j["integrate_brave"] = m_settings.integrate_brave;
        j["monitor_clipboard"] = m_settings.monitor_clipboard;
        j["exception_sites"] = m_settings.exception_sites;
        j["antivirus_scan_enabled"] = m_settings.antivirus_scan_enabled;
        j["antivirus_executable_path"] = m_settings.antivirus_executable_path;

        j["enable_small_file_bypass"] = m_settings.enable_small_file_bypass;
        j["min_capture_size_bytes"] = m_settings.min_capture_size_bytes;
        j["show_start_download_dialog"] = m_settings.show_start_download_dialog;
        j["file_types"] = m_settings.file_types;

        json cat_json = json::object();
        for (const auto &[cat, subfolder] : m_settings.category_folders) {
            cat_json[cat] = subfolder;
        }
        j["category_folders"] = cat_json;

        json ext_json = json::object();
        for (const auto &[ext, cat] : m_settings.extension_to_category) {
            ext_json[ext] = cat;
        }
        j["extension_to_category"] = ext_json;

        ofstream file(target_path);
        if (!file.is_open()) {
            return false;
        }

        file << j.dump(4);
        file.close();
        return true;
    } catch (const exception &e) {
        cerr << "[SettingsManager Error] Failed to save settings JSON: " << e.what() << endl;
        return false;
    }
}

AppSettings SettingsManager::get_settings() const {
    lock_guard<mutex> lock(m_mutex);
    return m_settings;
}

void SettingsManager::update_settings(const AppSettings &new_settings) {
    lock_guard<mutex> lock(m_mutex);
    m_settings = new_settings;
}

void SettingsManager::reset_to_defaults() {
    lock_guard<mutex> lock(m_mutex);
    m_settings = AppSettings();
    m_settings.default_download_directory = get_default_user_downloads_dir();
    m_settings.cache_directory = m_settings.default_download_directory + "/Cache";
    m_settings.show_start_download_dialog = true;
    m_settings.file_types = "ZIP RAR 7Z ISO TAR GZ EXE MSI MP4 MKV WEBM MP3 FLAC WAV PDF DMG DAT BIN APK CSV";
}

string SettingsManager::get_category_path(const string &category) const {
    lock_guard<mutex> lock(m_mutex);
    auto it = m_settings.category_folders.find(category);
    if (it != m_settings.category_folders.end()) {
        return m_settings.default_download_directory + "/" + it->second;
    }
    return m_settings.default_download_directory;
}

string SettingsManager::determine_category_for_filename(const string &filename) const {
    lock_guard<mutex> lock(m_mutex);

    size_t dot_pos = filename.find_last_of('.');
    if (dot_pos == string::npos || dot_pos + 1 >= filename.length()) {
        return "";
    }

    string ext = filename.substr(dot_pos);
    for (char &c : ext) {
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    }

    auto it = m_settings.extension_to_category.find(ext);
    if (it != m_settings.extension_to_category.end()) {
        return it->second;
    }

    return "";
}

string SettingsManager::get_target_filepath_for_filename(const string &filename) const {
    lock_guard<mutex> lock(m_mutex);

    string ext;
    size_t dot_pos = filename.find_last_of('.');
    if (dot_pos != string::npos && dot_pos + 1 < filename.length()) {
        ext = filename.substr(dot_pos);
        for (char &c : ext) {
            c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        }
    }

    string folder = m_settings.default_download_directory;
    auto it_ext = m_settings.extension_to_category.find(ext);
    if (it_ext != m_settings.extension_to_category.end()) {
        auto it_folder = m_settings.category_folders.find(it_ext->second);
        if (it_folder != m_settings.category_folders.end()) {
            folder += "/" + it_folder->second;
        }
    }

    return folder + "/" + filename;
}

bool SettingsManager::should_intercept_download(uint64_t file_size_bytes, bool user_force_override) const {
    lock_guard<mutex> lock(m_mutex);

    if (user_force_override) {
        return true;
    }

    if (!m_settings.enable_browser_interception) {
        return false;
    }

    if (!m_settings.enable_small_file_bypass) {
        return true;
    }

    if (file_size_bytes == 0) {
        return true;
    }

    return file_size_bytes >= m_settings.min_capture_size_bytes;
}

bool SettingsManager::is_browser_supported(const std::string& user_agent, const std::string& browser_hint) const {
    lock_guard<mutex> lock(m_mutex);

    if (!m_settings.enable_browser_interception) {
        return false;
    }

    std::string ua = user_agent;
    for (char &c : ua) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

    std::string hint = browser_hint;
    for (char &c : hint) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

    // 1. Edge: User-Agent has "edg/" or "edge/" or hint is "edge"
    if (hint == "edge" || ua.find("edg/") != std::string::npos || ua.find("edge/") != std::string::npos) {
        return m_settings.integrate_edge;
    }

    // 2. Brave: Hint is "brave" or UA has "brave"
    if (hint == "brave" || ua.find("brave") != std::string::npos) {
        return m_settings.integrate_brave;
    }

    // 3. Firefox: Hint is "firefox" or UA has "firefox/" or "fxios/"
    if (hint == "firefox" || ua.find("firefox/") != std::string::npos || ua.find("fxios/") != std::string::npos) {
        return m_settings.integrate_firefox;
    }

    // 4. Chrome: Hint is "chrome" or UA has "chrome/" or "crios/"
    if (hint == "chrome" || ua.find("chrome/") != std::string::npos || ua.find("crios/") != std::string::npos) {
        return m_settings.integrate_chrome;
    }

    // Default for unidentified browser
    return m_settings.enable_browser_interception;
}

bool SettingsManager::is_url_excepted(const std::string& url) const {
    lock_guard<mutex> lock(m_mutex);
    if (m_settings.exception_sites.empty() || url.empty()) {
        return false;
    }

    std::string lower_url = url;
    for (char &c : lower_url) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

    std::istringstream iss(m_settings.exception_sites);
    std::string site;
    while (iss >> site) {
        while (!site.empty() && (site.back() == ',' || site.back() == ';')) site.pop_back();
        while (!site.empty() && (site.front() == ',' || site.front() == ';')) site.erase(0, 1);
        if (site.empty()) continue;

        for (char &c : site) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

        std::string pattern = site;
        while (!pattern.empty() && pattern.back() == '*') pattern.pop_back();
        if (pattern.empty()) continue;

        if (lower_url.find(pattern) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool SettingsManager::is_interception_allowed(const std::string& url, const std::string& user_agent, const std::string& browser_hint) const {
    if (!is_browser_supported(user_agent, browser_hint)) {
        return false;
    }
    if (is_url_excepted(url)) {
        return false;
    }
    return true;
}

