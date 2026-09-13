#include "ResumeStore.h"
#include "ChunkManager.h"
#include <iostream>
#include <cassert>
#include <cstdio>
#include <string>

using namespace std;

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);

    cout << "[Test] Starting ResumeStore verification test..." << endl;

    string dest_file = "test_download.bin";
    string sidecar_file = ResumeStore::get_sidecar_filepath(dest_file);

    assert(sidecar_file == "test_download.bin.progress.json");
    cout << "[Test] Sidecar path generated: " << sidecar_file << endl;

    if (ResumeStore::exists(sidecar_file)) {
        ResumeStore::remove(sidecar_file);
    }

    string original_url = "https://example.com/large_file.zip";
    uint64_t original_total_size = 1000;
    string original_etag = "\"33a64df551425fcc55e4d42a148795d9f25f89d4\"";

    ChunkManager original_cm(original_total_size, 5); // 5 chunks, 200 bytes each

    auto chunks = original_cm.get_all_chunks();
    original_cm.update_chunk_progress(chunks[0].id, 200);
    original_cm.mark_chunk_done(chunks[0].id);

    original_cm.update_chunk_progress(chunks[1].id, 50); // InProgress

    cout << "[Test] Saving state to sidecar..." << endl;
    bool save_ok = ResumeStore::save(sidecar_file, original_url, original_total_size, original_etag, original_cm);
    assert(save_ok == true);
    assert(ResumeStore::exists(sidecar_file) == true);
    cout << "[Test] Save successful!" << endl;

    cout << "[Test] Loading state from sidecar..." << endl;
    string loaded_url;
    uint64_t loaded_total_size = 0;
    string loaded_etag;
    ChunkManager loaded_cm(0, 1);

    bool load_ok = ResumeStore::load(sidecar_file, loaded_url, loaded_total_size, loaded_etag, loaded_cm);
    assert(load_ok == true);
    cout << "[Test] Load successful!" << endl;

    assert(loaded_url == original_url);
    assert(loaded_total_size == original_total_size);
    assert(loaded_etag == original_etag);
    assert(loaded_cm.get_total_downloaded_bytes() == original_cm.get_total_downloaded_bytes());
    assert(loaded_cm.get_all_chunks().size() == original_cm.get_all_chunks().size());

    auto loaded_chunks = loaded_cm.get_all_chunks();
    assert(loaded_chunks[0].bytes_downloaded == 200);
    assert(loaded_chunks[0].state == ChunkState::Done);

    assert(loaded_chunks[1].bytes_downloaded == 50);
    assert(loaded_chunks[1].state == ChunkState::Pending);

    assert(loaded_chunks[2].bytes_downloaded == 0);
    assert(loaded_chunks[2].state == ChunkState::Pending);

    cout << "[Test] Downloaded bytes verified: " << loaded_cm.get_total_downloaded_bytes() << " / " << loaded_total_size << " bytes" << endl;

    cout << "[Test] Removing sidecar file..." << endl;
    bool remove_ok = ResumeStore::remove(sidecar_file);
    assert(remove_ok == true);
    assert(ResumeStore::exists(sidecar_file) == false);
    cout << "[PASSED] ResumeStore verification test completed successfully!" << endl;
    return 0;
}
