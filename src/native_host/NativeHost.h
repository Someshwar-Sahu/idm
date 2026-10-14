#ifndef NATIVE_HOST_H
#define NATIVE_HOST_H

#include <string>
#include <vector>
#include <cstdint>

namespace idm {

enum class BrowserType {
    Chrome,
    Edge,
    Firefox
};

struct InterceptedDownloadPayload {
    std::string url;
    std::string filename;
    std::string cookies;
    std::string user_agent;
    std::string referer;
    std::string page_title;
    uint64_t total_size{0};
    bool user_force_override{false};
};

class NativeHost {
public:
    // Stdio Framing (4-byte length prefix)
    static bool read_message_from_stdin(InterceptedDownloadPayload &out_payload);
    static bool write_response_to_stdout(bool success, const std::string &message);

    // Serialization
    static std::string payload_to_json(const InterceptedDownloadPayload &payload);
    static bool json_to_payload(const std::string &json_str, InterceptedDownloadPayload &out_payload);

    // Manifest & Registry Setup
    static std::string generate_native_manifest(const std::string& executable_path,
                                               const std::vector<std::string>& allowed_origins,
                                               BrowserType browser = BrowserType::Chrome);

    static std::string get_registry_key_path(BrowserType browser);
    static bool install_native_host_registry(BrowserType browser, const std::string& manifest_filepath);
    static bool uninstall_native_host_registry(BrowserType browser);

    // Forwarding to Engine LocalServer
    static bool forward_payload_to_server(const InterceptedDownloadPayload& payload,
                                         const std::string& host = "127.0.0.1",
                                         int port = 8080,
                                         std::string* out_response = nullptr);
};

} // namespace idm

#endif
