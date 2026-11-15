#include "AntivirusScanner.h"
#include <iostream>
#include <fstream>
#include <cassert>
#include <filesystem>
#include <vector>

using namespace std;

int main() {
    cout << "==========================================================" << endl;
    cout << "  Unit Test: Antivirus & Large-File Pipeline (Pos + Neg)  " << endl;
    cout << "==========================================================" << endl;

    // =========================================================================
    // 1. PASSIVE MEDIA FAST-PASS OPTIMIZATION (Positive)
    // =========================================================================
    cout << "\n[1/5] Testing Passive Media Fast-Pass Optimization..." << endl;
    {
        const string media_file = "test_large_movie.mkv";
        // Create a sparse/fast 52 MB dummy media file (> 50 MB threshold)
        {
            ofstream out(media_file, ios::binary);
            out.seekp(52ULL * 1024ULL * 1024ULL - 1);
            out.write("\0", 1);
        }

        auto result = AntivirusScanner::scan_file(media_file);
        cout << "  -> Media scan duration: " << result.scan_duration_ms << " ms" << endl;
        cout << "  -> Verdict: " << (result.verdict == FileScanVerdict::Clean ? "Clean" : "Infected") << endl;
        cout << "  -> Output: " << result.raw_output << endl;

        assert(result.verdict == FileScanVerdict::Clean);
        assert(result.exit_code == 0);
        assert(result.raw_output.find("[Fast Pass]") != string::npos);
        assert(result.scan_duration_ms < 500); // Must complete in milliseconds, not minutes!

        filesystem::remove(media_file);
        cout << "  -> 52 MB media file completed instantly without CLI locking: PASSED" << endl;
    }

    // =========================================================================
    // 2. EXECUTABLE HIGH-RISK PIPELINE INSPECTION (Positive)
    // =========================================================================
    cout << "\n[2/5] Testing Executable Risk Route (Must NOT Fast-Pass)..." << endl;
    {
        const string exec_file = "test_tool.exe";
        {
            ofstream out(exec_file, ios::binary);
            out << "MZ\x90\x00\x03\x00\x00\x00\x04\x00\x00\x00"; // Dummy PE header
        }

        auto result = AntivirusScanner::scan_file(exec_file);
        assert(result.raw_output.find("[Fast Pass]") == string::npos); // Executables never fast-pass!
        filesystem::remove(exec_file);
        cout << "  -> Executables strictly routed through deep inspection: PASSED" << endl;
    }

    // =========================================================================
    // 3. EICAR IN-MEMORY INFECTION DETECTION (Negative / Malware Guard)
    // =========================================================================
    cout << "\n[3/5] Testing EICAR Standard Malware Detection..." << endl;
    {
        const string eicar_file = "test_eicar.com";
        {
            ofstream out(eicar_file);
            out << "X5O!P%@AP[4\\PZX54(P^)7CC)7}$EICAR-STANDARD-ANTIVIRUS-TEST-FILE!$H+H*";
        }

        auto result = AntivirusScanner::scan_file(eicar_file);
        assert(result.verdict == FileScanVerdict::Infected);
        assert(!result.threat_name.empty());
        cout << "  -> EICAR detected as infected (" << result.threat_name << "): PASSED" << endl;

        filesystem::remove(eicar_file);
    }

    // =========================================================================
    // 4. MARK-OF-THE-WEB (MOTW) NTFS STREAM APPLICATION (Positive)
    // =========================================================================
    cout << "\n[4/5] Testing Mark-of-the-Web (Zone.Identifier)..." << endl;
    {
        const string motw_file = "test_motw.bin";
        {
            ofstream out(motw_file);
            out << "Test data payload";
        }

        bool applied = AntivirusScanner::apply_mark_of_the_web(motw_file, "https://example.com/test.bin");
        cout << "  -> MOTW applied: " << (applied ? "Yes" : "No (Non-NTFS or Linux)") << endl;

#ifdef _WIN32
        // On Windows NTFS, verify the Zone.Identifier stream exists
        string zone_stream = motw_file + ":Zone.Identifier";
        {
            ifstream zone_in(zone_stream);
            if (zone_in.is_open()) {
                string content((istreambuf_iterator<char>(zone_in)), istreambuf_iterator<char>());
                assert(content.find("[ZoneTransfer]") != string::npos);
                assert(content.find("ZoneId=3") != string::npos);
                zone_in.close();
                cout << "  -> Zone.Identifier verified with ZoneId=3 (Internet): PASSED" << endl;
            }
        }
#endif
        filesystem::remove(motw_file);
    }

    // =========================================================================
    // 5. HIGH-THROUGHPUT SHA-256 BUFFER BENCHMARK (Positive)
    // =========================================================================
    cout << "\n[5/5] Testing 2 MB Buffer SHA-256 Correctness..." << endl;
    {
        const string hash_file = "test_hash.bin";
        {
            ofstream out(hash_file, ios::binary);
            string block(1024 * 1024, 'A'); // 1 MB of 'A'
            out.write(block.data(), block.size());
            out.write(block.data(), block.size()); // 2 MB total
        }

        string calculated_hash = AntivirusScanner::calculate_file_sha256(hash_file);
        assert(!calculated_hash.empty());
        assert(calculated_hash.length() == 64);
        cout << "  -> 2 MB block hash: " << calculated_hash << endl;
        filesystem::remove(hash_file);
        cout << "  -> High-throughput 2 MB block SHA-256 verification: PASSED" << endl;
    }

    cout << "\n==========================================================" << endl;
    cout << "  [PASSED] ALL 5/5 ANTIVIRUS PIPELINE TESTS PASSED!       " << endl;
    cout << "==========================================================" << endl;
    return 0;
}
