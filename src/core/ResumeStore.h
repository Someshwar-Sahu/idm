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
                     const ChunkManager &chunk_manager,
                     bool supports_ranges = true);

    static bool load(const std::string &sidecar_filepath, 
                     std::string &out_url, 
                     uint64_t &out_total_size, 
                     std::string &out_etag, 
                     ChunkManager &out_chunk_manager,
                     bool &out_supports_ranges);

    static bool load(const std::string &sidecar_filepath, 
                     std::string &out_url, 
                     uint64_t &out_total_size, 
                     std::string &out_etag, 
                     ChunkManager &out_chunk_manager) {
        bool dummy = true;
        return load(sidecar_filepath, out_url, out_total_size, out_etag, out_chunk_manager, dummy);
    }

    static bool exists(const std::string &sidecar_filepath);
    static bool remove(const std::string &sidecar_filepath);

    static std::string get_effective_cache_dir(const std::string &cache_directory = "");
    static uint64_t get_cache_size_bytes(const std::string &cache_directory = "");
    static size_t prune_old_cache(int max_age_days = 7, const std::string &cache_directory = "");
    static size_t enforce_cache_limit(uint64_t max_bytes, const std::string &cache_directory = "");
    static size_t purge_all_cache(const std::string &cache_directory = "");
};

#endif 
