#include "ResumeStore.h"
#include "ChunkManager.h"
#include <iostream>
#include <fstream>
#include <cassert>
#include <cstdio>
#include <string>

using namespace std;

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);

    cout << "==========================================================" << endl;
    cout << "  Comprehensive Unit Test: ResumeStore (Pos + Neg)        " << endl;
    cout << "==========================================================" << endl;

    const string TEST_VALID_SIDECAR = "test_valid_download.bin.progress.json";
    const string TEST_CORRUPT_SIDECAR = "test_corrupt.json";
    const string TEST_NO_CHUNKS_SIDECAR = "test_no_chunks.json";

    // Clean up stale test artifacts
    ResumeStore::remove(TEST_VALID_SIDECAR);
    ResumeStore::remove(TEST_CORRUPT_SIDECAR);
    ResumeStore::remove(TEST_NO_CHUNKS_SIDECAR);

    // =========================================================================
    // 1. SIDECAR FILEPATH RESOLUTION (Positive)
    // =========================================================================
    cout << "\n[1/5] Testing get_sidecar_filepath()..." << endl;

    // 1.1 Positive: Standard file
    assert(ResumeStore::get_sidecar_filepath("test_file.zip") == "test_file.zip.progress.json");

    // 1.2 Positive: Nested path
    assert(ResumeStore::get_sidecar_filepath("C:/Downloads/video.mp4") == "C:/Downloads/video.mp4.progress.json");

    // 1.3 Positive / Edge: Empty string
    assert(ResumeStore::get_sidecar_filepath("") == ".progress.json");
    cout << "  -> get_sidecar_filepath() resolution: PASSED" << endl;

    // =========================================================================
    // 2. STATE SERIALIZATION & FILE EXISTENCE (Positive & Negative)
    // =========================================================================
    cout << "\n[2/5] Testing save() & exists() (Pos + Neg)..." << endl;

    const string URL = "https://example.com/huge_dataset.tar.gz";
    constexpr uint64_t TOTAL_SIZE = 1000;
    const string ETAG = "\"33a64df551425fcc55e4d42a148795d9f25f89d4\"";

    ChunkManager cm_save(TOTAL_SIZE, 4); // 4 chunks, 250 bytes each
    // Chunk 0: Done (250 bytes)
    cm_save.mark_chunk_done(0);
    // Chunk 1: InProgress (100 bytes)
    cm_save.update_chunk_progress(1, 100);

    // 2.1 Positive: Valid save to disk
    bool save_ok = ResumeStore::save(TEST_VALID_SIDECAR, URL, TOTAL_SIZE, ETAG, cm_save);
    assert(save_ok == true);
    assert(ResumeStore::exists(TEST_VALID_SIDECAR) == true);

    // 2.2 Negative: Saving to non-existent directory/path must fail gracefully
    bool save_bad = ResumeStore::save("Z:/non_existent_drive_9999/test.json", URL, TOTAL_SIZE, ETAG, cm_save);
    assert(save_bad == false);
    cout << "  -> save() and exists() state handling: PASSED" << endl;

    // =========================================================================
    // 3. STATE DESERIALIZATION (Positive)
    // =========================================================================
    cout << "\n[3/5] Testing load() State Reconstruction (Positive)..." << endl;

    string loaded_url;
    uint64_t loaded_total_size = 0;
    string loaded_etag;
    ChunkManager cm_loaded(0, 1);

    bool load_ok = ResumeStore::load(TEST_VALID_SIDECAR, loaded_url, loaded_total_size, loaded_etag, cm_loaded);
    assert(load_ok == true);

    // Verify reconstructed values match originals
    assert(loaded_url == URL);
    assert(loaded_total_size == TOTAL_SIZE);
    assert(loaded_etag == ETAG);

    auto chunks = cm_loaded.get_all_chunks();
    assert(chunks.size() == 4);

    // Chunk 0 was Done with 250 bytes
    assert(chunks[0].id == 0);
    assert(chunks[0].bytes_downloaded == 250);
    assert(chunks[0].state == ChunkState::Done);

    // Chunk 1 had 100 bytes downloaded
    assert(chunks[1].id == 1);
    assert(chunks[1].bytes_downloaded == 100);

    // Chunk 2 and 3 had 0 bytes
    assert(chunks[2].bytes_downloaded == 0);
    assert(chunks[3].bytes_downloaded == 0);

    assert(cm_loaded.get_total_downloaded_bytes() == 350); // 250 + 100
    cout << "  -> load() state reconstruction: PASSED" << endl;

    // =========================================================================
    // 4. CORRUPT & INVALID JSON FAILURE MODES (Negative)
    // =========================================================================
    cout << "\n[4/5] Testing load() Parsing Guards (Negative)..." << endl;

    // 4.1 Negative: Loading non-existent file returns false
    string dummy_url, dummy_etag;
    uint64_t dummy_size = 0;
    ChunkManager dummy_cm(0, 1);
    bool load_non_existent = ResumeStore::load("non_existent_file_xyz_12345.json", dummy_url, dummy_size, dummy_etag, dummy_cm);
    assert(load_non_existent == false);

    // 4.2 Negative: Loading file with corrupt/malformed JSON syntax
    {
        ofstream corrupt_file(TEST_CORRUPT_SIDECAR);
        corrupt_file << "{ this is not valid json : [ 12345 ] ";
        corrupt_file.close();
    }
    bool load_corrupt = ResumeStore::load(TEST_CORRUPT_SIDECAR, dummy_url, dummy_size, dummy_etag, dummy_cm);
    assert(load_corrupt == false);

    // 4.3 Negative: Loading JSON with missing "chunks" array
    {
        ofstream no_chunks_file(TEST_NO_CHUNKS_SIDECAR);
        no_chunks_file << "{\"url\":\"http://test.com\",\"total_size\":100,\"etag\":\"none\"}";
        no_chunks_file.close();
    }
    bool load_no_chunks = ResumeStore::load(TEST_NO_CHUNKS_SIDECAR, dummy_url, dummy_size, dummy_etag, dummy_cm);
    assert(load_no_chunks == false);
    cout << "  -> Non-existent, corrupt syntax, and missing array guards: PASSED" << endl;

    // =========================================================================
    // 5. DELETION & IDEMPOTENCE (Positive)
    // =========================================================================
    cout << "\n[5/5] Testing remove() & Idempotence..." << endl;

    // 5.1 Positive: Removing valid sidecar
    assert(ResumeStore::exists(TEST_VALID_SIDECAR) == true);
    bool remove_ok = ResumeStore::remove(TEST_VALID_SIDECAR);
    assert(remove_ok == true);
    assert(ResumeStore::exists(TEST_VALID_SIDECAR) == false);

    // 5.2 Positive / Idempotent: Calling remove on already deleted file returns true
    bool remove_again = ResumeStore::remove(TEST_VALID_SIDECAR);
    assert(remove_again == true);

    // Clean up temporary corrupt files
    ResumeStore::remove(TEST_CORRUPT_SIDECAR);
    ResumeStore::remove(TEST_NO_CHUNKS_SIDECAR);
    cout << "  -> remove() file deletion and idempotence: PASSED" << endl;

    cout << "\n==========================================================" << endl;
    cout << "  [PASSED] 100% OF RESUMESTORE FUNCTIONS VERIFIED!        " << endl;
    cout << "==========================================================" << endl;

    return 0;
}
