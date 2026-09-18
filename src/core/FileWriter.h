#ifndef FILE_WRITER_H
#define FILE_WRITER_H

#include <string>
#include <cstdint>
#include <cstddef>

class FileWriter {
public:
    FileWriter() = default;
    ~FileWriter();

    FileWriter(const FileWriter&) = delete;
    FileWriter& operator=(const FileWriter&) = delete;

    static bool file_exists(const std:: string &path);
    static std::string generate_unique_filepath(const std::string &path);

    static bool preallocate_file(const std::string &path, uint64_t total_size);

    bool open(const std::string &path);
    void close();

    bool write_at_offset(uint64_t offset, const void *data, size_t length);

    bool is_open() const;

private:
    std::string m_path;

#ifdef _WIN32
    void *m_file_handle{nullptr};
#else
    int m_file_descriptor{-1};
#endif
};

#endif
