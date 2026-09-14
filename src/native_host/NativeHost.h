#ifndef NATIVE_HOST_H
#define NATIVE_HOST_H

#include<string>
#include<cstdint>

struct InterceptedDownloadPayload {
    std::string url;
    std::string filename;
    std::string cookies;
    std::string user_agent;
    std::string referer;
    std::string page_title;
    uint64_t total_size{0};
};

class NativeHost {
public:
    static bool read_message_from_stdin(InterceptedDownloadPayload &out_payload);

    static bool write_response_to_stdout(bool success, const std::string &message);

    static std::string payload_to_json(const InterceptedDownloadPayload &payload);
    static bool json_to_payload(const std::string &json_str, InterceptedDownloadPayload &out_payload);
};

#endif