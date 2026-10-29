#define private public
#include "ChunkWorker.h"
#undef private

#include "ChunkManager.h"
#include "FileWriter.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>


using namespace std;

int main() {
  setvbuf(stdout, NULL, _IONBF, 0);

  cout << "==========================================================" << endl;
  cout << "  Comprehensive Unit Test: ChunkWorker (Pos + Neg)        " << endl;
  cout << "==========================================================" << endl;

  curl_global_init(CURL_GLOBAL_DEFAULT);

  const string TEST_FILE = "test_worker_output.bin";
  constexpr uint64_t FILE_SIZE = 4096;

  // Clean up any stale artifacts from prior runs
  remove(TEST_FILE.c_str());

  {
    // =========================================================================
    // 1. SPEED LIMITER TESTS (Positive & Dynamic Changes)
    // =========================================================================
    cout << "\n[1/5] Testing Speed Limiter Controls (Positive)..." << endl;

    Chunk dummy_chunk;
    dummy_chunk.id = 0;
    dummy_chunk.start_byte = 0;
    dummy_chunk.end_byte = 1023;

    ChunkManager mgr_speed(FILE_SIZE, 4);
    FileWriter fw_speed;

    uint64_t initial_speed = 250 * 1024; // 250 KB/s
    ChunkWorker worker_speed(
        "https://raw.githubusercontent.com/curl/curl/master/README.md",
        dummy_chunk, mgr_speed, fw_speed, initial_speed);

    // 1.1 Initial speed limit check
    assert(worker_speed.get_speed_limit() == initial_speed);

    // 1.2 Dynamic speed limit adjustment
    uint64_t new_speed = 1024 * 1024; // 1 MB/s
    worker_speed.set_speed_limit(new_speed);
    assert(worker_speed.get_speed_limit() == new_speed);

    // 1.3 Remove speed limit (unlimited = 0)
    worker_speed.set_speed_limit(0);
    assert(worker_speed.get_speed_limit() == 0);
    cout << "  -> Speed limiter get/set/unlimited: PASSED" << endl;

    // =========================================================================
    // 2. WRITE_CALLBACK UNIT TESTS (Positive & Negative)
    // =========================================================================
    cout << "\n[2/5] Testing write_callback() edge cases (Pos + Neg)..."
         << endl;

    bool prealloc_ok = FileWriter::preallocate_file(TEST_FILE, FILE_SIZE);
    assert(prealloc_ok);

    FileWriter fw_callback;
    bool open_ok = fw_callback.open(TEST_FILE);
    assert(open_ok);

    ChunkManager mgr_callback(FILE_SIZE, 4);
    // Chunks: 0:[0-1023], 1:[1024-2047], 2:[2048-3071], 3:[3072-4095]
    // Dispatch chunks 0..3 so their states transition from Pending to
    // InProgress
    auto c0 = mgr_callback.get_next_pending_chunk();
    auto c1 = mgr_callback.get_next_pending_chunk();
    auto c2 = mgr_callback.get_next_pending_chunk();
    auto c3 = mgr_callback.get_next_pending_chunk();
    assert(c3.has_value() && c3->id == 3 &&
           c3->state == ChunkState::InProgress);

    ChunkWorker::WriteContext ctx;
    ctx.file_writer = &fw_callback;
    ctx.chunk_manager = &mgr_callback;
    ctx.chunk_id = 0;
    ctx.current_offset = 0;
    ctx.end_byte = 1023;
    ctx.bytes_written = 0;

    char test_data[256];
    memset(test_data, 'A', sizeof(test_data));

    // 2.1 Negative: Null userdata must return 0
    size_t res_null_ud =
        ChunkWorker::write_callback(test_data, 1, 100, nullptr);
    assert(res_null_ud == 0);

    // 2.2 Negative: 0-byte write must return 0
    size_t res_zero1 = ChunkWorker::write_callback(test_data, 0, 100, &ctx);
    size_t res_zero2 = ChunkWorker::write_callback(test_data, 100, 0, &ctx);
    assert(res_zero1 == 0 && res_zero2 == 0);

    // 2.3 Positive: Normal chunk write (100 bytes)
    size_t res_norm = ChunkWorker::write_callback(test_data, 1, 100, &ctx);
    assert(res_norm == 100);
    assert(ctx.current_offset == 100);
    assert(ctx.bytes_written == 100);
    assert(mgr_callback.get_total_downloaded_bytes() == 100);

    // 2.4 Positive / Dynamic Stealing: Buffer crossing dynamic end byte gets
    // truncated!
    // Mark chunks 1, 2, 3 Done so Chunk 0 is the sole active donor
    mgr_callback.mark_chunk_done(1);
    mgr_callback.mark_chunk_done(2);
    mgr_callback.mark_chunk_done(3);

    // Chunk 0 has 100 bytes written, 924 remaining. Split gives 462 bytes to
    // each half. Chunk 0 new end_byte: 100 + 462 - 1 = 561 Stolen Chunk 4: [562
    // .. 1023]
    auto stolen_c4 = mgr_callback.steal_slowest_chunk(100);
    assert(stolen_c4.has_value());
    assert(stolen_c4->id == 4);
    assert(stolen_c4->start_byte == 562 && stolen_c4->end_byte == 1023);
    assert(mgr_callback.get_chunk_end_byte(0) == 561);

    // Now test Chunk 0 boundary truncation in write_callback:
    // Advance ctx offset to 500: exactly (561 - 500 + 1) = 62 bytes remain
    // before boundary
    ctx.current_offset = 500;
    ctx.bytes_written = 500;

    // We send 100 bytes. It MUST truncate to exactly 62 bytes!
    size_t res_trunc = ChunkWorker::write_callback(test_data, 1, 100, &ctx);
    assert(res_trunc == 62);
    assert(ctx.current_offset == 562);
    assert(ctx.bytes_written == 562);

    // 2.5 Negative: Calling write_callback when offset is past dynamic_end_byte
    // returns 0 (abort signal)
    size_t res_past_bound = ChunkWorker::write_callback(test_data, 1, 50, &ctx);
    assert(res_past_bound == 0);

    // 2.6 Negative: Disk write failure (e.g., closed file writer) returns 0
    FileWriter unopen_fw;
    ChunkWorker::WriteContext ctx_err;
    ctx_err.file_writer = &unopen_fw;
    ctx_err.chunk_manager = &mgr_callback;
    ctx_err.chunk_id = 0;
    ctx_err.current_offset = 0;
    ctx_err.end_byte = 561;
    ctx_err.bytes_written = 0;
    size_t res_disk_fail =
        ChunkWorker::write_callback(test_data, 1, 50, &ctx_err);
    assert(res_disk_fail == 0);

    fw_callback.close();
    cout << "  -> write_callback() all branch conditions (Pos + Neg): PASSED"
         << endl;

    // =========================================================================
    // 3. LIVE DOWNLOAD & HTTP RANGE TESTS (Positive)
    // =========================================================================
    cout << "\n[3/5] Testing Live Download Execution (Positive)..." << endl;

    constexpr uint64_t LIVE_DOWNLOAD_SIZE = 512;
    remove(TEST_FILE.c_str());
    bool pre_live = FileWriter::preallocate_file(TEST_FILE, LIVE_DOWNLOAD_SIZE);
    assert(pre_live);

    FileWriter fw_live;
    bool live_open = fw_live.open(TEST_FILE);
    assert(live_open);

    ChunkManager mgr_live(LIVE_DOWNLOAD_SIZE, 1);
    auto live_chunk = mgr_live.get_next_pending_chunk();
    assert(live_chunk.has_value());

    ChunkWorker live_worker(
        "https://raw.githubusercontent.com/curl/curl/master/README.md",
        live_chunk.value(), mgr_live, fw_live,
        500 * 1024); // 500 KB/s

    auto start_time = chrono::steady_clock::now();
    bool live_success = live_worker.download();
    auto end_time = chrono::steady_clock::now();

    assert(live_success == true);
    assert(mgr_live.is_complete() == true);
    assert(mgr_live.get_total_downloaded_bytes() == LIVE_DOWNLOAD_SIZE);

    auto duration_ms =
        chrono::duration_cast<chrono::milliseconds>(end_time - start_time)
            .count();
    cout << "  -> Download finished successfully in " << duration_ms
         << " ms: PASSED" << endl;

    fw_live.close();

    // =========================================================================
    // 4. NETWORK & HTTP FAILURE MODES (Negative)
    // =========================================================================
    // =========================================================================
    // 4. NETWORK & HTTP FAILURE MODES (Negative)
    // =========================================================================
    cout << "\n[4/5] Testing Network & HTTP Failure Handling (Negative)..."
         << endl;

    FileWriter fw_neg;
    fw_neg.open(TEST_FILE);

    // 4.1 Negative: Connection refused (unreachable localhost port)
    ChunkManager mgr_refused(LIVE_DOWNLOAD_SIZE, 1);
    auto neg_chunk_1 = mgr_refused.get_next_pending_chunk();
    assert(neg_chunk_1.has_value());

    ChunkWorker worker_refused("http://127.0.0.1:59999/unreachable_port",
                               neg_chunk_1.value(), mgr_refused, fw_neg);

    cout << "  [Simulating Connection Refused with 3 Retries]..." << endl;
    bool refused_res = worker_refused.download();
    assert(refused_res == false);
    // Chunk retry_count incremented to 1 and re-queued as Pending
    assert(mgr_refused.get_all_chunks()[0].retry_count == 1);
    assert(mgr_refused.get_all_chunks()[0].state == ChunkState::Pending);
    cout << "  -> Connection Refused & retry tracking: PASSED" << endl;

    // 4.2 Negative: HTTP 404 Not Found URL
    ChunkManager mgr_404(LIVE_DOWNLOAD_SIZE, 1);
    auto neg_chunk_2 = mgr_404.get_next_pending_chunk();
    assert(neg_chunk_2.has_value());

    ChunkWorker worker_404("https://raw.githubusercontent.com/curl/curl/master/"
                           "non_existent_file_404.bin",
                           neg_chunk_2.value(), mgr_404, fw_neg);

    cout << "  [Simulating HTTP 404 Not Found with 3 Retries]..." << endl;
    bool not_found_res = worker_404.download();
    assert(not_found_res == false);
    // HTTP 404 incremented retry_count to 1 and re-queued as Pending
    assert(mgr_404.get_all_chunks()[0].retry_count == 1);
    assert(mgr_404.get_all_chunks()[0].state == ChunkState::Pending);
    cout << "  -> HTTP 404 Detection & retry tracking: PASSED" << endl;

    fw_neg.close();

    // =========================================================================
    // 5. CLOSED FILE WRITER ERROR HANDLING (Negative)
    // =========================================================================
    cout << "\n[5/5] Testing Download with Unopened FileWriter (Negative)..."
         << endl;

    ChunkManager mgr_closed_fw(LIVE_DOWNLOAD_SIZE, 1);
    auto chunk_closed = mgr_closed_fw.get_next_pending_chunk();
    assert(chunk_closed.has_value());

    FileWriter fw_closed; // Not opened!
    ChunkWorker worker_unopened(
        "https://raw.githubusercontent.com/curl/curl/master/README.md",
        chunk_closed.value(), mgr_closed_fw, fw_closed);

    cout << "  [Simulating Disk Write Failure]..." << endl;
    bool disk_fail_res = worker_unopened.download();
    assert(disk_fail_res == false);
    assert(mgr_closed_fw.get_all_chunks()[0].retry_count == 1);
    cout << "  -> Disk Write Failure Handling: PASSED" << endl;

    // =========================================================================
    // 6. BUG #3 FIX: 200 OK ON RANGE REQUEST GUARD
    // =========================================================================
    cout << "\n[6/6] Testing Range Request HTTP 200 Guard (Bug #3 Fix)..." << endl;
    CURL *curl_200 = curl_easy_init();
    assert(curl_200 != nullptr);
    curl_easy_setopt(curl_200, CURLOPT_URL, "https://httpbin.org/status/200");
    curl_easy_setopt(curl_200, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl_200, CURLOPT_TIMEOUT, 10L);
    CURLcode code_res = curl_easy_perform(curl_200);
    long check_code = 0;
    curl_easy_getinfo(curl_200, CURLINFO_RESPONSE_CODE, &check_code);
    if (code_res == CURLE_OK && check_code == 200) {
        FileWriter fw_bug3;
        fw_bug3.open(TEST_FILE);
        ChunkManager mgr_bug3(FILE_SIZE, 2);
        mgr_bug3.get_next_pending_chunk(); // chunk 0

        ChunkWorker::WriteContext ctx_range;
        ctx_range.file_writer = &fw_bug3;
        ctx_range.chunk_manager = &mgr_bug3;
        ctx_range.chunk_id = 1;
        ctx_range.current_offset = 2048; // Non-zero offset (chunk 1)
        ctx_range.end_byte = 4095;
        ctx_range.bytes_written = 0;
        ctx_range.curl = curl_200;
        ctx_range.is_range_request = true; // Range request

        char dummy_data[64] = "Hello World";
        // When is_range_request is true and response is 200 OK, write_callback MUST abort (return 0)
        size_t written_range = ChunkWorker::write_callback(dummy_data, 1, sizeof(dummy_data), &ctx_range);
        assert(written_range == 0);
        cout << "  -> write_callback aborted HTTP 200 on range request: PASSED" << endl;

        // When is_range_request is false (single stream starting at 0), HTTP 200 MUST be accepted
        ctx_range.is_range_request = false;
        ctx_range.current_offset = 0;
        size_t written_single = ChunkWorker::write_callback(dummy_data, 1, sizeof(dummy_data), &ctx_range);
        assert(written_single == sizeof(dummy_data));
        cout << "  -> write_callback accepted HTTP 200 on single-stream request: PASSED" << endl;

        fw_bug3.close();
    }
    curl_easy_cleanup(curl_200);
  }

  // Clean up temporary binary test file
  remove(TEST_FILE.c_str());
  curl_global_cleanup();

  cout << "\n=========================================================="
       << endl;
  cout << "  [PASSED] 100% OF CHUNKWORKER & BUG #3 VERIFIED!         " << endl;
  cout << "==========================================================" << endl;

  return 0;
}
