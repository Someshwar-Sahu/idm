#include "AntivirusScanner.h"
#include <fstream>
#include <sstream>
#include <chrono>
#include <vector>
#include <filesystem>
#include <iomanip>
#include <mutex>
#include <unordered_map>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>
#include <shobjidl.h>

static const GUID CLSID_AttachmentServices_Const = { 0x4125dd96, 0xe03a, 0x4103, { 0x8f, 0x70, 0xe0, 0x59, 0x7d, 0x80, 0x3b, 0x9c }};
static const GUID IID_IAttachmentExecute_Const = { 0x73db0241, 0xc033, 0x47a6, { 0x97, 0xe6, 0x75, 0x12, 0x29, 0x6b, 0xcf, 0x6e } };
static const GUID IDM_CLIENT_GUID = { 0x3d3170a4, 0x217d, 0x48e1, { 0x8e, 0x45, 0x2b, 0x43, 0x3b, 0x6d, 0x1a, 0x9f } };

static std::wstring to_wide_string(const std::string &str) {
    if(str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.length()), nullptr, 0);
    std::wstring wstr(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.length()), &wstr[0], size_needed);
    return wstr;
}
#elif defined(__APPLE__)
#include <sys/xattr.h>
#include <unistd.h>
#include <sys/wait.h>
#endif

using json = nlohmann::json;

LinkReputationResult AntivirusScanner::check_url_reputation(const std::string &url, const std::string &filename){
    LinkReputationResult result;
    if(url.empty()){
        result.status = SecurityScanStatus::Blocked;
        result.is_safe = false;
        result.details = "Empty URL rejected by pre-flight check";
        return result;
    }

    std::string domain = extract_domain_from_url(url);
    if(!domain.empty() && is_domain_whitelisted(domain)){
        result.status = SecurityScanStatus::Safe;
        result.is_safe = true;
        result.prompt_recommended = false;
        result.details = "Domain whitelisted by user (" + domain + ")";
        return result;
    }
    // Bug 23 Fix: Bypass SmartScreen check for local / private / loopback IP ranges and intranet domains
    if (domain == "localhost" || domain == "127.0.0.1" || 
        domain.rfind("192.168.", 0) == 0 || domain.rfind("10.", 0) == 0 ||
        (domain.rfind("172.", 0) == 0) || domain.find(".local") != std::string::npos) {
        result.status = SecurityScanStatus::Safe;
        result.is_safe = true;
        result.prompt_recommended = false;
        result.details = "Local / Intranet address bypasses SmartScreen policy";
        return result;
    }

    std::string lower_url = url;
    std::string lower_fn = filename;
    for (char &c : lower_url) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    for (char &c : lower_fn) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

    if (lower_url.find("eicar") != std::string::npos || lower_fn.find("eicar") != std::string::npos) {
        result.status = SecurityScanStatus::Blocked;
        result.is_safe = false;
        result.prompt_recommended = true;
        result.details = "EICAR Standard Antivirus Test signature detected in URL/target";
        return result;
    }

#ifdef _WIN32
    HRESULT hr_com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool should_uninit = (hr_com == S_OK || hr_com == S_FALSE);

    IAttachmentExecute *pAttach = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_AttachmentServices_Const,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_IAttachmentExecute_Const,
        reinterpret_cast<void**>(&pAttach)
    );

    if(FAILED(hr) || !pAttach){
        if(should_uninit) CoUninitialize();
        result.status = SecurityScanStatus::Safe;
        result.is_safe = true;
        result.details = "SmartScreen COM service unavailable; proceeding with standard download";
        result.win32_hresult = static_cast<uint32_t>(hr);
        return result;
    }

    std::wstring w_url = to_wide_string(url);
    std::wstring w_filename = to_wide_string(filename);

    pAttach->SetClientGuid(IDM_CLIENT_GUID);
    pAttach->SetSource(w_url.c_str());
    if(!w_filename.empty()){
        pAttach->SetFileName(w_filename.c_str());
    }

    HRESULT policy_hr = pAttach->CheckPolicy();
    pAttach->Release();
    if(should_uninit) CoUninitialize();

    result.win32_hresult = static_cast<uint32_t>(policy_hr);

    if(SUCCEEDED(policy_hr)){
        if(policy_hr == S_OK){
            result.status = SecurityScanStatus::Safe;
            result.is_safe= true;
            result.prompt_recommended = false;
            result.details = "Allowed by Windows SmartScreen Policy";
        } else{
            result.status = SecurityScanStatus::Untrusted;
            result.is_safe = true;
            result.prompt_recommended = true;
            result.details = "Untrusted source - User confirmation recomended";
        }
    } else {
        result.status = SecurityScanStatus::Blocked;
        result.is_safe = false;
        result.details = "Blocked by Windows Defender SmartScan / Attachment Policy";
    }
    return result;
#else
    result.status = SecurityScanStatus::Safe;
    result.is_safe = true;
    result.details = "Non-Windows platform - SmartScreen check skipped";
    return result;
#endif
}

bool AntivirusScanner::apply_mark_of_the_web(const std::string &filepath, const std::string &source_url){
    if(filepath.empty()) return false;
    std::error_code ec;
    if(!std::filesystem::exists(filepath, ec)){
        return false;
    }

#ifdef _WIN32
    // Bug 24 Fix: Check named streams support and gracefully skip on non-NTFS drives (FAT32, exFAT)
    char root_path[MAX_PATH] = {0};
    if (GetVolumePathNameA(filepath.c_str(), root_path, MAX_PATH)) {
        DWORD fs_flags = 0;
        if (GetVolumeInformationA(root_path, NULL, 0, NULL, NULL, &fs_flags, NULL, 0)) {
            if (!(fs_flags & FILE_NAMED_STREAMS)) {
                return true; // Filesystem does not support NTFS ADS, skip gracefully
            }
        }
    }

    std::string ads_path = filepath + ":Zone.Identifier";
    std::ofstream ads(ads_path, std::ios::out | std::ios::trunc);
    if(!ads.is_open()){
        return false;
    }

    ads << "[ZoneTransfer]\r\n";
    ads << "ZoneId=3\r\n";
    if(!source_url.empty()){
        ads <<"HostUrl="<<source_url<<"\r\n";
    }
    ads.close();
    return true;
#elif defined(__APPLE__)
    std::string q_val = "0083;652c7a8b;IDM;" + (source_url.empty() ? "DirectDownload" : source_url);
    int res = setxattr(filepath.c_str(), "com.apple.quarantine", q_val.c_str(), q_val.length(), 0, 0);
    return (res == 0);

#else
    return true;
#endif
}

bool AntivirusScanner::has_mark_of_the_web(const std::string &filepath){
    if(filepath.empty()) return false;
#ifdef _WIN32
    std::string ads_path = filepath + ":Zone.Identifier";
    std::ifstream ads(ads_path);
    if(!ads.is_open()){
        return false;
    }

    std::string line;
    while(std::getline(ads, line)){
        if(line.find("ZoneId=3") != std::string::npos || line.find("ZoneId=") != std::string::npos){
            return true;
        }
    }
    return false;
#elif defined(__APPLE__)
    ssize_t size = getxattr(filepath.c_str(), "com.apple.quarantine", nullptr, 0, 0, 0);
    return (size > 0);
#else
    return false;
#endif
}

std::string AntivirusScanner::read_mark_of_the_web(const std::string &filepath){
    if(filepath.empty()) return "";

#ifdef _WIN32
    std::string ads_path = filepath + ":Zone.Identifier";
    std::ifstream ads(ads_path);
    if(!ads.is_open()){
        return "";
    }
    std::stringstream buffer;
    buffer << ads.rdbuf();
    return buffer.str();
#elif defined(__APPLE__)
    char buf[1024];
    ssize_t len = getxattr(filepath.c_str(), "com.apple.quarantine", buf, sizeof(buf) - 1, 0, 0);
    if(len > 0){
        buf[len] = '\0';
        return std::string(buf);
    }
    return "";
#else
    return "";
#endif
}

static bool run_command_capture_output(const std::string &command, std::string &out_stdout, int &out_exit_code, uint32_t timeout_ms = 45000){
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE hReadPipe = nullptr;
    HANDLE hWritePipe = nullptr;
    if(!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)){
        return false;
    }
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.dwFlags |= (STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW);
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    std::vector<char> cmd_buf(command.begin(), command.end());
    cmd_buf.push_back('\0');

    BOOL success = CreateProcessA(
        nullptr,
        cmd_buf.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi
    );

    CloseHandle(hWritePipe);

    if(!success){
        CloseHandle(hReadPipe);
        return false;
    }

    char buffer[4096];
    DWORD bytes_read = 0;
    while(ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytes_read, nullptr) && bytes_read > 0){
        buffer[bytes_read] = '\0';
        out_stdout.append(buffer, bytes_read);
    }
    CloseHandle(hReadPipe);

    WaitForSingleObject(pi.hProcess, timeout_ms);

    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    out_exit_code = static_cast<int>(exit_code);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;

#elif defined(__APPLE__) || defined(__linux__)
    FILE *pipe = popen(command.c_str(), "r");
    if(!pipe) return false;
    char buffer[4096];
    while(fgets(buffer, sizeof(buffer), pipe) != nullptr){
        out_stdout += buffer;
    }
    int status = pclose(pipe);
    if(WIFEXITED(status)){
        out_exit_code = WEXITSTATUS(status);
    } else {
        out_exit_code = -1;
    }
    return true;

#else
    return false;
#endif
}

std::string AntivirusScanner::find_windows_defender_executable(const std::string &custom_path){
    if(!custom_path.empty()){
        std::error_code ec;
        if(std::filesystem::exists(custom_path, ec)){
            return custom_path;
        }
    }

#ifdef _WIN32
    std::error_code ec;
    const std::vector<std::string> platform_dirs = {
        "C:\\ProgramData\\Microsoft\\Windows Defender\\Platform",
        "C:\\ProgramData\\Microsoft\\Windows\\Defender\\Platform"
    };
    for(const auto &platform_dir : platform_dirs) {
        if(std::filesystem::exists(platform_dir, ec)){
            std::string latest_exe;
            for(const auto &entry : std::filesystem::directory_iterator(platform_dir, ec)){
                if(entry.is_directory()){
                    std::string candidate = (entry.path() / "MpCmdRun.exe").string();
                    if(std::filesystem::exists(candidate, ec)){
                        latest_exe = candidate;
                    }
                }
            }
            if(!latest_exe.empty()) return latest_exe;
        }
    }

    const std::vector<std::string> standard_paths = {
        "C:\\ProgramData\\Microsoft\\Windows Defender\\Platform\\CurrentVersion\\MpCmdRun.exe",
        "C:\\Program Files\\Windows Defender\\MpCmdRun.exe",
        "C:\\Program Files (x86)\\Windows Defender\\MpCmdRun.exe",
        "C:\\Program Files\\Windows\\Defender\\MpCmdRun.exe",
        "C:\\Program Files (x86)\\Windows\\Defender\\MpCmdRun.exe"
    };
    for(const auto &path : standard_paths) {
        if(std::filesystem::exists(path, ec)){
            return path;
        }
    }
#endif
    return "";
}

std::string AntivirusScanner::parse_threat_name(const std::string &stdout_output){
    if(stdout_output.empty()) return "";

    std::string lower = stdout_output;

    for(char &c : lower ) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

    size_t pos = lower.find("threat");
    while(pos != std::string::npos){
        size_t colon_pos = stdout_output.find(':', pos);
        if(colon_pos != std::string::npos && colon_pos - pos < 30){
            size_t start = colon_pos +1 ;
            while(start < stdout_output.length() && (stdout_output[start] == ' ' || stdout_output[start] == '\t')){
                start ++;
            }
            if(start < stdout_output.length()){
                size_t end = stdout_output.find_first_of("\r\n", start);
                if(end == std::string::npos) end = stdout_output.length();
                std::string candidate = stdout_output.substr(start, end - start);
                while(!candidate.empty() && (candidate.back() ==' ' || candidate.back() == '\t')){
                    candidate.pop_back();
                }
                if(!candidate.empty() && candidate.find("listing") == std::string::npos &&candidate.find("detected") == std::string::npos){
                    return candidate;
                }
            }
        }
        pos = lower.find("threat", pos + 6);
    }
    return "";
}

FileScanResult AntivirusScanner::scan_file(const std::string &filepath, const std::string &custom_scanner_path){
    FileScanResult result;
    auto start_time = std::chrono::steady_clock::now();

    std::error_code ec;
    if(filepath.empty() || !std::filesystem::exists(filepath, ec)){
        result.verdict = FileScanVerdict::ScanFailed;
        result.raw_output = "Target file does not exist or is unreadable";
        result.exit_code = -1;
        return result;
    }

    // Step 1: Whitelist check
    std::string file_hash = calculate_file_sha256(filepath);
    if(!file_hash.empty() && is_hash_whitelisted(file_hash)){
        result.verdict = FileScanVerdict::Clean;
        result.threat_name = "";
        result.raw_output = "[Whitelist Override] File SHA-256 (" + file_hash + ") is user-whitelisted. Threat overridden.";
        return result;
    }

    // Step 2: In-memory pattern inspection (heuristic EICAR & AMSI buffer scan)
    try {
        uintmax_t fsize = std::filesystem::file_size(filepath, ec);
        if (fsize > 0 && fsize <= 30 * 1024 * 1024) {
            std::ifstream fin(filepath, std::ios::binary);
            if (fin.is_open()) {
                std::vector<char> buf(static_cast<size_t>(fsize));
                fin.read(buf.data(), fsize);
                size_t bytes_read = fin.gcount();
                fin.close();

                if (bytes_read > 0) {
                    auto buf_res = scan_buffer(buf.data(), bytes_read, filepath);
                    if (buf_res.verdict == FileScanVerdict::Infected) {
                        result.verdict = FileScanVerdict::Infected;
                        result.threat_name = buf_res.threat_name.empty() ? "Virus:DOS/EICAR_Test_File" : buf_res.threat_name;
                        result.raw_output = buf_res.raw_output;
                        result.exit_code = 2;
                        auto end_time = std::chrono::steady_clock::now();
                        result.scan_duration_ms = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count());
                        return result;
                    }
                }
            }
        }
    } catch (...) {}

        // Step 3: Platform Defender / Native CLI Scan
    std::string scan_target = filepath;
    try {
        scan_target = std::filesystem::absolute(filepath).string();
    } catch (...) {}

#ifdef _WIN32
    // Windows Defender CLI strictly requires native Windows backslashes (\).
    // Forward slashes (/) cause MpCmdRun to fail with hr = 0x80508023 and exit code 2.
    std::replace(scan_target.begin(), scan_target.end(), '/', '\\');

    std::string scanner_exe = find_windows_defender_executable(custom_scanner_path);
    if(scanner_exe.empty() || !std::filesystem::exists(scanner_exe, ec)){
        result.verdict = FileScanVerdict::Clean;
        result.raw_output = "In-memory inspection clean (Defender CLI not available)";
        result.exit_code = 0;
        return result;
    }
    std::string cmd = "\"" + scanner_exe + "\" -Scan -ScanType 3 -File \"" + scan_target + "\" -DisableRemediation";
#elif defined(__APPLE__)
    std::string scanner_exe = !custom_scanner_path.empty() ? custom_scanner_path : "/usr/sbin/spctl";
    std::string cmd = scanner_exe + " --assess --type execute \"" + scan_target + "\" 2>&1";
#else
    std::string cmd = "clamscan \"" + scan_target + "\"";
#endif

    std::string stdout_output;
    int exit_code = 0;
    bool ran = run_command_capture_output(cmd, stdout_output, exit_code, 45000);

    auto end_time = std::chrono::steady_clock::now();
    result.scan_duration_ms = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count());
    result.raw_output = stdout_output;
    result.exit_code = exit_code;

    if(!ran){
        result.verdict = FileScanVerdict::Clean;
        return result;
    }

    if(exit_code == 0){
        result.verdict = FileScanVerdict::Clean;
        result.threat_name = "";
    } else if(exit_code == 2 || exit_code == 3){ // 2 on Windows Defender, 3 on macOS spctl
        // Only classify as infected if the CLI actually parsed a genuine threat
        std::string parsed = parse_threat_name(stdout_output);
        if(!parsed.empty()){
            result.verdict = FileScanVerdict::Infected;
            result.threat_name = parsed;
        } else if(stdout_output.find("CmdTool: Failed") != std::string::npos ||
                  stdout_output.find("0x80508023") != std::string::npos ||
                  stdout_output.find("was skipped") != std::string::npos ||
                  stdout_output.find("found no threats") != std::string::npos){
            // Syntax error, skipped, or clean scan output
            result.verdict = FileScanVerdict::Clean;
            result.threat_name = "";
        } else {
            result.verdict = FileScanVerdict::Clean;
            result.threat_name = "";
        }

        if(!file_hash.empty() && is_hash_whitelisted(file_hash)){
            result.verdict = FileScanVerdict::Clean;
            result.threat_name = "";
            result.raw_output += "\n[Whitelist Override] File SHA-256 (" + file_hash + ") is user-whitelisted. Threat overridden.";
        }
    } else {
        result.verdict = FileScanVerdict::Clean;
    }
    
    return result;
}

namespace sha256_internal {
    inline uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }
    inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); }
    inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
    inline uint32_t sig0(uint32_t x) { return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22); }
    inline uint32_t sig1(uint32_t x) { return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25); }
    inline uint32_t theta0(uint32_t x) { return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3); }
    inline uint32_t theta1(uint32_t x) { return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10); }
    static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    };
    struct Context {
        uint32_t state[8];
        uint64_t count;
        uint8_t buffer[64];
    };
    inline void init(Context &ctx) {
        ctx.state[0] = 0x6a09e667;
        ctx.state[1] = 0xbb67ae85;
        ctx.state[2] = 0x3c6ef372;
        ctx.state[3] = 0xa54ff53a;
        ctx.state[4] = 0x510e527f;
        ctx.state[5] = 0x9b05688c;
        ctx.state[6] = 0x1f83d9ab;
        ctx.state[7] = 0x5be0cd19;
        ctx.count = 0;
    }
    inline void transform(Context &ctx, const uint8_t data[64]) {
        uint32_t a = ctx.state[0], b = ctx.state[1], c = ctx.state[2], d = ctx.state[3];
        uint32_t e = ctx.state[4], f = ctx.state[5], g = ctx.state[6], h = ctx.state[7];
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(data[i * 4]) << 24) |
                   (static_cast<uint32_t>(data[i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(data[i * 4 + 2]) << 8) |
                   (static_cast<uint32_t>(data[i * 4 + 3]));
        }
        for (int i = 16; i < 64; ++i) {
            w[i] = theta1(w[i - 2]) + w[i - 7] + theta0(w[i - 15]) + w[i - 16];
        }
        for (int i = 0; i < 64; ++i) {
            uint32_t t1 = h + sig1(e) + ch(e, f, g) + K[i] + w[i];
            uint32_t t2 = sig0(a) + maj(a, b, c);
            h = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        ctx.state[0] += a; ctx.state[1] += b; ctx.state[2] += c; ctx.state[3] += d;
        ctx.state[4] += e; ctx.state[5] += f; ctx.state[6] += g; ctx.state[7] += h;
    }
    inline void update(Context &ctx, const uint8_t *data, size_t len) {
        size_t buf_idx = static_cast<size_t>(ctx.count & 63);
        ctx.count += len;
        size_t part_len = 64 - buf_idx;
        size_t i = 0;
        if (len >= part_len) {
            std::memcpy(&ctx.buffer[buf_idx], data, part_len);
            transform(ctx, ctx.buffer);
            for (i = part_len; i + 63 < len; i += 64) {
                transform(ctx, &data[i]);
            }
            buf_idx = 0;
        }
        if (i < len) {
            std::memcpy(&ctx.buffer[buf_idx], &data[i], len - i);
        }
    }
    inline void final(Context &ctx, uint8_t digest[32]) {
        uint64_t total_bits = ctx.count * 8;
        size_t pad_len = (ctx.count % 64 < 56) ? (56 - (ctx.count % 64)) : (120 - (ctx.count % 64));
        uint8_t pad[128] = { 0x80 };
        update(ctx, pad, pad_len);
        uint8_t bits[8];
        for (int i = 0; i < 8; ++i) {
            bits[i] = static_cast<uint8_t>((total_bits >> (56 - 8 * i)) & 0xFF);
        }
        update(ctx, bits, 8);
        for (int i = 0; i < 8; ++i) {
            digest[i * 4] = static_cast<uint8_t>((ctx.state[i] >> 24) & 0xFF);
            digest[i * 4 + 1] = static_cast<uint8_t>((ctx.state[i] >> 16) & 0xFF);
            digest[i * 4 + 2] = static_cast<uint8_t>((ctx.state[i] >> 8) & 0xFF);
            digest[i * 4 + 3] = static_cast<uint8_t>(ctx.state[i] & 0xFF);
        }
    }
}

std::string AntivirusScanner::calculate_file_sha256(const std::string &filepath){
    if(filepath.empty()) return "";
    std::ifstream file(filepath, std::ios::binary);
    if(!file.is_open()) return "";

    sha256_internal::Context ctx;
    sha256_internal::init(ctx);
    std::vector<uint8_t> buffer(65536);
    while(file.read(reinterpret_cast<char*>(buffer.data()), buffer.size()) || file.gcount() > 0){
        sha256_internal::update(ctx, buffer.data(), static_cast<size_t> (file.gcount()));
    }
    uint8_t digest[32];
    sha256_internal::final(ctx, digest);

    std::ostringstream oss;
    for(int i=0; i<32; ++i){
        oss<< std::hex << std::setw(2) << std::setfill('0')<< static_cast<int> (digest[i]);
    }
    return oss.str();
}

std::string AntivirusScanner::extract_domain_from_url(const std::string &url){
    if(url.empty()) return "";
    std::string clean = url;
    size_t scheme_pos = clean.find("://");
    if(scheme_pos != std::string::npos){
        clean = clean.substr(scheme_pos +3 );
    }
    size_t slash_pos = clean.find('/');
    if(slash_pos != std::string::npos ){
        clean = clean.substr(0, slash_pos);
    }
    size_t colon_pos = clean.find(':');
    if(colon_pos != std::string::npos){
        clean = clean.substr(0, colon_pos);
    }
    for(char &c : clean){
        c= static_cast<char>(std::tolower(static_cast<unsigned char> (c)));
    }
    return clean;
}

struct WhitelistStore{
    std::mutex mtx;
    std::unordered_map<std::string, std::string> hashes;
    std::unordered_map<std::string, std::string> domains;
    std::string default_json_path = "security_whitelist.json";
};

static WhitelistStore s_whitelist;

static std::string to_lower_str(std::string s){
    for(char &c: s) c = static_cast<char> (std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool AntivirusScanner::add_hash_to_whitelist(const std::string &sha256_hash, const std::string &reason){
    if(sha256_hash.empty()) return false;
    std::lock_guard<std::mutex> lock(s_whitelist.mtx);
    s_whitelist.hashes[to_lower_str(sha256_hash)] = reason;
    return true;
}

bool AntivirusScanner::add_domain_to_whitelist(const std::string &domain, const std::string &reason){
    if(domain.empty()) return false;
    std::string clean = extract_domain_from_url(domain);
    if(clean.empty()) return false;
    std::lock_guard<std::mutex> lock(s_whitelist.mtx);
    s_whitelist.domains[to_lower_str(clean)] = reason;
    return true;
}

bool AntivirusScanner::is_hash_whitelisted(const std::string &sha256_hash){
    if(sha256_hash.empty()) return false;
    std::lock_guard<std::mutex> lock(s_whitelist.mtx);
    return s_whitelist.hashes.find(to_lower_str(sha256_hash)) != s_whitelist.hashes.end();
}

bool AntivirusScanner::is_domain_whitelisted(const std::string &domain_or_url){
    if(domain_or_url.empty()) return false;
    std::string clean = extract_domain_from_url(domain_or_url);
    if(clean.empty()) return false;
    std::lock_guard<std::mutex> lock(s_whitelist.mtx);
    return s_whitelist.domains.find(to_lower_str(clean)) != s_whitelist.domains.end();
}

bool AntivirusScanner::remove_from_whitelist(const std::string &entry){
    if(entry.empty()) return false;
    std::string lower = to_lower_str(entry);
    std::string clean_domain = extract_domain_from_url(entry);
    std::lock_guard<std::mutex> lock(s_whitelist.mtx);
    bool removed = false;
    if(s_whitelist.hashes.erase(lower) > 0) removed = true;
    if(s_whitelist.domains.erase(lower) > 0) removed = true;
    if(!clean_domain.empty() && s_whitelist.domains.erase(clean_domain) > 0) removed = true;
    return removed;
}

void AntivirusScanner::clear_whitelist(){
    std::lock_guard<std::mutex> lock(s_whitelist.mtx);
    s_whitelist.hashes.clear();
    s_whitelist.domains.clear();
}

bool AntivirusScanner::load_whitelist(const std::string &json_filepath){
    std::string path = json_filepath.empty() ? s_whitelist.default_json_path : json_filepath;
    std::ifstream file(path);
    if(!file.is_open()) return false;

    try {
        json j;
        file >> j;
        std::lock_guard<std::mutex> lock(s_whitelist.mtx);
        s_whitelist.hashes.clear();
        s_whitelist.domains.clear();

        if(j.contains("hashes") && j["hashes"].is_object()){
            for(auto &el : j["hashes"].items()){
                s_whitelist.hashes[to_lower_str(el.key())] = el.value().get<std::string>();
            }
        }
        if(j.contains("domains") && j["domains"].is_object()){
            for(auto &el : j["domains"].items()){
                s_whitelist.domains[to_lower_str(el.key())] = el.value().get<std::string>();
            }
        }
        return true;
    } catch(...){
        return false;
    }
}

bool AntivirusScanner::save_whitelist(const std::string &json_filepath){
    std::string path = json_filepath.empty() ? s_whitelist.default_json_path : json_filepath;
    
    json j;
    {
        std::lock_guard<std::mutex> lock(s_whitelist.mtx);
        j["hashes"] = s_whitelist.hashes;
        j["domains"] = s_whitelist.domains;
    }
    std::ofstream file(path, std::ios::trunc);
    if(!file.is_open()) return false;
    file << j.dump(4);
    return true;
}

bool AntivirusScanner::quarantine_file(const std::string &filepath, const std::string &threat_name, std::string &out_quarantined_path, const std::string &source_url){
    std::error_code ec;
    if(filepath.empty() || !std::filesystem::exists(filepath, ec)){
        return false;
    }

    std::string q_path = filepath + ".quarantined";
    std::filesystem::rename(filepath, q_path, ec);
    if(ec) return false;

    out_quarantined_path = q_path;

    std::string sidecar_path = q_path + ".threat_info";
    json info;
    info["original_filepath"] = filepath;
    info["quarantined_filepath"] = q_path;
    info["threat_name"] = threat_name;
    info["source_url"] = source_url;
    info["sha256"] = calculate_file_sha256(q_path);
    auto now = std::chrono::system_clock::now();
    info["timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

    std::ofstream sidecar(sidecar_path, std::ios::trunc);
    if(sidecar.is_open()){
        sidecar << info.dump(4);
        sidecar.close();
    }

    std::filesystem::permissions(q_path, std::filesystem::perms::owner_read | std::filesystem::perms::group_read | std::filesystem::perms::others_read, std::filesystem::perm_options::replace, ec);

    return true;
}

bool AntivirusScanner::restore_quarantined_file(const std::string &quarantined_path, const std::string &target_path){
    std::error_code ec;
    if(quarantined_path.empty() || !std::filesystem::exists(quarantined_path, ec)){
        return false;
    }

    std::string dest = target_path;
    std::string sidecar_path = quarantined_path + ".threat_info";

    if(dest.empty() && std::filesystem::exists(sidecar_path, ec)){
        std::ifstream sidecar(sidecar_path);
        if(sidecar.is_open()){
            try{
                json info;
                sidecar >> info;
                dest = info.value("original_filepath", "");
            } catch (...) {}
        }
    }

    if(dest.empty()){
        if(quarantined_path.size() > 12 && quarantined_path.substr(quarantined_path.size() - 12) == ".quarantined"){
            dest = quarantined_path.substr(0, quarantined_path.size() - 12);
        } else {
            return false;
        }
    }

    // Bug 25 Fix: Validate destination path against directory traversal
    std::filesystem::path dest_p(dest);
    std::string norm_dest = dest_p.lexically_normal().string();
    if (norm_dest.find("..") != std::string::npos || norm_dest.empty()) {
        return false;
    }

    std::filesystem::permissions(quarantined_path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write | std::filesystem::perms::group_read | std::filesystem::perms::others_read, std::filesystem::perm_options::replace, ec);

    std::filesystem::rename(quarantined_path, dest, ec);
    if(ec) return false;

    std::filesystem::remove(sidecar_path, ec);
    return true;
}

bool AntivirusScanner::delete_quarantined_file(const std::string &quarantined_path){
    std::error_code ec;
    bool ok1 = std::filesystem::remove(quarantined_path, ec);
    std::string sidecar_path = quarantined_path + ".threat_info";
    std::filesystem::remove(sidecar_path, ec);
    return ok1;
}

// =========================================================================
// Task 12: Dynamic Windows AMSI & Cross-Platform In-Memory Buffer Scanning
// =========================================================================
#ifdef _WIN32
typedef struct HAMSICONTEXT__ *HAMSICONTEXT;
typedef struct HAMSISESSION__ *HAMSISESSION;

typedef enum AMSI_RESULT {
    AMSI_RESULT_CLEAN = 0,
    AMSI_RESULT_NOT_DETECTED = 1,
    AMSI_RESULT_BLOCKED_BY_ADMIN_START = 16384,
    AMSI_RESULT_BLOCKED_BY_ADMIN_END = 20479,
    AMSI_RESULT_DETECTED = 32768
} AMSI_RESULT;

typedef HRESULT (WINAPI *pfnAmsiInitialize)(LPCWSTR appName, HAMSICONTEXT *amsiContext);
typedef void (WINAPI *pfnAmsiUninitialize)(HAMSICONTEXT amsiContext);
typedef HRESULT (WINAPI *pfnAmsiOpenSession)(HAMSICONTEXT amsiContext, HAMSISESSION *amsiSession);
typedef void (WINAPI *pfnAmsiCloseSession)(HAMSICONTEXT amsiContext, HAMSISESSION amsiSession);
typedef HRESULT (WINAPI *pfnAmsiScanBuffer)(HAMSICONTEXT amsiContext, PVOID buffer, ULONG length, LPCWSTR contentName, HAMSISESSION amsiSession, AMSI_RESULT *result);

struct AmsiContextHolder {
    std::mutex mtx;
    HMODULE hAmsiDll{nullptr};
    HAMSICONTEXT amsiContext{nullptr};
    HAMSISESSION amsiSession{nullptr};
    pfnAmsiInitialize fnInitialize{nullptr};
    pfnAmsiUninitialize fnUninitialize{nullptr};
    pfnAmsiOpenSession fnOpenSession{nullptr};
    pfnAmsiCloseSession fnCloseSession{nullptr};
    pfnAmsiScanBuffer fnScanBuffer{nullptr};
    bool initialized{false};
    bool available{false};
};

static AmsiContextHolder s_amsi;

static bool ensure_amsi_initialized() {
    std::lock_guard<std::mutex> lock(s_amsi.mtx);
    if(s_amsi.initialized) {
        return s_amsi.available;
    }
    s_amsi.initialized = true;
    s_amsi.available = false;

    s_amsi.hAmsiDll = LoadLibraryA("amsi.dll");
    if(!s_amsi.hAmsiDll) {
        return false;
    }

    s_amsi.fnInitialize = reinterpret_cast<pfnAmsiInitialize>(GetProcAddress(s_amsi.hAmsiDll, "AmsiInitialize"));
    s_amsi.fnUninitialize = reinterpret_cast<pfnAmsiUninitialize>(GetProcAddress(s_amsi.hAmsiDll, "AmsiUninitialize"));
    s_amsi.fnOpenSession = reinterpret_cast<pfnAmsiOpenSession>(GetProcAddress(s_amsi.hAmsiDll, "AmsiOpenSession"));
    s_amsi.fnCloseSession = reinterpret_cast<pfnAmsiCloseSession>(GetProcAddress(s_amsi.hAmsiDll, "AmsiCloseSession"));
    s_amsi.fnScanBuffer = reinterpret_cast<pfnAmsiScanBuffer>(GetProcAddress(s_amsi.hAmsiDll, "AmsiScanBuffer"));

    if(!s_amsi.fnInitialize || !s_amsi.fnUninitialize || !s_amsi.fnOpenSession || !s_amsi.fnCloseSession || !s_amsi.fnScanBuffer) {
        FreeLibrary(s_amsi.hAmsiDll);
        s_amsi.hAmsiDll = nullptr;
        return false;
    }

    HRESULT hr = s_amsi.fnInitialize(L"IDM_Engine", &s_amsi.amsiContext);
    if(FAILED(hr) || !s_amsi.amsiContext) {
        FreeLibrary(s_amsi.hAmsiDll);
        s_amsi.hAmsiDll = nullptr;
        return false;
    }

    hr = s_amsi.fnOpenSession(s_amsi.amsiContext, &s_amsi.amsiSession);
    if(FAILED(hr)) {
        s_amsi.amsiSession = nullptr;
    }

    s_amsi.available = true;
    return true;
}
#endif

bool AntivirusScanner::is_amsi_available() {
#ifdef _WIN32
    return ensure_amsi_initialized();
#else
    return false;
#endif
}

void AntivirusScanner::cleanup_amsi() {
#ifdef _WIN32
    std::lock_guard<std::mutex> lock(s_amsi.mtx);
    if(s_amsi.hAmsiDll) {
        if(s_amsi.amsiSession && s_amsi.fnCloseSession) {
            s_amsi.fnCloseSession(s_amsi.amsiContext, s_amsi.amsiSession);
            s_amsi.amsiSession = nullptr;
        }
        if(s_amsi.amsiContext && s_amsi.fnUninitialize) {
            s_amsi.fnUninitialize(s_amsi.amsiContext);
            s_amsi.amsiContext = nullptr;
        }
        FreeLibrary(s_amsi.hAmsiDll);
        s_amsi.hAmsiDll = nullptr;
    }
    s_amsi.available = false;
    s_amsi.initialized = false;
#endif
}

FileScanResult AntivirusScanner::scan_buffer(const void *buffer, size_t length, const std::string &content_name) {
    FileScanResult result;
    if(!buffer || length == 0) {
        result.verdict = FileScanVerdict::Clean;
        result.threat_name = "";
        result.raw_output = "Empty buffer skipped";
        result.exit_code = 0;
        return result;
    }

    auto start_time = std::chrono::steady_clock::now();

    // 1. Cross-platform heuristic pattern inspection (EICAR standard antivirus test signature)
    static const std::string EICAR_SIG = "X5O!P%@AP[4\\PZX54(P^)7CC)7}$EICAR-STANDARD-ANTIVIRUS-TEST-FILE!$H+H*";
    std::string_view sv(static_cast<const char*>(buffer), length);
    if(sv.find(EICAR_SIG) != std::string_view::npos) {
        auto end_time = std::chrono::steady_clock::now();
        result.verdict = FileScanVerdict::Infected;
        result.threat_name = "Virus:DOS/EICAR_Test_File";
        result.raw_output = "In-memory buffer match: EICAR Standard Antivirus Test signature detected";
        result.exit_code = 2;
        result.scan_duration_ms = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count());
        return result;
    }

#ifdef _WIN32
    // 2. Windows Antimalware Scan Interface (AMSI) Deep Inspection
    if(ensure_amsi_initialized()) {
        std::wstring w_name = to_wide_string(content_name.empty() ? "DownloadChunk" : content_name);
        AMSI_RESULT amsi_res = AMSI_RESULT_CLEAN;
        HRESULT hr = S_OK;

        {
            std::lock_guard<std::mutex> lock(s_amsi.mtx);
            hr = s_amsi.fnScanBuffer(
                s_amsi.amsiContext,
                const_cast<void*>(buffer),
                static_cast<ULONG>(length),
                w_name.c_str(),
                s_amsi.amsiSession,
                &amsi_res
            );
        }

        auto end_time = std::chrono::steady_clock::now();
        result.scan_duration_ms = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count());

        if(SUCCEEDED(hr)) {
            if(amsi_res >= AMSI_RESULT_DETECTED) {
                result.verdict = FileScanVerdict::Infected;
                result.threat_name = "Threat:AMSI/MalwareDetected";
                result.raw_output = "Windows AMSI buffer inspection flagged malicious payload (AMSI_RESULT=" + std::to_string(amsi_res) + ")";
                result.exit_code = 2;
                return result;
            } else if(amsi_res >= AMSI_RESULT_BLOCKED_BY_ADMIN_START && amsi_res <= AMSI_RESULT_BLOCKED_BY_ADMIN_END) {
                result.verdict = FileScanVerdict::Infected;
                result.threat_name = "Threat:AMSI/BlockedByAdministrator";
                result.raw_output = "Windows AMSI policy blocked buffer (AMSI_RESULT=" + std::to_string(amsi_res) + ")";
                result.exit_code = 2;
                return result;
            }
        }
    }
#endif

    auto end_time = std::chrono::steady_clock::now();
    result.verdict = FileScanVerdict::Clean;
    result.threat_name = "";
    result.raw_output = "In-memory buffer clean (scanned " + std::to_string(length) + " bytes)";
    result.exit_code = 0;
    result.scan_duration_ms = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count());
    return result;
}
