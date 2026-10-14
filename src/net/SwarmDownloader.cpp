#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "SwarmDownloader.h"
#include "../security/AntivirusScanner.h"
#include <nlohmann/json.hpp>
#include <curl/curl.h>

#include <algorithm>
#include <chrono>
#include <thread>
#include <cctype>

namespace idm {

namespace {

std::string trim_str(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string extract_xml_tag(const std::string& xml, const std::string& tag, size_t start_pos = 0) {
    std::string open_tag = "<" + tag;
    size_t o_pos = xml.find(open_tag, start_pos);
    if (o_pos == std::string::npos) return "";

    size_t content_start = xml.find('>', o_pos);
    if (content_start == std::string::npos) return "";
    content_start++;

    std::string close_tag = "</" + tag + ">";
    size_t c_pos = xml.find(close_tag, content_start);
    if (c_pos == std::string::npos) return "";

    return trim_str(xml.substr(content_start, c_pos - content_start));
}

std::string extract_xml_attribute(const std::string& element, const std::string& attr) {
    std::string pattern = attr + "=\"";
    size_t pos = element.find(pattern);
    if (pos == std::string::npos) {
        pattern = attr + "='";
        pos = element.find(pattern);
        if (pos == std::string::npos) return "";
    }
    pos += pattern.length();
    char quote = pattern[pattern.length() - 1];
    size_t end_pos = element.find(quote, pos);
    if (end_pos == std::string::npos) return "";
    return element.substr(pos, end_pos - pos);
}

} // namespace

SwarmDownloader::SwarmDownloader(const std::string& destination_path, int num_threads)
    : m_destination_path(destination_path),
      m_num_threads(num_threads > 0 ? num_threads : 4) {}

SwarmDownloader::~SwarmDownloader() = default;

int SwarmDownloader::add_mirror(const std::string& url, int priority, const std::string& location) {
    std::string clean_url = trim_str(url);
    if (clean_url.empty()) return -1;

    std::lock_guard<std::mutex> lock(m_mirror_mutex);
    for (const auto& m : m_mirrors) {
        if (m.url == clean_url) {
            return m.id; // Already registered
        }
    }

    Mirror m;
    m.id = m_next_mirror_id++;
    m.url = clean_url;
    m.priority = priority > 0 ? priority : 1;
    m.location = location;
    m.active = true;
    m_mirrors.push_back(m);
    return m.id;
}

bool SwarmDownloader::remove_mirror(const std::string& url) {
    std::lock_guard<std::mutex> lock(m_mirror_mutex);
    for (auto it = m_mirrors.begin(); it != m_mirrors.end(); ++it) {
        if (it->url == url) {
            m_mirrors.erase(it);
            return true;
        }
    }
    return false;
}

void SwarmDownloader::clear_mirrors() {
    std::lock_guard<std::mutex> lock(m_mirror_mutex);
    m_mirrors.clear();
}

std::vector<Mirror> SwarmDownloader::get_mirrors() const {
    std::lock_guard<std::mutex> lock(m_mirror_mutex);
    return m_mirrors;
}

std::vector<Mirror> SwarmDownloader::get_active_mirrors() const {
    std::lock_guard<std::mutex> lock(m_mirror_mutex);
    std::vector<Mirror> active;
    for (const auto& m : m_mirrors) {
        if (m.active) active.push_back(m);
    }
    return active;
}

void SwarmDownloader::set_mirror_active(int mirror_id, bool active) {
    std::lock_guard<std::mutex> lock(m_mirror_mutex);
    for (auto& m : m_mirrors) {
        if (m.id == mirror_id) {
            m.active = active;
            break;
        }
    }
}

void SwarmDownloader::set_strategy(MultiplexStrategy strategy) {
    m_strategy = strategy;
}

MultiplexStrategy SwarmDownloader::get_strategy() const {
    return m_strategy;
}

void SwarmDownloader::set_max_mirror_failures(uint32_t max_failures) {
    m_max_mirror_failures = max_failures;
}

uint32_t SwarmDownloader::get_max_mirror_failures() const {
    return m_max_mirror_failures;
}

MetalinkMetadata SwarmDownloader::parse_metalink_xml(const std::string& xml_content) {
    MetalinkMetadata meta;
    if (xml_content.empty()) return meta;

    // File name: Match <file ...> while skipping container tags like <files>
    size_t file_tag_pos = 0;
    while ((file_tag_pos = xml_content.find("<file", file_tag_pos)) != std::string::npos) {
        char next_ch = (file_tag_pos + 5 < xml_content.size()) ? xml_content[file_tag_pos + 5] : '\0';
        if (std::isspace(static_cast<unsigned char>(next_ch)) || next_ch == '>') {
            size_t tag_end = xml_content.find('>', file_tag_pos);
            if (tag_end != std::string::npos) {
                std::string file_header = xml_content.substr(file_tag_pos, tag_end - file_tag_pos);
                meta.filename = extract_xml_attribute(file_header, "name");
            }
            break;
        }
        file_tag_pos += 5;
    }

    if (meta.filename.empty()) {
        meta.filename = extract_xml_tag(xml_content, "identity");
        if (meta.filename.empty()) {
            meta.filename = extract_xml_tag(xml_content, "name");
        }
    }

    // Size
    std::string size_str = extract_xml_tag(xml_content, "size");
    if (!size_str.empty()) {
        try {
            meta.size = std::stoull(size_str);
        } catch (...) {
            meta.size = 0;
        }
    }

    // Hash (sha256 or md5)
    size_t hash_pos = 0;
    while ((hash_pos = xml_content.find("<hash", hash_pos)) != std::string::npos) {
        char next_ch = (hash_pos + 5 < xml_content.size()) ? xml_content[hash_pos + 5] : '\0';
        if (next_ch != '>' && !std::isspace(static_cast<unsigned char>(next_ch))) {
            hash_pos += 5;
            continue;
        }

        size_t tag_end = xml_content.find('>', hash_pos);
        if (tag_end == std::string::npos) break;
        std::string hash_header = xml_content.substr(hash_pos, tag_end - hash_pos);
        std::string type = extract_xml_attribute(hash_header, "type");
        std::transform(type.begin(), type.end(), type.begin(), [](unsigned char c) { return std::tolower(c); });

        size_t close_pos = xml_content.find("</hash>", tag_end);
        if (close_pos != std::string::npos) {
            std::string hash_val = trim_str(xml_content.substr(tag_end + 1, close_pos - (tag_end + 1)));
            if (type == "sha256" || type == "sha-256") {
                meta.sha256_hash = hash_val;
            } else if (type == "md5") {
                meta.md5_hash = hash_val;
            }
            hash_pos = close_pos + 7;
        } else {
            break;
        }
    }

    // Mirrors: <url ...>...</url>
    size_t url_pos = 0;
    int mirror_id = 1;
    while ((url_pos = xml_content.find("<url", url_pos)) != std::string::npos) {
        char next_ch = (url_pos + 4 < xml_content.size()) ? xml_content[url_pos + 4] : '\0';
        if (next_ch != '>' && !std::isspace(static_cast<unsigned char>(next_ch))) {
            url_pos += 4;
            continue;
        }

        size_t tag_end = xml_content.find('>', url_pos);
        if (tag_end == std::string::npos) break;
        std::string url_header = xml_content.substr(url_pos, tag_end - url_pos);
        std::string location = extract_xml_attribute(url_header, "location");
        std::string prio_str = extract_xml_attribute(url_header, "priority");

        int prio = 1;
        if (!prio_str.empty()) {
            try { prio = std::stoi(prio_str); } catch (...) { prio = 1; }
        }

        size_t close_pos = xml_content.find("</url>", tag_end);
        if (close_pos != std::string::npos) {
            std::string mirror_url = trim_str(xml_content.substr(tag_end + 1, close_pos - (tag_end + 1)));
            if (!mirror_url.empty()) {
                Mirror m;
                m.id = mirror_id++;
                m.url = mirror_url;
                m.priority = prio;
                m.location = location;
                m.active = true;
                meta.mirrors.push_back(m);
            }
            url_pos = close_pos + 6;
        } else {
            break;
        }
    }

    meta.is_valid = (!meta.mirrors.empty() && meta.size > 0);
    return meta;
}


MetalinkMetadata SwarmDownloader::parse_metalink_json(const std::string& json_content) {
    MetalinkMetadata meta;
    if (json_content.empty()) return meta;

    try {
        auto j = nlohmann::json::parse(json_content);
        if (j.contains("name") && j["name"].is_string()) {
            meta.filename = j["name"].get<std::string>();
        }
        if (j.contains("size") && j["size"].is_number()) {
            meta.size = j["size"].get<uint64_t>();
        }
        if (j.contains("hashes") && j["hashes"].is_object()) {
            const auto& h = j["hashes"];
            if (h.contains("sha256") && h["sha256"].is_string()) {
                meta.sha256_hash = h["sha256"].get<std::string>();
            }
            if (h.contains("md5") && h["md5"].is_string()) {
                meta.md5_hash = h["md5"].get<std::string>();
            }
        }
        if (j.contains("mirrors") && j["mirrors"].is_array()) {
            int mirror_id = 1;
            for (const auto& item : j["mirrors"]) {
                if (item.is_object() && item.contains("url") && item["url"].is_string()) {
                    Mirror m;
                    m.id = mirror_id++;
                    m.url = item["url"].get<std::string>();
                    m.priority = item.value("priority", 1);
                    m.location = item.value("location", "");
                    m.active = true;
                    meta.mirrors.push_back(m);
                }
            }
        }
        meta.is_valid = (!meta.mirrors.empty() && meta.size > 0);
    } catch (...) {
        meta.is_valid = false;
    }
    return meta;
}

bool SwarmDownloader::load_metalink(const MetalinkMetadata& meta) {
    if (!meta.is_valid) return false;
    std::lock_guard<std::mutex> lock(m_mirror_mutex);
    m_mirrors = meta.mirrors;
    return true;
}

int SwarmDownloader::select_mirror_for_chunk(int chunk_id) {
    std::lock_guard<std::mutex> lock(m_mirror_mutex);
    std::vector<int> active_indices;
    for (size_t i = 0; i < m_mirrors.size(); ++i) {
        if (m_mirrors[i].active) {
            active_indices.push_back(static_cast<int>(i));
        }
    }

    if (active_indices.empty()) {
        return -1; // No active mirrors available!
    }

    if (m_strategy == MultiplexStrategy::RoundRobin) {
        size_t idx = m_round_robin_counter.fetch_add(1) % active_indices.size();
        return m_mirrors[active_indices[idx]].id;
    }

    if (m_strategy == MultiplexStrategy::PriorityWeighted) {
        // Find mirrors with best (lowest) priority value
        int best_prio = 999999;
        for (int idx : active_indices) {
            if (m_mirrors[idx].priority < best_prio) {
                best_prio = m_mirrors[idx].priority;
            }
        }
        std::vector<int> top_tier;
        for (int idx : active_indices) {
            if (m_mirrors[idx].priority == best_prio) {
                top_tier.push_back(idx);
            }
        }
        size_t idx = m_round_robin_counter.fetch_add(1) % top_tier.size();
        return m_mirrors[top_tier[idx]].id;
    }

    if (m_strategy == MultiplexStrategy::FastestFirst) {
        // Choose mirror with highest success count and lowest failures
        int best_idx = active_indices[0];
        uint32_t min_failures = m_mirrors[best_idx].failure_count;
        uint32_t max_success = m_mirrors[best_idx].success_count;

        for (int idx : active_indices) {
            if (m_mirrors[idx].failure_count < min_failures ||
               (m_mirrors[idx].failure_count == min_failures && m_mirrors[idx].success_count > max_success)) {
                min_failures = m_mirrors[idx].failure_count;
                max_success = m_mirrors[idx].success_count;
                best_idx = idx;
            }
        }
        return m_mirrors[best_idx].id;
    }

    return m_mirrors[active_indices[0]].id;
}

void SwarmDownloader::record_mirror_success(int mirror_id, uint64_t bytes) {
    std::lock_guard<std::mutex> lock(m_mirror_mutex);
    for (auto& m : m_mirrors) {
        if (m.id == mirror_id) {
            m.success_count++;
            m.bytes_downloaded += bytes;
            break;
        }
    }
}

void SwarmDownloader::record_mirror_failure(int mirror_id) {
    std::lock_guard<std::mutex> lock(m_mirror_mutex);
    for (auto& m : m_mirrors) {
        if (m.id == mirror_id) {
            m.failure_count++;
            if (m.failure_count >= m_max_mirror_failures) {
                m.active = false; // Demote/disable degraded mirror
            }
            break;
        }
    }
}

void SwarmDownloader::set_mock_transport(MockTransport mock) {
    m_mock_transport = mock;
}

uint64_t SwarmDownloader::get_total_downloaded() const {
    if (!m_chunk_manager) return 0;
    return m_chunk_manager->get_total_downloaded_bytes();
}

std::string SwarmDownloader::get_last_error() const {
    return m_last_error;
}

bool SwarmDownloader::download(uint64_t file_size, int num_chunks, ProgressCallback callback) {
    if (file_size == 0) {
        m_last_error = "File size cannot be zero";
        return false;
    }

    std::vector<Mirror> active = get_active_mirrors();
    if (active.empty()) {
        m_last_error = "No active mirrors registered in swarm";
        return false;
    }

    int chunks_count = (num_chunks > 0) ? num_chunks : m_num_threads;
    m_chunk_manager = std::make_unique<ChunkManager>(file_size, chunks_count);

    FileWriter::preallocate_file(m_destination_path, file_size);
    m_file_writer = std::make_unique<FileWriter>();
    if (!m_file_writer->open(m_destination_path)) {
        m_last_error = "Failed to open destination file for writing";
        return false;
    }

    m_thread_pool = std::make_unique<ThreadPool>(m_num_threads);

    for (int t = 0; t < m_num_threads; ++t) {
        m_thread_pool->enqueue([this]() {
            while (!m_chunk_manager->is_complete()) {
                auto chunk_opt = m_chunk_manager->get_next_pending_chunk();
                if (!chunk_opt.has_value()) {
                    chunk_opt = m_chunk_manager->steal_slowest_chunk();
                    if (!chunk_opt.has_value()) break;
                }

                Chunk chunk = chunk_opt.value();
                bool chunk_downloaded = false;

                // Try downloading chunk with active mirrors (supports failover)
                while (!chunk_downloaded) {
                    int mirror_id = select_mirror_for_chunk(chunk.id);
                    if (mirror_id < 0) {
                        m_chunk_manager->mark_chunk_failed(chunk.id);
                        break; // Exhausted all active mirrors
                    }

                    std::string mirror_url;
                    {
                        std::lock_guard<std::mutex> lock(m_mirror_mutex);
                        for (const auto& m : m_mirrors) {
                            if (m.id == mirror_id) {
                                mirror_url = m.url;
                                break;
                            }
                        }
                    }

                    uint64_t chunk_length = (chunk.end_byte >= chunk.start_byte) ? (chunk.end_byte - chunk.start_byte + 1) : 0;
                    bool success = false;

                    if (m_mock_transport) {
                        std::vector<uint8_t> buffer;
                        success = m_mock_transport(mirror_url, chunk.start_byte, chunk.end_byte, buffer);
                        if (success && !buffer.empty()) {
                            m_file_writer->write_at_offset(chunk.start_byte, buffer.data(), buffer.size());
                        }
                    } else {
                        // Real curl range download
                        CURL* curl = curl_easy_init();
                        if (curl) {
                            std::string range_hdr = std::to_string(chunk.start_byte) + "-" + std::to_string(chunk.end_byte);
                            curl_easy_setopt(curl, CURLOPT_URL, mirror_url.c_str());
                            curl_easy_setopt(curl, CURLOPT_RANGE, range_hdr.c_str());
                            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
                            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);

                            struct WorkerCtx {
                                FileWriter* writer;
                                uint64_t offset;
                                uint64_t bytes_written{0};
                            } ctx{m_file_writer.get(), chunk.start_byte, 0};

                            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](char* ptr, size_t size, size_t nmemb, void* udata) -> size_t {
                                auto* c = static_cast<WorkerCtx*>(udata);
                                size_t total = size * nmemb;
                                if (c->writer->write_at_offset(c->offset + c->bytes_written, ptr, total)) {
                                    c->bytes_written += total;
                                    return total;
                                }
                                return 0;
                            });
                            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);

                            CURLcode res = curl_easy_perform(curl);
                            long code = 0;
                            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
                            curl_easy_cleanup(curl);

                            success = (res == CURLE_OK && (code == 200 || code == 206));
                        }
                    }

                    if (success) {
                        record_mirror_success(mirror_id, chunk_length);
                        m_chunk_manager->update_chunk_progress(chunk.id, chunk_length);
                        m_chunk_manager->mark_chunk_done(chunk.id);
                        chunk_downloaded = true;
                    } else {
                        // Failover: penalize this mirror and retry this chunk with next mirror
                        record_mirror_failure(mirror_id);
                    }
                }
            }
        });
    }

    // Monitor progress
    while (!m_chunk_manager->is_complete()) {
        if (get_active_mirrors().empty()) {
            m_last_error = "All mirrors failed during swarm transfer";
            break;
        }
        if (callback) {
            callback(m_chunk_manager->get_total_downloaded_bytes(), file_size, 0.0);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    m_thread_pool.reset(); // Wait for all workers to join
    m_file_writer->close();

    bool complete = m_chunk_manager->is_complete();
    if (!complete && m_last_error.empty()) {
        m_last_error = "Swarm download terminated before completion";
    }
    return complete;
}

bool SwarmDownloader::verify_integrity(const std::string& expected_sha256) const {
    if (expected_sha256.empty()) return true;
    std::string actual_hash = AntivirusScanner::calculate_file_sha256(m_destination_path);
    if (actual_hash.empty()) return false;

    std::string exp = expected_sha256;
    std::string act = actual_hash;
    std::transform(exp.begin(), exp.end(), exp.begin(), [](unsigned char c) { return std::tolower(c); });
    std::transform(act.begin(), act.end(), act.begin(), [](unsigned char c) { return std::tolower(c); });

    return (exp == act);
}

} // namespace idm
