#include <curl/curl.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <filesystem>
#include "DownloadTask.h"
#include "AntivirusScanner.h"
#include "ChunkWorker.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>

using namespace std;

static size_t header_callback(char *buffer, size_t size, size_t nitems, void *userdata) {
    size_t total_size = size * nitems;
    string *headers = static_cast<string*>(userdata);
    headers->append(buffer, total_size);
    return total_size;
}

DownloadTask::DownloadTask(const string &url,
                           const string &destination_filepath,
                           int num_threads,
                           uint64_t max_bytes_per_sec,
                           const string &referer,
                           const string &cookies,
                           const string &user_agent,
                           const string &cache_directory)
    : m_url(url),
      m_cache_directory(cache_directory),
      m_num_threads(num_threads),
      m_max_bytes_per_sec(max_bytes_per_sec),
      m_worker_bytes_per_sec((num_threads > 0 && max_bytes_per_sec > 0) ? (max_bytes_per_sec / num_threads) : max_bytes_per_sec),
      m_referer(referer),
      m_cookies(cookies) {
    if (!user_agent.empty()) {
        m_user_agent = user_agent;
    }
    // Sanitize destination filepath immediately to prevent path overflow or invalid chars
    try {
        std::filesystem::path dest_p(destination_filepath);
        std::string safe_name = sanitize_filename(dest_p.filename().string());
        if (dest_p.has_parent_path()) {
            m_destination_filepath = (dest_p.parent_path() / safe_name).string();
        } else {
            m_destination_filepath = safe_name;
        }
    } catch (...) {
        m_destination_filepath = sanitize_filename(destination_filepath);
    }
    m_sidecar_filepath = ResumeStore::get_sidecar_filepath(m_destination_filepath, m_cache_directory);
}

DownloadTask::~DownloadTask() {
    pause();
}

void DownloadTask::set_speed_limit(uint64_t max_bytes_per_sec) {
    m_max_bytes_per_sec.store(max_bytes_per_sec);
    int count = m_supports_ranges ? m_num_threads : 1;
    if (count > 0 && max_bytes_per_sec > 0) {
        m_worker_bytes_per_sec.store(max_bytes_per_sec / count);
    } else {
        m_worker_bytes_per_sec.store(max_bytes_per_sec);
    }
}

uint64_t DownloadTask::get_speed_limit() const {
    return m_max_bytes_per_sec.load();
}

uint64_t DownloadTask::get_available_disk_space(const string &filepath) {
#ifdef _WIN32
    namespace fs = std::filesystem;
    try {
        std::error_code ec;
        fs::path p(filepath);
        fs::path dir = p.has_parent_path() ? p.parent_path() : fs::current_path(ec);
        int max_depth = 50;
        while (!dir.empty() && !fs::exists(dir, ec) && max_depth-- > 0) {
            fs::path parent = dir.parent_path();
            if (parent == dir || parent.empty()) {
                dir = fs::current_path(ec);
                break;
            }
            dir = parent;
        }

        string dir_str = dir.string();
        ULARGE_INTEGER free_bytes_available;
        ULARGE_INTEGER total_bytes;
        ULARGE_INTEGER total_free_bytes;

        if (GetDiskFreeSpaceExA(dir_str.c_str(), &free_bytes_available, &total_bytes, &total_free_bytes)) {
            return static_cast<uint64_t>(free_bytes_available.QuadPart);
        }
        if (GetDiskFreeSpaceExA(NULL, &free_bytes_available, &total_bytes, &total_free_bytes)) {
            return static_cast<uint64_t>(free_bytes_available.QuadPart);
        }
    } catch (...) {}
#endif

    std::error_code ec;
    auto s = std::filesystem::space(".", ec);
    if (!ec && s.available != static_cast<std::uintmax_t>(-1)) {
        return static_cast<uint64_t>(s.available);
    }
    return UINT64_MAX;
}

bool DownloadTask::has_sufficient_disk_space(uint64_t required_bytes) const {
    uint64_t available = get_available_disk_space(m_destination_filepath);
    if (available == UINT64_MAX) {
        return true;
    }
    return available >= required_bytes;
}

void DownloadTask::set_http_context(const string &referer, const string &cookies, const string &user_agent) {
    std::lock_guard<std::mutex> lock(m_string_mutex);
    m_referer = referer;
    m_cookies = cookies;
    if (!user_agent.empty()) {
        m_user_agent = user_agent;
    }
}

void DownloadTask::set_auth_credentials(const string &username, const string &password, const string &auth_header) {
    std::lock_guard<std::mutex> lock(m_string_mutex);
    m_username = username;
    m_password = password;
    m_auth_header = auth_header;
}

string DownloadTask::get_username() const {
    std::lock_guard<std::mutex> lock(m_string_mutex);
    return m_username;
}

string DownloadTask::get_password() const {
    std::lock_guard<std::mutex> lock(m_string_mutex);
    return m_password;
}

string DownloadTask::get_auth_header() const {
    std::lock_guard<std::mutex> lock(m_string_mutex);
    return m_auth_header;
}

string DownloadTask::get_url() const {
    std::lock_guard<std::mutex> lock(m_string_mutex);
    return m_url;
}

string DownloadTask::get_referer() const {
    std::lock_guard<std::mutex> lock(m_string_mutex);
    return m_referer;
}

string DownloadTask::get_cookies() const {
    std::lock_guard<std::mutex> lock(m_string_mutex);
    return m_cookies;
}

string DownloadTask::get_user_agent() const {
    std::lock_guard<std::mutex> lock(m_string_mutex);
    return m_user_agent;
}

string DownloadTask::get_destination_filepath() const {
    std::lock_guard<std::mutex> lock(m_string_mutex);
    return m_destination_filepath;
}

string DownloadTask::sanitize_filename(const string &filename) {
    if (filename.empty()) return "download.bin";
    string clean;
    clean.reserve(filename.size());
    for (char c : filename) {
        if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || 
            c == '"' || c == '<' || c == '>' || c == '|' || static_cast<unsigned char>(c) < 32) {
            clean += '_';
        } else {
            clean += c;
        }
    }
    while (!clean.empty() && (clean.back() == ' ' || clean.back() == '.')) {
        clean.pop_back();
    }
    if (clean.empty()) return "download.bin";

    size_t dot = clean.find_last_of('.');
    string ext;
    string stem = clean;
    if (dot != string::npos && dot + 1 < clean.length() && (clean.length() - dot <= 10)) {
        ext = clean.substr(dot);
        stem = clean.substr(0, dot);
    }

    if (stem.length() > 60) {
        stem = stem.substr(0, 60);
    }
    return stem + ext;
}

string DownloadTask::mime_to_extension(const string &mime) {
    string m = mime;
    for (char &c : m) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    if (m.find("video/mp4") != string::npos) return ".mp4";
    if (m.find("video/webm") != string::npos) return ".webm";
    if (m.find("video/x-matroska") != string::npos || m.find("video/mkv") != string::npos) return ".mkv";
    if (m.find("video/quicktime") != string::npos) return ".mov";
    if (m.find("video/x-msvideo") != string::npos) return ".avi";
    if (m.find("video/x-flv") != string::npos) return ".flv";
    if (m.find("audio/mpeg") != string::npos || m.find("audio/mp3") != string::npos) return ".mp3";
    if (m.find("audio/wav") != string::npos || m.find("audio/x-wav") != string::npos) return ".wav";
    if (m.find("audio/flac") != string::npos) return ".flac";
    if (m.find("audio/aac") != string::npos) return ".aac";
    if (m.find("application/zip") != string::npos) return ".zip";
    if (m.find("application/x-rar") != string::npos) return ".rar";
    if (m.find("application/x-7z") != string::npos) return ".7z";
    if (m.find("application/pdf") != string::npos) return ".pdf";
    if (m.find("application/x-msdos-program") != string::npos || m.find("application/x-msdownload") != string::npos) return ".exe";
    if (m.find("image/jpeg") != string::npos) return ".jpg";
    if (m.find("image/png") != string::npos) return ".png";
    return "";
}

string DownloadTask::extract_filename_from_url(const string &url){
    if(url.empty()) return "download.bin";

    size_t q_pos = url.find('?');
    if (q_pos != string::npos) {
        string query = url.substr(q_pos + 1);
        string lower_q = query;
        for (char &c : lower_q) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

        const vector<string> keys = {"filename=", "fn=", "name=", "title="};
        for (const auto &key : keys) {
            size_t k_pos = lower_q.find(key);
            if (k_pos != string::npos) {
                size_t val_start = k_pos + key.length();
                size_t val_end = query.find('&', val_start);
                if (val_end == string::npos) val_end = query.find('#', val_start);
                if (val_end == string::npos) val_end = query.length();
                string val = query.substr(val_start, val_end - val_start);
                if (!val.empty() && val.find('.') != string::npos) {
                    return sanitize_filename(val);
                }
            }
        }
    }

    string clean = url;
    if(q_pos != string::npos) clean = clean.substr(0, q_pos);
    size_t h_pos = clean.find('#');
    if (h_pos != string::npos) clean = clean.substr(0, h_pos);

    while (clean.length() > 1 && (clean.back() == '/' || clean.back() == '\\')){
        clean.pop_back();
    }
    size_t slash_pos = clean.find_last_of("/\\");
    if(slash_pos != string::npos && slash_pos + 1 < clean.length()){
        string name = clean.substr(slash_pos + 1);
        if(!name.empty()) return sanitize_filename(name);
    }
    if (!clean.empty() && clean.find('.') != string::npos) {
        return sanitize_filename(clean);
    }
    return "download.bin";
}

static string url_decode_rfc5987(const string &in) {
    string out;
    out.reserve(in.length());
    for (size_t i = 0; i < in.length(); ++i) {
        if (in[i] == '%' && i + 2 < in.length()) {
            char hex[3] = { in[i+1], in[i+2], 0 };
            char *end = nullptr;
            long val = strtol(hex, &end, 16);
            if (end == hex + 2) {
                out += static_cast<char>(val);
                i += 2;
                continue;
            }
        }
        out += in[i];
    }
    return out;
}

string DownloadTask::extract_filename_from_content_disposition(const string &header){
    string last_headers = get_last_response_headers(header);
    string lower_header = last_headers;
    for(char &c: lower_header) c= static_cast<char> (tolower(static_cast<unsigned char> (c)));

    // Bug 14 Fix: Support modern RFC 5987 UTF-8 encoded filename*= (e.g. filename*=UTF-8''document.pdf)
    size_t star_pos = lower_header.find("filename*=");
    if (star_pos != string::npos) {
        size_t start = star_pos + 10;
        size_t end = last_headers.find_first_of(";\r\n", start);
        if (end == string::npos) end = last_headers.length();
        string raw = last_headers.substr(start, end - start);
        if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
            raw = raw.substr(1, raw.size() - 2);
        }
        size_t quote2 = raw.find("''");
        if (quote2 != string::npos) {
            string decoded = url_decode_rfc5987(raw.substr(quote2 + 2));
            if (!decoded.empty()) return sanitize_filename(decoded);
        } else {
            string decoded = url_decode_rfc5987(raw);
            if (!decoded.empty()) return sanitize_filename(decoded);
        }
    }

    size_t pos = lower_header.find("filename=");
    if(pos == string::npos) return "";

    size_t start = pos + 9;
    if(start >= last_headers.length()) return "";

    if(last_headers[start] == '"'){
        start++;
        size_t end = last_headers.find('"', start);
        if(end != string::npos){
            return last_headers.substr(start, end-start);
        }
    }
    else {
        size_t end = last_headers.find_first_of(";\r\n", start);
        if(end == string::npos) end = last_headers.length();
        return last_headers.substr(start, end - start);
    }
    return "";
}

string DownloadTask::get_last_response_headers(const string &all_headers) {
    if (all_headers.empty()) return "";
    size_t last_pos = string::npos;
    size_t pos = 0;
    while (true) {
        size_t found = all_headers.find("HTTP/", pos);
        if (found == string::npos) break;
        if (found == 0 || all_headers[found - 1] == '\n' || all_headers[found - 1] == '\r') {
            last_pos = found;
        }
        pos = found + 5;
    }
    if (last_pos != string::npos) {
        return all_headers.substr(last_pos);
    }
    return all_headers;
}

string DownloadTask::get_header_value(const string &headers, const string &header_name) {
    if (headers.empty() || header_name.empty()) return "";

    string target = header_name;
    for (char &c : target) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

    size_t pos = 0;
    while (pos < headers.length()) {
        size_t line_end = headers.find_first_of("\r\n", pos);
        if (line_end == string::npos) line_end = headers.length();
        string line = headers.substr(pos, line_end - pos);

        size_t colon = line.find(':');
        if (colon != string::npos) {
            string key = line.substr(0, colon);
            while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
            string lower_key = key;
            for (char &c : lower_key) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

            if (lower_key == target) {
                size_t val_start = colon + 1;
                while (val_start < line.length() && (line[val_start] == ' ' || line[val_start] == '\t')) {
                    val_start++;
                }
                string val = line.substr(val_start);
                while (!val.empty() && (val.back() == ' ' || val.back() == '\t' || val.back() == '\r')) {
                    val.pop_back();
                }
                return val;
            }
        }

        pos = headers.find_first_not_of("\r\n", line_end);
        if (pos == string::npos) break;
    }
    return "";
}

bool DownloadTask::etags_match(const string &e1, const string &e2) {
    auto clean = [](string s) {
        if (s.size() >= 2 && (s.substr(0, 2) == "W/" || s.substr(0, 2) == "w/")) {
            s = s.substr(2);
        }
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
            s = s.substr(1, s.size() - 2);
        }
        return s;
    };
    return clean(e1) == clean(e2);
}

bool DownloadTask::update_url(const string &new_url) {
    if (new_url.empty()) {
        set_last_error("Cannot update to empty URL");
        return false;
    }

    if (m_status == DownloadStatus::Downloading) {
        set_last_error("Cannot update URL while download is active. Pause the task first.");
        return false;
    }

    string ua, ref, ck;
    {
        std::lock_guard<std::mutex> lock(m_string_mutex);
        ua = m_user_agent;
        ref = m_referer;
        ck = m_cookies;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        set_last_error("Failed to initialize libcurl handle for URL validation");
        return false;
    }

    string response_headers;
    curl_easy_setopt(curl, CURLOPT_URL, new_url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, ua.c_str());
    if (!ref.empty()) {
        curl_easy_setopt(curl, CURLOPT_REFERER, ref.c_str());
    }
    if (!ck.empty()) {
        curl_easy_setopt(curl, CURLOPT_COOKIE, ck.c_str());
    }
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response_headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NO_REVOKE);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        set_last_error(string("Validation of refreshed URL failed: ") + curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        return false;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code >= 400) {
        set_last_error("Server rejected refreshed URL with HTTP " + to_string(http_code));
        curl_easy_cleanup(curl);
        return false;
    }

    curl_off_t cl = 0;
    curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &cl);
    curl_easy_cleanup(curl);

    if (m_total_size > 0 && cl > 0 && static_cast<uint64_t>(cl) != m_total_size) {
        set_last_error("Refreshed URL size mismatch: expected " + to_string(m_total_size) +
                       " bytes, server returned " + to_string(cl) + " bytes");
        return false;
    }

    string sidecar;
    {
        std::lock_guard<std::mutex> lock(m_string_mutex);
        m_url = new_url;
        sidecar = m_sidecar_filepath;
    }

    if (m_chunk_manager && ResumeStore::exists(sidecar)) {
        ResumeStore::save(sidecar, new_url, m_total_size, m_etag, *m_chunk_manager, m_supports_ranges);
    }

    if (m_status == DownloadStatus::Failed) {
        m_status = DownloadStatus::Paused;
        clear_last_error();
    }

    return true;
}

bool DownloadTask::probe_url() {
    update_last_try_time();
    m_status = DownloadStatus::Probing;
    m_supports_ranges = false;
    CURL *curl = curl_easy_init();
    if (!curl) {
        set_last_error("Failed to initialize libcurl handle for URL probing");
        m_status = DownloadStatus::Failed;
        return false;
    }

    string response_headers;
    curl_easy_setopt(curl, CURLOPT_URL, m_url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, m_user_agent.c_str());
    if (!m_referer.empty()) {
        curl_easy_setopt(curl, CURLOPT_REFERER, m_referer.c_str());
    }
    if (!m_cookies.empty()) {
        curl_easy_setopt(curl, CURLOPT_COOKIE, m_cookies.c_str());
    }

    struct curl_slist *probe_headers = nullptr;
    if (!m_username.empty()) {
        std::string userpwd = m_username + ":" + m_password;
        curl_easy_setopt(curl, CURLOPT_USERPWD, userpwd.c_str());
    }
    if (!m_auth_header.empty()) {
        probe_headers = curl_slist_append(probe_headers, m_auth_header.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, probe_headers);
    }

    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response_headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_UNRESTRICTED_AUTH, 1L);
    curl_easy_setopt(curl, CURLOPT_AUTOREFERER, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 20L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NO_REVOKE);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    if (res != CURLE_OK) {
        set_last_error(string("HTTP HEAD probe failed: ") + curl_easy_strerror(res));
    } else if (http_code >= 400) {
        set_last_error("HTTP HEAD probe failed: Server returned HTTP " + to_string(http_code));
    } else {
        string eff_headers = get_last_response_headers(response_headers);
        char *effective_url = nullptr;
        curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &effective_url);
        if (effective_url && strlen(effective_url) > 0) {
            std::lock_guard<std::mutex> lock(m_string_mutex);
            m_url = effective_url;
        }

        curl_off_t cl = 0;
        curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &cl);
        if (cl > 0) {
            m_total_size = static_cast<uint64_t>(cl);
        }

        if (m_total_size == 0) {
            string cl_str = get_header_value(eff_headers, "Content-Length");
            if (!cl_str.empty()) {
                try {
                    m_total_size = std::stoull(cl_str);
                } catch (...) {}
            }
        }

        string ar_val = get_header_value(eff_headers, "Accept-Ranges");
        for (char &c : ar_val) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        if (ar_val.find("bytes") != string::npos) {
            m_supports_ranges = true;
        } else if (ar_val.find("none") != string::npos) {
            m_supports_ranges = false;
        }

        string etag_val = get_header_value(eff_headers, "ETag");
        if (!etag_val.empty()) {
            m_etag = etag_val;
        }
    }
    curl_easy_cleanup(curl);
    if (probe_headers) {
        curl_slist_free_all(probe_headers);
        probe_headers = nullptr;
    }

    // Probing Range support: If Accept-Ranges: bytes was not explicitly advertised in HEAD,
    // probe with a 1-byte GET Range: 0-0. This verifies HTTP 206 Partial Content capability
    // for CDNs (Google, Cloudflare, Akamai, etc.) which support ranges but omit Accept-Ranges on HEAD.
    string get_headers;
    if (!m_supports_ranges || m_total_size == 0) {
        CURL *get_curl = curl_easy_init();
        if (get_curl) {
            struct curl_slist *get_probe_headers = nullptr;
            if (!m_username.empty()) {
                std::string userpwd = m_username + ":" + m_password;
                curl_easy_setopt(get_curl, CURLOPT_USERPWD, userpwd.c_str());
            }
            if (!m_auth_header.empty()) {
                get_probe_headers = curl_slist_append(get_probe_headers, m_auth_header.c_str());
                curl_easy_setopt(get_curl, CURLOPT_HTTPHEADER, get_probe_headers);
            }

            curl_easy_setopt(get_curl, CURLOPT_URL, m_url.c_str());
            curl_easy_setopt(get_curl, CURLOPT_USERAGENT, m_user_agent.c_str());
            if (!m_referer.empty()) curl_easy_setopt(get_curl, CURLOPT_REFERER, m_referer.c_str());
            if (!m_cookies.empty()) curl_easy_setopt(get_curl, CURLOPT_COOKIE, m_cookies.c_str());
            curl_easy_setopt(get_curl, CURLOPT_RANGE, "0-0");
            curl_easy_setopt(get_curl, CURLOPT_HEADERFUNCTION, header_callback);
            curl_easy_setopt(get_curl, CURLOPT_HEADERDATA, &get_headers);
            curl_easy_setopt(get_curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(get_curl, CURLOPT_TIMEOUT, 15L);
            curl_easy_setopt(get_curl, CURLOPT_SSL_VERIFYPEER, 0L);
            curl_easy_setopt(get_curl, CURLOPT_SSL_VERIFYHOST, 0L);
            curl_easy_setopt(get_curl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NO_REVOKE);
            curl_easy_setopt(get_curl, CURLOPT_WRITEFUNCTION, +[](char*, size_t s, size_t n, void*) -> size_t { return s * n; });

            CURLcode get_res = curl_easy_perform(get_curl);
            long get_code = 0;
            curl_easy_getinfo(get_curl, CURLINFO_RESPONSE_CODE, &get_code);
            string eff_get_headers = get_last_response_headers(get_headers);
            if (get_res == CURLE_OK && (get_code == 200 || get_code == 206)) {
                if (get_code == 206) m_supports_ranges = true;
                // Parse Content-Range: bytes 0-0/104857600
                string cr_str = get_header_value(eff_get_headers, "Content-Range");
                if (!cr_str.empty()) {
                    size_t slash = cr_str.find('/');
                    if (slash != string::npos) {
                        try {
                            uint64_t parsed_size = std::stoull(cr_str.substr(slash + 1));
                            if (parsed_size > 0) m_total_size = parsed_size;
                        } catch (...) {}
                    }
                }
                if (m_total_size == 0) {
                    curl_off_t cl = 0;
                    curl_easy_getinfo(get_curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &cl);
                    if (cl > 0) m_total_size = static_cast<uint64_t>(cl);
                    if (m_total_size == 0) {
                        string cl_str = get_header_value(eff_get_headers, "Content-Length");
                        if (!cl_str.empty()) {
                            try {
                                m_total_size = std::stoull(cl_str);
                            } catch (...) {}
                        }
                    }
                }
                if (m_etag.empty()) {
                    string etag_str = get_header_value(eff_get_headers, "ETag");
                    if (!etag_str.empty()) m_etag = etag_str;
                }
                char *eff_url = nullptr;
                curl_easy_getinfo(get_curl, CURLINFO_EFFECTIVE_URL, &eff_url);
                if (eff_url && strlen(eff_url) > 0) {
                    std::lock_guard<std::mutex> lock(m_string_mutex);
                    m_url = eff_url;
                }
                clear_last_error();
            }
            curl_easy_cleanup(get_curl);
            if (get_probe_headers) {
                curl_slist_free_all(get_probe_headers);
                get_probe_headers = nullptr;
            }
        }
    }

    string eff_headers = get_last_response_headers(response_headers);
    string eff_get_headers = get_last_response_headers(get_headers);

    // Auto-resolve filename and extension from Content-Disposition and Content-Type headers
    // Prefer effective headers from HEAD, but seamlessly fall back to get_headers from GET probe
    string cd_name = extract_filename_from_content_disposition(eff_headers);
    if (cd_name.empty() && !eff_get_headers.empty()) {
        cd_name = extract_filename_from_content_disposition(eff_get_headers);
    }

    std::string current_dest;
    {
        std::lock_guard<std::mutex> lock(m_string_mutex);
        current_dest = m_destination_filepath;
    }

    if (!cd_name.empty()) {
        cd_name = sanitize_filename(cd_name);
        try {
            std::filesystem::path cur_p(current_dest);
            std::string new_dest = cur_p.has_parent_path() ? (cur_p.parent_path() / cd_name).string() : cd_name;
            std::string new_sidecar = ResumeStore::get_sidecar_filepath(new_dest, m_cache_directory);
            std::lock_guard<std::mutex> lock(m_string_mutex);
            m_destination_filepath = new_dest;
            m_sidecar_filepath = new_sidecar;
        } catch (...) {
            std::lock_guard<std::mutex> lock(m_string_mutex);
            m_destination_filepath = cd_name;
        }
    } else {
        string ct_val = get_header_value(eff_headers, "Content-Type");
        if (ct_val.empty() && !eff_get_headers.empty()) {
            ct_val = get_header_value(eff_get_headers, "Content-Type");
        }
        if (!ct_val.empty()) {
            size_t semi = ct_val.find(';');
            string ct = (semi != string::npos) ? ct_val.substr(0, semi) : ct_val;
            string ext = mime_to_extension(ct);
            if (!ext.empty()) {
                try {
                    std::filesystem::path cur_p(current_dest);
                    string cur_ext = cur_p.extension().string();
                    if (cur_ext.empty() || cur_ext == ".bin" || cur_ext == ".dat" || cur_ext == ".tmp") {
                        std::string new_dest = cur_p.replace_extension(ext).string();
                        std::string new_sidecar = ResumeStore::get_sidecar_filepath(new_dest, m_cache_directory);
                        std::lock_guard<std::mutex> lock(m_string_mutex);
                        m_destination_filepath = new_dest;
                        m_sidecar_filepath = new_sidecar;
                    }
                } catch (...) {}
            }
        }
    }

    // Ensure m_destination_filepath is strictly sanitized and length-safe
    try {
        std::lock_guard<std::mutex> lock(m_string_mutex);
        std::filesystem::path dest_p(m_destination_filepath);
        std::string safe_name = sanitize_filename(dest_p.filename().string());
        if (dest_p.has_parent_path()) {
            m_destination_filepath = (dest_p.parent_path() / safe_name).string();
        } else {
            m_destination_filepath = safe_name;
        }
    } catch (...) {}

    if (m_total_size == 0) {
        string combined_headers = response_headers + "\n" + get_headers;
        if ((http_code == 200 || get_headers.find(" 200 ") != string::npos) &&
            (combined_headers.find("Content-Length: 0") != string::npos || combined_headers.find("content-length: 0") != string::npos)) {
            // Legitimate 0-byte file download completed immediately
            m_status = DownloadStatus::Completed;
            FileWriter::preallocate_file(m_destination_filepath, 0);
            return true;
        }
        if (m_last_error.empty()) {
            m_last_error = "Server did not provide file size or connection failed";
        }
        m_status = DownloadStatus::Failed;
        return false;
    }

    return true;
}

bool DownloadTask::start(ProgressCallback callback) {
    update_last_try_time();
    std::unique_lock<std::mutex> run_lock(m_run_mutex);

    if(m_antivirus_scan_enabled && !m_security_override){
        auto rep = AntivirusScanner::check_url_reputation(m_url, m_destination_filepath);
        if(!rep.is_safe){
            m_last_error = "Security Warning: Threat detected (" + rep.details + ")";
            m_status = DownloadStatus::Failed;
            return false;
        }
    }
    if (m_pause_requested.load()) {
        m_status = DownloadStatus::Paused;
        return false;
    }
    m_pause_requested = false;

    bool is_resuming = false;
    m_chunk_manager = make_unique<ChunkManager>();

    if (ResumeStore::exists(m_sidecar_filepath)) {
        string saved_url, saved_etag;
        uint64_t saved_size = 0;
        bool saved_supports_ranges = true;
        if (ResumeStore::load(m_sidecar_filepath, saved_url, saved_size, saved_etag, *m_chunk_manager, saved_supports_ranges)) {
            if (saved_url == m_url && saved_size > 0) {
                is_resuming = true;
                m_total_size = saved_size;
                m_etag = saved_etag;
                m_supports_ranges = saved_supports_ranges;
                if (m_supports_ranges) {
                    m_chunk_manager->ensure_chunk_count(m_num_threads);
                }
                cout << "[DownloadTask] Resuming existing download state from sidecar (" << (m_supports_ranges ? "Multi-threaded" : "Single-thread") << ") with " << m_chunk_manager->get_all_chunks().size() << " chunks!" << endl;

                // Bug 4 Fix: Send If-None-Match to ensure server resource hasn't changed
                if (!m_etag.empty()) {
                    CURL *val_curl = curl_easy_init();
                    if (val_curl) {
                        string val_headers;
                        curl_easy_setopt(val_curl, CURLOPT_URL, m_url.c_str());
                        curl_easy_setopt(val_curl, CURLOPT_USERAGENT, m_user_agent.c_str());
                        if (!m_referer.empty()) curl_easy_setopt(val_curl, CURLOPT_REFERER, m_referer.c_str());
                        if (!m_cookies.empty()) curl_easy_setopt(val_curl, CURLOPT_COOKIE, m_cookies.c_str());
                        curl_easy_setopt(val_curl, CURLOPT_NOBODY, 1L);
                        curl_easy_setopt(val_curl, CURLOPT_FOLLOWLOCATION, 1L);
                        curl_easy_setopt(val_curl, CURLOPT_TIMEOUT, 10L);
                        curl_easy_setopt(val_curl, CURLOPT_SSL_VERIFYPEER, 0L);
                        curl_easy_setopt(val_curl, CURLOPT_SSL_VERIFYHOST, 0L);
                        curl_easy_setopt(val_curl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NO_REVOKE);
                        curl_easy_setopt(val_curl, CURLOPT_HEADERFUNCTION, header_callback);
                        curl_easy_setopt(val_curl, CURLOPT_HEADERDATA, &val_headers);

                        struct curl_slist *val_headers_list = nullptr;
                        string if_match = "If-None-Match: " + m_etag;
                        val_headers_list = curl_slist_append(val_headers_list, if_match.c_str());
                        curl_easy_setopt(val_curl, CURLOPT_HTTPHEADER, val_headers_list);

                        CURLcode vres = curl_easy_perform(val_curl);
                        long vcode = 0;
                        curl_easy_getinfo(val_curl, CURLINFO_RESPONSE_CODE, &vcode);
                        curl_slist_free_all(val_headers_list);
                        curl_easy_cleanup(val_curl);

                        bool resource_changed = false;
                        if (vres == CURLE_OK) {
                            if (vcode == 412) {
                                resource_changed = true;
                            } else if (vcode == 200) {
                                string eff_val_headers = get_last_response_headers(val_headers);
                                string new_etag = get_header_value(eff_val_headers, "ETag");
                                if (!new_etag.empty()) {
                                    if (!etags_match(new_etag, m_etag)) {
                                        resource_changed = true;
                                    }
                                } else {
                                    string cl_str = get_header_value(eff_val_headers, "Content-Length");
                                    if (!cl_str.empty()) {
                                        try {
                                            uint64_t new_size = std::stoull(cl_str);
                                            if (new_size > 0 && new_size != m_total_size) {
                                                resource_changed = true;
                                            }
                                        } catch (...) {}
                                    }
                                }
                            }
                        }

                        if (resource_changed) {
                            cout << "[DownloadTask] Server resource modified (ETag mismatch). Redownloading fresh..." << endl;
                            is_resuming = false;
                            ResumeStore::remove(m_sidecar_filepath);
                            m_chunk_manager = make_unique<ChunkManager>();
                        }
                    }
                }
            }
        }
    }

    if (!is_resuming) {
        cout << "[DownloadTask] Probing server URL..." << endl;
        if (!probe_url()) {
            return false;
        }

        // Fix Bug #1: Handle 0-byte files that completed immediately in probe_url()
        if (m_status == DownloadStatus::Completed) {
            m_progress_tracker = make_unique<ProgressTracker>(0, 0);
            if (callback) {
                callback(*m_progress_tracker, "Completed");
            }
            cout << "[DownloadTask] 0-byte download completed successfully!" << endl;
            return true;
        }

        if (m_total_size == 0) {
            set_last_error("Server returned invalid file size (0 bytes)");
            m_status = DownloadStatus::Failed;
            return false;
        }

        int chunks_to_use = m_supports_ranges ? m_num_threads : 1;
        m_chunk_manager->init(m_total_size, chunks_to_use);
    }

    if (!has_sufficient_disk_space(m_total_size)) {
        uint64_t available = get_available_disk_space(m_destination_filepath);
        set_last_error("Insufficient disk space: required " + to_string(m_total_size) +
                       " bytes, available " + to_string(available) + " bytes");
        cerr << "[DownloadTask Error] " << get_last_error() << endl;
        m_status = DownloadStatus::Failed;
        return false;
    }

    try{
        std::filesystem::path dest_p(m_destination_filepath);
        if(dest_p.has_parent_path()){
            std::error_code ec;
            std::filesystem::create_directories(dest_p.parent_path(), ec);
        }
    } catch (...) {}

    if(!is_resuming && FileWriter::file_exists(m_destination_filepath)){
        std::string new_dest = FileWriter::generate_unique_filepath(m_destination_filepath);
        std::string new_sidecar = ResumeStore::get_sidecar_filepath(new_dest, m_cache_directory);
        std::lock_guard<std::mutex> lock(m_string_mutex);
        m_destination_filepath = new_dest;
        m_sidecar_filepath = new_sidecar;
    }

    // Only preallocate file if starting fresh. If resuming, preserve already downloaded bytes!
    if (!is_resuming) {
        FileWriter::preallocate_file(m_destination_filepath, m_total_size);
    }
    m_file_writer = make_unique<FileWriter>();
    if (!m_file_writer->open(m_destination_filepath)) {
        set_last_error("Failed to create/open destination file on disk: " + m_destination_filepath);
        cerr << "[DownloadTask Error] " << get_last_error() << endl;
        m_status = DownloadStatus::Failed;
        return false;
    }

    uint64_t initial_bytes = m_chunk_manager ? m_chunk_manager->get_total_downloaded_bytes() : 0;
    m_progress_tracker = make_unique<ProgressTracker>(m_total_size, initial_bytes);
    m_thread_pool = make_unique<ThreadPool>(m_supports_ranges ? m_num_threads : 1);

    m_status = DownloadStatus::Downloading;

    int worker_count = m_supports_ranges ? m_num_threads : 1;
    for (int i = 0; i < worker_count; ++i) {
        m_thread_pool->enqueue([this, worker_count, i]() {
            if (i > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(i * 40));
            }
            while (!m_pause_requested && !m_chunk_manager->is_complete()) {
                auto chunk_opt = m_chunk_manager->get_next_pending_chunk();
                if (!chunk_opt.has_value()) {
                    size_t max_chunks = std::max<size_t>(32, static_cast<size_t>(m_num_threads * 2));
                    chunk_opt = m_chunk_manager->steal_slowest_chunk(1024 * 1024, max_chunks);
                    if (!chunk_opt.has_value()) {
                        // Wait briefly for donor chunks to establish or finish
                        std::this_thread::sleep_for(std::chrono::milliseconds(150));
                        continue;
                    }
                }

                Chunk chunk = chunk_opt.value();

                uint64_t total_limit = m_max_bytes_per_sec.load();
                uint64_t worker_speed = (worker_count > 0 && total_limit > 0) ? (total_limit / worker_count) : total_limit;

                ChunkWorker worker(m_url, chunk, *m_chunk_manager, *m_file_writer, worker_speed, m_referer, m_cookies, m_user_agent);
                worker.set_supports_ranges(m_supports_ranges);
                worker.set_pause_flag(&m_pause_requested);
                worker.set_dynamic_speed_limit(&m_worker_bytes_per_sec);
                worker.set_auth_credentials(m_username, m_password, m_auth_header);

                worker.download();
            }
        });
    }

    bool was_paused = false;
    while (!m_pause_requested && !m_chunk_manager->is_complete()) {
        if (m_chunk_manager->has_failed()) {
            set_last_error("One or more chunks failed to download after maximum retries.");
            break;
        }
        uint64_t downloaded = m_chunk_manager->get_total_downloaded_bytes();
        m_progress_tracker->update(downloaded);

        if (callback) {
            callback(*m_progress_tracker, "Downloading");
        }
        ResumeStore::save(m_sidecar_filepath, m_url, m_total_size, m_etag, *m_chunk_manager, m_supports_ranges);

        this_thread::sleep_for(chrono::milliseconds(200));
    }

    if (m_pause_requested.load() || m_status == DownloadStatus::Paused) {
        was_paused = true;
    }

    if (was_paused || m_pause_requested.load() || m_status == DownloadStatus::Paused) {
        m_thread_pool.reset();
        m_file_writer->close();
        ResumeStore::save(m_sidecar_filepath, m_url, m_total_size, m_etag, *m_chunk_manager, m_supports_ranges);
        if (m_status != DownloadStatus::NotStarted && m_status != DownloadStatus::Cancelled) {
            m_status = DownloadStatus::Paused;
        }
        if (m_status == DownloadStatus::Cancelled) {
            cout << "[DownloadTask] Download cancelled successfully." << endl;
        } else {
            cout << "[DownloadTask] Download paused successfully." << endl;
        }
        return true;
    }

    if (m_chunk_manager->is_complete() && (m_total_size == 0 || m_chunk_manager->get_total_downloaded_bytes() >= m_total_size)) {
        m_thread_pool.reset();
        m_file_writer->close();
        ResumeStore::remove(m_sidecar_filepath);

        if(m_antivirus_scan_enabled && !m_security_override){
            set_status_message("🛡️ Scanning with Windows Defender...");
            if (callback) {
                callback(*m_progress_tracker, "Scanning with Windows Defender...");
            }
            AntivirusScanner::apply_mark_of_the_web(m_destination_filepath, m_url);

            FileScanResult scan_res = AntivirusScanner::scan_file(m_destination_filepath);
            set_status_message("");
            if (scan_res.verdict == FileScanVerdict::Infected) {
                set_last_error("Security Warning: Threat detected (" + (scan_res.threat_name.empty() ? "Virus:DOS/EICAR_Test_File" : scan_res.threat_name) + ")");
                m_status = DownloadStatus::Failed;
                cout << "[DownloadTask Security Alert] Threat detected in downloaded file: " << scan_res.threat_name << endl;
                if (callback) {
                    callback(*m_progress_tracker, "Security Alert");
                }
                return false;
            }
        }

        set_status_message("");
        m_status = DownloadStatus::Completed;
        if (callback) {
            m_progress_tracker->update(m_total_size);
            callback(*m_progress_tracker, "Completed");
        }
        cout << "[DownloadTask] Download completed successfully!" << endl;
        return true;
    }

    m_status = DownloadStatus::Failed;
    m_last_error = "Download ended prematurely before reaching 100%";
    return false;
}

void DownloadTask::set_cache_directory(const string &cache_directory){
    std::lock_guard<std::mutex> lock(m_string_mutex);
    m_cache_directory = cache_directory;
    m_sidecar_filepath = ResumeStore::get_sidecar_filepath(m_destination_filepath, m_cache_directory);
}

string DownloadTask::get_cache_directory() const {
    std::lock_guard<std::mutex> lock(m_string_mutex);
    return m_cache_directory;
}

string DownloadTask::get_sidecar_filepath() const {
    std::lock_guard<std::mutex> lock(m_string_mutex);
    return m_sidecar_filepath;
}

void DownloadTask::pause() {
    m_pause_requested = true;
    if (m_status == DownloadStatus::NotStarted || m_status == DownloadStatus::Downloading || m_status == DownloadStatus::Probing) {
        m_status = DownloadStatus::Paused;
    }
}

void DownloadTask::resume() {
    m_pause_requested = false;
    if (m_status == DownloadStatus::Paused || m_status == DownloadStatus::Failed || m_status == DownloadStatus::Cancelled) {
        m_status = DownloadStatus::NotStarted;
    }
}

void DownloadTask::cancel() {
    pause();
    m_status = DownloadStatus::Cancelled;
}

void DownloadTask::clear_cache_and_partial_file() {
    pause();
    // Bug #14 Fix: Synchronize with m_run_mutex and destroy thread pool so active workers join
    // before closing file handles or deleting partial files from disk.
    std::unique_lock<std::mutex> run_lock(m_run_mutex);
    if (m_thread_pool) {
        m_thread_pool.reset();
    }
    if (m_file_writer) {
        m_file_writer->close();
    }
    std::error_code ec;
    std::string dest, sidecar;
    {
        std::lock_guard<std::mutex> slock(m_string_mutex);
        dest = m_destination_filepath;
        sidecar = m_sidecar_filepath;
    }
    if (!dest.empty()) {
        std::filesystem::remove(dest, ec);
    }
    if (!sidecar.empty()) {
        ResumeStore::remove(sidecar);
    }
    m_chunk_manager.reset();
    m_progress_tracker.reset();
    m_status = DownloadStatus::Cancelled;
}

DownloadStatus DownloadTask::get_status() const {
    return m_status.load();
}

string DownloadTask::get_status_message() const {
    std::lock_guard<std::mutex> lock(m_string_mutex);
    return m_status_message;
}

void DownloadTask::set_status_message(const string &msg) {
    std::lock_guard<std::mutex> lock(m_string_mutex);
    m_status_message = msg;
}

string DownloadTask::get_last_error() const {
    std::lock_guard<std::mutex> lock(m_string_mutex);
    return m_last_error;
}

void DownloadTask::set_last_error(const string &err) {
    std::lock_guard<std::mutex> lock(m_string_mutex);
    m_last_error = err;
}

void DownloadTask::clear_last_error() {
    std::lock_guard<std::mutex> lock(m_string_mutex);
    m_last_error.clear();
}

string DownloadTask::get_queue_name() const {
    std::lock_guard<std::mutex> lock(m_string_mutex);
    return m_queue_name;
}

void DownloadTask::set_queue_name(const string &name) {
    std::lock_guard<std::mutex> lock(m_string_mutex);
    m_queue_name = name;
}

bool DownloadTask::is_antivirus_scan_enabled() const {
    return m_antivirus_scan_enabled;
}

void DownloadTask::set_antivirus_scan_enabled(bool enabled){
    m_antivirus_scan_enabled = enabled;
}

uint64_t DownloadTask::get_total_size() const {
    return m_total_size;
}

uint64_t DownloadTask::get_downloaded_size() const {
    if (m_status == DownloadStatus::Completed && m_total_size > 0) {
        return m_total_size;
    }
    if (m_chunk_manager) {
        return m_chunk_manager->get_total_downloaded_bytes();
    }
    return 0;
}

double DownloadTask::get_progress_pct() const {
    if (m_status == DownloadStatus::Completed) {
        return 100.0;
    }
    if (m_total_size > 0 && m_chunk_manager) {
        return std::min(100.0, (m_chunk_manager->get_total_downloaded_bytes() * 100.0) / m_total_size);
    }
    return 0.0;
}

double DownloadTask::get_speed_bps() const {
    if (m_status == DownloadStatus::Downloading && m_progress_tracker) {
        return m_progress_tracker->get_speed_bytes_per_sec();
    }
    return 0.0;
}

std::string DownloadTask::get_eta() const {
    if (m_status == DownloadStatus::Downloading && m_progress_tracker) {
        return m_progress_tracker->get_formatted_eta();
    }
    if (m_status == DownloadStatus::Completed) {
        return "Done";
    }
    return "--";
}

std::vector<Chunk> DownloadTask::get_chunks() const {
    if (m_chunk_manager) {
        return m_chunk_manager->get_all_chunks();
    }
    return {};
}

void DownloadTask::override_security_and_complete(bool whitelist_permanently) {
    m_security_override = true;
    if (whitelist_permanently) {
        std::string fhash = AntivirusScanner::calculate_file_sha256(m_destination_filepath);
        if (!fhash.empty()) {
            AntivirusScanner::add_hash_to_whitelist(fhash, "User override");
        }
        std::string domain = AntivirusScanner::extract_domain_from_url(m_url);
        if (!domain.empty()) {
            AntivirusScanner::add_domain_to_whitelist(domain, "User override");
        }
    }
    m_status = DownloadStatus::Completed;
    clear_last_error();
}

bool DownloadTask::is_destination_file_present() const {
    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(m_string_mutex);
    if (m_last_file_check_time.time_since_epoch().count() != 0 &&
        std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last_file_check_time).count() < 2000) {
        return m_file_present_cached.load();
    }
    m_last_file_check_time = now;
    if (m_destination_filepath.empty()) {
        m_file_present_cached.store(false);
        return false;
    }
    std::error_code ec;
    bool exists = std::filesystem::exists(m_destination_filepath, ec);
    m_file_present_cached.store(exists);
    return exists;
}

std::chrono::system_clock::time_point DownloadTask::get_last_try_time() const {
    std::lock_guard<std::mutex> lock(m_string_mutex);
    return m_last_try_time;
}

void DownloadTask::update_last_try_time() {
    std::lock_guard<std::mutex> lock(m_string_mutex);
    m_last_try_time = std::chrono::system_clock::now();
}

