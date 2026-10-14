#ifndef SWARM_DOWNLOADER_H
#define SWARM_DOWNLOADER_H

#include "../core/ChunkManager.h"
#include "../core/FileWriter.h"
#include "../core/ThreadPool.h"

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <functional>
#include <cstdint>

namespace idm {

struct Mirror {
    int id{0};
    std::string url;
    int priority{1}; // 1 = highest priority
    std::string location; // e.g. "US", "DE", "JP"
    bool active{true};
    uint64_t bytes_downloaded{0};
    uint32_t failure_count{0};
    uint32_t success_count{0};
};

struct MetalinkMetadata {
    std::string filename;
    uint64_t size{0};
    std::string sha256_hash;
    std::string md5_hash;
    std::vector<Mirror> mirrors;
    bool is_valid{false};
};

enum class MultiplexStrategy {
    RoundRobin,
    PriorityWeighted,
    FastestFirst
};

class SwarmDownloader {
public:
    // Pluggable transport callback for deterministic offline unit testing
    // Signature: (url, start_byte, end_byte, output_buffer) -> bool (true = success)
    using MockTransport = std::function<bool(const std::string&, uint64_t, uint64_t, std::vector<uint8_t>&)>;
    using ProgressCallback = std::function<void(uint64_t downloaded, uint64_t total, double speed_bps)>;

    SwarmDownloader(const std::string& destination_path, int num_threads = 4);
    ~SwarmDownloader();

    // Mirror management
    int add_mirror(const std::string& url, int priority = 1, const std::string& location = "");
    bool remove_mirror(const std::string& url);
    void clear_mirrors();
    std::vector<Mirror> get_mirrors() const;
    std::vector<Mirror> get_active_mirrors() const;
    void set_mirror_active(int mirror_id, bool active);

    // Strategy & Config
    void set_strategy(MultiplexStrategy strategy);
    MultiplexStrategy get_strategy() const;
    void set_max_mirror_failures(uint32_t max_failures);
    uint32_t get_max_mirror_failures() const;

    // Metalink Parsing (XML & JSON)
    static MetalinkMetadata parse_metalink_xml(const std::string& xml_content);
    static MetalinkMetadata parse_metalink_json(const std::string& json_content);
    bool load_metalink(const MetalinkMetadata& meta);

    // Mirror Selection & Failover Dispatch
    int select_mirror_for_chunk(int chunk_id);
    void record_mirror_success(int mirror_id, uint64_t bytes);
    void record_mirror_failure(int mirror_id);

    // Execution & Verification
    void set_mock_transport(MockTransport mock);
    bool download(uint64_t file_size, int num_chunks, ProgressCallback callback = nullptr);
    bool verify_integrity(const std::string& expected_sha256) const;

    // Accessors
    std::string get_destination_path() const { return m_destination_path; }
    uint64_t get_total_downloaded() const;
    std::string get_last_error() const;

private:
    std::string m_destination_path;
    int m_num_threads{4};
    MultiplexStrategy m_strategy{MultiplexStrategy::RoundRobin};
    uint32_t m_max_mirror_failures{3};

    std::vector<Mirror> m_mirrors;
    mutable std::mutex m_mirror_mutex;
    int m_next_mirror_id{1};
    std::atomic<size_t> m_round_robin_counter{0};

    std::unique_ptr<ChunkManager> m_chunk_manager;
    std::unique_ptr<FileWriter> m_file_writer;
    std::unique_ptr<ThreadPool> m_thread_pool;

    MockTransport m_mock_transport{nullptr};
    std::string m_last_error;
};

} // namespace idm

#endif // SWARM_DOWNLOADER_H
