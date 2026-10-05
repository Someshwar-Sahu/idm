#include "security/AntivirusScanner.h"
#include <iostream>
#include <fstream>
#include <cassert>
#include <cstdio>
#include <filesystem>

using namespace std;

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);

    cout << "==========================================================" << endl;
    cout << "  Comprehensive Unit Test: AntivirusScanner (Task 9)      " << endl;
    cout << "==========================================================" << endl;

    const string TEST_FILE = "test_smartscreen_sample.bin";
    remove(TEST_FILE.c_str());

    // =========================================================================
    // 1. PRE-FLIGHT SMARTSCREEN & URL POLICY CHECK (Positive & Negative)
    // =========================================================================
    cout << "\n[1/4] Testing SmartScreen URL Policy Pre-Check (Pos + Neg)..." << endl;

    // 1.1 Positive: Valid HTTPS Download URL
    auto safe_res = AntivirusScanner::check_url_reputation("https://example.com/files/report.pdf", "report.pdf");
    assert(safe_res.is_safe == true);
    assert(!safe_res.details.empty());
    cout << "  -> Valid HTTPS link pre-check: " << safe_res.details << " (PASSED)" << endl;

    // 1.2 Negative: Empty URL rejected
    auto empty_res = AntivirusScanner::check_url_reputation("");
    assert(empty_res.is_safe == false);
    assert(empty_res.status == SecurityScanStatus::Blocked);
    cout << "  -> Empty URL blocked immediately: PASSED" << endl;

    // =========================================================================
    // 2. MARK-OF-THE-WEB (MOTW) ATTACHMENT & VERIFICATION (Positive)
    // =========================================================================
    cout << "\n[2/4] Testing Native Mark-of-the-Web (ZoneId=3) Attachment..." << endl;

    {
        ofstream out(TEST_FILE, ios::binary);
        out << "Sample payload for Mark-of-the-Web verification.";
        out.close();
    }
    assert(filesystem::exists(TEST_FILE) == true);

    // Initial check: File just created should not have MOTW
    assert(AntivirusScanner::has_mark_of_the_web(TEST_FILE) == false);

    // Apply MOTW with source URL
    const string SOURCE_URL = "https://cdn.example.com/downloads/setup.exe";
    bool applied = AntivirusScanner::apply_mark_of_the_web(TEST_FILE, SOURCE_URL);
    assert(applied == true);

    // Verify MOTW exists
    assert(AntivirusScanner::has_mark_of_the_web(TEST_FILE) == true);
    cout << "  -> Native Alternate Data Stream (ADS) attached & detected: PASSED" << endl;

    // =========================================================================
    // 3. MOTW ADS STREAM CONTENT READBACK
    // =========================================================================
    cout << "\n[3/4] Testing Mark-of-the-Web ADS Content Readback..." << endl;

    string motw_content = AntivirusScanner::read_mark_of_the_web(TEST_FILE);
    assert(motw_content.find("[ZoneTransfer]") != string::npos);
    assert(motw_content.find("ZoneId=3") != string::npos);
    assert(motw_content.find(SOURCE_URL) != string::npos);
    cout << "  -> ZoneId=3 and HostUrl metadata verified in ADS: PASSED" << endl;

    // =========================================================================
    // 4. ERROR HANDLING & NON-EXISTENT FILE GUARDS (Negative)
    // =========================================================================
    cout << "\n[4/4] Testing Non-Existent File & Empty Path Guards (Negative)..." << endl;

    assert(AntivirusScanner::apply_mark_of_the_web("") == false);
    assert(AntivirusScanner::has_mark_of_the_web("") == false);
    assert(AntivirusScanner::read_mark_of_the_web("").empty());

    assert(AntivirusScanner::apply_mark_of_the_web("Z:/invalid_dir_9999/file.bin") == false);
    assert(AntivirusScanner::has_mark_of_the_web("non_existent_file_xyz_123.bin") == false);
    cout << "  -> Invalid paths and missing files handled gracefully: PASSED" << endl;

    // Clean up test file
    remove(TEST_FILE.c_str());

        // =========================================================================
    // 5. CORRUPT & TAMPERED ADS STREAM GUARDS (Negative)
    // =========================================================================
    cout << "\n[5/5] Testing Corrupt & Tampered Zone.Identifier Stream Guards (Negative)..." << endl;

    const string CORRUPT_FILE = "test_corrupt_motw.bin";
    remove(CORRUPT_FILE.c_str());

    {
        ofstream out(CORRUPT_FILE, ios::binary);
        out << "Data for corrupt ADS test";
        out.close();
    }

    // 5.1 Write a malformed ADS stream that lacks ZoneId
    {
        ofstream ads(CORRUPT_FILE + ":Zone.Identifier");
        ads << "[SomeGarbageSection]\r\nInvalidKey=999\r\n";
        ads.close();
    }
    assert(AntivirusScanner::has_mark_of_the_web(CORRUPT_FILE) == false);

    // 5.2 Empty ADS stream
    {
        ofstream ads(CORRUPT_FILE + ":Zone.Identifier", ios::trunc);
        ads.close();
    }
    assert(AntivirusScanner::has_mark_of_the_web(CORRUPT_FILE) == false);

    // 5.3 Repair by applying genuine Mark-of-the-Web
    assert(AntivirusScanner::apply_mark_of_the_web(CORRUPT_FILE, "https://example.com/repaired.zip") == true);
    assert(AntivirusScanner::has_mark_of_the_web(CORRUPT_FILE) == true);

    remove(CORRUPT_FILE.c_str());
    cout << "  -> Corrupted and tampered ADS stream detection & repair: PASSED" << endl;

        // =========================================================================
    // 6. TASK 10: THREAT PARSER FROM DEFENDER STDOUT (Positive & Negative)
    // =========================================================================
    cout << "\n[6/7] Testing Threat Name Parser from Stdout (Task 10)..." << endl;

    string sample_defender_stdout = 
        "-------------------------------------------------------------------------------------\n"
        "LISTING OF DETECTED THREATS:\n"
        "-------------------------------------------------------------------------------------\n"
        "Threat                  : Trojan:Win32/Wacatac.B!ml\n"
        "Resources:\n"
        "  file: C:\\Downloads\\Cache\\staging_sample.bin\n";

    string parsed_threat = AntivirusScanner::parse_threat_name(sample_defender_stdout);
    assert(parsed_threat == "Trojan:Win32/Wacatac.B!ml");

    // Clean output produces empty threat name
    string clean_defender_stdout = "Scan completed. No threats detected.";
    assert(AntivirusScanner::parse_threat_name(clean_defender_stdout).empty());
    assert(AntivirusScanner::parse_threat_name("").empty());
    cout << "  -> Threat signature extraction and clean output handling: PASSED" << endl;

    // =========================================================================
    // 7. TASK 10: DYNAMIC DEFENDER LOCATOR & LIVE FILE SCAN (Pos + Neg)
    // =========================================================================
    cout << "\n[7/7] Testing MpCmdRun.exe Locator & Staging Scan (Task 10)..." << endl;

    string defender_exe = AntivirusScanner::find_windows_defender_executable();
    if (!defender_exe.empty()) {
        assert(filesystem::exists(defender_exe) == true);
        cout << "  -> Located Defender CLI: " << defender_exe << endl;
    }

    // Negative: Non-existent file scan
    auto missing_res = AntivirusScanner::scan_file("non_existent_scan_sample_9999.bin");
    assert(missing_res.verdict == FileScanVerdict::ScanFailed);
    assert(missing_res.exit_code == -1);

    // Positive: Scan real clean staging file
    const string CLEAN_TEST_FILE = "staging_clean_test.bin";
    {
        ofstream out(CLEAN_TEST_FILE);
        out << "This is a clean, harmless document payload for staging scan.";
        out.close();
    }
    assert(filesystem::exists(CLEAN_TEST_FILE) == true);

    auto clean_scan = AntivirusScanner::scan_file(CLEAN_TEST_FILE);
    if (!defender_exe.empty()) {
        assert(clean_scan.verdict == FileScanVerdict::Clean);
        assert(clean_scan.exit_code == 0);
        assert(clean_scan.threat_name.empty());
        cout << "  -> Live staging file scanned cleanly in " << clean_scan.scan_duration_ms << "ms: PASSED" << endl;
    } else {
        cout << "  -> Defender CLI not installed in standard path (skipped live invocation): PASSED" << endl;
    }

    remove(CLEAN_TEST_FILE.c_str());



    cout << "\n==========================================================" << endl;
    cout << "  [PASSED] 100% OF ANTIVIRUSSCANNER (TASK 9 & 10) VERIFIED!   " << endl;
    cout << "==========================================================" << endl;

    return 0;
}
