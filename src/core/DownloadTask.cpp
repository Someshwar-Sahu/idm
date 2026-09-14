#include <curl/curl.h>
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

DownloadTask::DownloadTask(const string &url, const string &destination_filepath, int num_threads)
    : m_url(url),
      m_destination_filepath(destination_filepath),
      m_num_threads(num_threads) {
    m_sidecar_filepath = ResumeStore::get_sidecar_filepath(m_destination_filepath);
}

DownloadTask::~DownloadTask() {
    pause();
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
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "IDM_Engine/1.0");
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

    // Preallocate target file and open handle
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
        m_thread_pool->enqueue([this]() {
            while (!m_pause_requested && !m_chunk_manager->is_complete()) {
                auto chunk_opt = m_chunk_manager->get_next_pending_chunk();
                if (!chunk_opt.has_value()) {
                    break;
                }

                Chunk chunk = chunk_opt.value();
                ChunkWorker worker(m_url, chunk, *m_chunk_manager, *m_file_writer);

                bool worker_ok = worker.download();
                if (!worker_ok) {
                    m_chunk_manager->mark_chunk_failed(chunk.id);
                }
            }
        });
    }

    while (!m_chunk_manager->is_complete() && !m_pause_requested) {
        this_thread::sleep_for(chrono::milliseconds(200));

        uint64_t current_bytes = m_chunk_manager->get_total_downloaded_bytes();
        m_progress_tracker->update(current_bytes);

        ResumeStore::save(m_sidecar_filepath, m_url, m_total_size, m_etag, *m_chunk_manager);

        if (callback) {
            callback(*m_progress_tracker, "Downloading");
        }
    }

    if (m_pause_requested) {
        m_status = DownloadStatus::Paused;
        ResumeStore::save(m_sidecar_filepath, m_url, m_total_size, m_etag, *m_chunk_manager);
        cout << "[DownloadTask] Download paused successfully." << endl;
        return false;
    }

    if (m_chunk_manager->is_complete()) {
        m_status = DownloadStatus::Completed;
        ResumeStore::remove(m_sidecar_filepath);
        if (callback) {
            callback(*m_progress_tracker, "Completed");
        }
        cout << "[DownloadTask] Download completed successfully!" << endl;
        return true;
    }

    m_status = DownloadStatus::Failed;
    return false;
}

void DownloadTask::pause() {
    m_pause_requested = true;
}

void DownloadTask::cancel() {
    pause();
    ResumeStore::remove(m_sidecar_filepath);
}

DownloadStatus DownloadTask::get_status() const {
    return m_status;
}

string DownloadTask::get_last_error() const {
    return m_last_error;
}
