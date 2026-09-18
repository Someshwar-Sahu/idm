#include "ChunkManager.h"
#include <algorithm>

using namespace std;

ChunkManager::ChunkManager(uint64_t total_file_size, int num_chunks) {
    init(total_file_size, num_chunks);
}

void ChunkManager::init(uint64_t total_file_size, int num_chunks) {
    lock_guard<mutex> lock(m_mutex);
    m_total_file_size = total_file_size;
    m_chunks.clear();

    if (total_file_size == 0 || num_chunks <= 0) {
        return;
    }

    uint64_t chunk_size = total_file_size / num_chunks;
    uint64_t start = 0;

    for (int i = 0; i < num_chunks; ++i) {
        Chunk chunk;
        chunk.id = i;
        chunk.start_byte = start;
        
        if (i == num_chunks - 1) {
            chunk.end_byte = total_file_size - 1;
        } else {
            chunk.end_byte = start + chunk_size - 1;
        }
        chunk.bytes_downloaded = 0;
        chunk.state = ChunkState::Pending;
        chunk.retry_count = 0;

        m_chunks.push_back(chunk);
        start = chunk.end_byte + 1;
    }
}

optional<Chunk> ChunkManager::get_next_pending_chunk() {
    lock_guard<mutex> lock(m_mutex);
    for (auto &chunk : m_chunks) {
        if (chunk.state == ChunkState::Pending) {
            chunk.state = ChunkState::InProgress;
            return chunk;
        }
    }
    return nullopt;
}

std::optional<Chunk> ChunkManager::steal_slowest_chunk(uint64_t min_steal_threshold){
    std::lock_guard<std::mutex> lock(m_mutex);

    int best_donor_id = -1;
    uint64_t max_remaining_bytes = 0;

    for(size_t i=0;i<m_chunks.size(); ++i){
        if(m_chunks[i].state == ChunkState::InProgress){
            uint64_t total = m_chunks[i].total_bytes();
            uint64_t downloaded = m_chunks[i].bytes_downloaded;
            if(total > downloaded){
                uint64_t remaining = total - downloaded;
                if(remaining > max_remaining_bytes && remaining >= min_steal_threshold){
                    max_remaining_bytes = remaining;
                    best_donor_id = static_cast<int>(i);
                }
            }
        }
    }

    if (best_donor_id == -1){
        return std::nullopt;
    }

    Chunk &donor = m_chunks[best_donor_id];
    uint64_t current_pos = donor.start_byte + donor.bytes_downloaded;
    uint64_t remaining = (donor.end_byte >= current_pos) ? (donor.end_byte - current_pos + 1): 0;

    if(remaining< min_steal_threshold){
        return std::nullopt;
    }

    uint64_t half = remaining /2;
    uint64_t new_chunk_start = current_pos + half;
    uint64_t new_chunk_end = donor.end_byte;

    donor.end_byte = new_chunk_start -1;

    Chunk stolen;
    stolen.id = static_cast<int>(m_chunks.size());
    stolen.start_byte = new_chunk_start;
    stolen.end_byte = new_chunk_end;
    stolen.bytes_downloaded = 0;
    stolen.state = ChunkState::InProgress;
    stolen.retry_count =0;
    m_chunks.push_back(stolen);
    return stolen;
}

uint64_t ChunkManager::get_chunk_end_byte(int chunk_id) const{
    std::lock_guard<std::mutex> lock(m_mutex);
    if(chunk_id>=0 && chunk_id <static_cast<int>(m_chunks.size())){
        return m_chunks[chunk_id].end_byte;
    }
    return 0;
}

void ChunkManager::update_chunk_progress(int chunk_id, uint64_t bytes_downloaded) {
    lock_guard<mutex> lock(m_mutex);
    if (chunk_id >= 0 && chunk_id < static_cast<int>(m_chunks.size())) {
        m_chunks[chunk_id].bytes_downloaded = bytes_downloaded;
    }
}

void ChunkManager::mark_chunk_done(int chunk_id) {
    lock_guard<mutex> lock(m_mutex);
    if (chunk_id >= 0 && chunk_id < static_cast<int>(m_chunks.size())) {
        m_chunks[chunk_id].state = ChunkState::Done;
        m_chunks[chunk_id].bytes_downloaded = m_chunks[chunk_id].total_bytes();
    }
}

void ChunkManager::mark_chunk_failed(int chunk_id, int max_retries) {
    lock_guard<mutex> lock(m_mutex);
    if (chunk_id >= 0 && chunk_id < static_cast<int>(m_chunks.size())) {
        m_chunks[chunk_id].retry_count++;
        if (m_chunks[chunk_id].retry_count < max_retries) {
            m_chunks[chunk_id].state = ChunkState::Pending; 
        } else {
            m_chunks[chunk_id].state = ChunkState::Failed; 
        }
    }
}

bool ChunkManager::is_complete() const {
    lock_guard<mutex> lock(m_mutex);
    return all_of(m_chunks.begin(), m_chunks.end(), [](const Chunk &c) {
        return c.state == ChunkState::Done;
    });
}

uint64_t ChunkManager::get_total_downloaded_bytes() const {
    lock_guard<mutex> lock(m_mutex);
    uint64_t total = 0;
    for (const auto &chunk : m_chunks) {
        total += chunk.bytes_downloaded;
    }
    return total;
}

uint64_t ChunkManager::get_total_file_size() const {
    lock_guard<mutex> lock(m_mutex);
    return m_total_file_size;
}

vector<Chunk> ChunkManager::get_all_chunks() const {
    lock_guard<mutex> lock(m_mutex);
    return m_chunks;
}
