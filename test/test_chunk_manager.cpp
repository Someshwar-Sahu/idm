#include "ChunkManager.h"
#include <iostream>
#include <cassert>
#include <cstdio>

using namespace std;

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);

    cout << "[Test] Starting ChunkManager verification test..." << endl;

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

    cout << "[Test] Chunk boundaries correctly calculated." << endl;

    auto c1 = manager.get_next_pending_chunk();
    assert(c1.has_value() && c1->id == 0);

    auto c2 = manager.get_next_pending_chunk();
    assert(c2.has_value() && c2->id == 1);

    manager.update_chunk_progress(0, 100);
    assert(manager.get_total_downloaded_bytes() == 100);

    manager.mark_chunk_done(0);
    assert(manager.get_total_downloaded_bytes() == 250);

    manager.mark_chunk_done(1);
    manager.mark_chunk_done(2);
    manager.mark_chunk_done(3);

    assert(manager.is_complete());
    assert(manager.get_total_downloaded_bytes() == FILE_SIZE);

    cout << "[PASSED] ChunkManager test completed successfully!" << endl;
    return 0;
}
