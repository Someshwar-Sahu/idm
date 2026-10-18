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
#include <mutex>
#include <cstdint>

enum class DownloadStatus {
    NotStarted,
    Probing,
    Downloading,
    Paused,
    Completed,
    Failed,
    Cancelled
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
    void resume();
    void cancel();

    void set_speed_limit(uint64_t max_bytes_per_sec);
    uint64_t get_speed_limit() const;
    void set_thread_count(int count) { if (count > 0) m_num_threads = count; }
    int get_thread_count() const { return m_num_threads; }

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
    static std::string sanitize_filename(const std::string &filename);
    static std::string mime_to_extension(const std::string &mime);

    DownloadStatus get_status() const;
    void set_status(DownloadStatus status) { m_status = status; }
    std::string get_last_error() const;
    void clear_last_error() { m_last_error.clear(); }
    void set_last_error(const std::string &err) { m_last_error = err; }
    bool is_antivirus_scan_enabled() const;
    void set_antivirus_scan_enabled(bool enabled);
    bool is_security_override() const { return m_security_override; }
    void set_security_override(bool override) { m_security_override = override; }
    void override_security_and_complete();

    std::string get_queue_name() const { return m_queue_name; }
    void set_queue_name(const std::string &name) { m_queue_name = name; }

    uint64_t get_total_size() const;
    uint64_t get_downloaded_size() const;
    double get_progress_pct() const;
    double get_speed_bps() const;
    std::string get_eta() const;
    std::vector<Chunk> get_chunks() const;
    bool probe_url();
    bool supports_ranges() const { return m_supports_ranges; }

private:

    std::string m_url;
    std::string m_destination_filepath;
    std::string m_cache_directory;
    std::string m_sidecar_filepath;
    std::string m_queue_name{"Main Download Queue"};
    int m_num_threads{8};
    std::atomic<uint64_t> m_max_bytes_per_sec{0};

    std::string m_referer;
    std::string m_cookies;
    std::string m_user_agent{"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36"};

    uint64_t m_total_size{0};
    std::string m_etag;
    bool m_supports_ranges{true};

    std::atomic<DownloadStatus> m_status{DownloadStatus::NotStarted};
    std::string m_last_error;

    std::unique_ptr<ChunkManager> m_chunk_manager;
    std::unique_ptr<FileWriter> m_file_writer;
    std::unique_ptr<ThreadPool> m_thread_pool;
    std::unique_ptr<ProgressTracker> m_progress_tracker;

    std::atomic<bool> m_pause_requested{false};

    bool m_antivirus_scan_enabled{true};
    bool m_security_override{false};
    mutable std::mutex m_run_mutex;
};

#endif