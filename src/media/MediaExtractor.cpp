#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

#include "MediaExtractor.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <sstream>
#include <filesystem>
#include <cstdio>
#include <cctype>

using namespace std;

namespace idm {

namespace {

string to_lower_str(string s) {
    for (char& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return s;
}

uint64_t parse_size_unit(double val, const string& unit) {
    string u = to_lower_str(unit);
    if (u.find("gib") != string::npos || u.find("gb") != string::npos) {
        return static_cast<uint64_t>(val * 1024.0 * 1024.0 * 1024.0);
    }
    if (u.find("mib") != string::npos || u.find("mb") != string::npos) {
        return static_cast<uint64_t>(val * 1024.0 * 1024.0);
    }
    if (u.find("kib") != string::npos || u.find("kb") != string::npos) {
        return static_cast<uint64_t>(val * 1024.0);
    }
    return static_cast<uint64_t>(val);
}

int parse_eta_seconds(const string& eta_str) {
    int h = 0, m = 0, s = 0;
    int colons = 0;
    for (char c : eta_str) if (c == ':') colons++;

    if (colons == 1) {
        if (sscanf(eta_str.c_str(), "%d:%d", &m, &s) == 2) {
            return m * 60 + s;
        }
    } else if (colons == 2) {
        if (sscanf(eta_str.c_str(), "%d:%d:%d", &h, &m, &s) == 3) {
            return h * 3600 + m * 60 + s;
        }
    }
    return 0;
}

} // namespace

vector<int> MediaMetadata::get_available_resolutions() const {
    vector<int> res;
    for (const auto& f : formats) {
        if (f.is_video && f.height > 0) {
            if (find(res.begin(), res.end(), f.height) == res.end()) {
                res.push_back(f.height);
            }
        }
    }
    sort(res.begin(), res.end(), greater<int>());
    return res;
}

const MediaFormat* MediaMetadata::get_best_video_format(int max_height) const {
    const MediaFormat* best = nullptr;
    for (const auto& f : formats) {
        if (!f.is_video) continue;
        if (max_height > 0 && f.height > max_height) continue;

        if (!best) {
            best = &f;
        } else if (f.height > best->height) {
            best = &f;
        } else if (f.height == best->height && f.tbr > best->tbr) {
            best = &f;
        }
    }
    return best;
}

const MediaFormat* MediaMetadata::get_best_audio_format() const {
    const MediaFormat* best = nullptr;
    for (const auto& f : formats) {
        if (!f.is_audio) continue;
        if (!best) {
            best = &f;
        } else if (f.abr > best->abr) {
            best = &f;
        }
    }
    return best;
}

MediaExtractor::MediaExtractor(const string& ytdlp_path, const string& ffmpeg_path)
    : m_ytdlp_path(ytdlp_path), m_ffmpeg_path(ffmpeg_path) {
    if (m_ytdlp_path.empty()) {
        m_ytdlp_path = find_executable_in_system("yt-dlp");
        if (m_ytdlp_path.empty()) {
            m_ytdlp_path = find_executable_in_system("youtube-dl");
        }
    }
    if (m_ffmpeg_path.empty()) {
        m_ffmpeg_path = find_executable_in_system("ffmpeg");
    }
}

MediaExtractor::~MediaExtractor() = default;

bool MediaExtractor::is_supported_media_url(const string& url) {
    if (url.empty()) return false;
    string lower = to_lower_str(url);

    static const vector<string> domains = {
        "youtube.com", "youtu.be", "vimeo.com", "twitch.tv", "twitter.com", "x.com",
        "tiktok.com", "instagram.com", "reddit.com", "facebook.com", "fb.watch",
        "bilibili.com", "dailymotion.com", "soundcloud.com", "bandcamp.com"
    };

    for (const auto& d : domains) {
        if (lower.find(d) != string::npos) return true;
    }
    if (lower.find(".m3u8") != string::npos || lower.find(".mpd") != string::npos) {
        return true;
    }
    return false;
}

string MediaExtractor::find_executable_in_system(const string& exe_name) {
    error_code ec;

    // Check adjacent tools folder
#ifdef _WIN32
    string candidate_local = "tools/" + exe_name + ".exe";
#else
    string candidate_local = "tools/" + exe_name;
#endif
    if (filesystem::exists(candidate_local, ec)) {
        return candidate_local;
    }

#ifdef _WIN32
    char buffer[MAX_PATH];
    string target = exe_name + ".exe";
    DWORD res = SearchPathA(nullptr, target.c_str(), nullptr, MAX_PATH, buffer, nullptr);
    if (res > 0 && res < MAX_PATH) {
        return string(buffer);
    }
#else
    const char* path_env = getenv("PATH");
    if (path_env) {
        stringstream ss(path_env);
        string dir;
        while (getline(ss, dir, ':')) {
            string full = dir + "/" + exe_name;
            if (filesystem::exists(full, ec)) {
                return full;
            }
        }
    }
#endif
    return "";
}

string MediaExtractor::get_ytdlp_path() const {
    lock_guard<mutex> lock(m_mutex);
    return m_ytdlp_path;
}

string MediaExtractor::get_ffmpeg_path() const {
    lock_guard<mutex> lock(m_mutex);
    return m_ffmpeg_path;
}

void MediaExtractor::set_ytdlp_path(const string& path) {
    lock_guard<mutex> lock(m_mutex);
    m_ytdlp_path = path;
}

void MediaExtractor::set_ffmpeg_path(const string& path) {
    lock_guard<mutex> lock(m_mutex);
    m_ffmpeg_path = path;
}

bool MediaExtractor::check_tools_installed(string* details) const {
    string y_path = get_ytdlp_path();
    string f_path = get_ffmpeg_path();

    bool has_ytdlp = !y_path.empty();
    bool has_ffmpeg = !f_path.empty();

    if (details) {
        ostringstream ss;
        ss << "yt-dlp: " << (has_ytdlp ? y_path : "NOT FOUND") << " | "
           << "ffmpeg: " << (has_ffmpeg ? f_path : "NOT FOUND");
        *details = ss.str();
    }
    return has_ytdlp;
}

MediaMetadata MediaExtractor::parse_metadata_json(const string& json_str) const {
    MediaMetadata meta;
    if (json_str.empty()) return meta;

    try {
        auto j = nlohmann::json::parse(json_str);
        meta.id = j.value("id", "");
        meta.title = j.value("title", "");
        meta.uploader = j.value("uploader", "");
        meta.description = j.value("description", "");
        meta.duration_sec = j.value("duration", 0);
        meta.thumbnail_url = j.value("thumbnail", "");
        meta.webpage_url = j.value("webpage_url", "");
        meta.raw_json = json_str;

        if (j.contains("formats") && j["formats"].is_array()) {
            for (const auto& item : j["formats"]) {
                if (!item.is_object()) continue;

                MediaFormat f;
                f.format_id = item.value("format_id", "");
                f.extension = item.value("ext", "");
                f.width = item.value("width", 0);
                f.height = item.value("height", 0);
                f.fps = item.value("fps", 0);
                f.vcodec = item.value("vcodec", "none");
                f.acodec = item.value("acodec", "none");
                f.tbr = item.value("tbr", 0.0);
                f.abr = item.value("abr", 0.0);
                f.vbr = item.value("vbr", 0.0);
                f.format_note = item.value("format_note", "");

                if (item.contains("filesize") && item["filesize"].is_number()) {
                    f.filesize = item["filesize"].get<uint64_t>();
                } else if (item.contains("filesize_approx") && item["filesize_approx"].is_number()) {
                    f.filesize = item["filesize_approx"].get<uint64_t>();
                }

                f.is_video = (f.vcodec != "none" && !f.vcodec.empty());
                f.is_audio = (f.acodec != "none" && !f.acodec.empty());

                meta.formats.push_back(f);
            }
        }

        meta.is_valid = (!meta.id.empty() && !meta.title.empty());
    } catch (...) {
        meta.is_valid = false;
    }
    return meta;
}

string MediaExtractor::build_format_filter(MediaQualityPreset preset, const string& preferred_container) const {
    switch (preset) {
        case MediaQualityPreset::BestVideoAudio:
            return "-f \"bestvideo+bestaudio/best\" --merge-output-format " + preferred_container;
        case MediaQualityPreset::UHD_4K:
            return "-f \"bestvideo[height<=2160]+bestaudio/best\" --merge-output-format " + preferred_container;
        case MediaQualityPreset::QHD_1440p:
            return "-f \"bestvideo[height<=1440]+bestaudio/best\" --merge-output-format " + preferred_container;
        case MediaQualityPreset::FHD_1080p:
            return "-f \"bestvideo[height<=1080]+bestaudio/best\" --merge-output-format " + preferred_container;
        case MediaQualityPreset::HD_720p:
            return "-f \"bestvideo[height<=720]+bestaudio/best\" --merge-output-format " + preferred_container;
        case MediaQualityPreset::SD_480p:
            return "-f \"bestvideo[height<=480]+bestaudio/best\" --merge-output-format " + preferred_container;
        case MediaQualityPreset::AudioOnly_320k:
            return "-x --audio-format mp3 --audio-quality 0";
        case MediaQualityPreset::AudioOnly_192k:
            return "-x --audio-format mp3 --audio-quality 2";
        case MediaQualityPreset::AudioOnly_Best:
            return "-f \"bestaudio/best\" -x";
    }
    return "-f \"bestvideo+bestaudio/best\" --merge-output-format " + preferred_container;
}

string MediaExtractor::build_download_command(const string& url,
                                                  const string& output_path,
                                                  MediaQualityPreset preset,
                                                  const string& custom_format) const {
    ostringstream cmd;
    string bin = get_ytdlp_path();
    if (bin.empty()) bin = "yt-dlp";

    cmd << "\"" << bin << "\" --newline --no-playlist ";

    string ffmpeg_bin = get_ffmpeg_path();
    if (!ffmpeg_bin.empty()) {
        cmd << "--ffmpeg-location \"" << ffmpeg_bin << "\" ";
    }

    if (!custom_format.empty()) {
        cmd << "-f \"" << custom_format << "\" ";
    } else {
        cmd << build_format_filter(preset) << " ";
    }

    cmd << "-o \"" << output_path << "\" \"" << url << "\"";
    return cmd.str();
}

ExtractionProgress MediaExtractor::parse_progress_line(const string& line, ExtractionStage current_stage) {
    ExtractionProgress prog;
    prog.stage = current_stage;
    prog.status_text = line;

    // Detect format merger
    if (line.find("[Merger]") != string::npos || line.find("Merging formats") != string::npos) {
        prog.stage = ExtractionStage::MergingStreams;
        prog.percent = 99.0;
        prog.status_text = "Merging video and audio with FFmpeg...";
        return prog;
    }

    // Detect destination change for secondary stream (check audio first)
    if (line.find("[download] Destination:") != string::npos) {
        string lower = to_lower_str(line);
        if (lower.find(".m4a") != string::npos || lower.find(".mp3") != string::npos ||
            lower.find(".opus") != string::npos || lower.find(".aac") != string::npos ||
            lower.find(".flac") != string::npos || lower.find("audio") != string::npos) {
            prog.stage = ExtractionStage::DownloadingAudio;
        } else {
            prog.stage = ExtractionStage::DownloadingVideo;
        }
        return prog;
    }

    // Parse progress: [download]  45.2% of ~120.50MiB at 14.85MiB/s ETA 00:04
    size_t dl_pos = line.find("[download]");
    if (dl_pos == string::npos) return prog;

    size_t pct_pos = line.find('%');
    if (pct_pos != string::npos && pct_pos > dl_pos) {
        size_t num_start = line.rfind(' ', pct_pos - 1);
        if (num_start != string::npos) {
            try {
                prog.percent = stod(line.substr(num_start + 1, pct_pos - num_start - 1));
            } catch (...) {}
        }
    }

    // Total size
    size_t of_pos = line.find(" of ");
    if (of_pos != string::npos) {
        size_t size_start = of_pos + 4;
        if (size_start < line.size() && line[size_start] == '~') size_start++;
        size_t size_end = line.find(' ', size_start);
        if (size_end != string::npos) {
            string size_str = line.substr(size_start, size_end - size_start);
            // Split value and unit (e.g. 120.50MiB)
            size_t unit_idx = 0;
            while (unit_idx < size_str.size() && (isdigit(size_str[unit_idx]) || size_str[unit_idx] == '.')) {
                unit_idx++;
            }
            try {
                double s_val = stod(size_str.substr(0, unit_idx));
                string s_unit = size_str.substr(unit_idx);
                prog.total_bytes = parse_size_unit(s_val, s_unit);
                prog.downloaded_bytes = static_cast<uint64_t>(prog.total_bytes * (prog.percent / 100.0));
            } catch (...) {}
        }
    }

    // Speed
    size_t at_pos = line.find(" at ");
    if (at_pos != string::npos) {
        size_t spd_start = at_pos + 4;
        size_t spd_end = line.find("/s", spd_start);
        if (spd_end != string::npos) {
            string spd_str = line.substr(spd_start, spd_end - spd_start);
            size_t u_idx = 0;
            while (u_idx < spd_str.size() && (isdigit(spd_str[u_idx]) || spd_str[u_idx] == '.')) {
                u_idx++;
            }
            try {
                double val = stod(spd_str.substr(0, u_idx));
                string unit = spd_str.substr(u_idx);
                prog.speed_bps = static_cast<double>(parse_size_unit(val, unit));
            } catch (...) {}
        }
    }

    // ETA
    size_t eta_pos = line.find("ETA ");
    if (eta_pos != string::npos) {
        size_t eta_start = eta_pos + 4;
        size_t eta_end = line.find_first_of(" \r\n", eta_start);
        string eta_val = line.substr(eta_start, eta_end - eta_start);
        prog.eta_seconds = parse_eta_seconds(eta_val);
    }

    if (prog.stage == ExtractionStage::Idle) {
        prog.stage = ExtractionStage::DownloadingVideo;
    }

    return prog;
}

void MediaExtractor::set_process_executor(ProcessExecutor executor) {
    m_mock_executor = executor;
}

void MediaExtractor::cancel() {
    m_cancel_requested = true;
}

string MediaExtractor::get_last_error() const {
    lock_guard<mutex> lock(m_mutex);
    return m_last_error;
}

MediaMetadata MediaExtractor::fetch_metadata(const string& url) {
    if (!is_supported_media_url(url)) {
        m_last_error = "URL is not a recognized streaming media service";
        return MediaMetadata{};
    }

    string bin = get_ytdlp_path();
    if (bin.empty()) bin = "yt-dlp";

    string cmd = "\"" + bin + "\" --dump-json --no-playlist \"" + url + "\"";
    string json_output;

    if (m_mock_executor) {
        m_mock_executor(cmd, [&json_output](const string& l) { json_output += l; }, nullptr);
    } else {
        // Native pipe execution
#ifdef _WIN32
        SECURITY_ATTRIBUTES sa;
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        sa.lpSecurityDescriptor = nullptr;

        HANDLE hRead, hWrite;
        if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return MediaMetadata{};
        SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOA si;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        si.hStdOutput = hWrite;
        si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;

        PROCESS_INFORMATION pi;
        ZeroMemory(&pi, sizeof(pi));

        vector<char> cmd_buf(cmd.begin(), cmd.end());
        cmd_buf.push_back('\0');

        if (CreateProcessA(nullptr, cmd_buf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            CloseHandle(hWrite);
            char buf[4096];
            DWORD read_bytes = 0;
            while (ReadFile(hRead, buf, sizeof(buf) - 1, &read_bytes, nullptr) && read_bytes > 0) {
                buf[read_bytes] = '\0';
                json_output.append(buf, read_bytes);
            }
            CloseHandle(hRead);
            WaitForSingleObject(pi.hProcess, 15000);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        } else {
            CloseHandle(hRead);
            CloseHandle(hWrite);
        }
#else
        FILE* pipe = popen(cmd.c_str(), "r");
        if (pipe) {
            char buf[4096];
            while (fgets(buf, sizeof(buf), pipe)) {
                json_output += buf;
            }
            pclose(pipe);
        }
#endif
    }

    return parse_metadata_json(json_output);
}

bool MediaExtractor::extract_media(const string& url,
                                  const string& output_path,
                                  MediaQualityPreset preset,
                                  ProgressCallback callback,
                                  const string& custom_format) {
    m_cancel_requested = false;
    string cmd = build_download_command(url, output_path, preset, custom_format);

    ExtractionStage current_stage = ExtractionStage::DownloadingVideo;

    if (m_mock_executor) {
        int code = m_mock_executor(cmd, [this, &callback, &current_stage](const string& line) {
            if (m_cancel_requested) return;
            ExtractionProgress p = parse_progress_line(line, current_stage);
            current_stage = p.stage;
            if (callback) callback(p);
        }, nullptr);

        if (code != 0) {
            m_last_error = "Media extraction process failed with code " + to_string(code);
            return false;
        }

        if (callback) {
            ExtractionProgress finished;
            finished.stage = ExtractionStage::Completed;
            finished.percent = 100.0;
            finished.status_text = "Extraction completed successfully";
            callback(finished);
        }
        return true;
    }

    // Live native execution with streaming stdout
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE hRead, hWrite;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        m_last_error = "Failed to initialize communication pipe";
        return false;
    }
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    vector<char> cmd_buf(cmd.begin(), cmd.end());
    cmd_buf.push_back('\0');

    BOOL spawned = CreateProcessA(nullptr, cmd_buf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(hWrite);

    if (!spawned) {
        CloseHandle(hRead);
        m_last_error = "Failed to spawn yt-dlp child process";
        return false;
    }

    char buf[1024];
    DWORD read_bytes = 0;
    string accumulator;

    while (ReadFile(hRead, buf, sizeof(buf) - 1, &read_bytes, nullptr) && read_bytes > 0) {
        if (m_cancel_requested) {
            TerminateProcess(pi.hProcess, 1);
            break;
        }

        buf[read_bytes] = '\0';
        accumulator.append(buf, read_bytes);

        size_t nl;
        while ((nl = accumulator.find_first_of("\r\n")) != string::npos) {
            string line = accumulator.substr(0, nl);
            size_t next_idx = nl + 1;
            if (next_idx < accumulator.size() && accumulator[nl] == '\r' && accumulator[next_idx] == '\n') {
                next_idx++;
            }
            accumulator.erase(0, next_idx);

            if (!line.empty()) {
                ExtractionProgress p = parse_progress_line(line, current_stage);
                current_stage = p.stage;
                if (callback) callback(p);
            }
        }
    }

    CloseHandle(hRead);
    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (m_cancel_requested) {
        m_last_error = "Extraction cancelled by user";
        return false;
    }

    if (exit_code != 0) {
        m_last_error = "yt-dlp exited with error code " + to_string(exit_code);
        return false;
    }

    if (callback) {
        ExtractionProgress finished;
        finished.stage = ExtractionStage::Completed;
        finished.percent = 100.0;
        finished.status_text = "Extraction completed successfully";
        callback(finished);
    }
    return true;
#else
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        m_last_error = "Failed to spawn media extraction pipe";
        return false;
    }
    char buf[1024];
    while (fgets(buf, sizeof(buf), pipe)) {
        if (m_cancel_requested) break;
        string line(buf);
        ExtractionProgress p = parse_progress_line(line, current_stage);
        current_stage = p.stage;
        if (callback) callback(p);
    }
    int status = pclose(pipe);
    return (status == 0 && !m_cancel_requested);
#endif
}

} // namespace idm
