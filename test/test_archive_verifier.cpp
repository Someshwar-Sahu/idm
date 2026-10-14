#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "../src/tools/ArchiveVerifier.h"
#include <iostream>
#include <cassert>
#include <vector>
#include <string>
#include <filesystem>
#include <fstream>

using namespace std;
using namespace idm;

void test_archive_signature_detection() {
    cout << "\n[1/8] Testing Archive Magic Byte Signatures (ZIP, 7Z, RAR, GZ, BZ2, TAR)..." << endl;

    // 1. ZIP
    uint8_t zip_sig[16] = {'P', 'K', 0x03, 0x04, 0, 0, 0, 0};
    assert(ArchiveVerifier::detect_archive_type_from_buffer(zip_sig, 16) == ArchiveType::Zip);

    // 2. 7-Zip
    uint8_t seven_zip_sig[16] = {0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C, 0, 0};
    assert(ArchiveVerifier::detect_archive_type_from_buffer(seven_zip_sig, 16) == ArchiveType::SevenZip);

    // 3. RAR4 & RAR5
    uint8_t rar4_sig[16] = {'R', 'a', 'r', '!', 0x1A, 0x07, 0x00, 0};
    assert(ArchiveVerifier::detect_archive_type_from_buffer(rar4_sig, 16) == ArchiveType::Rar);

    uint8_t rar5_sig[16] = {'R', 'a', 'r', '!', 0x1A, 0x07, 0x01, 0x00};
    assert(ArchiveVerifier::detect_archive_type_from_buffer(rar5_sig, 16) == ArchiveType::Rar);

    // 4. GZip
    uint8_t gz_sig[16] = {0x1F, 0x8B, 0x08, 0};
    assert(ArchiveVerifier::detect_archive_type_from_buffer(gz_sig, 16) == ArchiveType::Gz);

    // 5. BZip2
    uint8_t bz2_sig[16] = {'B', 'Z', 'h', '9', 0};
    assert(ArchiveVerifier::detect_archive_type_from_buffer(bz2_sig, 16) == ArchiveType::Bz2);

    // 6. TAR
    vector<uint8_t> tar_sig(512, 0);
    memcpy(tar_sig.data() + 257, "ustar", 5);
    assert(ArchiveVerifier::detect_archive_type_from_buffer(tar_sig.data(), tar_sig.size()) == ArchiveType::Tar);

    // 7. Unknown
    uint8_t junk[16] = {0x00, 0x11, 0x22, 0x33};
    assert(ArchiveVerifier::detect_archive_type_from_buffer(junk, 16) == ArchiveType::Unknown);

    cout << "  -> All 6 archive container magic signatures verified: PASSED" << endl;
}

void test_rfc1321_md5_test_vectors() {
    cout << "\n[2/8] Testing RFC 1321 MD5 Test Vectors & Streaming Hashing..." << endl;

    // Vector 1: Empty string -> d41d8cd98f00b204e9800998ecf8427e
    assert(ArchiveVerifier::calculate_buffer_md5(nullptr, 0) == "d41d8cd98f00b204e9800998ecf8427e");

    // Vector 2: "a" -> 0cc175b9c0f1b6a831c399e269772661
    string a_str = "a";
    assert(ArchiveVerifier::calculate_buffer_md5(reinterpret_cast<const uint8_t*>(a_str.data()), a_str.size()) ==
           "0cc175b9c0f1b6a831c399e269772661");

    // Vector 3: "abc" -> 900150983cd24fb0d6963f7d28e17f72
    string abc_str = "abc";
    assert(ArchiveVerifier::calculate_buffer_md5(reinterpret_cast<const uint8_t*>(abc_str.data()), abc_str.size()) ==
           "900150983cd24fb0d6963f7d28e17f72");

    // Vector 4: "The quick brown fox jumps over the lazy dog" -> 9e107d9d372bb6826bd81d3542a419d6
    string fox = "The quick brown fox jumps over the lazy dog";
    assert(ArchiveVerifier::calculate_buffer_md5(reinterpret_cast<const uint8_t*>(fox.data()), fox.size()) ==
           "9e107d9d372bb6826bd81d3542a419d6");

    // File streaming test
    const string md5_test_file = "test_md5_stream.bin";
    filesystem::remove(md5_test_file);
    ofstream fout(md5_test_file, ios::binary);
    fout << fox;
    fout.close();

    assert(ArchiveVerifier::calculate_file_md5(md5_test_file) == "9e107d9d372bb6826bd81d3542a419d6");
    filesystem::remove(md5_test_file);

    cout << "  -> Bit-exact RFC 1321 MD5 test vectors & file streaming: PASSED" << endl;
}

void test_sha1_and_sha256_integrities() {
    cout << "\n[3/8] Testing RFC 3174 SHA-1 & SHA-256 Digest Calculations..." << endl;

    // SHA-1 Vector 1: Empty string -> da39a3ee5e6b4b0d3255bfef95601890afd80709
    assert(ArchiveVerifier::calculate_buffer_sha1(nullptr, 0) == "da39a3ee5e6b4b0d3255bfef95601890afd80709");

    // SHA-1 Vector 2: "The quick brown fox jumps over the lazy dog" -> 2fd4e1c67a2d28fced849ee1bb76e7391b93eb12
    string fox = "The quick brown fox jumps over the lazy dog";
    assert(ArchiveVerifier::calculate_buffer_sha1(reinterpret_cast<const uint8_t*>(fox.data()), fox.size()) ==
           "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12");

    const string hash_file = "test_sha_stream.bin";
    filesystem::remove(hash_file);
    ofstream fout(hash_file, ios::binary);
    fout << fox;
    fout.close();

    assert(ArchiveVerifier::calculate_file_sha1(hash_file) == "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12");

    // SHA-256 Vector: "The quick brown fox jumps over the lazy dog" -> d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592
    assert(ArchiveVerifier::calculate_file_sha256(hash_file) ==
           "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");

    filesystem::remove(hash_file);
    cout << "  -> Multi-algorithm cryptographic hash generation: PASSED" << endl;
}

void test_text_and_clipboard_hash_extraction() {
    cout << "\n[4/8] Testing Automatic Hash Token & Format Parsing from Text..." << endl;

    // 1. Raw MD5
    auto m1 = ArchiveVerifier::extract_hash_from_text("9e107d9d372bb6826bd81d3542a419d6");
    assert(m1.has_value());
    assert(m1->first == HashAlgorithm::MD5);
    assert(m1->second == "9e107d9d372bb6826bd81d3542a419d6");

    // 2. Formatted SHA-1 with label
    auto m2 = ArchiveVerifier::extract_hash_from_text("SHA1: 2fd4e1c67a2d28fced849ee1bb76e7391b93eb12");
    assert(m2.has_value());
    assert(m2->first == HashAlgorithm::SHA1);
    assert(m2->second == "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12");

    // 3. Checksum file format: "<sha256>  *package.zip"
    auto m3 = ArchiveVerifier::extract_hash_from_text("d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592  *package.zip\n");
    assert(m3.has_value());
    assert(m3->first == HashAlgorithm::SHA256);
    assert(m3->second == "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");

    // 4. Case-insensitivity
    auto m4 = ArchiveVerifier::extract_hash_from_text("MD5=9E107D9D372BB6826BD81D3542A419D6");
    assert(m4.has_value());
    assert(m4->second == "9e107d9d372bb6826bd81d3542a419d6");

    // 5. Invalid string
    auto m5 = ArchiveVerifier::extract_hash_from_text("This is just regular text without any checksum!");
    assert(!m5.has_value());

    cout << "  -> Robust extraction across MD5, SHA-1, and SHA-256 text patterns: PASSED" << endl;
}

void test_checksum_file_verification() {
    cout << "\n[5/8] Testing Automated File Checksum Verification..." << endl;
    const string test_file = "test_verify_target.bin";
    filesystem::remove(test_file);

    ofstream fout(test_file, ios::binary);
    fout << "The quick brown fox jumps over the lazy dog";
    fout.close();

    // 1. Correct MD5
    auto r1 = ArchiveVerifier::verify_file_checksum(test_file, "9e107d9d372bb6826bd81d3542a419d6");
    assert(r1.verified);
    assert(r1.algorithm == HashAlgorithm::MD5);

    // 2. Correct SHA-256 with noise
    auto r2 = ArchiveVerifier::verify_file_checksum(test_file, "SHA256: D7A8FBB307D7809469CA9ABCB0082E4F8D5651E46D3CDB762D02D0BF37C9E592");
    assert(r2.verified);
    assert(r2.algorithm == HashAlgorithm::SHA256);

    // 3. Corrupted / Mismatched Hash
    auto r3 = ArchiveVerifier::verify_file_checksum(test_file, "9e107d9d372bb6826bd81d3542a41900");
    assert(!r3.verified);

    // 4. Mock Clipboard Verification
    ArchiveVerifier::set_mock_clipboard_reader([]() -> string {
        return "sha256 = d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592";
    });
    auto r4 = ArchiveVerifier::verify_against_clipboard(test_file);
    assert(r4.verified);

    filesystem::remove(test_file);
    cout << "  -> File checksum verification and clipboard matching: PASSED" << endl;
}

void test_destination_folder_and_zip_slip_security() {
    cout << "\n[6/8] Testing Subfolder Path Generation & Zip-Slip Traversal Guards..." << endl;

    // Subfolder generation
    string dest = ArchiveVerifier::generate_destination_folder("C:/Downloads/archive.tar.gz", "C:/Downloads", true);
    assert(dest.find("archive") != string::npos);

    // Zip-Slip Traversal Guards
    assert(ArchiveVerifier::is_safe_extract_path("C:/dest", "subfolder/file.txt"));
    assert(ArchiveVerifier::is_safe_extract_path("C:/dest", "file.txt"));

    // Traversal attempts MUST be blocked
    assert(!ArchiveVerifier::is_safe_extract_path("C:/dest", "../evil.exe"));
    assert(!ArchiveVerifier::is_safe_extract_path("C:/dest", "sub/../../evil.exe"));
    assert(!ArchiveVerifier::is_safe_extract_path("C:/dest", "/etc/passwd"));
    assert(!ArchiveVerifier::is_safe_extract_path("C:/dest", "C:/Windows/system32/cmd.exe"));

    cout << "  -> Sanitized paths and Zip-Slip vulnerability prevention: PASSED" << endl;
}

void test_extraction_command_construction() {
    cout << "\n[7/8] Testing Platform Extraction Command Construction..." << endl;

    ExtractionOptions opts;
    opts.overwrite_existing = true;

    // ZIP -> tar.exe or Expand-Archive
    string cmd_zip = ArchiveVerifier::build_extraction_command(ArchiveType::Zip, "test.zip", "out_dir", opts);
    assert(cmd_zip.find("test.zip") != string::npos);
    assert(cmd_zip.find("out_dir") != string::npos);

    // TAR / GZ / BZ2 -> tar
    string cmd_tar = ArchiveVerifier::build_extraction_command(ArchiveType::Tar, "test.tar", "out_dir", opts);
    assert(cmd_tar.find("-xf") != string::npos);

    // 7-Zip -> 7z.exe
    string cmd_7z = ArchiveVerifier::build_extraction_command(ArchiveType::SevenZip, "test.7z", "out_dir", opts);
    assert(cmd_7z.find("7z") != string::npos);

    cout << "  -> System and toolchain command strings constructed: PASSED" << endl;
}

void test_archive_extraction_lifecycle_with_mock_runner() {
    cout << "\n[8/8] Testing Archive Extraction Lifecycle & Mock Process Runner..." << endl;
    const string dummy_zip = "test_dummy.zip";
    filesystem::remove(dummy_zip);

    // Create dummy ZIP with magic header
    ofstream fout(dummy_zip, ios::binary);
    uint8_t zip_hdr[16] = {'P', 'K', 0x03, 0x04, 0, 0, 0, 0};
    fout.write(reinterpret_cast<const char*>(zip_hdr), 16);
    fout.close();

    string executed_cmd;
    ArchiveVerifier::set_mock_process_runner([&executed_cmd](const string& cmd, string& output) -> bool {
        executed_cmd = cmd;
        output = "Archive successfully unpacked: 5 files extracted (0 errors)";
        return true;
    });

    ExtractionOptions opts;
    opts.create_subfolder = true;
    opts.delete_archive_on_success = true;

    string log;
    bool ok = ArchiveVerifier::extract_archive(dummy_zip, "test_extract_out", opts, nullptr, &log);
    assert(ok);
    assert(!executed_cmd.empty());
    assert(log.find("successfully unpacked") != string::npos);

    // Archive should be removed after successful unpack
    assert(!filesystem::exists(dummy_zip));

    // Cleanup output folder
    filesystem::remove_all("test_extract_out");

    cout << "  -> Full extraction lifecycle, options, and deletion trigger: PASSED" << endl;
}

int main() {
    cout << "==========================================================" << endl;
    cout << "  Comprehensive Unit Test: ArchiveVerifier (Task 25)      " << endl;
    cout << "==========================================================" << endl;

    test_archive_signature_detection();
    test_rfc1321_md5_test_vectors();
    test_sha1_and_sha256_integrities();
    test_text_and_clipboard_hash_extraction();
    test_checksum_file_verification();
    test_destination_folder_and_zip_slip_security();
    test_extraction_command_construction();
    test_archive_extraction_lifecycle_with_mock_runner();

    cout << "\n==========================================================" << endl;
    cout << "  [PASSED] 100% OF ARCHIVE UNPACK & INTEGRITY VERIFIED!   " << endl;
    cout << "==========================================================" << endl;

    return 0;
}
