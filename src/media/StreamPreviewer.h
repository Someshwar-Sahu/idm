#ifndef STREAM_PREVIEWER_H
#define STREAM_PREVIEWER_H

#include "../core/Chunk.h"
#include "../ipc/LocalServer.h"

#include <string>
#include <vector>
#include <cstdint>

using namespace std;

class DownloadQueue;

namespace idm {

enum class MediaContainerType {
    Unknown,
    MP4,
    MKV,
    WebM,
    MP3,
    WAV,
    FLV,
    AAC
};

struct PreviewStatus {
    bool is_ready{false};
    MediaContainerType container{MediaContainerType::Unknown};
    string mime_type{"application/octet-stream"};
    uint64_t playable_bytes{0};
    uint64_t total_file_size{0};
    double playable_percent{0.0};
    bool has_faststart{false};
    string details;
};

class StreamPreviewer {
public:
    StreamPreviewer() = default;
    ~StreamPreviewer() = default;

    // Container & MIME Detection
    static MediaContainerType detect_container_from_buffer(const uint8_t* data, size_t size);
    static MediaContainerType detect_container_from_file(const string& filepath);
    static string container_to_mime_type(MediaContainerType container);
    static string detect_mime_type(const string& filepath);

    // MP4 FastStart (moov atom) analysis
    static bool has_faststart_moov(const uint8_t* data, size_t size);
    static bool has_faststart_moov(const string& filepath);

    // Contiguous Playable Range Calculation
    static uint64_t calculate_contiguous_playable_bytes(const vector<Chunk>& chunks);

    // Preview Readiness Assessment
    static PreviewStatus assess_preview_readiness(const string& filepath,
                                                 uint64_t total_file_size,
                                                 const vector<Chunk>& chunks,
                                                 uint64_t min_buffer_bytes = 512 * 1024);

    // Shared Non-Exclusive File Range Slicing
    static bool read_shared_slice(const string& filepath,
                                  uint64_t offset,
                                  size_t length,
                                  vector<uint8_t>& out_buffer);

    // HTTP 206 Partial Content Generation
    static HttpResponse handle_preview_request(const HttpRequest& req,
                                              const string& filepath,
                                              uint64_t total_file_size,
                                              uint64_t playable_bytes);

    // LocalServer Route Integration
    static void attach_to_server(LocalServer& server, DownloadQueue* queue, const string& route = "/api/preview");
};

} // namespace idm

#endif // STREAM_PREVIEWER_H
