#include "FileWriter.h"
#include <filesystem>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#endif

using namespace std; 

FileWriter::~FileWriter() {
    close();
}

bool FileWriter::file_exists(const string &path){
    namespace fs = std::filesystem;
    error_code ec;
    return fs::exists(path, ec);
}

string FileWriter::generate_unique_filepath(const string &filepath){
    if(!file_exists(filepath)){
        return filepath;
    }

    namespace fs = std::filesystem;
    error_code ec;

    if(!fs::exists(filepath, ec)){
        return filepath;
    }

    fs::path original_path(filepath);
    fs::path parent_dir = original_path.parent_path();
    string filename = original_path.filename().string();
    string stem = original_path.stem().string();
    string extension = original_path.extension().string();

    // Bug 8 Fix: Preserve compound archive extensions (.tar.gz, .tar.bz2, etc.)
    const std::vector<std::string> compound_extensions = {
        ".tar.gz", ".tar.bz2", ".tar.xz", ".tar.zst", ".tar.lz", ".tar.lzma"
    };
    string lower_filename = filename;
    for (char &c : lower_filename) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

    for (const auto &comp : compound_extensions) {
        if (lower_filename.size() > comp.size() && 
            lower_filename.compare(lower_filename.size() - comp.size(), comp.size(), comp) == 0) {
            extension = filename.substr(filename.size() - comp.size());
            stem = filename.substr(0, filename.size() - comp.size());
            break;
        }
    }

    int counter = 1;
    while(true){
        string candidate_filename = stem + " (" + to_string(counter)+")"+extension;
        fs::path candidate = parent_dir.empty() ? fs::path(candidate_filename) : (parent_dir / candidate_filename);

        if(!fs::exists(candidate, ec)){
            return candidate.string();
        }
        counter++;
    }
}

bool FileWriter::preallocate_file(const string &path, uint64_t total_size){
#ifdef _WIN32
    HANDLE hFile = CreateFileA(
        path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if(hFile == INVALID_HANDLE_VALUE){
        return false;
    }

    LARGE_INTEGER cur_size;
    if (GetFileSizeEx(hFile, &cur_size)) {
        if (static_cast<uint64_t>(cur_size.QuadPart) >= total_size && total_size > 0) {
            CloseHandle(hFile);
            return true;
        }
    }

    // Bug 7 Fix: Preallocate physical disk space on NTFS via FileAllocationInfo
    if (total_size > 0) {
        FILE_ALLOCATION_INFO alloc_info;
        alloc_info.AllocationSize.QuadPart = static_cast<LONGLONG>(total_size);
        SetFileInformationByHandle(hFile, FileAllocationInfo, &alloc_info, sizeof(alloc_info));
    }

    LARGE_INTEGER li;
    li.QuadPart = static_cast<LONGLONG>(total_size);

    if(!SetFilePointerEx(hFile, li, NULL, FILE_BEGIN)) {
        CloseHandle(hFile);
        return false;
    }

    if(!SetEndOfFile(hFile)){
        CloseHandle(hFile);
        return false;
    }

    CloseHandle(hFile);
    return true;
#else
    int fd = ::open(path.c_str(), O_RDWR | O_CREAT, 0644);
    if(fd < 0){
        return false;
    }

    struct stat st;
    if (fstat(fd, &st) == 0 && static_cast<uint64_t>(st.st_size) >= total_size && total_size > 0) {
        ::close(fd);
        return true;
    }

    if(ftruncate(fd, static_cast<off_t>(total_size)) != 0) {
        ::close(fd);
        return false;
    }

    ::close(fd);
    return true;
#endif
}

bool FileWriter::open(const string &path){
    close();
    m_path = path;

#ifdef _WIN32
    HANDLE hFile = CreateFileA(
        path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if(hFile == INVALID_HANDLE_VALUE){
        m_file_handle = nullptr;
        return false;
    }
    m_file_handle = static_cast<void*> (hFile);
    return true;
#else
    int fd = ::open(path.c_str(), O_RDWR);
    if(fd <0){
        m_file_descriptor = -1;
        return false;
    }
    m_file_descriptor = fd;
    return true;
#endif
}

void FileWriter::close() {
#ifdef _WIN32
    if(m_file_handle != nullptr){
        HANDLE hFile = static_cast<HANDLE> (m_file_handle);
        CloseHandle(hFile);
        m_file_handle = nullptr;
    }
#else
    if(m_file_descriptor >=0){
        ::close(m_file_descriptor);
        m_file_descriptor = -1;
    }
#endif
}

bool FileWriter::write_at_offset(uint64_t offset, const void* data, size_t length){
    if(!is_open() || data == nullptr || length == 0) return false;

    // Bug 6 Fix: Guard concurrent writes across threads to avoid handle offset collisions
    std::lock_guard<std::mutex> lock(m_write_mutex);

#ifdef _WIN32
    HANDLE hFile = static_cast<HANDLE>(m_file_handle);

    OVERLAPPED ov = {0};
    ov.Offset = static_cast<DWORD>(offset & 0xFFFFFFFF);
    ov.OffsetHigh = static_cast<DWORD>((offset >> 32) & 0xFFFFFFFF);

    DWORD bytesWritten = 0;
    BOOL result = WriteFile(
        hFile,
        data,
        static_cast<DWORD>(length),
        &bytesWritten,
        &ov
    );
    return (result != FALSE) && (bytesWritten == length);

#else 
    ssize_t bytesWritten = pwrite(
        m_file_descriptor,
        data,
        length,
        static_cast<off_t>(offset)
    );
    return bytesWritten == static_cast<ssize_t>(length);
#endif
}

bool FileWriter::is_open() const {
    #ifdef _WIN32
        return m_file_handle != nullptr;
    #else
        return m_file_descriptor >= 0;
    #endif
}