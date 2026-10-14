#ifndef MEDIA_EXTRACTOR_H
#define MEDIA_EXTRACTOR_H

#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <atomic>
#include <mutex>

using namespace std;

namespace idm {

struct MediaFormat {
    string format_id;
    string extension;   // mp4, webm, m4a, etc.
    int width{0};
    int height{0};           // 2160, 1080, 720, etc.
    int fps{0};              // 60, 30, etc.
    string vcodec;      // avc1, vp9, av01, none
    string acodec;      // mp4a, opus, none
    double tbr{0.0};         // Total bitrate in kbps
    double abr{0.0};         // Audio bitrate in kbps
    double vbr{0.0};         // Video bitrate in kbps
    uint64_t filesize{0};    // Bytes (0 if unknown/stream)
    string format_note; // e.g. "1080p60", "audio only"
    bool is_video{false};
    bool is_audio{false};
};

struct MediaMetadata {
    string id;
    string title;
    string uploader;
    string description;
    uint32_t duration_sec{0};
    string thumbnail_url;
    string webpage_url;
    vector<MediaFormat> formats;
    bool is_valid{false};
    string raw_json;

    vector<int> get_available_resolutions() const;
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
    string status_text;
};

class MediaExtractor {
public:
    using ProgressCallback = function<void(const ExtractionProgress&)>;
    // Mock runner signature: (command, stdout_line_cb, stderr_line_cb) -> int (exit_code)
    using ProcessExecutor = function<int(const string&,
                                             function<void(const string&)>,
                                             function<void(const string&)>)>;

    MediaExtractor(const string& ytdlp_path = "", const string& ffmpeg_path = "");
    ~MediaExtractor();

    // URL Detection
    static bool is_supported_media_url(const string& url);

    // Executable paths & environment
    string get_ytdlp_path() const;
    string get_ffmpeg_path() const;
    void set_ytdlp_path(const string& path);
    void set_ffmpeg_path(const string& path);
    bool check_tools_installed(string* details = nullptr) const;

    // Metadata Extraction
    MediaMetadata parse_metadata_json(const string& json_str) const;
    MediaMetadata fetch_metadata(const string& url);

    // Command Line Generation
    string build_format_filter(MediaQualityPreset preset, const string& preferred_container = "mp4") const;
    string build_download_command(const string& url,
                                       const string& output_path,
                                       MediaQualityPreset preset,
                                       const string& custom_format = "") const;

    // Progress Parsing
    static ExtractionProgress parse_progress_line(const string& line, ExtractionStage current_stage);

    // Execution & Cancellation
    void set_process_executor(ProcessExecutor executor);
    bool extract_media(const string& url,
                       const string& output_path,
                       MediaQualityPreset preset,
                       ProgressCallback callback = nullptr,
                       const string& custom_format = "");
    void cancel();

    string get_last_error() const;

private:
    static string find_executable_in_system(const string& exe_name);

    string m_ytdlp_path;
    string m_ffmpeg_path;
    ProcessExecutor m_mock_executor{nullptr};

    atomic<bool> m_cancel_requested{false};
    string m_last_error;
    mutable mutex m_mutex;
};

} // namespace idm

#endif // MEDIA_EXTRACTOR_H
