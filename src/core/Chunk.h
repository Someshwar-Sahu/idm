#ifndef CHUNK_H
#define CHUNK_H

#include <cstdint>
#include <string>

enum class ChunkState {
    Pending,
    InProgress,
    Done,
    Failed
};

struct Chunk {
    int id{0};
    uint64_t start_byte{0};
    uint64_t end_byte{0};
    uint64_t bytes_downloaded{0};
    ChunkState state{ChunkState::Pending};
    int retry_count{0};

    uint64_t total_bytes() const {
        return (end_byte >= start_byte) ? (end_byte - start_byte + 1) : 0;
    }

    static std::string state_to_string(ChunkState s) {
        switch (s) {
            case ChunkState::Pending: return "Pending";
            case ChunkState::InProgress: return "InProgress";
            case ChunkState::Done: return "Done";
            case ChunkState::Failed: return "Failed";
        }
        return "Unknown";
    }
};

#endif 
