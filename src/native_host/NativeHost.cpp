#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>
#include <fcntl.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

#include "NativeHost.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <sstream>

using namespace std;
using json = nlohmann::json;

namespace idm {

bool NativeHost::json_to_payload(const string &json_str, InterceptedDownloadPayload &out_payload){
    try{
        json j = json::parse(json_str);
        out_payload.url = j.value("url", "");
        out_payload.filename = j.value("filename", "");
        out_payload.cookies = j.value("cookies", "");
        out_payload.user_agent = j.value("user_agent", "");
        out_payload.referer = j.value("referer", "");
        out_payload.page_title = j.value("page_title", "");
        out_payload.total_size = j.value("total_size", static_cast<uint64_t>(0));
        out_payload.user_force_override = j.value("user_force_override", false);
        return !out_payload.url.empty();
    }catch (const exception &e){
        cerr<<"[NativeHost Error] JSON parse failure: "<<e.what()<<endl;
        return false;
    }
}

string NativeHost::payload_to_json(const InterceptedDownloadPayload &payload) {
    json j;
    j["url"] = payload.url;
    j["filename"] = payload.filename;
    j["cookies"] = payload.cookies;
    j["user_agent"] = payload.user_agent;
    j["referer"] = payload.referer;
    j["page_title"] = payload.page_title;
    j["total_size"] = payload.total_size;
    j["user_force_override"] = payload.user_force_override;
    return j.dump();
}

bool NativeHost::read_message_from_stdin(InterceptedDownloadPayload &out_payload){
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
#endif

    uint32_t message_length = 0;
    cin.read(reinterpret_cast<char*>(&message_length), 4);

    if(!cin || cin.gcount() < 4){
        return false;
    }

    if(message_length == 0 || message_length > 10 * 1024 * 1024){
        cerr<<"[NativeHost Error] Invalid message length header: "<<message_length<<endl;
        return false;
    }

    string json_buffer(message_length, '\0');
    cin.read(&json_buffer[0], message_length);

    if(!cin || static_cast<uint32_t>(cin.gcount()) < message_length){
        return false;
    }

    return json_to_payload(json_buffer, out_payload);
}

bool NativeHost::write_response_to_stdout(bool success, const string &message){
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    json j;
    j["success"] = success;
    j["message"] = message;
    string json_str = j.dump();

    uint32_t message_length = static_cast<uint32_t>(json_str.size());
    cout.write(reinterpret_cast<const char*>(&message_length), 4);
    cout.write(json_str.data(), message_length);
    cout.flush();

    return cout.good();
}

string NativeHost::generate_native_manifest(const string& executable_path,
                                           const vector<string>& allowed_origins,
                                           BrowserType browser) {
    json j;
    j["name"] = "com.idm.native_host";
    j["description"] = "IDM Native Messaging Bridge Host";
    j["path"] = executable_path;
    j["type"] = "stdio";

    if (browser == BrowserType::Firefox) {
        j["allowed_extensions"] = allowed_origins;
    } else {
        j["allowed_origins"] = allowed_origins;
    }

    return j.dump(2);
}

string NativeHost::get_registry_key_path(BrowserType browser) {
    switch (browser) {
        case BrowserType::Chrome:
            return "Software\\Google\\Chrome\\NativeMessagingHosts\\com.idm.native_host";
        case BrowserType::Edge:
            return "Software\\Microsoft\\Edge\\NativeMessagingHosts\\com.idm.native_host";
        case BrowserType::Firefox:
            return "Software\\Mozilla\\NativeMessagingHosts\\com.idm.native_host";
    }
    return "";
}

bool NativeHost::install_native_host_registry(BrowserType browser, const string& manifest_filepath) {
#ifdef _WIN32
    string subkey = get_registry_key_path(browser);
    if (subkey.empty() || manifest_filepath.empty()) return false;

    HKEY hKey;
    LSTATUS status = RegCreateKeyExA(
        HKEY_CURRENT_USER,
        subkey.c_str(),
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        KEY_SET_VALUE,
        nullptr,
        &hKey,
        nullptr
    );

    if (status != ERROR_SUCCESS) return false;

    status = RegSetValueExA(
        hKey,
        nullptr, // Default value
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(manifest_filepath.c_str()),
        static_cast<DWORD>(manifest_filepath.length() + 1)
    );

    RegCloseKey(hKey);
    return (status == ERROR_SUCCESS);
#else
    return false;
#endif
}

bool NativeHost::uninstall_native_host_registry(BrowserType browser) {
#ifdef _WIN32
    string subkey = get_registry_key_path(browser);
    if (subkey.empty()) return false;

    LSTATUS status = RegDeleteKeyA(HKEY_CURRENT_USER, subkey.c_str());
    return (status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND);
#else
    return false;
#endif
}

bool NativeHost::forward_payload_to_server(const InterceptedDownloadPayload& payload,
                                          const string& host,
                                          int port,
                                          string* out_response) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }
#else
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;
#endif

    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr);

    if (connect(sock, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) != 0) {
#ifdef _WIN32
        closesocket(sock);
        WSACleanup();
#else
        close(sock);
#endif
        return false;
    }

    string json_body = payload_to_json(payload);
    ostringstream oss;
    oss << "POST /api/add_download HTTP/1.1\r\n"
        << "Host: " << host << "\r\n"
        << "Content-Type: application/json\r\n"
        << "Content-Length: " << json_body.length() << "\r\n"
        << "Connection: close\r\n\r\n"
        << json_body;

    string req_str = oss.str();
    send(sock, req_str.c_str(), static_cast<int>(req_str.length()), 0);

    char buf[1024];
    string response;
    int bytes = 0;
    while ((bytes = recv(sock, buf, sizeof(buf) - 1, 0)) > 0) {
        buf[bytes] = '\0';
        response += buf;
    }

#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif

    if (out_response) *out_response = response;
    return (response.find("200 OK") != string::npos || response.find("201 Created") != string::npos);
}

} // namespace idm
