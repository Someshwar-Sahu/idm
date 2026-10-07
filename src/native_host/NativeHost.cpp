#include "NativeHost.h"
#include <nlohmann/json.hpp>
#include <iostream>

#ifdef _WIN32
#include <io.h>
#include<fcntl.h>
#endif

using namespace std;
using json = nlohmann::json;

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

    if(!cin || static_cast<uint32_t> (cin.gcount() ) < message_length){
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