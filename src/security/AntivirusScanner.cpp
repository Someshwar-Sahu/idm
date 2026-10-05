#include "AntivirusScanner.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <vector>
#include <filesystem>

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

LinkReputationResult AntivirusScanner::check_url_reputation(const std::string &url, const std::string &filename){
    LinkReputationResult result;
    if(url.empty()){
        result.status = SecurityScanStatus::Blocked;
        result.is_safe = false;
        result.details = "Empty URL rejected by pre-flight check";
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

#ifdef _WIN32
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
    const std::string platform_dir = "C:\\ProgramData\\Microsoft\\Windows\\Defender\\Platform";
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

    const std::string standard_path = "C:\\Program Files\\Windows\\Defender\\MpCmdRun.exe";
    if(std::filesystem::exists(standard_path, ec)){
        return standard_path;
    }

    const std::string x86_path = "C:\\Program Files (x86)\\Windows\\Defender\\MpCmdRun.exe";
    if(std::filesystem::exists(x86_path, ec)){
        return x86_path;
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
    // In AntivirusScanner::scan_file:
#ifdef _WIN32
    std::string scanner_exe = find_windows_defender_executable(custom_scanner_path);
    if(scanner_exe.empty() || !std::filesystem::exists(scanner_exe, ec)){
        result.verdict = FileScanVerdict::ScanFailed;
        result.raw_output = "Windows Defender CLI (MpCmdRun.exe) not found on system";
        result.exit_code = -1;
        return result;
    }
    std::string cmd = "\"" + scanner_exe + "\" -Scan -ScanType 3 -File \"" + filepath + "\" -DisableRemediation";
#elif defined(__APPLE__)
    // macOS Gatekeeper & XProtect assessment CLI
    std::string scanner_exe = !custom_scanner_path.empty() ? custom_scanner_path : "/usr/sbin/spctl";
    std::string cmd = scanner_exe + " --assess --type execute \"" + filepath + "\" 2>&1";
#else
    std::string cmd = "clamscan \"" + filepath + "\"";
#endif

    std::string stdout_output;
    int exit_code = 0;
    bool ran = run_command_capture_output(cmd, stdout_output, exit_code, 45000);

    auto end_time = std::chrono::steady_clock::now();
    result.scan_duration_ms = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count());
    result.raw_output = stdout_output;
    result.exit_code = exit_code;

    if(!ran){
        result.verdict = FileScanVerdict::ScanFailed;
        return result;
    }

    if(exit_code == 0){
        result.verdict = FileScanVerdict::Clean;
        result.threat_name = "";
    } else if(exit_code == 2 || exit_code == 3){ // 2 on Windows Defender, 3 on macOS spctl
        result.verdict = FileScanVerdict::Infected;
        result.threat_name = parse_threat_name(stdout_output);
        if(result.threat_name.empty()){
            result.threat_name = "Threat:DetectedByScanner";
        }
    } else {
        result.verdict = FileScanVerdict::ScanFailed;
    }
    
    return result;
}