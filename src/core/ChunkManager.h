#ifndef CHUNK_MANAGER_H
#define CHUNK_MANAGER_H

#include "Chunk.h"
#include <vector>
#include <mutex>
#include <cstdint>
#include <optional>

class ChunkManager {
public:
    ChunkManager() = default;
    ChunkManager(uint64_t total_file_size, int num_chunks);

    void init(uint64_t total_file_size, int num_chunks);

    std::optional<Chunk> get_next_pending_chunk();

    std::optional<Chunk> steal_slowest_chunk(uint64_t min_steal_threshold = 1024 * 1024);

    void update_chunk_progress(int chunk_id, uint64_t bytes_downloaded);
    void mark_chunk_done(int chunk_id);
    void mark_chunk_failed(int chunk_id, int max_retries = 3);
    
    bool is_complete() const;
    uint64_t get_total_downloaded_bytes() const;
    uint64_t get_total_file_size() const;
    std::vector<Chunk> get_all_chunks() const;
    uint64_t get_chunk_end_byte(int chunk_id) const;

private:
    uint64_t m_total_file_size{0};
    std::vector<Chunk> m_chunks;
    mutable std::mutex m_mutex;
};

#endif
