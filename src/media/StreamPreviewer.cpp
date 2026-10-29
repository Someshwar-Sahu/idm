#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include "StreamPreviewer.h"
#include "../queue/DownloadQueue.h"

#include <algorithm>
#include <cstring>

using namespace std;

namespace idm {

MediaContainerType StreamPreviewer::detect_container_from_buffer(const uint8_t* data, size_t size) {
    if (!data || size < 4) return MediaContainerType::Unknown;

    // 1. MP4 / MOV / M4V (ISO Base Media): Box header [length (4 bytes)][box type (4 bytes)]
    // Usually starts with ftyp at offset 4
    if (size >= 8) {
        if (memcmp(data + 4, "ftyp", 4) == 0 ||
            memcmp(data + 4, "moov", 4) == 0 ||
            memcmp(data + 4, "mdat", 4) == 0) {
            return MediaContainerType::MP4;
        }
    }

    // 2. MKV / WebM: Starts with EBML ID 0x1A 0x45 0xDF 0xA3
    if (size >= 4 && data[0] == 0x1A && data[1] == 0x45 && data[2] == 0xDF && data[3] == 0xA3) {
        // Distinguish WebM vs MKV if "webm" string is found in first 64 bytes
        size_t probe_len = (size < 128) ? size : 128;
        for (size_t i = 0; i + 4 <= probe_len; ++i) {
            if (memcmp(data + i, "webm", 4) == 0) {
                return MediaContainerType::WebM;
            }
        }
        return MediaContainerType::MKV;
    }

    // 3. WAV: "RIFF" .... "WAVE"
    if (size >= 12 && memcmp(data, "RIFF", 4) == 0 && memcmp(data + 8, "WAVE", 4) == 0) {
        return MediaContainerType::WAV;
    }

    // 4. FLV: "FLV\x01"
    if (size >= 4 && memcmp(data, "FLV", 3) == 0) {
        return MediaContainerType::FLV;
    }

    // 5. MP3: Starts with ID3 tag "ID3" or MPEG audio sync word (11 bits set: 0xFF 0xFB/FA/F3/F2)
    if (size >= 3 && memcmp(data, "ID3", 3) == 0) {
        return MediaContainerType::MP3;
    }
    if (size >= 2 && data[0] == 0xFF && (data[1] & 0xE0) == 0xE0) {
        return MediaContainerType::MP3;
    }

    // 6. AAC: ADTS sync word 0xFF 0xF1 or 0xFF 0xF9
    if (size >= 2 && data[0] == 0xFF && (data[1] == 0xF1 || data[1] == 0xF9)) {
        return MediaContainerType::AAC;
    }

    return MediaContainerType::Unknown;
}

MediaContainerType StreamPreviewer::detect_container_from_file(const string& filepath) {
    vector<uint8_t> buffer(256);
    if (!read_shared_slice(filepath, 0, buffer.size(), buffer)) {
        return MediaContainerType::Unknown;
    }
    return detect_container_from_buffer(buffer.data(), buffer.size());
}

string StreamPreviewer::container_to_mime_type(MediaContainerType container) {
    switch (container) {
        case MediaContainerType::MP4: return "video/mp4";
        case MediaContainerType::WebM: return "video/webm";
        case MediaContainerType::MKV: return "video/x-matroska";
        case MediaContainerType::MP3: return "audio/mpeg";
        case MediaContainerType::WAV: return "audio/wav";
        case MediaContainerType::FLV: return "video/x-flv";
        case MediaContainerType::AAC: return "audio/aac";
        case MediaContainerType::Unknown: return "application/octet-stream";
    }
    return "application/octet-stream";
}

string StreamPreviewer::detect_mime_type(const string& filepath) {
    MediaContainerType c = detect_container_from_file(filepath);
    if (c != MediaContainerType::Unknown) {
        return container_to_mime_type(c);
    }

    // Fallback to extension check
    size_t dot_pos = filepath.rfind('.');
    if (dot_pos != string::npos) {
        string ext = filepath.substr(dot_pos + 1);
        for (char& ch : ext) ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
        if (ext == "mp4" || ext == "m4v") return "video/mp4";
        if (ext == "webm") return "video/webm";
        if (ext == "mkv") return "video/x-matroska";
        if (ext == "mp3") return "audio/mpeg";
        if (ext == "wav") return "audio/wav";
        if (ext == "flv") return "video/x-flv";
        if (ext == "aac" || ext == "m4a") return "audio/aac";
        if (ext == "ogg" || ext == "ogv") return "video/ogg";
    }
    return "application/octet-stream";
}

bool StreamPreviewer::has_faststart_moov(const uint8_t* data, size_t size) {
    if (!data || size < 16) return false;

    size_t offset = 0;
    bool found_moov = false;
    bool found_mdat = false;

    while (offset + 8 <= size) {
        uint32_t box_size = (static_cast<uint32_t>(data[offset]) << 24) |
                            (static_cast<uint32_t>(data[offset + 1]) << 16) |
                            (static_cast<uint32_t>(data[offset + 2]) << 8) |
                            static_cast<uint32_t>(data[offset + 3]);

        char box_type[5] = {0};
        memcpy(box_type, data + offset + 4, 4);

        if (strcmp(box_type, "moov") == 0) {
            found_moov = true;
            if (!found_mdat) {
                return true; // moov is located before mdat! FastStart verified!
            }
        } else if (strcmp(box_type, "mdat") == 0) {
            found_mdat = true;
        }

        if (box_size == 0) break; // Box extends to end of file
        if (box_size == 1) {
            // Extended 64-bit size
            if (offset + 16 > size) break;
            uint64_t ext_size = 0;
            for (int b = 0; b < 8; ++b) {
                ext_size = (ext_size << 8) | data[offset + 8 + b];
            }
            if (ext_size == 0) break;
            offset += ext_size;
        } else {
            offset += box_size;
        }
    }

    return (found_moov && !found_mdat);
}

bool StreamPreviewer::has_faststart_moov(const string& filepath) {
    // Read first 2 MB to locate headers
    vector<uint8_t> buffer(2 * 1024 * 1024);
    if (!read_shared_slice(filepath, 0, buffer.size(), buffer)) {
        return false;
    }
    return has_faststart_moov(buffer.data(), buffer.size());
}

uint64_t StreamPreviewer::calculate_contiguous_playable_bytes(const vector<Chunk>& chunks) {
    if (chunks.empty()) return 0;

    // Sort chunks by start_byte
    vector<Chunk> sorted = chunks;
    sort(sorted.begin(), sorted.end(), [](const Chunk& a, const Chunk& b) {
        return a.start_byte < b.start_byte;
    });

    uint64_t contiguous_end = 0;
    for (const auto& c : sorted) {
        if (c.start_byte > contiguous_end) {
            // Gap detected!
            break;
        }
        uint64_t chunk_available = c.start_byte + c.bytes_downloaded;
        if (chunk_available > contiguous_end) {
            contiguous_end = chunk_available;
        }

        if (c.state != ChunkState::Done && c.bytes_downloaded < c.total_bytes()) {
            // This chunk is incomplete; cannot bridge further
            break;
        }
    }

    return contiguous_end;
}

PreviewStatus StreamPreviewer::assess_preview_readiness(const string& filepath,
                                                       uint64_t total_file_size,
                                                       const vector<Chunk>& chunks,
                                                       uint64_t min_buffer_bytes) {
    PreviewStatus status;
    status.total_file_size = total_file_size;
    status.playable_bytes = calculate_contiguous_playable_bytes(chunks);
    status.playable_percent = (total_file_size > 0) ? (static_cast<double>(status.playable_bytes) / total_file_size * 100.0) : 0.0;

    status.container = detect_container_from_file(filepath);
    status.mime_type = detect_mime_type(filepath);

    if (status.container == MediaContainerType::MP4) {
        status.has_faststart = has_faststart_moov(filepath);
    } else if (status.container != MediaContainerType::Unknown) {
        status.has_faststart = true; // Audio formats, WebM, and MKV naturally stream linearly from byte 0
    }

    // Readiness threshold:
    // 1. Container must be identified (or file 100% complete)
    // 2. Contiguous playable bytes >= min_buffer_bytes (or 100% for tiny files)
    // 3. If MP4: must have faststart moov at the beginning
    bool meets_buffer = (status.playable_bytes >= min_buffer_bytes) || (status.playable_bytes == total_file_size && total_file_size > 0);
    bool container_ok = (status.container != MediaContainerType::Unknown);
    bool moov_ok = (status.container != MediaContainerType::MP4 || status.has_faststart);

    if (meets_buffer && container_ok && moov_ok) {
        status.is_ready = true;
        status.details = "Ready for instant stream preview (" + to_string(status.playable_bytes / 1024) + " KB buffered)";
    } else {
        status.is_ready = false;
        if (!container_ok) {
            status.details = "Waiting for initial container header to download...";
        } else if (!moov_ok) {
            status.details = "MP4 container requires moov atom (fast-start header) before playback";
        } else {
            status.details = "Buffering... (" + to_string(status.playable_bytes / 1024) + " KB of " +
                             to_string(min_buffer_bytes / 1024) + " KB needed)";
        }
    }

    return status;
}

bool StreamPreviewer::read_shared_slice(const string& filepath,
                                       uint64_t offset,
                                       size_t length,
                                       vector<uint8_t>& out_buffer) {
    if (filepath.empty() || length == 0) return false;

#ifdef _WIN32
    HANDLE hFile = CreateFileA(
        filepath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER li;
    li.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(hFile, li, nullptr, FILE_BEGIN)) {
        CloseHandle(hFile);
        return false;
    }

    out_buffer.resize(length);
    DWORD bytes_read = 0;
    BOOL ok = ReadFile(hFile, out_buffer.data(), static_cast<DWORD>(length), &bytes_read, nullptr);
    CloseHandle(hFile);

    if (!ok) return false;
    out_buffer.resize(bytes_read);
    return !out_buffer.empty();
#else
    int fd = ::open(filepath.c_str(), O_RDONLY);
    if (fd < 0) return false;

    out_buffer.resize(length);
    ssize_t bytes_read = ::pread(fd, out_buffer.data(), length, static_cast<off_t>(offset));
    ::close(fd);

    if (bytes_read <= 0) return false;
    out_buffer.resize(bytes_read);
    return true;
#endif
}

HttpResponse StreamPreviewer::handle_preview_request(const HttpRequest& req,
                                                    const string& filepath,
                                                    uint64_t total_file_size,
                                                    uint64_t playable_bytes) {
    if (playable_bytes == 0) {
        return HttpResponse::error_response(404, "No playable data available yet");
    }

    string mime_type = detect_mime_type(filepath);

    uint64_t range_start = 0;
    uint64_t range_end = playable_bytes - 1;

    // Check for Range header: "Range: bytes=0-1024" or "bytes=512-"
    auto it = req.headers.find("range");
    if (it == req.headers.end()) {
        it = req.headers.find("Range");
    }

    bool is_range = false;
    if (it != req.headers.end()) {
        string range_val = it->second;
        size_t eq_pos = range_val.find('=');
        if (eq_pos != string::npos) {
            string spec = range_val.substr(eq_pos + 1);
            size_t dash_pos = spec.find('-');
            if (dash_pos != string::npos) {
                is_range = true;
                string s_str = spec.substr(0, dash_pos);
                string e_str = spec.substr(dash_pos + 1);
                try {
                    if (!s_str.empty()) range_start = stoull(s_str);
                    if (!e_str.empty()) range_end = stoull(e_str);
                } catch (...) {}
            }
        }
    }

    if (range_start >= playable_bytes) {
        HttpResponse res;
        res.status_code = 416; // Range Not Satisfiable
        res.status_message = "Range Not Satisfiable";
        res.headers["Content-Range"] = "bytes */" + to_string(total_file_size > 0 ? total_file_size : playable_bytes);
        return res;
    }

    if (range_end >= playable_bytes) {
        range_end = playable_bytes - 1; // Clamp to available buffer
    }

    size_t request_len = static_cast<size_t>(range_end - range_start + 1);
    // Limit single HTTP chunk to 2 MB to prevent memory exhaustion
    if (request_len > 2 * 1024 * 1024) {
        request_len = 2 * 1024 * 1024;
        range_end = range_start + request_len - 1;
    }

    vector<uint8_t> buffer;
    if (!read_shared_slice(filepath, range_start, request_len, buffer)) {
        return HttpResponse::error_response(500, "Failed to read media slice from disk");
    }

    HttpResponse res;
    res.status_code = is_range ? 206 : 200;
    res.status_message = is_range ? "Partial Content" : "OK";
    res.headers["Content-Type"] = mime_type;
    res.headers["Accept-Ranges"] = "bytes";
    res.headers["Content-Length"] = to_string(buffer.size());
    res.headers["Access-Control-Allow-Origin"] = "*";

    uint64_t full_size = (total_file_size > 0) ? total_file_size : playable_bytes;
    res.headers["Content-Range"] = "bytes " + to_string(range_start) + "-" +
                                   to_string(range_start + buffer.size() - 1) + "/" +
                                   to_string(full_size);

    res.body.assign(reinterpret_cast<const char*>(buffer.data()), buffer.size());
    return res;
}

void StreamPreviewer::attach_to_server(LocalServer& server, DownloadQueue* queue, const string& route) {
    server.register_route("GET", route, [queue](const HttpRequest& req) -> HttpResponse {
        auto it_id = req.query_params.find("id");
        string task_id = (it_id != req.query_params.end()) ? it_id->second : "";

        auto it_path = req.query_params.find("path");
        string path = (it_path != req.query_params.end()) ? it_path->second : "";

        if (task_id.empty() && path.empty()) {
            return HttpResponse::error_response(400, "Missing 'id' or 'path' query parameter");
        }

        string target_file = path;
        uint64_t total_size = 0;
        uint64_t playable_bytes = 0;
        std::shared_ptr<DownloadTask> matched_task = nullptr;

        if (queue) {
            for (const auto& t : queue->get_tasks()) {
                if (t) {
                    if (!task_id.empty() && (t->get_destination_filepath() == task_id || t->get_url() == task_id)) {
                        matched_task = t;
                        target_file = t->get_destination_filepath();
                        break;
                    }
                    if (!path.empty() && t->get_destination_filepath() == path) {
                        matched_task = t;
                        target_file = t->get_destination_filepath();
                        break;
                    }
                }
            }
        }

        if (target_file.empty()) {
            return HttpResponse::error_response(404, "Target media file path unavailable");
        }

        // Check file size on disk
        error_code ec;
        uint64_t disk_size = filesystem::file_size(target_file, ec);
        if (ec) {
            return HttpResponse::error_response(404, "Media file does not exist on disk");
        }

        if (matched_task) {
            total_size = matched_task->get_total_size();
            if (total_size == 0) total_size = disk_size;

            if (matched_task->get_status() == DownloadStatus::Completed) {
                playable_bytes = disk_size;
            } else {
                playable_bytes = calculate_contiguous_playable_bytes(matched_task->get_chunks());
            }
        } else {
            total_size = disk_size;
            playable_bytes = disk_size;
        }

        return handle_preview_request(req, target_file, total_size, playable_bytes);
    });
}

} // namespace idm
