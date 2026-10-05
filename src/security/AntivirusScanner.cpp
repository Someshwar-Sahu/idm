#include "AntivirusScanner.h"
#include <iostream>
#include <fstream>
#include <sstream>

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
    buffer <<ads.rdbuf();
    return buffer.str();
#else
    return "";
#endif
}