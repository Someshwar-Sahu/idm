#include "ChunkManager.h"
#include <iostream>
#include <cassert>
#include <cstdio>

using namespace std;

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);

    cout << "==========================================================" << endl;
    cout << "  Comprehensive Unit Test: Chunk & ChunkManager (Pos + Neg) " << endl;
    cout << "==========================================================" << endl;

    // =========================================================================
    // 1. CHUNK STRUCT UNIT TESTS (Positive & Negative)
    // =========================================================================
    cout << "\n[1/9] Testing Chunk Struct Methods..." << endl;

    // 1.1 Positive: total_bytes() on normal range
    Chunk c_valid;
    c_valid.start_byte = 500;
    c_valid.end_byte = 999;
    assert(c_valid.total_bytes() == 500);

    // 1.2 Negative: total_bytes() on inverted range (start > end)
    Chunk c_inverted;
    c_inverted.start_byte = 1000;
    c_inverted.end_byte = 500;
    assert(c_inverted.total_bytes() == 0); // Must return 0 without underflow!

    // 1.3 Positive: state_to_string() for valid states
    assert(Chunk::state_to_string(ChunkState::Pending) == "Pending");
    assert(Chunk::state_to_string(ChunkState::InProgress) == "InProgress");
    assert(Chunk::state_to_string(ChunkState::Done) == "Done");
    assert(Chunk::state_to_string(ChunkState::Failed) == "Failed");

    // 1.4 Negative: state_to_string() for unknown/corrupt enum value
    assert(Chunk::state_to_string(static_cast<ChunkState>(999)) == "Unknown");
    cout << "  -> Chunk struct methods (Pos + Neg): PASSED" << endl;

    // =========================================================================
    // 2. INITIALIZATION TESTS (Positive & Negative)
    // =========================================================================
    cout << "\n[2/9] Testing ChunkManager Initialization..." << endl;

    // 2.1 Positive: Normal file split (1000 bytes, 4 chunks)
    constexpr uint64_t FILE_SIZE = 1000;
    constexpr int NUM_CHUNKS = 4;
    ChunkManager manager(FILE_SIZE, NUM_CHUNKS);

    assert(manager.get_total_file_size() == FILE_SIZE);
    assert(manager.get_all_chunks().size() == NUM_CHUNKS);

    auto chunks = manager.get_all_chunks();
    assert(chunks[0].start_byte == 0 && chunks[0].end_byte == 249);
    assert(chunks[1].start_byte == 250 && chunks[1].end_byte == 499);
    assert(chunks[2].start_byte == 500 && chunks[2].end_byte == 749);
    assert(chunks[3].start_byte == 750 && chunks[3].end_byte == 999);

    // 2.2 Negative: 0-byte file size initialization
    ChunkManager zero_size_mgr(0, 4);
    assert(zero_size_mgr.get_all_chunks().empty());
    assert(zero_size_mgr.get_total_file_size() == 0);

    // 2.3 Negative: 0 chunks and negative chunks initialization
    ChunkManager zero_chunks_mgr(1000, 0);
    assert(zero_chunks_mgr.get_all_chunks().empty());

    ChunkManager neg_chunks_mgr(1000, -5);
    assert(neg_chunks_mgr.get_all_chunks().empty());
    cout << "  -> Initialization edge cases (Pos + Neg): PASSED" << endl;

    // =========================================================================
    // 3. PENDING CHUNK DISPATCH TESTS (Positive & Negative)
    // =========================================================================
    cout << "\n[3/9] Testing get_next_pending_chunk()..." << endl;

    // 3.1 Positive: sequentially fetching all 4 chunks
    auto c0 = manager.get_next_pending_chunk();
    assert(c0.has_value() && c0->id == 0 && c0->state == ChunkState::InProgress);

    auto c1 = manager.get_next_pending_chunk();
    assert(c1.has_value() && c1->id == 1 && c1->state == ChunkState::InProgress);

    auto c2 = manager.get_next_pending_chunk();
    assert(c2.has_value() && c2->id == 2 && c2->state == ChunkState::InProgress);

    auto c3 = manager.get_next_pending_chunk();
    assert(c3.has_value() && c3->id == 3 && c3->state == ChunkState::InProgress);

    // 3.2 Negative: all chunks are now InProgress -> must return nullopt
    auto c_none = manager.get_next_pending_chunk();
    assert(!c_none.has_value());
    cout << "  -> get_next_pending_chunk() dispatch (Pos + Neg): PASSED" << endl;

    // =========================================================================
    // 4. PROGRESS UPDATE TESTS (Positive & Negative)
    // =========================================================================
    cout << "\n[4/9] Testing update_chunk_progress()..." << endl;

    // 4.1 Positive: Valid progress updates
    manager.update_chunk_progress(0, 100);
    manager.update_chunk_progress(1, 50);
    assert(manager.get_total_downloaded_bytes() == 150);

    // 4.2 Negative: Out-of-bounds chunk IDs must NOT crash or corrupt memory
    manager.update_chunk_progress(-1, 500);  // Negative ID
    manager.update_chunk_progress(999, 500); // Beyond chunk count
    assert(manager.get_total_downloaded_bytes() == 150); // Total unchanged!
    cout << "  -> update_chunk_progress() out-of-bounds guards: PASSED" << endl;

    // =========================================================================
    // 5. CHUNK COMPLETION & BOUNDARY GETTER TESTS (Positive & Negative)
    // =========================================================================
    cout << "\n[5/9] Testing mark_chunk_done() and get_chunk_end_byte()..." << endl;

    // 5.1 Positive: Marking chunks done sets downloaded = total_bytes
    manager.mark_chunk_done(0); // 250 bytes
    manager.mark_chunk_done(1); // 250 bytes
    assert(manager.get_total_downloaded_bytes() == 500);

    // 5.2 Positive: Querying valid chunk end byte
    assert(manager.get_chunk_end_byte(0) == 249);
    assert(manager.get_chunk_end_byte(3) == 999);

    // 5.3 Negative: mark_chunk_done with invalid IDs
    manager.mark_chunk_done(-1);
    manager.mark_chunk_done(999);
    assert(manager.get_total_downloaded_bytes() == 500);

    // 5.4 Negative: get_chunk_end_byte with invalid IDs
    assert(manager.get_chunk_end_byte(-1) == 0);
    assert(manager.get_chunk_end_byte(999) == 0);
    cout << "  -> mark_chunk_done() & get_chunk_end_byte() (Pos + Neg): PASSED" << endl;

    // =========================================================================
    // 6. RETRY & FAILURE STATE MACHINE TESTS (Positive & Negative)
    // =========================================================================
    cout << "\n[6/9] Testing mark_chunk_failed() & Retry State Machine..." << endl;

    // 6.1 Positive: First failure increments retry count and resets state to Pending
    manager.mark_chunk_failed(2, 3); // Attempt 1 of 3
    auto retry_c2_attempt1 = manager.get_next_pending_chunk();
    assert(retry_c2_attempt1.has_value() && retry_c2_attempt1->id == 2);
    assert(retry_c2_attempt1->retry_count == 1);

    // 6.2 Positive: Second failure
    manager.mark_chunk_failed(2, 3); // Attempt 2 of 3
    auto retry_c2_attempt2 = manager.get_next_pending_chunk();
    assert(retry_c2_attempt2.has_value() && retry_c2_attempt2->id == 2);
    assert(retry_c2_attempt2->retry_count == 2);

    // 6.3 Negative/Terminal: Third failure exceeds max_retries -> Permanently Failed!
    manager.mark_chunk_failed(2, 3); // Attempt 3 of 3 (max reached)
    auto retry_c2_exhausted = manager.get_next_pending_chunk();
    assert(!retry_c2_exhausted.has_value()); // Cannot be scheduled again!
    assert(manager.get_all_chunks()[2].state == ChunkState::Failed);

    // 6.4 Negative: mark_chunk_failed with invalid IDs
    manager.mark_chunk_failed(-1, 3);
    manager.mark_chunk_failed(999, 3);
    cout << "  -> Retry & Terminal Failure State Machine: PASSED" << endl;

    // Reset chunk 2 back to Done so we can continue testing
    manager.mark_chunk_done(2);

    // =========================================================================
    // 7. DYNAMIC CHUNK STEALING TESTS (Positive & Negative)
    // =========================================================================
    cout << "\n[7/9] Testing steal_slowest_chunk() (Positive & Negative)..." << endl;

    // Current State: Chunk 0, 1, 2 are Done. Chunk 3 is InProgress (750 to 999 = 250 bytes).
    // 7.1 Negative: Attempting to steal with threshold higher than remaining bytes
    auto steal_too_large = manager.steal_slowest_chunk(500); // 500 > 250 remaining
    assert(!steal_too_large.has_value()); // Must reject stealing!

    // 7.2 Positive: Stealing with reasonable threshold (100 bytes)
    auto stolen_chunk = manager.steal_slowest_chunk(100);
    assert(stolen_chunk.has_value());
    assert(stolen_chunk->id == 4); // Created 5th chunk!
    
    // Chunk 3 had 250 bytes remaining. Half = 125 bytes.
    // Chunk 3 ends at: 750 + 125 - 1 = 874.
    // Chunk 4 starts at 875, ends at 999.
    assert(manager.get_chunk_end_byte(3) == 874);
    assert(stolen_chunk->start_byte == 875 && stolen_chunk->end_byte == 999);
    assert(manager.get_all_chunks().size() == 5);

    // 7.3 Negative: Stealing again when remaining bytes are now below threshold
    // Chunk 3 now has 125 bytes, Chunk 4 has 125 bytes.
    // Asking to steal with threshold = 200 must fail!
    auto steal_again_rejected = manager.steal_slowest_chunk(200);
    assert(!steal_again_rejected.has_value());

    // Finish chunks 3 and 4
    manager.mark_chunk_done(3);
    manager.mark_chunk_done(4);

    // 7.4 Negative: Stealing when all chunks are Done
    auto steal_when_all_done = manager.steal_slowest_chunk(10);
    assert(!steal_when_all_done.has_value());
    cout << "  -> Dynamic Chunk Stealing (Pos + Neg): PASSED" << endl;

    // =========================================================================
    // 8. COMPLETION DETECTION TESTS (Positive & Negative)
    // =========================================================================
    cout << "\n[8/9] Testing is_complete()..." << endl;

    // 8.1 Positive: All chunks are Done -> is_complete() is true
    assert(manager.is_complete() == true);
    assert(manager.get_total_downloaded_bytes() == FILE_SIZE);

    // 8.2 Negative: If even 1 chunk is incomplete, is_complete() must be false
    ChunkManager partial_mgr(1000, 2);
    assert(partial_mgr.is_complete() == false);
    partial_mgr.mark_chunk_done(0);
    assert(partial_mgr.is_complete() == false); // Chunk 1 still pending!
    cout << "  -> is_complete() validation (Pos + Neg): PASSED" << endl;

    // =========================================================================
    // 9. CONCURRENCY INTEGRITY SUMMARY
    // =========================================================================
    cout << "\n[9/9] All Chunk and ChunkManager functions tested." << endl;
    cout << "==========================================================" << endl;
    cout << "  [PASSED] 100% OF CHUNK & CHUNKMANAGER FUNCTIONS VERIFIED!" << endl;
    cout << "==========================================================" << endl;

    return 0;
}
