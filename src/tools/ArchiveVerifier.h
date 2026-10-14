#ifndef ARCHIVE_VERIFIER_H
#define ARCHIVE_VERIFIER_H

#include <optional>
#include <string>
#include <cstdint>
#include <functional>

using namespace std;

namespace idm {

enum class ArchiveType {
    Unknown,
    Zip,
    Rar,
    SevenZip,
    Tar,
    Gz,
    Bz2
};

enum class HashAlgorithm {
    Unknown,
    MD5,
    SHA1,
    SHA256
};

struct ChecksumResult {
    bool verified{false};
    HashAlgorithm algorithm{HashAlgorithm::Unknown};
    string expected_hash;
    string actual_hash;
    string details;
};

struct ExtractionOptions {
    bool create_subfolder{true};       // Extract into <dest_dir>/<archive_stem>/
    bool overwrite_existing{true};      // Overwrite target files
    bool delete_archive_on_success{false}; // Remove archive upon verified unpack
    string custom_tool_path;       // Path to 7z.exe or unrar if specified
};

enum class ExtractionStatus {
    Pending,
    InProgress,
    Completed,
    Failed
};

class ArchiveVerifier {
public:
    using ProcessRunner = function<bool(const string& cmd, string& output)>;
    using ClipboardReader = function<string()>;

    // 1. Archive Signature & Type Detection
    static ArchiveType detect_archive_type_from_buffer(const uint8_t* data, size_t size);
    static ArchiveType detect_archive_type(const string& filepath);
    static bool is_supported_archive(const string& filepath);
    static string archive_type_to_string(ArchiveType type);

    // 2. Cryptographic Checksum Engine
    static string calculate_file_md5(const string& filepath);
    static string calculate_buffer_md5(const uint8_t* data, size_t size);
    static string calculate_file_sha1(const string& filepath);
    static string calculate_buffer_sha1(const uint8_t* data, size_t size);
    static string calculate_file_sha256(const string& filepath);

    // 3. Hash Extraction & Verification
    static HashAlgorithm detect_hash_algorithm(const string& hash_str);
    static optional<pair<HashAlgorithm, string>> extract_hash_from_text(const string& text);
    static ChecksumResult verify_file_checksum(const string& filepath, const string& expected_hash);
    static ChecksumResult verify_against_clipboard(const string& filepath, ClipboardReader custom_reader = nullptr);

    // 4. Archive Extraction & Execution
    static string generate_destination_folder(const string& archive_path, 
                                                  const string& output_base_dir = "", 
                                                  bool create_subfolder = true);

    static bool is_safe_extract_path(const string& base_dest_dir, const string& relative_entry_path);

    static string build_extraction_command(ArchiveType type,
                                               const string& archive_path,
                                               const string& dest_dir,
                                               const ExtractionOptions& options = ExtractionOptions());

    static bool extract_archive(const string& archive_path,
                               const string& output_dir = "",
                               const ExtractionOptions& options = ExtractionOptions(),
                               ProcessRunner custom_runner = nullptr,
                               string* out_log = nullptr);

    // Native Windows Clipboard Reader (with mock hook)
    static string read_system_clipboard();
    static void set_mock_clipboard_reader(ClipboardReader reader);

    // Native Process Runner (with mock hook)
    static bool run_system_command(const string& cmd, string& output);
    static void set_mock_process_runner(ProcessRunner runner);

private:
    static ClipboardReader s_mock_clipboard;
    static ProcessRunner s_mock_runner;
};

} // namespace idm

#endif // ARCHIVE_VERIFIER_H
