#include "ChunkWorker.h"
#include "ChunkManager.h"
#include "FileWriter.h"
#include <iostream>
#include <fstream>
#include <cassert>
#include <cstdio>

using namespace std; // <-- Placed AFTER all includes

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);

    cout << "[Test] Starting ChunkWorker verification test..." << endl;

    const string TEST_URL = "http://httpbin.org/bytes/1000";
    const string OUTPUT_FILE = "chunk_worker_test.bin";
    constexpr uint64_t FILE_SIZE = 1000;

    bool prealloc_ok = FileWriter::preallocate_file(OUTPUT_FILE, FILE_SIZE);
    assert(prealloc_ok);

    FileWriter writer;
    bool open_ok = writer.open(OUTPUT_FILE);
    assert(open_ok);

    ChunkManager manager(FILE_SIZE, 2);

    auto chunk0_opt = manager.get_next_pending_chunk();
    assert(chunk0_opt.has_value());
    Chunk chunk0 = chunk0_opt.value();

    cout << "[Test] Downloading Chunk 0 (bytes " 
         << chunk0.start_byte << "-" << chunk0.end_byte << ")..." << endl;

    ChunkWorker worker0(TEST_URL, chunk0, manager, writer);
    bool ok0 = worker0.download();
    assert(ok0);

    auto chunks = manager.get_all_chunks();
    assert(chunks[0].state == ChunkState::Done);
    cout << "[Test] Chunk 0 downloaded successfully!" << endl;

    writer.close();

    ifstream infile(OUTPUT_FILE, ios::binary | ios::ate);
    assert(infile.is_open());
    assert(static_cast<uint64_t>(infile.tellg()) == FILE_SIZE);
    infile.close();

    remove(OUTPUT_FILE.c_str());

    cout << "[PASSED] ChunkWorker integration test completed successfully!" << endl;
    return 0;
}
