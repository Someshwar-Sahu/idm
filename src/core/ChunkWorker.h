#ifndef CHUNK_WORKER_H
#define CHUNK_WORKER_H

#include "Chunk.h"
#include"ChunkManager.h"
#include"FileWriter.h"
#include<string>
#include<curl/curl.h>

class ChunkWorker {
public:
    ChunkWorker(const std::string &url, Chunk chunk, ChunkManager &chunk_manager, FileWriter &file_writer);
    ~ChunkWorker();

    ChunkWorker(const ChunkWorker&) = delete;
    ChunkWorker& operator=(const ChunkWorker&) = delete;

    bool download();

private:
    static size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata);

    struct WriteContext {
        FileWriter *file_writer{nullptr};
        ChunkManager *chunk_manager{nullptr};
        int chunk_id{0};
        uint64_t current_offset{0};
        uint64_t end_byte{0};
    };

    std::string m_url;
    Chunk m_chunk;
    ChunkManager &m_chunk_manager;
    FileWriter &m_file_writer;
    CURL *m_curl{nullptr};
};

#endif