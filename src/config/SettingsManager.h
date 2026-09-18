#ifndef SETTINGS_MANAGE_H
#define SETTINGS_MANAGE_H

#include <string>
#include <map>
#include <cstdint>
#include <mutex>

enum class SpeedLimiterMode{
    Uncapped,
    AutoQoS,
    ManualCap
};

struct AppSettings {
    std::string default_download_directory{"C:/Downloads"};
    std::string cache_directory{"C:/Downloads/Cache"};
    int max_concurrent_download{3};
    int default_connections_per_task{8};

    SpeedLimiterMode speed_mode{SpeedLimiterMode::Uncapped};
    uint64_t manual_speed_limit_bps{0};

    bool enable_browser_interception{true};
    bool antivirus_scan_enabled{true};
    std::string antivirus_executable_path{""};

    std::map<std::string, std::string> category_folders{
        {"Documents", "Documents"},
        {"Videos", "Videos"},
        {"Music", "Music"},
        {"Compressed", "Compressed"},
        {"Programs", "Programs"}
    };

    static std::string speed_mode_to_string(SpeedLimiterMode mode);
    static SpeedLimiterMode string_to_speed_mode(const std::string &str);
};

class SettingsManager{
public: 
    SettingsManager() ;
    explicit SettingsManager(const std::string &config_filepath);

    bool load(const std::string &config_filepath = "");
    bool save(const std::string &config_filepath = "") const;

    AppSettings get_settings() const;
    void update_settings(const AppSettings &new_settings);

    void reset_to_defaults();

    std::string get_category_path(const std::string &category) const;

private:
    std::string m_config_filepath{"settings.json"};
    AppSettings m_settings;
    mutable std::mutex m_mutex;
};

#endif