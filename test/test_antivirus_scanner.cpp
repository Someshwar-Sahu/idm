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


    cout << "\n==========================================================" << endl;
    cout << "  [PASSED] 100% OF ANTIVIRUSSCANNER (TASK 9) VERIFIED!   " << endl;
    cout << "==========================================================" << endl;

    return 0;
}
