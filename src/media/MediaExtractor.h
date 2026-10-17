#ifndef MEDIA_EXTRACTOR_H
#define MEDIA_EXTRACTOR_H

#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <atomic>
#include <mutex>

namespace idm {

struct MediaFormat {
    std::string format_id;
    std::string extension;   // mp4, webm, m4a, etc.
    int width{0};
    int height{0};           // 2160, 1080, 720, etc.
    int fps{0};              // 60, 30, etc.
    std::string vcodec;      // avc1, vp9, av01, none
    std::string acodec;      // mp4a, opus, none
    double tbr{0.0};         // Total bitrate in kbps
    double abr{0.0};         // Audio bitrate in kbps
    double vbr{0.0};         // Video bitrate in kbps
    uint64_t filesize{0};    // Bytes (0 if unknown/stream)
    std::string format_note; // e.g. "1080p60", "audio only"
    bool is_video{false};
    bool is_audio{false};
};

struct MediaMetadata {
    std::string id;
    std::string title;
    std::string uploader;
    std::string description;
    uint32_t duration_sec{0};
    std::string thumbnail_url;
    std::string webpage_url;
    std::vector<MediaFormat> formats;
    bool is_valid{false};
    std::string raw_json;

    std::vector<int> get_available_resolutions() const;
    const MediaFormat* get_best_video_format(int max_height = 0) const;
    const MediaFormat* get_best_audio_format() const;
};

enum class MediaQualityPreset {
    BestVideoAudio, // Highest available resolution & audio
    UHD_4K,         // 2160p
    QHD_1440p,      // 1440p
    FHD_1080p,      // 1080p
    HD_720p,        // 720p
    SD_480p,        // 480p
    AudioOnly_320k, // MP3 320 kbps
    AudioOnly_192k, // MP3 192 kbps
    AudioOnly_Best  // Best untouched audio stream
};

enum class ExtractionStage {
    Idle,
    ProbingMetadata,
    DownloadingVideo,
    DownloadingAudio,
    MergingStreams,
    Completed,
    Failed,
    Cancelled
};

struct ExtractionProgress {
    ExtractionStage stage{ExtractionStage::Idle};
    double percent{0.0};          // 0.0 to 100.0
    uint64_t downloaded_bytes{0};
    uint64_t total_bytes{0};
    double speed_bps{0.0};        // Bytes per second
    int eta_seconds{0};
    std::string status_text;
};

class MediaExtractor {
public:
    using ProgressCallback = std::function<void(const ExtractionProgress&)>;
    // Mock runner signature: (command, stdout_line_cb, stderr_line_cb) -> int (exit_code)
    using ProcessExecutor = std::function<int(const std::string&,
                                             std::function<void(const std::string&)>,
                                             std::function<void(const std::string&)>)>;

    MediaExtractor(const std::string& ytdlp_path = "", const std::string& ffmpeg_path = "");
    ~MediaExtractor();

    // URL Detection
    static bool is_supported_media_url(const std::string& url);

    // Executable paths & environment
    std::string get_ytdlp_path() const;
    std::string get_ffmpeg_path() const;
    void set_ytdlp_path(const std::string& path);
    void set_ffmpeg_path(const std::string& path);
    bool check_tools_installed(std::string* details = nullptr) const;

    // Metadata Extraction
    MediaMetadata parse_metadata_json(const std::string& json_str) const;
    MediaMetadata fetch_metadata(const std::string& url);

    // Command Line Generation
    std::string build_format_filter(MediaQualityPreset preset, const std::string& preferred_container = "mp4") const;
    std::string build_download_command(const std::string& url,
                                       const std::string& output_path,
                                       MediaQualityPreset preset,
                                       const std::string& custom_format = "") const;

    // Progress Parsing
    static ExtractionProgress parse_progress_line(const std::string& line, ExtractionStage current_stage);

    // Execution & Cancellation
    void set_process_executor(ProcessExecutor executor);
    bool extract_media(const std::string& url,
                       const std::string& output_path,
                       MediaQualityPreset preset,
                       ProgressCallback callback = nullptr,
                       const std::string& custom_format = "");
    void cancel();

    std::string get_last_error() const;

private:
    static std::string find_executable_in_system(const std::string& exe_name);

    std::string m_ytdlp_path;
    std::string m_ffmpeg_path;
    ProcessExecutor m_mock_executor{nullptr};

    std::atomic<bool> m_cancel_requested{false};
    std::string m_last_error;
    mutable std::mutex m_mutex;
};

} // namespace idm

#endif // MEDIA_EXTRACTOR_H
