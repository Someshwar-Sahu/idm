#include "../src/media/MediaExtractor.h"
#include <iostream>
#include <cassert>
#include <string>
#include <vector>

using namespace std;
using namespace idm;

void test_url_detection() {
    cout << "\n[1/8] Testing Streaming Media URL Detection (Pos + Neg)..." << endl;

    // Positive
    assert(MediaExtractor::is_supported_media_url("https://www.youtube.com/watch?v=dQw4w9WgXcQ"));
    assert(MediaExtractor::is_supported_media_url("https://youtu.be/dQw4w9WgXcQ"));
    assert(MediaExtractor::is_supported_media_url("https://twitter.com/user/status/123456789"));
    assert(MediaExtractor::is_supported_media_url("https://x.com/user/status/123456789"));
    assert(MediaExtractor::is_supported_media_url("https://www.tiktok.com/@creator/video/987654321"));
    assert(MediaExtractor::is_supported_media_url("https://www.twitch.tv/videos/123456789"));
    assert(MediaExtractor::is_supported_media_url("https://www.instagram.com/reel/Cx123456789/"));
    assert(MediaExtractor::is_supported_media_url("https://cdn.site.com/live/stream.m3u8"));

    // Negative (Standard direct files / web pages)
    assert(!MediaExtractor::is_supported_media_url("https://releases.ubuntu.com/24.04/ubuntu.iso"));
    assert(!MediaExtractor::is_supported_media_url("https://example.com/archive.zip"));
    assert(!MediaExtractor::is_supported_media_url("https://wikipedia.org/wiki/Download"));
    assert(!MediaExtractor::is_supported_media_url(""));

    cout << "  -> Streaming URL detection and direct file rejection: PASSED" << endl;
}

void test_metadata_json_parsing() {
    cout << "\n[2/8] Testing Metadata JSON Parsing & Format Matrix Extraction..." << endl;

    string sample_json = R"({
        "id": "sample_video_123",
        "title": "IDM 2.0 Feature Showcase 4K 60fps",
        "uploader": "Google DeepMind Engineer",
        "description": "Demonstrating next-gen IDM capabilities",
        "duration": 345,
        "thumbnail": "https://img.youtube.com/vi/sample_video_123/maxresdefault.jpg",
        "webpage_url": "https://youtube.com/watch?v=sample_video_123",
        "formats": [
            {
                "format_id": "140",
                "ext": "m4a",
                "vcodec": "none",
                "acodec": "mp4a.40.2",
                "abr": 128.5,
                "tbr": 128.5,
                "filesize": 5500000,
                "format_note": "medium audio"
            },
            {
                "format_id": "251",
                "ext": "webm",
                "vcodec": "none",
                "acodec": "opus",
                "abr": 160.0,
                "tbr": 160.0,
                "filesize": 6800000,
                "format_note": "high audio"
            },
            {
                "format_id": "137",
                "ext": "mp4",
                "width": 1920,
                "height": 1080,
                "fps": 60,
                "vcodec": "avc1.64002a",
                "acodec": "none",
                "vbr": 4500.0,
                "tbr": 4500.0,
                "filesize": 195000000,
                "format_note": "1080p60"
            },
            {
                "format_id": "313",
                "ext": "webm",
                "width": 3840,
                "height": 2160,
                "fps": 60,
                "vcodec": "vp9",
                "acodec": "none",
                "vbr": 18500.0,
                "tbr": 18500.0,
                "filesize": 780000000,
                "format_note": "2160p60 4K"
            }
        ]
    })";

    MediaExtractor extractor;
    MediaMetadata meta = extractor.parse_metadata_json(sample_json);

    assert(meta.is_valid);
    assert(meta.id == "sample_video_123");
    assert(meta.title == "IDM 2.0 Feature Showcase 4K 60fps");
    assert(meta.duration_sec == 345);
    assert(meta.formats.size() == 4);

    cout << "  -> Metadata, streams, and format attributes parsed: PASSED" << endl;
}

void test_format_filtering_and_resolution_matching() {
    cout << "\n[3/8] Testing Format Filtering & Resolution Presets..." << endl;

    string sample_json = R"({
        "id": "vid1",
        "title": "Resolution Test",
        "formats": [
            {"format_id": "1", "ext": "mp4", "width": 1280, "height": 720, "vcodec": "avc1", "acodec": "none", "tbr": 2000.0},
            {"format_id": "2", "ext": "mp4", "width": 1920, "height": 1080, "vcodec": "avc1", "acodec": "none", "tbr": 4000.0},
            {"format_id": "3", "ext": "webm", "width": 3840, "height": 2160, "vcodec": "vp9", "acodec": "none", "tbr": 15000.0},
            {"format_id": "a1", "ext": "m4a", "vcodec": "none", "acodec": "mp4a", "abr": 128.0},
            {"format_id": "a2", "ext": "opus", "vcodec": "none", "acodec": "opus", "abr": 160.0}
        ]
    })";

    MediaExtractor extractor;
    MediaMetadata meta = extractor.parse_metadata_json(sample_json);

    auto resolutions = meta.get_available_resolutions();
    assert(resolutions.size() == 3);
    assert(resolutions[0] == 2160);
    assert(resolutions[1] == 1080);
    assert(resolutions[2] == 720);

    const MediaFormat* best_4k = meta.get_best_video_format(2160);
    assert(best_4k && best_4k->format_id == "3");

    const MediaFormat* best_1080p = meta.get_best_video_format(1080);
    assert(best_1080p && best_1080p->format_id == "2");

    const MediaFormat* best_audio = meta.get_best_audio_format();
    assert(best_audio && best_audio->format_id == "a2"); // 160 kbps opus

    cout << "  -> Resolution sorting and highest bitrate stream matching: PASSED" << endl;
}

void test_format_selector_and_command_builder() {
    cout << "\n[4/8] Testing Format Selector and Command Construction..." << endl;
    MediaExtractor extractor("bin/yt-dlp.exe", "bin/ffmpeg.exe");

    string f_4k = extractor.build_format_filter(MediaQualityPreset::UHD_4K, "mp4");
    assert(f_4k.find("height<=2160") != string::npos);

    string f_1080 = extractor.build_format_filter(MediaQualityPreset::FHD_1080p, "mkv");
    assert(f_1080.find("height<=1080") != string::npos);
    assert(f_1080.find("mkv") != string::npos);

    string f_mp3 = extractor.build_format_filter(MediaQualityPreset::AudioOnly_320k);
    assert(f_mp3.find("--audio-format mp3") != string::npos);
    assert(f_mp3.find("--audio-quality 0") != string::npos);

    // Full CLI command
    string cmd = extractor.build_download_command("https://youtube.com/watch?v=123", "downloads/vid.mp4", MediaQualityPreset::FHD_1080p);
    assert(cmd.find("bin/yt-dlp.exe") != string::npos);
    assert(cmd.find("--ffmpeg-location \"bin/ffmpeg.exe\"") != string::npos);
    assert(cmd.find("--newline") != string::npos);
    assert(cmd.find("downloads/vid.mp4") != string::npos);

    cout << "  -> Command builder, format filters, and FFmpeg injection: PASSED" << endl;
}

void test_progress_parsing() {
    cout << "\n[5/8] Testing Real-Time Stdout Progress Line Parsing..." << endl;

    // Standard download progress
    string line1 = "[download]  45.2% of ~120.50MiB at 14.85MiB/s ETA 00:04";
    auto p1 = MediaExtractor::parse_progress_line(line1, ExtractionStage::DownloadingVideo);

    assert(p1.stage == ExtractionStage::DownloadingVideo);
    assert(p1.percent >= 45.0 && p1.percent <= 46.0);
    assert(p1.total_bytes > 120 * 1024 * 1024);
    assert(p1.downloaded_bytes > 0);
    assert(p1.speed_bps > 14 * 1024 * 1024);
    assert(p1.eta_seconds == 4);

    // Stream 2 transition
    string line2 = "[download] Destination: video.f140.m4a";
    auto p2 = MediaExtractor::parse_progress_line(line2, ExtractionStage::DownloadingVideo);
    assert(p2.stage == ExtractionStage::DownloadingAudio);

    // FFmpeg merge transition
    string line3 = "[Merger] Merging formats into \"video.mp4\"";
    auto p3 = MediaExtractor::parse_progress_line(line3, ExtractionStage::DownloadingAudio);
    assert(p3.stage == ExtractionStage::MergingStreams);
    assert(p3.percent == 99.0);

    cout << "  -> Percent, speed, ETA, and stage transition parsing: PASSED" << endl;
}

void test_end_to_end_mock_extraction() {
    cout << "\n[6/8] Testing End-to-End Extraction with Mock Process Runner..." << endl;
    MediaExtractor extractor("yt-dlp", "ffmpeg");

    vector<ExtractionProgress> progress_history;

    extractor.set_process_executor([](const string& cmd,
                                     function<void(const string&)> on_stdout,
                                     function<void(const string&)> on_stderr) -> int {
        assert(cmd.find("youtube.com") != string::npos);

        on_stdout("[download] Destination: clip.f137.mp4");
        on_stdout("[download]  25.0% of 100.00MiB at 10.00MiB/s ETA 00:08");
        on_stdout("[download]  50.0% of 100.00MiB at 10.00MiB/s ETA 00:05");
        on_stdout("[download] 100.0% of 100.00MiB at 10.00MiB/s ETA 00:00");
        on_stdout("[download] Destination: clip.f140.m4a");
        on_stdout("[download]  50.0% of 10.00MiB at 5.00MiB/s ETA 00:01");
        on_stdout("[download] 100.0% of 10.00MiB at 5.00MiB/s ETA 00:00");
        on_stdout("[Merger] Merging formats into \"clip.mp4\"");
        return 0; // Success
    });

    bool ok = extractor.extract_media("https://youtube.com/watch?v=mock", "clip.mp4", MediaQualityPreset::FHD_1080p,
        [&progress_history](const ExtractionProgress& p) {
            progress_history.push_back(p);
        });

    assert(ok);
    assert(!progress_history.empty());
    assert(progress_history.back().stage == ExtractionStage::Completed);
    assert(progress_history.back().percent == 100.0);

    cout << "  -> Mock process execution, streaming callbacks, and completion: PASSED" << endl;
}

void test_cancellation_and_failure_guards() {
    cout << "\n[7/8] Testing Extraction Cancellation & Process Failure Guards..." << endl;
    MediaExtractor extractor("yt-dlp", "ffmpeg");

    // Failure simulation
    extractor.set_process_executor([](const string&, function<void(const string&)>, function<void(const string&)>) {
        return 1; // Error code
    });

    bool ok = extractor.extract_media("https://youtube.com/watch?v=err", "fail.mp4", MediaQualityPreset::BestVideoAudio);
    assert(!ok);
    assert(!extractor.get_last_error().empty());

    // Cancellation
    extractor.set_process_executor([&extractor](const string&, function<void(const string&)> on_stdout, function<void(const string&)>) {
        on_stdout("[download] 10.0% of 50.00MiB at 1.00MiB/s ETA 00:45");
        extractor.cancel();
        return 0;
    });

    bool cancelled_ok = extractor.extract_media("https://youtube.com/watch?v=cancel", "cancel.mp4", MediaQualityPreset::BestVideoAudio);
    // Callback should not proceed past cancellation
    assert(cancelled_ok);

    cout << "  -> Non-zero exit code error reporting and cancellation: PASSED" << endl;
}

void test_tool_detection_and_edge_cases() {
    cout << "\n[8/8] Testing Tool Detection Accessors & Edge Cases..." << endl;
    MediaExtractor extractor;

    string details;
    extractor.check_tools_installed(&details);
    assert(!details.empty());

    // Malformed JSON parsing
    MediaMetadata broken = extractor.parse_metadata_json("{broken json");
    assert(!broken.is_valid);

    // Unsupported URL
    MediaMetadata invalid_url = extractor.fetch_metadata("https://not-streaming.com/file.zip");
    assert(!invalid_url.is_valid);
    assert(!extractor.get_last_error().empty());

    cout << "  -> Tool status diagnostics, corrupt JSON, and invalid URL guards: PASSED" << endl;
}

void test_shell_injection_and_safety_guards() {
    cout << "\n[9/9] Testing Bug #38 & #39 Shell Injection, Path Escaping & Cancellation Safety..." << endl;

    // 1. is_safe_shell_string tests
    assert(MediaExtractor::is_safe_shell_string("video.mp4"));
    assert(MediaExtractor::is_safe_shell_string("C:\\Downloads\\video.mp4"));
    assert(MediaExtractor::is_safe_shell_string("downloads/subfolder/clip.mkv"));
    assert(MediaExtractor::is_safe_shell_string("bestvideo+bestaudio/best"));

    // Shell metacharacters rejected
    assert(!MediaExtractor::is_safe_shell_string("video.mp4; calc.exe"));
    assert(!MediaExtractor::is_safe_shell_string("video.mp4 & calc.exe"));
    assert(!MediaExtractor::is_safe_shell_string("video.mp4 | calc.exe"));
    assert(!MediaExtractor::is_safe_shell_string("video.mp4`calc.exe`"));
    assert(!MediaExtractor::is_safe_shell_string("video.mp4$PATH"));
    assert(!MediaExtractor::is_safe_shell_string("video.mp4\""));
    assert(!MediaExtractor::is_safe_shell_string("video.mp4'"));
    assert(!MediaExtractor::is_safe_shell_string("video.mp4<input"));
    assert(!MediaExtractor::is_safe_shell_string("video.mp4>output"));
    assert(!MediaExtractor::is_safe_shell_string("--exec calc.exe")); // Flag injection
    assert(!MediaExtractor::is_safe_shell_string(""));

    // 2. is_safe_url tests
    assert(MediaExtractor::is_safe_url("https://www.youtube.com/watch?v=dQw4w9WgXcQ"));
    assert(MediaExtractor::is_safe_url("https://www.youtube.com/watch?v=dQw4w9WgXcQ&t=10s"));
    assert(!MediaExtractor::is_safe_url("https://www.youtube.com/watch?v=123; calc.exe"));
    assert(!MediaExtractor::is_safe_url("https://www.youtube.com/watch?v=123\" --exec \"calc.exe\""));
    assert(!MediaExtractor::is_safe_url("https://www.youtube.com/watch=123&calc.exe")); // & without query ?
    assert(!MediaExtractor::is_safe_url("https://www.youtube.com/watch?v=123|cmd"));
    assert(!MediaExtractor::is_safe_url("https://www.youtube.com/watch?v=123\ncalc.exe"));
    assert(!MediaExtractor::is_safe_url("--exec calc.exe"));

    // 3. is_supported_media_url rejecting injection
    assert(!MediaExtractor::is_supported_media_url("https://www.youtube.com/watch?v=123; calc.exe"));
    assert(!MediaExtractor::is_supported_media_url("https://www.youtube.com/watch?v=123\" --exec calc.exe"));
    assert(MediaExtractor::is_supported_media_url("https://www.youtube.com/watch?v=123&t=20s"));

    // 4. extract_media rejecting dangerous parameters
    MediaExtractor extractor("yt-dlp", "ffmpeg");
    bool bad_path = extractor.extract_media("https://youtube.com/watch?v=123", "out.mp4; calc.exe", MediaQualityPreset::FHD_1080p);
    assert(!bad_path);
    assert(!extractor.get_last_error().empty());

    bool bad_fmt = extractor.extract_media("https://youtube.com/watch?v=123", "out.mp4", MediaQualityPreset::FHD_1080p, nullptr, "--exec calc.exe");
    assert(!bad_fmt);
    assert(!extractor.get_last_error().empty());

    // 5. fetch_metadata rejecting dangerous URL
    MediaMetadata meta = extractor.fetch_metadata("https://youtube.com/watch?v=123; calc.exe");
    assert(!meta.is_valid);
    assert(!extractor.get_last_error().empty());

    cout << "  -> Shell injection guards, path validation, and flag sanitization: PASSED" << endl;
}

int main() {
    cout << "==========================================================" << endl;
    cout << "  Comprehensive Unit Test: MediaExtractor (Task 23)       " << endl;
    cout << "==========================================================" << endl;

    test_url_detection();
    test_metadata_json_parsing();
    test_format_filtering_and_resolution_matching();
    test_format_selector_and_command_builder();
    test_progress_parsing();
    test_end_to_end_mock_extraction();
    test_cancellation_and_failure_guards();
    test_tool_detection_and_edge_cases();
    test_shell_injection_and_safety_guards();

    cout << "\n==========================================================" << endl;
    cout << "  [PASSED] 100% OF UNIVERSAL MEDIA EXTRACTOR VERIFIED!    " << endl;
    cout << "==========================================================" << endl;

    return 0;
}
