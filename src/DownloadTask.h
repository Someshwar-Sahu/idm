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
                 int num_threads = 8);
    
    ~DownloadTask();

    bool start(ProgressCallback callback = nullptr);

    void pause();
    void cancel();

    DownloadStatus get_status() const;
    std::string get_last_error() const;

private:
    bool probe_url();

    std::string m_url;
    std::string m_destination_filepath;
    std::string m_sidecar_filepath;
    int m_num_threads{8};

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
};

#endif
