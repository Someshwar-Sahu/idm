#ifndef DOWNLOAD_TASK_H
#define DOWNLOAD_TASK_H

#include "ChunkManager.h"
#include "FileWriter.h"
#include "ThreadPool.h"
#include "ProgressTracker.h"
#include "ResumeStore.h"

#include <string>
#include <functional>
#include <atomic>
#include <memory>
#include <cstdint>

enum class DownloadStatus {
    NotStarted,
    Probing,
    Downloading,
    Paused,
    Completed,
    Failed
};

class DownloadTask {
public: 
    using ProgressCallback = std::function<void(const ProgressTracker&, const std::string&)>;

    DownloadTask(const std::string &url,
                 const std::string &destination_filepath,
                 int num_threads = 8,
                 uint64_t max_bytes_per_sec = 0,
                 const std::string &referer = "",
                 const std::string &cookies = "",
                 const std::string &user_agent = "",
                 const std::string &cache_directory = "");
    
    ~DownloadTask();

    bool start(ProgressCallback callback = nullptr);

    void pause();
    void cancel();

    void set_speed_limit(uint64_t max_bytes_per_sec);
    uint64_t get_speed_limit() const;

    static uint64_t get_available_disk_space(const std::string &filepath);
    bool has_sufficient_disk_space(uint64_t required_bytes) const;

    bool update_url(const std::string &new_url);
    void set_http_context(const std::string &referer, const std::string &cookies, const std::string &user_agent = "");

    std::string get_url() const;
    std::string get_referer() const;
    std::string get_cookies() const;
    std::string get_user_agent() const;

    void set_cache_directory(const std::string &cache_directory);
    std::string get_cache_directory() const;
    std::string get_sidecar_filepath() const;
    std::string get_destination_filepath() const;

    static std::string extract_filename_from_url(const std::string &url);
    static std::string extract_filename_from_content_disposition(const std::string &header_value);

    DownloadStatus get_status() const;
    std::string get_last_error() const;
    bool is_antivirus_scan_enabled() const;
    void set_antivirus_scan_enabled(bool enabled);

private:
    bool probe_url();

    std::string m_url;
    std::string m_destination_filepath;
    std::string m_cache_directory;
    std::string m_sidecar_filepath;
    int m_num_threads{8};
    std::atomic<uint64_t> m_max_bytes_per_sec{0};

    std::string m_referer;
    std::string m_cookies;
    std::string m_user_agent{"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36"};

    uint64_t m_total_size{0};
    std::string m_etag;
    bool m_supports_ranges{false};

    std::atomic<DownloadStatus> m_status{DownloadStatus::NotStarted};
    std::string m_last_error;

    std::unique_ptr<ChunkManager> m_chunk_manager;
    std::unique_ptr<FileWriter> m_file_writer;
    std::unique_ptr<ThreadPool> m_thread_pool;
    std::unique_ptr<ProgressTracker> m_progress_tracker;

    std::atomic<bool> m_pause_requested{false};

    bool m_antivirus_scan_enabled{true};
};

#endif