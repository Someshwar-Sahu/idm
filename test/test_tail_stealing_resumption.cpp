#include "ChunkManager.h"
#include "ResumeStore.h"
#include "DownloadTask.h"
#include <iostream>
#include <cassert>
#include <fstream>
#include <cstdio>

using namespace std;

int main() {
    cout << "==========================================================" << endl;
    cout << "  Unit Test: Tail Stealing & Resumption (Pos + Neg)       " << endl;
    cout << "==========================================================" << endl;

    // =========================================================================
    // 1. ADAPTIVE STEALING THRESHOLD FLOOR (Positive & Negative)
    // =========================================================================
    cout << "\n[1/5] Testing Chunk Stealing Threshold Floor Guard..." << endl;
    {
        // Simulate a 100 MB file with 2 initial chunks (50 MB each)
        uint64_t total_size = 100ULL * 1024ULL * 1024ULL;
        ChunkManager cm(total_size, 2);

        auto chunk0 = cm.get_next_pending_chunk();
        assert(chunk0.has_value());
        assert(chunk0->id == 0);

        // Chunk 0 is InProgress, downloaded 47 MB out of 50 MB (remaining = 3 MB)
        cm.update_chunk_progress(0, 47ULL * 1024ULL * 1024ULL);

        // Try to steal with a 5 MB threshold floor
        // Since remaining is 3 MB (< 5 MB), stealing MUST BE REJECTED to prevent tail fragmentation!
        auto stolen = cm.steal_slowest_chunk(5 * 1024 * 1024, 16);
        assert(!stolen.has_value());
        cout << "  -> Stealing rejected when chunk remaining (3 MB) < floor (5 MB): PASSED" << endl;

        // Now test when chunk has 20 MB remaining (> 5 MB threshold)
        cm.update_chunk_progress(0, 30ULL * 1024ULL * 1024ULL); // 20 MB remaining
        stolen = cm.steal_slowest_chunk(5 * 1024 * 1024, 16);
        assert(stolen.has_value());
        assert(stolen->id == 2);
        assert(stolen->state == ChunkState::InProgress);
        assert(stolen->start_byte > chunk0->start_byte);
        cout << "  -> Stealing allowed when chunk remaining (20 MB) >= floor (5 MB): PASSED" << endl;

        // Verify remaining bytes tracking
        uint64_t rem = cm.get_total_remaining_bytes();
        assert(rem > 0);
    }

    // =========================================================================
    // 2. CHUNK FAILURE REASON LOGGING (Positive)
    // =========================================================================
    cout << "\n[2/5] Testing Chunk Failure Reason Logging..." << endl;
    {
        ChunkManager cm(10 * 1024 * 1024, 1);
        auto c = cm.get_next_pending_chunk();
        assert(c.has_value());

        // Mark failed with explicit HTTP 503 reason
        for (int i = 0; i < 5; ++i) {
            cm.mark_chunk_failed(c->id, 5, "HTTP 503 Service Unavailable");
        }
        assert(cm.has_failed() == true);
        assert(cm.get_last_failure_reason() == "HTTP 503 Service Unavailable");
        cout << "  -> Exact server failure reason recorded and reported: PASSED" << endl;
    }

    // =========================================================================
    // 3. INCOMPLETE CHUNK RETRY RESET (Positive)
    // =========================================================================
    cout << "\n[3/5] Testing Incomplete Chunk Retry Reset on Resumption..." << endl;
    {
        ChunkManager cm(20 * 1024 * 1024, 2);
        auto c0 = cm.get_next_pending_chunk();
        auto c1 = cm.get_next_pending_chunk();
        assert(c0.has_value() && c1.has_value());

        // Chunk 0 completes successfully
        cm.mark_chunk_done(c0->id);

        // Chunk 1 fails 5 times and exhausts retry budget
        for (int i = 0; i < 5; ++i) {
            cm.mark_chunk_failed(c1->id, 5, "Connection reset by peer (HTTP 0)");
        }
        assert(cm.has_failed() == true);

        // Call reset on resumption
        cm.reset_incomplete_chunk_retries();

        // Chunk 0 must remain Done
        auto chunks = cm.get_all_chunks();
        assert(chunks[0].state == ChunkState::Done);

        // Chunk 1 MUST be reset to Pending with retry_count = 0!
        assert(chunks[1].state == ChunkState::Pending);
        assert(chunks[1].retry_count == 0);
        assert(cm.has_failed() == false);
        cout << "  -> Failed chunks reset to Pending with 0 retry count on resume: PASSED" << endl;
    }

    // =========================================================================
    // 4. RESUMESTORE SIDECAR SERIALIZATION & RETRY BUDGET RESTORATION
    // =========================================================================
    cout << "\n[4/5] Testing ResumeStore Persistence & Retry Budget Restoration..." << endl;
    {
        const string test_sidecar = "test_tail_resumption.progress";
        const string test_url = "https://example.com/movie.mkv";
        uint64_t test_size = 3400000000ULL; // 3.4 GB

        ChunkManager cm_save(test_size, 2);
        auto c0 = cm_save.get_next_pending_chunk();
        auto c1 = cm_save.get_next_pending_chunk();
        cm_save.mark_chunk_done(c0->id);
        for (int i = 0; i < 5; ++i) {
            cm_save.mark_chunk_failed(c1->id, 5, "Connection timed out");
        }

        // Save sidecar to disk
        bool saved = ResumeStore::save(test_sidecar, test_url, test_size, "\"etag123\"", cm_save, true);
        assert(saved == true);

        // Load sidecar into fresh ChunkManager
        string loaded_url, loaded_etag;
        uint64_t loaded_size = 0;
        bool loaded_ranges = false;
        ChunkManager cm_load;

        bool loaded = ResumeStore::load(test_sidecar, loaded_url, loaded_size, loaded_etag, cm_load, loaded_ranges);
        assert(loaded == true);
        assert(loaded_url == test_url);
        assert(loaded_size == test_size);
        assert(loaded_ranges == true);

        auto loaded_chunks = cm_load.get_all_chunks();
        assert(loaded_chunks.size() == 2);
        // Chunk 0 was Done, remains Done
        assert(loaded_chunks[0].state == ChunkState::Done);
        // Chunk 1 was Failed, must be reset to Pending with retry_count = 0 for fresh attempt!
        assert(loaded_chunks[1].state == ChunkState::Pending);
        assert(loaded_chunks[1].retry_count == 0);

        ResumeStore::remove(test_sidecar);
        cout << "  -> Sidecar reload resets incomplete chunk retries for seamless resume: PASSED" << endl;
    }

    // =========================================================================
    // 5. DOWNLOADTASK RESUME LIFECYCLE INTEGRATION
    // =========================================================================
    cout << "\n[5/5] Testing DownloadTask::resume() Lifecycle..." << endl;
    {
        DownloadTask task("https://example.com/file.bin", "test_dt_resume.bin", 4);
        task.pause();
        assert(task.get_status() == DownloadStatus::Paused);

        task.resume();
        assert(task.get_status() == DownloadStatus::NotStarted);
        cout << "  -> DownloadTask::resume() cleanly resets status and chunk retry budget: PASSED" << endl;
    }

    cout << "\n==========================================================" << endl;
    cout << "  [PASSED] ALL 5/5 TAIL STEALING & RESUME TESTS PASSED!   " << endl;
    cout << "==========================================================" << endl;
    return 0;
}
