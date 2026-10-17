#ifndef ARCHIVE_VERIFIER_H
#define ARCHIVE_VERIFIER_H

#include <optional>
#include <string>
#include <cstdint>
#include <functional>
#include <utility>

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
    std::string expected_hash;
    std::string actual_hash;
    std::string details;
};

struct ExtractionOptions {
    bool create_subfolder{true};       // Extract into <dest_dir>/<archive_stem>/
    bool overwrite_existing{true};      // Overwrite target files
    bool delete_archive_on_success{false}; // Remove archive upon verified unpack
    std::string custom_tool_path;       // Path to 7z.exe or unrar if specified
};

enum class ExtractionStatus {
    Pending,
    InProgress,
    Completed,
    Failed
};

class ArchiveVerifier {
public:
    using ProcessRunner = std::function<bool(const std::string& cmd, std::string& output)>;
    using ClipboardReader = std::function<std::string()>;

    // 1. Archive Signature & Type Detection
    static ArchiveType detect_archive_type_from_buffer(const uint8_t* data, size_t size);
    static ArchiveType detect_archive_type(const std::string& filepath);
    static bool is_supported_archive(const std::string& filepath);
    static std::string archive_type_to_string(ArchiveType type);

    // 2. Cryptographic Checksum Engine
    static std::string calculate_file_md5(const std::string& filepath);
    static std::string calculate_buffer_md5(const uint8_t* data, size_t size);
    static std::string calculate_file_sha1(const std::string& filepath);
    static std::string calculate_buffer_sha1(const uint8_t* data, size_t size);
    static std::string calculate_file_sha256(const std::string& filepath);

    // 3. Hash Extraction & Verification
    static HashAlgorithm detect_hash_algorithm(const std::string& hash_str);
    static std::optional<std::pair<HashAlgorithm, std::string>> extract_hash_from_text(const std::string& text);
    static ChecksumResult verify_file_checksum(const std::string& filepath, const std::string& expected_hash);
    static ChecksumResult verify_against_clipboard(const std::string& filepath, ClipboardReader custom_reader = nullptr);

    // 4. Archive Extraction & Execution
    static std::string generate_destination_folder(const std::string& archive_path, 
                                                  const std::string& output_base_dir = "", 
                                                  bool create_subfolder = true);

    static bool is_safe_extract_path(const std::string& base_dest_dir, const std::string& relative_entry_path);

    static std::string build_extraction_command(ArchiveType type,
                                               const std::string& archive_path,
                                               const std::string& dest_dir,
                                               const ExtractionOptions& options = ExtractionOptions());

    static bool extract_archive(const std::string& archive_path,
                               const std::string& output_dir = "",
                               const ExtractionOptions& options = ExtractionOptions(),
                               ProcessRunner custom_runner = nullptr,
                               std::string* out_log = nullptr);

    // Native Windows Clipboard Reader (with mock hook)
    static std::string read_system_clipboard();
    static void set_mock_clipboard_reader(ClipboardReader reader);

    // Native Process Runner (with mock hook)
    static bool run_system_command(const std::string& cmd, std::string& output);
    static void set_mock_process_runner(ProcessRunner runner);

private:
    static ClipboardReader s_mock_clipboard;
    static ProcessRunner s_mock_runner;
};

} // namespace idm

#endif // ARCHIVE_VERIFIER_H
