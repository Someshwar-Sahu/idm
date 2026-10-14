#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif
#include "../src/media/StreamPreviewer.h"
#include "../src/ipc/LocalServer.h"
#include <iostream>
#include <cassert>
#include <vector>
#include <string>
#include <filesystem>
#include <fstream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

using namespace std;
using namespace idm;

void test_container_detection() {
    cout << "\n[1/8] Testing Media Container Signature Detection..." << endl;

    // 1. MP4
    uint8_t mp4_sig[16] = {0, 0, 0, 20, 'f', 't', 'y', 'p', 'i', 's', 'o', 'm'};
    assert(StreamPreviewer::detect_container_from_buffer(mp4_sig, 16) == MediaContainerType::MP4);

    // 2. WebM / MKV
    uint8_t webm_sig[32] = {0x1A, 0x45, 0xDF, 0xA3, 0x01, 0x00, 0x00, 0x00};
    std::memcpy(webm_sig + 8, "webm", 4);
    assert(StreamPreviewer::detect_container_from_buffer(webm_sig, 32) == MediaContainerType::WebM);

    uint8_t mkv_sig[16] = {0x1A, 0x45, 0xDF, 0xA3, 0x01, 0x00, 0x00, 0x00};
    assert(StreamPreviewer::detect_container_from_buffer(mkv_sig, 16) == MediaContainerType::MKV);

    // 3. WAV
    uint8_t wav_sig[16] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E'};
    assert(StreamPreviewer::detect_container_from_buffer(wav_sig, 16) == MediaContainerType::WAV);

    // 4. MP3
    uint8_t mp3_id3[16] = {'I', 'D', '3', 3, 0, 0, 0, 0};
    assert(StreamPreviewer::detect_container_from_buffer(mp3_id3, 16) == MediaContainerType::MP3);

    uint8_t mp3_sync[16] = {0xFF, 0xFB, 0x90, 0x64};
    assert(StreamPreviewer::detect_container_from_buffer(mp3_sync, 16) == MediaContainerType::MP3);

    // 5. Unknown
    uint8_t junk[16] = {0x01, 0x02, 0x03, 0x04};
    assert(StreamPreviewer::detect_container_from_buffer(junk, 16) == MediaContainerType::Unknown);

    cout << "  -> MP4, WebM, MKV, WAV, MP3 magic signatures detected: PASSED" << endl;
}

void test_mime_type_mapping() {
    cout << "\n[2/8] Testing MIME Type Mapping & Fallbacks..." << endl;

    assert(StreamPreviewer::container_to_mime_type(MediaContainerType::MP4) == "video/mp4");
    assert(StreamPreviewer::container_to_mime_type(MediaContainerType::WebM) == "video/webm");
    assert(StreamPreviewer::container_to_mime_type(MediaContainerType::MP3) == "audio/mpeg");
    assert(StreamPreviewer::container_to_mime_type(MediaContainerType::WAV) == "audio/wav");

    // Extension fallback
    assert(StreamPreviewer::detect_mime_type("movie.mkv") == "video/x-matroska");
    assert(StreamPreviewer::detect_mime_type("track.aac") == "audio/aac");
    assert(StreamPreviewer::detect_mime_type("data.bin") == "application/octet-stream");

    cout << "  -> Audio/video MIME types correctly mapped: PASSED" << endl;
}

void test_mp4_faststart_moov_detection() {
    cout << "\n[3/8] Testing MP4 Fast-Start (moov atom) Analysis..." << endl;

    // Fast-Start: [ftyp box (16 bytes)][moov box (24 bytes)][mdat box (64 bytes)]
    vector<uint8_t> faststart_mp4(128, 0);
    // ftyp (16 bytes)
    faststart_mp4[3] = 16;
    memcpy(&faststart_mp4[4], "ftyp", 4);
    // moov (24 bytes)
    faststart_mp4[16 + 3] = 24;
    memcpy(&faststart_mp4[16 + 4], "moov", 4);
    // mdat (40 bytes)
    faststart_mp4[40 + 3] = 40;
    memcpy(&faststart_mp4[40 + 4], "mdat", 4);

    assert(StreamPreviewer::has_faststart_moov(faststart_mp4.data(), faststart_mp4.size()));

    // Non-Fast-Start (deferred moov): [ftyp][mdat][moov at end]
    vector<uint8_t> deferred_mp4(128, 0);
    deferred_mp4[3] = 16;
    memcpy(&deferred_mp4[4], "ftyp", 4);
    deferred_mp4[16 + 3] = 40;
    memcpy(&deferred_mp4[16 + 4], "mdat", 4);
    deferred_mp4[56 + 3] = 24;
    memcpy(&deferred_mp4[56 + 4], "moov", 4);

    assert(!StreamPreviewer::has_faststart_moov(deferred_mp4.data(), deferred_mp4.size()));

    cout << "  -> Fast-start moov-before-mdat atom detection: PASSED" << endl;
}

void test_contiguous_playable_range() {
    cout << "\n[4/8] Testing Contiguous Playable Range Calculation..." << endl;

    vector<Chunk> chunks;
    // Chunk 0: 0 - 999 (1000 bytes, Done)
    Chunk c0; c0.id = 0; c0.start_byte = 0; c0.end_byte = 999; c0.bytes_downloaded = 1000; c0.state = ChunkState::Done;
    // Chunk 1: 1000 - 1999 (500 bytes downloaded, InProgress)
    Chunk c1; c1.id = 1; c1.start_byte = 1000; c1.end_byte = 1999; c1.bytes_downloaded = 500; c1.state = ChunkState::InProgress;
    // Chunk 2: 2000 - 2999 (1000 bytes, Done)
    Chunk c2; c2.id = 2; c2.start_byte = 2000; c2.end_byte = 2999; c2.bytes_downloaded = 1000; c2.state = ChunkState::Done;

    chunks = {c2, c0, c1}; // Unordered

    uint64_t playable = StreamPreviewer::calculate_contiguous_playable_bytes(chunks);
    // Continuous span is c0 (1000) + c1 (500) = 1500 bytes
    assert(playable == 1500);

    // If chunk 0 is pending: playable is 0
    c0.bytes_downloaded = 0;
    c0.state = ChunkState::Pending;
    chunks = {c0, c1, c2};
    assert(StreamPreviewer::calculate_contiguous_playable_bytes(chunks) == 0);

    cout << "  -> Zero-gap continuous byte buffer calculation: PASSED" << endl;
}

void test_preview_readiness_assessment() {
    cout << "\n[5/8] Testing Preview Readiness Threshold Assessment..." << endl;
    const string test_file = "test_preview_assess.mp4";
    filesystem::remove(test_file);

    // Create faststart MP4 file
    vector<uint8_t> mp4_data(1024 * 1024, 0); // 1 MB
    mp4_data[3] = 16; memcpy(&mp4_data[4], "ftyp", 4);
    mp4_data[19] = 24; memcpy(&mp4_data[20], "moov", 4);
    mp4_data[43] = 100; memcpy(&mp4_data[44], "mdat", 4);

    ofstream fout(test_file, ios::binary);
    fout.write(reinterpret_cast<const char*>(mp4_data.data()), mp4_data.size());
    fout.close();

    // 1. When buffer < min_buffer (512 KB) -> Not ready
    vector<Chunk> chunks_small;
    Chunk c_small; c_small.start_byte = 0; c_small.end_byte = 1024 * 1024 - 1; c_small.bytes_downloaded = 256 * 1024;
    chunks_small.push_back(c_small);

    auto status1 = StreamPreviewer::assess_preview_readiness(test_file, 1024 * 1024, chunks_small, 512 * 1024);
    assert(!status1.is_ready);
    assert(status1.playable_bytes == 256 * 1024);

    // 2. When buffer >= 512 KB and faststart present -> Ready!
    c_small.bytes_downloaded = 768 * 1024;
    chunks_small[0] = c_small;

    auto status2 = StreamPreviewer::assess_preview_readiness(test_file, 1024 * 1024, chunks_small, 512 * 1024);
    assert(status2.is_ready);
    assert(status2.has_faststart);
    assert(status2.container == MediaContainerType::MP4);

    filesystem::remove(test_file);
    cout << "  -> Playability threshold and buffering gates verified: PASSED" << endl;
}

void test_shared_non_exclusive_file_slice_reading() {
    cout << "\n[6/8] Testing Shared Non-Exclusive File Slice Reading..." << endl;
    const string slice_file = "test_shared_slice.bin";
    filesystem::remove(slice_file);

    ofstream fout(slice_file, ios::binary);
    string sample_pattern = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    for (int i = 0; i < 100; ++i) fout << sample_pattern;
    fout.close();

    vector<uint8_t> read_buf;
    bool ok = StreamPreviewer::read_shared_slice(slice_file, 10, 26, read_buf);
    assert(ok);
    assert(read_buf.size() == 26);

    string read_str(reinterpret_cast<const char*>(read_buf.data()), read_buf.size());
    assert(read_str == "ABCDEFGHIJKLMNOPQRSTUVWXYZ");

    filesystem::remove(slice_file);
    cout << "  -> Shared non-exclusive range reading without lock conflicts: PASSED" << endl;
}

void test_http_206_partial_content_response() {
    cout << "\n[7/8] Testing HTTP 206 Partial Content Generation..." << endl;
    const string test_file = "test_http_slice.mp4";
    filesystem::remove(test_file);

    ofstream fout(test_file, ios::binary);
    for (int i = 0; i < 10000; ++i) fout << static_cast<char>(i % 256);
    fout.close();

    // 1. Request Range: bytes=0-499
    HttpRequest req;
    req.method = "GET";
    req.path = "/api/preview";
    req.headers["Range"] = "bytes=0-499";

    HttpResponse res = StreamPreviewer::handle_preview_request(req, test_file, 10000, 5000);
    assert(res.status_code == 206);
    assert(res.headers["Content-Range"] == "bytes 0-499/10000");
    assert(res.headers["Accept-Ranges"] == "bytes");
    assert(res.body.size() == 500);

    // 2. Request beyond playable buffer (should clamp to available bytes)
    req.headers["Range"] = "bytes=4000-8000";
    res = StreamPreviewer::handle_preview_request(req, test_file, 10000, 5000);
    assert(res.status_code == 206);
    assert(res.headers["Content-Range"] == "bytes 4000-4999/10000");
    assert(res.body.size() == 1000);

    // 3. Request completely out of bounds -> 416 Range Not Satisfiable
    req.headers["Range"] = "bytes=6000-7000";
    res = StreamPreviewer::handle_preview_request(req, test_file, 10000, 5000);
    assert(res.status_code == 416);

    filesystem::remove(test_file);
    cout << "  -> HTTP 206 Partial Content and 416 out-of-range guards: PASSED" << endl;
}

void test_live_local_server_streaming_socket() {
    cout << "\n[8/8] Testing Live Loopback TCP Stream Preview via LocalServer..." << endl;
    const string live_file = "test_live_stream.mp4";
    filesystem::remove(live_file);

    ofstream fout(live_file, ios::binary);
    string payload = "HTTP-206-STREAM-PREVIEW-VERIFIED-DATA-CHUNKS";
    fout.write(payload.data(), payload.size());
    fout.close();

    LocalServer server;
    // Dynamic port
    bool started = server.start("127.0.0.1", 0);
    assert(started);
    int port = server.get_port();
    assert(port > 0);

    StreamPreviewer::attach_to_server(server, nullptr, "/api/preview");

    // Connect raw TCP socket to verify HTTP 206 response
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    assert(sock != INVALID_SOCKET);

    sockaddr_in addr;
    ZeroMemory(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    int conn = connect(sock, (sockaddr*)&addr, sizeof(addr));
    assert(conn == 0);

    string req = "GET /api/preview?path=" + live_file + " HTTP/1.1\r\n"
                 "Host: 127.0.0.1\r\n"
                 "Range: bytes=0-19\r\n"
                 "Connection: close\r\n\r\n";

    send(sock, req.c_str(), static_cast<int>(req.size()), 0);

    char buf[1024];
    string response;
    int bytes = 0;
    while ((bytes = recv(sock, buf, sizeof(buf) - 1, 0)) > 0) {
        buf[bytes] = '\0';
        response += buf;
    }
    closesocket(sock);
    server.stop();
    filesystem::remove(live_file);

    assert(response.find("206 Partial Content") != string::npos);
    assert(response.find("Content-Range: bytes 0-19/") != string::npos);
    assert(response.find("HTTP-206-STREAM-PREV") != string::npos);

    cout << "  -> Live HTTP 206 TCP socket round-trip with Range header: PASSED" << endl;
}

int main() {
    cout << "==========================================================" << endl;
    cout << "  Comprehensive Unit Test: StreamPreviewer (Task 24)      " << endl;
    cout << "==========================================================" << endl;

    test_container_detection();
    test_mime_type_mapping();
    test_mp4_faststart_moov_detection();
    test_contiguous_playable_range();
    test_preview_readiness_assessment();
    test_shared_non_exclusive_file_slice_reading();
    test_http_206_partial_content_response();
    test_live_local_server_streaming_socket();

    cout << "\n==========================================================" << endl;
    cout << "  [PASSED] 100% OF INSTANT STREAM PREVIEW VERIFIED!        " << endl;
    cout << "==========================================================" << endl;

    return 0;
}
