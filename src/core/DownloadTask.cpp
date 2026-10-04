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
#include "ChunkWorker.h"
#include <iostream>
#include <thread>
#include <chrono>

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
      m_destination_filepath(destination_filepath),
      m_cache_directory(cache_directory),
      m_num_threads(num_threads),
      m_max_bytes_per_sec(max_bytes_per_sec),
      m_referer(referer),
      m_cookies(cookies) {
    if (!user_agent.empty()) {
        m_user_agent = user_agent;
    }
    m_sidecar_filepath = ResumeStore::get_sidecar_filepath(m_destination_filepath, m_cache_directory);
}

DownloadTask::~DownloadTask() {
    pause();
}

void DownloadTask::set_speed_limit(uint64_t max_bytes_per_sec) {
    m_max_bytes_per_sec.store(max_bytes_per_sec);
}

uint64_t DownloadTask::get_speed_limit() const {
    return m_max_bytes_per_sec.load();
}

uint64_t DownloadTask::get_available_disk_space(const string &filepath) {
#ifdef _WIN32
    namespace fs = std::filesystem;
    try {
        std::error_code ec;
        fs::path p = fs::absolute(filepath, ec);
        if (ec) {
            return 0;
        }

        fs::path dir = p.parent_path();
        while (!dir.empty() && !fs::exists(dir, ec)) {
            fs::path parent = dir.parent_path();
            if (parent == dir || parent.empty()) {
                return 0;
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
        return 0;
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
    m_referer = referer;
    m_cookies = cookies;
    if (!user_agent.empty()) {
        m_user_agent = user_agent;
    }
}

string DownloadTask::get_url() const {
    return m_url;
}

string DownloadTask::get_referer() const {
    return m_referer;
}

string DownloadTask::get_cookies() const {
    return m_cookies;
}

string DownloadTask::get_user_agent() const {
    return m_user_agent;
}

string DownloadTask::get_destination_filepath() const {
    return m_destination_filepath;
}

string DownloadTask::extract_filename_from_url(const string &url){
    if(url.empty()) return "download.bin";

    string clean =url;
    size_t q_pos = clean.find('?');
    if(q_pos != string::npos) clean = clean.substr(0, q_pos);
    size_t h_pos = clean.find('#');
    if (h_pos != string::npos) clean = clean.substr(0, h_pos);

    while (clean.length() > 1 && (clean.back() == '/' || clean.back() == '\\')){
        clean.pop_back();
    }
    size_t slash_pos = clean.find_last_of("/\\");
    if(slash_pos != string::npos && slash_pos + 1 < clean.length()){
        string name = clean.substr(slash_pos + 1);
        if(!name.empty()) return name;
    }
    return "download.bin";
}

string DownloadTask::extract_filename_from_content_disposition(const string &header){
    string lower_header = header;
    for(char &c: lower_header) c= static_cast<char> (tolower(static_cast<unsigned char> (c)));
    size_t pos = lower_header.find("filename=");
    if(pos == string::npos) return "";

    size_t start = pos + 9;
    if(start >= header.length()) return "";

    if(header[start] == '"'){
        start++;
        size_t end = header.find('"', start);
        if(end != string::npos){
            return header.substr(start, end-start);
        }
    }
    else {
        size_t end = header.find_first_of(";\r\n", start);
        if(end == string::npos) end = header.length();
        return header.substr(start, end - start);
    }
    return "";
}

bool DownloadTask::update_url(const string &new_url) {
    if (new_url.empty()) {
        m_last_error = "Cannot update to empty URL";
        return false;
    }

    if (m_status == DownloadStatus::Downloading) {
        m_last_error = "Cannot update URL while download is active. Pause the task first.";
        return false;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        m_last_error = "Failed to initialize libcurl handle for URL validation";
        return false;
    }

    string response_headers;
    curl_easy_setopt(curl, CURLOPT_URL, new_url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, m_user_agent.c_str());
    if (!m_referer.empty()) {
        curl_easy_setopt(curl, CURLOPT_REFERER, m_referer.c_str());
    }
    if (!m_cookies.empty()) {
        curl_easy_setopt(curl, CURLOPT_COOKIE, m_cookies.c_str());
    }
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response_headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        m_last_error = string("Validation of refreshed URL failed: ") + curl_easy_strerror(res);
        curl_easy_cleanup(curl);
        return false;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code >= 400) {
        m_last_error = "Server rejected refreshed URL with HTTP " + to_string(http_code);
        curl_easy_cleanup(curl);
        return false;
    }

    curl_off_t cl = 0;
    curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &cl);
    curl_easy_cleanup(curl);

    if (m_total_size > 0 && cl > 0 && static_cast<uint64_t>(cl) != m_total_size) {
        m_last_error = "Refreshed URL size mismatch: expected " + to_string(m_total_size) +
                       " bytes, server returned " + to_string(cl) + " bytes";
        return false;
    }

    m_url = new_url;

    if (m_chunk_manager && ResumeStore::exists(m_sidecar_filepath)) {
        ResumeStore::save(m_sidecar_filepath, m_url, m_total_size, m_etag, *m_chunk_manager);
    }

    if (m_status == DownloadStatus::Failed) {
        m_status = DownloadStatus::Paused;
        m_last_error.clear();
    }

    return true;
}

bool DownloadTask::probe_url() {
    m_status = DownloadStatus::Probing;
    CURL *curl = curl_easy_init();
    if (!curl) {
        m_last_error = "Failed to initialize libcurl handle for URL probing";
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
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response_headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        m_last_error = string("HTTP HEAD probe failed: ") + curl_easy_strerror(res);
        curl_easy_cleanup(curl);
        m_status = DownloadStatus::Failed;
        return false;
    }

    curl_off_t cl = 0;
    curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &cl);
    if (cl > 0) {
        m_total_size = static_cast<uint64_t>(cl);
    }

    if (m_total_size == 0) {
        size_t cl_pos = response_headers.find("Content-Length: ");
        if (cl_pos == string::npos) cl_pos = response_headers.find("content-length: ");
        if (cl_pos != string::npos) {
            size_t end_line = response_headers.find("\r\n", cl_pos);
            string cl_str = response_headers.substr(cl_pos + 16, end_line - (cl_pos + 16));
            try {
                m_total_size = std::stoull(cl_str);
            } catch (...) {}
        }
    }

    curl_easy_cleanup(curl);

    if (response_headers.find("Accept-Ranges: bytes") != string::npos || 
        response_headers.find("accept-ranges: bytes") != string::npos) {
        m_supports_ranges = true;
    } else if (response_headers.find("Accept-Ranges: none") != string::npos || 
               response_headers.find("accept-ranges: none") != string::npos) {
        m_supports_ranges = false;
    }

    size_t etag_pos = response_headers.find("ETag: ");
    if (etag_pos == string::npos) etag_pos = response_headers.find("etag: ");
    if (etag_pos != string::npos) {
        size_t end_line = response_headers.find("\r\n", etag_pos);
        m_etag = response_headers.substr(etag_pos + 6, end_line - (etag_pos + 6));
    }

    return true;
}

bool DownloadTask::start(ProgressCallback callback) {
    m_pause_requested = false;

    bool is_resuming = false;
    m_chunk_manager = make_unique<ChunkManager>();

    if (ResumeStore::exists(m_sidecar_filepath)) {
        string saved_url, saved_etag;
        uint64_t saved_size = 0;
        if (ResumeStore::load(m_sidecar_filepath, saved_url, saved_size, saved_etag, *m_chunk_manager)) {
            if (saved_url == m_url && saved_size > 0) {
                is_resuming = true;
                m_total_size = saved_size;
                m_etag = saved_etag;
                cout << "[DownloadTask] Resuming existing download state from sidecar!" << endl;
            }
        }
    }

    if (!is_resuming) {
        cout << "[DownloadTask] Probing server URL..." << endl;
        if (!probe_url()) {
            return false;
        }

        if (m_total_size == 0) {
            m_last_error = "Server returned invalid file size (0 bytes)";
            m_status = DownloadStatus::Failed;
            return false;
        }

        int chunks_to_use = m_supports_ranges ? m_num_threads : 1;
        m_chunk_manager->init(m_total_size, chunks_to_use);
    }

    if (!has_sufficient_disk_space(m_total_size)) {
        uint64_t available = get_available_disk_space(m_destination_filepath);
        m_last_error = "Insufficient disk space: required " + to_string(m_total_size) +
                       " bytes, available " + to_string(available) + " bytes";
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
        m_destination_filepath = FileWriter::generate_unique_filepath(m_destination_filepath);
        m_sidecar_filepath = ResumeStore::get_sidecar_filepath(m_destination_filepath, m_cache_directory);
    }

    FileWriter::preallocate_file(m_destination_filepath, m_total_size);
    m_file_writer = make_unique<FileWriter>();
    if (!m_file_writer->open(m_destination_filepath)) {
        m_last_error = "Failed to create/open destination file on disk";
        m_status = DownloadStatus::Failed;
        return false;
    }

    m_progress_tracker = make_unique<ProgressTracker>(m_total_size);
    m_thread_pool = make_unique<ThreadPool>(m_supports_ranges ? m_num_threads : 1);

    m_status = DownloadStatus::Downloading;

    int worker_count = m_supports_ranges ? m_num_threads : 1;
    for (int i = 0; i < worker_count; ++i) {
        m_thread_pool->enqueue([this, worker_count]() {
            while (!m_pause_requested && !m_chunk_manager->is_complete()) {
                auto chunk_opt = m_chunk_manager->get_next_pending_chunk();
                if (!chunk_opt.has_value()) {
                    chunk_opt = m_chunk_manager->steal_slowest_chunk();
                    if(!chunk_opt.has_value()){
                        break;
                    }
                }

                Chunk chunk = chunk_opt.value();

                uint64_t total_limit = m_max_bytes_per_sec.load();
                uint64_t worker_speed = (worker_count > 0 && total_limit > 0) ? (total_limit / worker_count) : total_limit;

                ChunkWorker worker(m_url, chunk, *m_chunk_manager, *m_file_writer, worker_speed, m_referer, m_cookies, m_user_agent);

                bool worker_ok = worker.download();
                if (!worker_ok) {
                    m_chunk_manager->mark_chunk_failed(chunk.id);
                }
            }
        });
    }

    while (!m_pause_requested && !m_chunk_manager->is_complete()) {
        uint64_t downloaded = m_chunk_manager->get_total_downloaded_bytes();
        m_progress_tracker->update(downloaded);

        if (callback) {
            callback(*m_progress_tracker, "Downloading");
        }
        ResumeStore::save(m_sidecar_filepath, m_url, m_total_size, m_etag, *m_chunk_manager);

        this_thread::sleep_for(chrono::milliseconds(200));
    }

    if (m_pause_requested) {
        m_status = DownloadStatus::Paused;
        m_thread_pool.reset();
        m_file_writer->close();
        ResumeStore::save(m_sidecar_filepath, m_url, m_total_size, m_etag, *m_chunk_manager);
        cout << "[DownloadTask] Download paused successfully." << endl;
        return true;
    }

    if (m_chunk_manager->is_complete()) {
        m_status = DownloadStatus::Completed;
        m_thread_pool.reset();
        m_file_writer->close();
        ResumeStore::remove(m_sidecar_filepath);
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
    m_cache_directory = cache_directory;
    m_sidecar_filepath = ResumeStore::get_sidecar_filepath(m_destination_filepath, m_cache_directory);
}

string DownloadTask::get_cache_directory() const {
    return m_cache_directory;
}

string DownloadTask::get_sidecar_filepath() const {
    return m_sidecar_filepath;
}

void DownloadTask::pause() {
    m_pause_requested = true;
}

void DownloadTask::cancel() {
    pause();
    ResumeStore::remove(m_sidecar_filepath);
    m_status = DownloadStatus::Failed;
}

DownloadStatus DownloadTask::get_status() const {
    return m_status.load();
}

string DownloadTask::get_last_error() const {
    return m_last_error;
}
