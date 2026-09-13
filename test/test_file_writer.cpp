#include "FileWriter.h"
#include "ThreadPool.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <cassert>
#include <cstdio>

using namespace std;

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);

    cout << "[Test] Starting FileWriter verification test..." << endl;

    const string TEST_FILE = "test_output.bin";
    constexpr uint64_t FILE_SIZE = 1000;

    bool prealloc_ok = FileWriter::preallocate_file(TEST_FILE, FILE_SIZE);
    assert(prealloc_ok);
    cout << "[Test] Pre-allocated " << FILE_SIZE << " bytes on disk." << endl;

    FileWriter writer;
    bool open_ok = writer.open(TEST_FILE);
    assert(open_ok);
    assert(writer.is_open());

    constexpr int NUM_THREADS = 4;
    constexpr uint64_t CHUNK_SIZE = 250;

    {
        ThreadPool pool(NUM_THREADS);
        for (int i = 0; i < NUM_THREADS; ++i) { // Fix: ++i (was ++t)
            uint64_t offset = i * CHUNK_SIZE;
            char fill_char = 'A' + i;

            pool.enqueue([&writer, offset, fill_char, CHUNK_SIZE]() {
                vector<char> buffer(CHUNK_SIZE, fill_char);
                bool write_ok = writer.write_at_offset(offset, buffer.data(), buffer.size());
                assert(write_ok);
            });
        }
    }

    writer.close();
    assert(!writer.is_open());

    ifstream infile(TEST_FILE, ios::binary);
    assert(infile.is_open());

    vector<char> file_content((istreambuf_iterator<char>(infile)), istreambuf_iterator<char>());
    assert(file_content.size() == FILE_SIZE);

    for (size_t i = 0; i < 250; ++i) assert(file_content[i] == 'A');
    for (size_t i = 250; i < 500; ++i) assert(file_content[i] == 'B');
    for (size_t i = 500; i < 750; ++i) assert(file_content[i] == 'C');
    for (size_t i = 750; i < 1000; ++i) assert(file_content[i] == 'D');

    infile.close();

    remove(TEST_FILE.c_str());

    cout << "[PASSED] FileWriter multi-threaded offset write test completed successfully!" << endl;
    return 0;
}
