#ifndef RESUME_STORE_H
#define RESUME_STORE_H

#include "ChunkManager.h"
#include <string>
#include <cstdint>

class ResumeStore {
public:
    static std::string get_sidecar_filepath(const std::string &destination_filepath, const std::string &cache_directory = "");

    static bool save(const std::string &sidecar_filepath, 
                     const std::string &url, 
                     uint64_t total_size, 
                     const std::string &etag, 
                     const ChunkManager &chunk_manager);

    static bool load(const std::string &sidecar_filepath, 
                     std::string &out_url, 
                     uint64_t &out_total_size, 
                     std::string &out_etag, 
                     ChunkManager &out_chunk_manager);

    static bool exists(const std::string &sidecar_filepath);
    static bool remove(const std::string &sidecar_filepath);
};

#endif 
