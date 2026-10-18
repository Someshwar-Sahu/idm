#ifndef SETTINGS_MANAGE_H
#define SETTINGS_MANAGE_H

#include <string>
#include <map>
#include <cstdint>
#include <mutex>

enum class SpeedLimiterMode {
    Uncapped,
    AutoQoS,
    ManualCap
};

struct AppSettings {
    std::string default_download_directory{"C:/Downloads"};
    std::string cache_directory{"C:/Downloads/Cache"};
    uint64_t max_cache_size_mb{1024};
    int cache_expiry_days{7};
    int max_concurrent_download{3};
    int default_connections_per_task{8};

    SpeedLimiterMode speed_mode{SpeedLimiterMode::Uncapped};
    uint64_t manual_speed_limit_bps{0};

    bool enable_browser_interception{true};
    bool launch_on_startup{true};
    bool integrate_chrome{true};
    bool integrate_edge{true};
    bool integrate_firefox{true};
    bool integrate_brave{true};
    bool monitor_clipboard{true};
    std::string exception_sites{""};

    bool antivirus_scan_enabled{true};
    std::string antivirus_executable_path{""};

    bool enable_small_file_bypass{true};
    uint64_t min_capture_size_bytes{10 * 1024 * 1024};
    bool show_start_download_dialog{false};
    std::string file_types{"ZIP RAR 7Z ISO TAR GZ EXE MSI MP4 MKV WEBM MP3 FLAC WAV PDF DMG DAT BIN APK CSV"};

    std::map<std::string, std::string> category_folders{
        {"General", "General"},
        {"Documents", "Documents"},
        {"Videos", "Videos"},
        {"Video", "Videos"},
        {"Music", "Music"},
        {"Compressed", "Compressed"},
        {"Programs", "Programs"}
    };

    std::map<std::string, std::string> extension_to_category{
        {".pdf", "Documents"}, {".doc", "Documents"}, {".docx", "Documents"},
        {".txt", "Documents"}, {".epub", "Documents"}, {".xlsx", "Documents"},
        {".pptx", "Documents"}, {".odt", "Documents"}, {".csv", "Documents"},
        
        {".zip", "Compressed"}, {".rar", "Compressed"}, {".7z", "Compressed"},
        {".tar", "Compressed"}, {".gz", "Compressed"}, {".iso", "Compressed"},
        {".bz2", "Compressed"}, {".xz", "Compressed"},
        
        {".mp4", "Videos"}, {".mkv", "Videos"}, {".avi", "Videos"},
        {".mov", "Videos"}, {".flv", "Videos"}, {".wmv", "Videos"},
        {".webm", "Videos"}, {".m4v", "Videos"},

        {".mp3", "Music"}, {".wav", "Music"}, {".flac", "Music"},
        {".aac", "Music"}, {".ogg", "Music"}, {".m4a", "Music"},
        {".wma", "Music"},
        
        {".exe", "Programs"}, {".msi", "Programs"}, {".apk", "Programs"},
        {".dmg", "Programs"}, {".pkg", "Programs"}, {".deb", "Programs"},
        {".rpm", "Programs"}, {".bat", "Programs"}
    };

    static std::string speed_mode_to_string(SpeedLimiterMode mode);
    static SpeedLimiterMode string_to_speed_mode(const std::string &str);
};

class SettingsManager {
public: 
    SettingsManager();
    explicit SettingsManager(const std::string &config_filepath);

    bool load(const std::string &config_filepath = "");
    bool save(const std::string &config_filepath = "") const;

    AppSettings get_settings() const;
    void update_settings(const AppSettings &new_settings);

    void reset_to_defaults();

    std::string get_category_path(const std::string &category) const;

    std::string determine_category_for_filename(const std::string &filename) const;
    std::string get_target_filepath_for_filename(const std::string &filename) const;

    bool should_intercept_download(uint64_t file_size_bytes, bool user_force_override = false) const;
    bool is_browser_supported(const std::string& user_agent, const std::string& browser_hint = "") const;
    bool is_url_excepted(const std::string& url) const;
    bool is_interception_allowed(const std::string& url, const std::string& user_agent, const std::string& browser_hint = "") const;

private:
    std::string m_config_filepath{"settings.json"};
    AppSettings m_settings;
    mutable std::mutex m_mutex;
};

#endif
