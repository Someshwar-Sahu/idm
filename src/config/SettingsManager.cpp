#include "SettingsManager.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <cstdlib>

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
        m_settings.antivirus_scan_enabled = j.value("antivirus_scan_enabled", true);
        m_settings.antivirus_executable_path = j.value("antivirus_executable_path", "");

        if (j.contains("category_folders") && j["category_folders"].is_object()) {
            m_settings.category_folders.clear();
            for (auto &[cat, subfolder] : j["category_folders"].items()) {
                m_settings.category_folders[cat] = subfolder.get<string>();
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
        j["antivirus_scan_enabled"] = m_settings.antivirus_scan_enabled;
        j["antivirus_executable_path"] = m_settings.antivirus_executable_path;

        json cat_json = json::object();
        for (const auto &[cat, subfolder] : m_settings.category_folders) {
            cat_json[cat] = subfolder;
        }
        j["category_folders"] = cat_json;

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
}

string SettingsManager::get_category_path(const string &category) const {
    lock_guard<mutex> lock(m_mutex);
    auto it = m_settings.category_folders.find(category);
    if (it != m_settings.category_folders.end()) {
        return m_settings.default_download_directory + "/" + it->second;
    }
    return m_settings.default_download_directory;
}
