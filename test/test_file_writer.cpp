#include "FileWriter.h"
#include "ThreadPool.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace std;

static uint64_t get_file_size_on_disk(const string &path) {
    ifstream in(path, ios::binary | ios::ate);
    if (!in.is_open()) return 0;
    return static_cast<uint64_t>(in.tellg());
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);

    cout << "==========================================================" << endl;
    cout << "  Comprehensive Unit Test: FileWriter (Pos + Neg)         " << endl;
    cout << "==========================================================" << endl;

    const string TEST_FILE_1 = "test_fw_output1.bin";
    const string TEST_FILE_2 = "test_fw_output2.bin";
    const string TEST_ZERO = "test_fw_zero.bin";

    // Clean up any stale files from previous runs
    remove(TEST_FILE_1.c_str());
    remove(TEST_FILE_2.c_str());
    remove(TEST_ZERO.c_str());

    // =========================================================================
    // 1. PREALLOCATION TESTS (Positive & Negative)
    // =========================================================================
    cout << "\n[1/5] Testing preallocate_file() (Pos + Neg)..." << endl;

    // 1.1 Positive: Normal preallocation (1000 bytes)
    constexpr uint64_t FILE_SIZE = 1000;
    bool prealloc_ok = FileWriter::preallocate_file(TEST_FILE_1, FILE_SIZE);
    assert(prealloc_ok == true);
    assert(get_file_size_on_disk(TEST_FILE_1) == FILE_SIZE);

    // 1.2 Positive / Edge: Preallocate 0-byte file
    bool prealloc_zero_ok = FileWriter::preallocate_file(TEST_ZERO, 0);
    assert(prealloc_zero_ok == true);
    assert(get_file_size_on_disk(TEST_ZERO) == 0);

    // 1.3 Negative: Preallocate to non-existent drive/path
    bool prealloc_bad_path = FileWriter::preallocate_file("Z:/non_existent_drive_9999/test.bin", 1000);
    assert(prealloc_bad_path == false);

    // 1.4 Negative: Preallocate with empty path
    bool prealloc_empty = FileWriter::preallocate_file("", 1000);
    assert(prealloc_empty == false);
    cout << "  -> preallocate_file() all branches: PASSED" << endl;

    // =========================================================================
    // 2. OPEN & HANDLE GUARDS (Positive & Negative)
    // =========================================================================
    cout << "\n[2/5] Testing open() & is_open() (Pos + Neg)..." << endl;

    FileWriter writer;
    assert(writer.is_open() == false);

    // 2.1 Positive: Opening existing preallocated file
    bool open_ok = writer.open(TEST_FILE_1);
    assert(open_ok == true);
    assert(writer.is_open() == true);

    // 2.2 Positive (Handle re-use): Calling open on another file while already open
    bool prealloc2 = FileWriter::preallocate_file(TEST_FILE_2, 500);
    assert(prealloc2 == true);
    bool reopen_ok = writer.open(TEST_FILE_2);
    assert(reopen_ok == true);
    assert(writer.is_open() == true);

    // Switch back to TEST_FILE_1
    writer.open(TEST_FILE_1);
    assert(writer.is_open() == true);

    // 2.3 Negative: Opening non-existent file must fail (enforces OPEN_EXISTING)
    FileWriter writer_bad;
    bool open_non_exist = writer_bad.open("non_existent_file_xyz_12345.bin");
    assert(open_non_exist == false);
    assert(writer_bad.is_open() == false);

    // 2.4 Negative: Opening empty path
    bool open_empty = writer_bad.open("");
    assert(open_empty == false);
    assert(writer_bad.is_open() == false);
    cout << "  -> open() & handle re-use (Pos + Neg): PASSED" << endl;

    // =========================================================================
    // 3. CLOSE IDEMPOTENCE & DESTRUCTOR (Positive)
    // =========================================================================
    cout << "\n[3/5] Testing close() Idempotence..." << endl;

    writer.close();
    assert(writer.is_open() == false);

    // Calling close() again must be safe and idempotent
    writer.close();
    assert(writer.is_open() == false);
    cout << "  -> close() idempotence: PASSED" << endl;

    // =========================================================================
    // 4. WRITE AT OFFSET GUARDS & VALIDATION (Positive & Negative)
    // =========================================================================
    cout << "\n[4/5] Testing write_at_offset() Guards (Pos + Neg)..." << endl;

    char sample_data[64];
    memset(sample_data, 'X', sizeof(sample_data));

    // 4.1 Negative: Writing to an unopened writer must fail
    assert(writer.is_open() == false);
    bool write_unopened = writer.write_at_offset(0, sample_data, sizeof(sample_data));
    assert(write_unopened == false);

    // Reopen for positive checks
    writer.open(TEST_FILE_1);
    assert(writer.is_open() == true);

    // 4.2 Negative: Writing with null data pointer must fail
    bool write_null = writer.write_at_offset(0, nullptr, sizeof(sample_data));
    assert(write_null == false);

    // 4.3 Negative: Writing with 0 length must fail
    bool write_zero_len = writer.write_at_offset(0, sample_data, 0);
    assert(write_zero_len == false);

    // 4.4 Positive: Non-contiguous single-threaded writes
    bool write_head = writer.write_at_offset(0, "HEAD", 4);
    assert(write_head == true);

    bool write_mid = writer.write_at_offset(500, "MIDD", 4);
    assert(write_mid == true);

    bool write_tail = writer.write_at_offset(996, "TAIL", 4);
    assert(write_tail == true);
    cout << "  -> write_at_offset() parameter guards: PASSED" << endl;

    writer.close();

    // =========================================================================
    // 5. CONCURRENT MULTI-THREADED WRITES (Positive Integrity)
    // =========================================================================
    cout << "\n[5/5] Testing Multi-Threaded Concurrent Offset Writes..." << endl;

    // Re-preallocate fresh 1000-byte file
    FileWriter::preallocate_file(TEST_FILE_1, FILE_SIZE);
    writer.open(TEST_FILE_1);

    constexpr int NUM_THREADS = 4;
    constexpr uint64_t CHUNK_SIZE = 250;

    {
        ThreadPool pool(NUM_THREADS);
        for (int i = 0; i < NUM_THREADS; ++i) {
            uint64_t offset = i * CHUNK_SIZE;
            char fill_char = 'A' + i;

            pool.enqueue([&writer, offset, fill_char, CHUNK_SIZE]() {
                vector<char> buffer(CHUNK_SIZE, fill_char);
                bool write_ok = writer.write_at_offset(offset, buffer.data(), buffer.size());
                assert(write_ok == true);
            });
        }
    } // ThreadPool scope exits here, waiting for all 4 threads to complete

    writer.close();
    assert(writer.is_open() == false);

    // Byte-for-byte readback verification
    ifstream infile(TEST_FILE_1, ios::binary);
    assert(infile.is_open());

    vector<char> file_content((istreambuf_iterator<char>(infile)), istreambuf_iterator<char>());
    assert(file_content.size() == FILE_SIZE);

    for (size_t i = 0; i < 250; ++i) assert(file_content[i] == 'A');
    for (size_t i = 250; i < 500; ++i) assert(file_content[i] == 'B');
    for (size_t i = 500; i < 750; ++i) assert(file_content[i] == 'C');
    for (size_t i = 750; i < 1000; ++i) assert(file_content[i] == 'D');

    infile.close();
    cout << "  -> Multi-threaded concurrency & byte-for-byte readback: PASSED" << endl;

    // Clean up temporary binary test files
    remove(TEST_FILE_1.c_str());
    remove(TEST_FILE_2.c_str());
    remove(TEST_ZERO.c_str());

    cout << "\n==========================================================" << endl;
    cout << "  [PASSED] 100% OF FILEWRITER FUNCTIONS VERIFIED!         " << endl;
    cout << "==========================================================" << endl;

    return 0;
}
