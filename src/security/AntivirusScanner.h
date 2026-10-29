#ifndef ANTIVIRUS_SCANNER_H
#define ANTIVIRUS_SCANNER_H

#include <string>
#include <cstdint>
#include <map>

enum class SecurityScanStatus{
    Safe,
    Untrusted,
    Blocked,
    Error,
};

enum class FileScanVerdict{
    Clean,
    Infected,
    ScanFailed,
    Skipped
};

struct LinkReputationResult {
    SecurityScanStatus status{SecurityScanStatus::Safe};
    bool is_safe{true};
    bool prompt_recommended{false};
    std::string details;
    uint32_t win32_hresult{0};
};

struct FileScanResult{
    FileScanVerdict verdict{FileScanVerdict::Clean};
    std::string threat_name;
    std::string raw_output;
    int exit_code{0};
    uint64_t scan_duration_ms{0};
};

class AntivirusScanner{
public:
    AntivirusScanner() = default;
    ~AntivirusScanner() = default;

    static LinkReputationResult check_url_reputation(const std::string &url, const std::string &filename = "");

    static bool apply_mark_of_the_web(const std::string &filepath, const std::string &source_url = "");
    static bool has_mark_of_the_web(const std::string &filepath);
    static std::string read_mark_of_the_web(const std::string &filepath);

    static std::string find_windows_defender_executable(const std::string &custom_path="");
    static std::string parse_threat_name(const std::string &stdout_output);
    static FileScanResult scan_file(const std::string &filepath, const std::string &custom_scanner_path="");

    static std::string calculate_file_sha256(const std::string &filepath);
    static std::string extract_domain_from_url(const std::string &url);
    
    static bool add_hash_to_whitelist(const std::string &sha256_hash, const std::string &reason = "");
    static bool add_domain_to_whitelist(const std::string &domain, const std::string &reason = "");
    static bool is_hash_whitelisted(const std::string &sha256_hash);
    static bool is_domain_whitelisted(const std::string &domain_or_url);
    static bool remove_from_whitelist(const std::string &entry);
    static void clear_whitelist();
    static std::map<std::string, std::string> get_whitelisted_domains();
    static std::map<std::string, std::string> get_whitelisted_hashes();
    static bool has_whitelisted_hashes();
    static bool load_whitelist(const std::string &json_filepath ="");
    static bool save_whitelist(const std::string &json_filepath = "");

    static bool quarantine_file(const std::string &filepath, const std::string &threat_name, std::string &out_quarantined_path, const std::string &source_url = "");
    static bool restore_quarantined_file(const std::string &quarantined_path, const std::string &target_path = "");
    static bool delete_quarantined_file(const std::string &quarantined_path);

    static bool is_amsi_available();
    static FileScanResult scan_buffer(const void *buffer, size_t length, const std::string &content_name = "DownloadChunk");
    static void cleanup_amsi();
};
#endif