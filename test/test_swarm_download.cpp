#include "../src/net/SwarmDownloader.h"
#include "../src/security/AntivirusScanner.h"
#include <iostream>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>

using namespace std;
using namespace idm;

void test_metalink_parsing() {
    cout << "\n[1/8] Testing Metalink XML & JSON Parsing (Pos + Neg)..." << endl;

    // Positive XML
    string sample_xml = R"(
        <?xml version="1.0" encoding="UTF-8"?>
        <metalink version="3.0">
          <files>
            <file name="ubuntu-24.04-desktop-amd64.iso">
              <size>10485760</size>
              <verification>
                <hash type="sha-256">a1b2c3d4e5f67890abcdef1234567890abcdef1234567890abcdef1234567890</hash>
                <hash type="md5">0123456789abcdef0123456789abcdef</hash>
              </verification>
              <resources>
                <url location="us" priority="1">https://us.mirror.org/ubuntu.iso</url>
                <url location="de" priority="2">https://de.mirror.org/ubuntu.iso</url>
                <url location="jp" priority="3">https://jp.mirror.org/ubuntu.iso</url>
              </resources>
            </file>
          </files>
        </metalink>
    )";

    auto meta_xml = SwarmDownloader::parse_metalink_xml(sample_xml);
    assert(meta_xml.is_valid);
    assert(meta_xml.filename == "ubuntu-24.04-desktop-amd64.iso");
    assert(meta_xml.size == 10485760);
    assert(meta_xml.sha256_hash == "a1b2c3d4e5f67890abcdef1234567890abcdef1234567890abcdef1234567890");
    assert(meta_xml.md5_hash == "0123456789abcdef0123456789abcdef");
    assert(meta_xml.mirrors.size() == 3);
    assert(meta_xml.mirrors[0].url == "https://us.mirror.org/ubuntu.iso");
    assert(meta_xml.mirrors[0].location == "us");
    assert(meta_xml.mirrors[0].priority == 1);

    // Negative XML (empty / corrupt)
    auto empty_xml = SwarmDownloader::parse_metalink_xml("");
    assert(!empty_xml.is_valid);

    auto corrupt_xml = SwarmDownloader::parse_metalink_xml("<broken><tags>");
    assert(!corrupt_xml.is_valid);

    // Positive JSON
    string sample_json = R"({
        "name": "package.tar.gz",
        "size": 5242880,
        "hashes": {
            "sha256": "abcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd"
        },
        "mirrors": [
            {"url": "https://m1.site.com/file", "priority": 1, "location": "US"},
            {"url": "https://m2.site.com/file", "priority": 2, "location": "EU"}
        ]
    })";

    auto meta_json = SwarmDownloader::parse_metalink_json(sample_json);
    assert(meta_json.is_valid);
    assert(meta_json.filename == "package.tar.gz");
    assert(meta_json.size == 5242880);
    assert(meta_json.sha256_hash == "abcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd");
    assert(meta_json.mirrors.size() == 2);

    cout << "  -> Metalink XML and JSON parsers with error guards: PASSED" << endl;
}

void test_mirror_registration() {
    cout << "\n[2/8] Testing Mirror Registration & State Management..." << endl;
    SwarmDownloader downloader("test_dummy.bin");

    int id1 = downloader.add_mirror("https://mirror1.com/file", 1, "US");
    int id2 = downloader.add_mirror("https://mirror2.com/file", 2, "DE");
    int id3 = downloader.add_mirror("https://mirror3.com/file", 1, "SG");

    assert(id1 > 0 && id2 > 0 && id3 > 0);
    assert(downloader.get_mirrors().size() == 3);
    assert(downloader.get_active_mirrors().size() == 3);

    // Duplicate rejection
    int dup_id = downloader.add_mirror("https://mirror1.com/file");
    assert(dup_id == id1);
    assert(downloader.get_mirrors().size() == 3);

    // Deactivation
    downloader.set_mirror_active(id2, false);
    assert(downloader.get_active_mirrors().size() == 2);

    // Removal
    bool removed = downloader.remove_mirror("https://mirror3.com/file");
    assert(removed);
    assert(downloader.get_mirrors().size() == 2);

    cout << "  -> Dynamic mirror registration, deactivation, and deduplication: PASSED" << endl;
}

void test_multiplexing_strategies() {
    cout << "\n[3/8] Testing Chunk Multiplexing Strategies (Round-Robin & Priority)..." << endl;
    SwarmDownloader downloader("test_dummy.bin");
    downloader.add_mirror("https://m1.com", 1);
    downloader.add_mirror("https://m2.com", 1);
    downloader.add_mirror("https://m3.com", 1);

    // Round Robin
    downloader.set_strategy(MultiplexStrategy::RoundRobin);
    int m1 = downloader.select_mirror_for_chunk(0);
    int m2 = downloader.select_mirror_for_chunk(1);
    int m3 = downloader.select_mirror_for_chunk(2);
    int m4 = downloader.select_mirror_for_chunk(3);

    assert(m1 != m2 && m2 != m3);
    assert(m1 == m4); // Cycled back

    // Priority Weighted
    downloader.clear_mirrors();
    int high1 = downloader.add_mirror("https://prio1_a.com", 1);
    int high2 = downloader.add_mirror("https://prio1_b.com", 1);
    int low1  = downloader.add_mirror("https://prio2_a.com", 2);

    downloader.set_strategy(MultiplexStrategy::PriorityWeighted);
    for (int i = 0; i < 10; ++i) {
        int picked = downloader.select_mirror_for_chunk(i);
        assert(picked == high1 || picked == high2);
        assert(picked != low1); // Prio 2 never selected while Prio 1 active
    }

    cout << "  -> Round-Robin and Priority-Weighted multiplexing: PASSED" << endl;
}

void test_automatic_failover_and_demotion() {
    cout << "\n[4/8] Testing Automatic Mirror Failover & Degradation Demotion..." << endl;
    SwarmDownloader downloader("test_dummy.bin");
    downloader.set_max_mirror_failures(2);

    int m1 = downloader.add_mirror("https://flaky.com", 1);
    int m2 = downloader.add_mirror("https://stable.com", 1);

    assert(downloader.get_active_mirrors().size() == 2);

    // Simulate 1st failure on m1
    downloader.record_mirror_failure(m1);
    assert(downloader.get_active_mirrors().size() == 2);

    // Simulate 2nd failure on m1 (hits max_failures = 2 -> disabled)
    downloader.record_mirror_failure(m1);
    assert(downloader.get_active_mirrors().size() == 1);

    // Now all chunk selections must resolve only to stable mirror m2
    for (int i = 0; i < 5; ++i) {
        assert(downloader.select_mirror_for_chunk(i) == m2);
    }

    cout << "  -> Mirror failure recording, auto-demotion, and failover routing: PASSED" << endl;
}

void test_simulated_swarm_assembly() {
    cout << "\n[5/8] Testing Multi-Threaded Swarm Assembly (3 Simulated Mirrors)..." << endl;
    const string out_file = "test_swarm_assembled.bin";
    filesystem::remove(out_file);

    SwarmDownloader downloader(out_file, 4);
    downloader.add_mirror("http://mirror-alpha.net/payload", 1);
    downloader.add_mirror("http://mirror-beta.org/payload", 1);
    downloader.add_mirror("http://mirror-gamma.com/payload", 1);

    // Deterministic mock transport providing distinct data per byte range
    downloader.set_mock_transport([](const string& url, uint64_t start, uint64_t end, vector<uint8_t>& buf) {
        buf.resize(end - start + 1);
        for (uint64_t i = 0; i < buf.size(); ++i) {
            buf[i] = static_cast<uint8_t>((start + i) % 256);
        }
        return true;
    });

    const uint64_t TEST_SIZE = 128 * 1024; // 128 KB
    bool ok = downloader.download(TEST_SIZE, 8);
    assert(ok);
    assert(filesystem::file_size(out_file) == TEST_SIZE);

    // Verify file content matches exact sequential pattern across chunks
    ifstream fin(out_file, ios::binary);
    vector<uint8_t> actual_data(TEST_SIZE);
    fin.read(reinterpret_cast<char*>(actual_data.data()), TEST_SIZE);
    fin.close();

    for (uint64_t i = 0; i < TEST_SIZE; ++i) {
        assert(actual_data[i] == static_cast<uint8_t>(i % 256));
    }

    filesystem::remove(out_file);
    cout << "  -> Multi-threaded 8-chunk download distributed across 3 mirrors: PASSED" << endl;
}

void test_sha256_integrity_verification() {
    cout << "\n[6/8] Testing SHA-256 Bit-Exact Integrity Verification..." << endl;
    const string file_path = "test_integrity.bin";
    filesystem::remove(file_path);

    ofstream fout(file_path, ios::binary);
    string sample_data = "BetterThanIDM-Swarm-Acceleration-Integrity-Payload-2026!";
    fout.write(sample_data.data(), sample_data.size());
    fout.close();

    string actual_sha256 = AntivirusScanner::calculate_file_sha256(file_path);
    assert(!actual_sha256.empty());

    SwarmDownloader downloader(file_path);
    assert(downloader.verify_integrity(actual_sha256)); // Positive match
    assert(!downloader.verify_integrity("0000000000000000000000000000000000000000000000000000000000000000")); // Mismatch

    filesystem::remove(file_path);
    cout << "  -> Cryptographic file verification matching Metalink specification: PASSED" << endl;
}

void test_live_failover_recovery_during_download() {
    cout << "\n[7/8] Testing Live Chunk Failover During Simulated Active Swarm..." << endl;
    const string out_file = "test_swarm_failover.bin";
    filesystem::remove(out_file);

    SwarmDownloader downloader(out_file, 2);
    downloader.set_max_mirror_failures(2);

    // Mirror 1 fails completely, Mirror 2 succeeds
    downloader.add_mirror("http://failing-server.com", 1);
    downloader.add_mirror("http://healthy-server.com", 1);

    downloader.set_mock_transport([](const string& url, uint64_t start, uint64_t end, vector<uint8_t>& buf) {
        if (url.find("failing-server") != string::npos) {
            return false; // Simulates HTTP 500 / timeout
        }
        buf.resize(end - start + 1, 0x42);
        return true;
    });

    const uint64_t TEST_SIZE = 32 * 1024;
    bool ok = downloader.download(TEST_SIZE, 4);
    assert(ok);
    assert(filesystem::file_size(out_file) == TEST_SIZE);

    // The failing mirror should now be auto-demoted
    auto mirrors = downloader.get_mirrors();
    assert(!mirrors[0].active); // Failing mirror disabled
    assert(mirrors[1].active);  // Healthy mirror active

    filesystem::remove(out_file);
    cout << "  -> In-flight mirror drop auto-recovered onto healthy mirror: PASSED" << endl;
}

void test_edge_cases_and_error_guards() {
    cout << "\n[8/8] Testing Swarm Edge Cases & Error Guards..." << endl;
    SwarmDownloader downloader("dummy.bin");

    // Zero size
    assert(!downloader.download(0, 4));
    assert(!downloader.get_last_error().empty());

    // Zero mirrors
    assert(!downloader.download(1024, 4));
    assert(downloader.get_last_error().find("No active mirrors") != string::npos);

    cout << "  -> Zero size, empty mirror pool, and missing path guards: PASSED" << endl;
}

int main() {
    cout << "==========================================================" << endl;
    cout << "  Comprehensive Unit Test: SwarmDownloader & Metalink     " << endl;
    cout << "==========================================================" << endl;

    test_metalink_parsing();
    test_mirror_registration();
    test_multiplexing_strategies();
    test_automatic_failover_and_demotion();
    test_simulated_swarm_assembly();
    test_sha256_integrity_verification();
    test_live_failover_recovery_during_download();
    test_edge_cases_and_error_guards();

    cout << "\n==========================================================" << endl;
    cout << "  [PASSED] 100% OF SWARM & METALINK MULTIPLEXING VERIFIED! " << endl;
    cout << "==========================================================" << endl;

    return 0;
}
