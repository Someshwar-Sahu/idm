#ifndef CHUNK_WORKER_H
#define CHUNK_WORKER_H

#include "Chunk.h"
#include"ChunkManager.h"
#include"FileWriter.h"
#include<string>
#include<cstdint>
#include<curl/curl.h>

#include <atomic>
#include <memory>

class ChannelBonding;

class ChunkWorker {
public:
    ChunkWorker(const std::string &url, Chunk chunk, ChunkManager &chunk_manager, FileWriter &file_writer, uint64_t max_bytes_per_sec =0, const std::string &referer = "", const std::string &cookies = "", const std::string &user_agent = "");
    ~ChunkWorker();

    ChunkWorker(const ChunkWorker&) = delete;
    ChunkWorker& operator=(const ChunkWorker&) = delete;

    bool download();

    void set_pause_flag(std::atomic<bool> *pause_requested) { m_pause_requested = pause_requested; }
    void set_speed_limit(uint64_t max_bytes_per_sec);
    void set_dynamic_speed_limit(const std::atomic<uint64_t> *speed_limit_ptr) { m_speed_limit_ptr = speed_limit_ptr; }
    void set_speed_limiter(void*) {} // Stub for compatibility
    uint64_t get_speed_limit() const;
    void set_user_agent(const std::string &ua) { m_user_agent = ua; }
    void set_referer(const std::string &ref) { m_referer = ref; }
    void set_cookies(const std::string &cookies) { m_cookies = cookies; }
    void set_supports_ranges(bool supports) { m_supports_ranges = supports; }

    void set_channel_bonding(std::shared_ptr<ChannelBonding> bonding) { m_channel_bonding = bonding; }
    static void set_default_channel_bonding(std::shared_ptr<ChannelBonding> bonding) { s_default_channel_bonding = bonding; }
    static std::shared_ptr<ChannelBonding> get_default_channel_bonding() { return s_default_channel_bonding; }

    void set_auth_credentials(const std::string &username, const std::string &password, const std::string &auth_header = "") {
        m_username = username;
        m_password = password;
        m_auth_header = auth_header;
    }

private:
    static size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata);

    struct WriteContext {
        FileWriter *file_writer{nullptr};
        ChunkManager *chunk_manager{nullptr};
        int chunk_id{0};
        uint64_t current_offset{0};
        uint64_t end_byte{0};
        uint64_t bytes_written{0};
        CURL *curl{nullptr};
        std::atomic<bool> *pause_requested{nullptr};
        bool was_paused{false};
        bool is_range_request{false};
        const std::atomic<uint64_t> *dynamic_speed_limit{nullptr};
        uint64_t last_applied_speed_limit{0};
    };

    std::string m_url;
    Chunk m_chunk;
    ChunkManager &m_chunk_manager;
    FileWriter &m_file_writer;
    uint64_t m_max_bytes_per_sec{0};
    const std::atomic<uint64_t> *m_speed_limit_ptr{nullptr};
    std::string m_referer;
    std::string m_cookies;
    std::string m_user_agent;
    std::string m_username;
    std::string m_password;
    std::string m_auth_header;
    bool m_supports_ranges{true};
    CURL *m_curl{nullptr};
    std::atomic<bool> *m_pause_requested{nullptr};
    std::shared_ptr<ChannelBonding> m_channel_bonding{nullptr};
    inline static std::shared_ptr<ChannelBonding> s_default_channel_bonding{nullptr};
};

#endif