#ifndef ANTIVIRUS_SCANNER_H
#define ANTIVIRUS_SCANNER_H

#include <string>
#include <cstdint>

enum class SecurityScanStatus{
    Safe,
    Untrusted,
    Blocked,
    Error,
};

struct LinkReputationResult {
    SecurityScanStatus status{SecurityScanStatus::Safe};
    bool is_safe{true};
    bool prompt_recommended{false};
    std::string details;
    uint32_t win32_hresult{0};
};

class AntivirusScanner{
public:
    AntivirusScanner() = default;
    ~AntivirusScanner() = default;

    static LinkReputationResult check_url_reputation(const std::string &url, const std::string &filename = "");

    static bool apply_mark_of_the_web(const std::string &filepath, const std::string &source_url = "");
    static bool has_mark_of_the_web(const std::string &filepath);
    static std::string read_mark_of_the_web(const std::string &filepath);
};
#endif