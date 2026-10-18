#include "ChunkWorker.h"
#include <iostream>
#include <thread>
#include<chrono>
#include<sstream>

using namespace std;

ChunkWorker::ChunkWorker(const string &url,
                         Chunk chunk,
                         ChunkManager &chunk_manager,
                         FileWriter &file_writer,
                         uint64_t max_bytes_per_sec,
                         const string &referer,
                         const string &cookies,
                         const string &user_agent)
    : m_url(url),
      m_chunk(chunk),
      m_chunk_manager(chunk_manager),
      m_file_writer(file_writer),
      m_max_bytes_per_sec(max_bytes_per_sec),
      m_referer(referer),
      m_cookies(cookies),
      m_user_agent(user_agent) {
    m_curl = curl_easy_init();
}

ChunkWorker::~ChunkWorker() {
    if(m_curl != nullptr){
        curl_easy_cleanup(m_curl);
        m_curl = nullptr;
    }
}

void ChunkWorker::set_speed_limit(uint64_t max_bytes_per_sec){
    m_max_bytes_per_sec =max_bytes_per_sec;
    if(m_curl != nullptr){
        curl_easy_setopt(m_curl, CURLOPT_MAX_RECV_SPEED_LARGE, static_cast<curl_off_t>(max_bytes_per_sec));
    }
}

uint64_t ChunkWorker::get_speed_limit() const{
    return m_max_bytes_per_sec;
}

size_t ChunkWorker::write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t total_bytes = size * nmemb;
    if (userdata == nullptr || total_bytes == 0) {
        return 0;
    }
    WriteContext *ctx = static_cast<WriteContext*>(userdata);

    // Abort curl immediately if user requested pause
    if (ctx->pause_requested && ctx->pause_requested->load()) {
        ctx->was_paused = true;
        return 0;
    }

    // Guard against writing HTTP error HTML pages (429, 503, 404, etc.) to target file
    if (ctx->curl != nullptr) {
        long http_code = 0;
        curl_easy_getinfo(ctx->curl, CURLINFO_RESPONSE_CODE, &http_code);
        if (http_code >= 400 || (http_code != 200 && http_code != 206 && http_code != 0)) {
            // Discard error body payload without writing to disk or advancing progress
            return total_bytes;
        }
    }

    uint64_t dynamic_end_byte = ctx->chunk_manager->get_chunk_end_byte(ctx->chunk_id);
    if (dynamic_end_byte > 0 && ctx->current_offset > dynamic_end_byte) {
        return 0; // Boundary reached or chunk stolen
    }

    if (dynamic_end_byte > 0 && (ctx->current_offset + total_bytes - 1) > dynamic_end_byte) {
        total_bytes = static_cast<size_t>(dynamic_end_byte - ctx->current_offset + 1);
    }

    bool ok = ctx->file_writer->write_at_offset(ctx->current_offset, ptr, total_bytes);
    if (!ok) {
        return 0;
    }

    ctx->current_offset += total_bytes;
    ctx->bytes_written += total_bytes;

    ctx->chunk_manager->update_chunk_progress(ctx->chunk_id, ctx->bytes_written);

    return total_bytes;
}

bool ChunkWorker::download() {
    if (m_curl == nullptr) {
        m_chunk_manager.mark_chunk_failed(m_chunk.id);
        return false;
    }

    uint64_t current_end = m_chunk_manager.get_chunk_end_byte(m_chunk.id);
    if (current_end == 0) current_end = m_chunk.end_byte;
    uint64_t expected_total_bytes = (current_end >= m_chunk.start_byte) ? (current_end - m_chunk.start_byte + 1) : 0;

    // Check if chunk is already fully downloaded
    if (expected_total_bytes > 0 && m_chunk.bytes_downloaded >= expected_total_bytes) {
        m_chunk_manager.mark_chunk_done(m_chunk.id);
        return true;
    }

    constexpr int MAX_RETRIES = 5;
    int attempt = 0;
    int backoff_seconds = 2;

    while (attempt < MAX_RETRIES) {
        if (m_pause_requested && m_pause_requested->load()) {
            return false;
        }

        attempt++;
        uint64_t attempt_start_bytes = m_chunk.bytes_downloaded;

        // Bug 1 Fix: Refresh dynamic chunk end byte before every attempt and range calculation
        uint64_t dynamic_end = m_chunk_manager.get_chunk_end_byte(m_chunk.id);
        if (dynamic_end > 0) {
            m_chunk.end_byte = dynamic_end;
        }

        curl_easy_reset(m_curl);
        curl_easy_setopt(m_curl, CURLOPT_URL, m_url.c_str());
        curl_easy_setopt(m_curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(m_curl, CURLOPT_UNRESTRICTED_AUTH, 1L);
        curl_easy_setopt(m_curl, CURLOPT_AUTOREFERER, 1L);
        curl_easy_setopt(m_curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(m_curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(m_curl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NO_REVOKE);

        // Maximize network transfer speed & buffer size
        curl_easy_setopt(m_curl, CURLOPT_BUFFERSIZE, 524288L); // 512 KB transfer buffer
        curl_easy_setopt(m_curl, CURLOPT_TCP_NODELAY, 1L);     // Disable Nagle algorithm

        // Socket timeouts & keepalive to prevent hung connections
        curl_easy_setopt(m_curl, CURLOPT_CONNECTTIMEOUT, 15L);

        // Bug 13 Fix: Scale low speed limit dynamically to avoid false positives during low bandwidth throttling
        long low_speed_limit = 1024L;
        long low_speed_time = 25L;
        if (m_max_bytes_per_sec > 0) {
            low_speed_limit = std::min(1024L, static_cast<long>(m_max_bytes_per_sec / 4));
            if (low_speed_limit < 10L) low_speed_limit = 10L;
            low_speed_time = 30L;
        }
        curl_easy_setopt(m_curl, CURLOPT_LOW_SPEED_LIMIT, low_speed_limit);
        curl_easy_setopt(m_curl, CURLOPT_LOW_SPEED_TIME, low_speed_time);
        curl_easy_setopt(m_curl, CURLOPT_TCP_KEEPALIVE, 1L);
        curl_easy_setopt(m_curl, CURLOPT_TCP_KEEPIDLE, 30L);
        curl_easy_setopt(m_curl, CURLOPT_TCP_KEEPINTVL, 15L);

        string ua = m_user_agent.empty() ? "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36" : m_user_agent;
        curl_easy_setopt(m_curl, CURLOPT_USERAGENT, ua.c_str());

        if (!m_referer.empty()) {
            curl_easy_setopt(m_curl, CURLOPT_REFERER, m_referer.c_str());
        }
        if (!m_cookies.empty()) {
            curl_easy_setopt(m_curl, CURLOPT_COOKIE, m_cookies.c_str());
        }

        if (m_max_bytes_per_sec > 0) {
            curl_easy_setopt(m_curl, CURLOPT_MAX_RECV_SPEED_LARGE, static_cast<curl_off_t>(m_max_bytes_per_sec));
        }

        uint64_t total_size = m_chunk_manager.get_total_file_size();
        bool is_full_file_single_chunk = (m_chunk.start_byte == 0 && 
                                          m_chunk.bytes_downloaded == 0 && 
                                          total_size > 0 && 
                                          m_chunk.end_byte == total_size - 1);

        // Bug 3 Fix: If server does not support ranges, single chunk must restart from 0
        if (!m_supports_ranges) {
            m_chunk.bytes_downloaded = 0;
            m_chunk_manager.update_chunk_progress(m_chunk.id, 0);
            attempt_start_bytes = 0;
        } else if (!is_full_file_single_chunk) {
            uint64_t req_start = m_chunk.start_byte + m_chunk.bytes_downloaded;
            if (req_start > m_chunk.end_byte) {
                m_chunk_manager.mark_chunk_done(m_chunk.id);
                return true;
            }
            stringstream range_ss;
            range_ss << req_start << "-" << m_chunk.end_byte;
            string range_str = range_ss.str();
            curl_easy_setopt(m_curl, CURLOPT_RANGE, range_str.c_str());
        }

        WriteContext ctx;
        ctx.file_writer = &m_file_writer;
        ctx.chunk_manager = &m_chunk_manager;
        ctx.chunk_id = m_chunk.id;
        ctx.current_offset = (!m_supports_ranges) ? 0 : (m_chunk.start_byte + m_chunk.bytes_downloaded);
        ctx.end_byte = m_chunk.end_byte;
        ctx.bytes_written = (!m_supports_ranges) ? 0 : m_chunk.bytes_downloaded;
        ctx.curl = m_curl;
        ctx.pause_requested = m_pause_requested;

        curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, &ctx);
        curl_easy_setopt(m_curl, CURLOPT_FAILONERROR, 0L);

        CURLcode res = curl_easy_perform(m_curl);

        // Clean pause check right after perform
        if (ctx.was_paused || (m_pause_requested && m_pause_requested->load())) {
            m_chunk.bytes_downloaded = ctx.bytes_written;
            m_chunk_manager.update_chunk_progress(m_chunk.id, ctx.bytes_written);
            return false;
        }

        long http_code = 0;
        curl_easy_getinfo(m_curl, CURLINFO_RESPONSE_CODE, &http_code);

        // Check if rate limited (429) or service temporarily unavailable (503)
        if (http_code == 429 || http_code == 503) {
            m_chunk.bytes_downloaded = attempt_start_bytes;
            m_chunk_manager.update_chunk_progress(m_chunk.id, attempt_start_bytes);

            int jitter_ms = (m_chunk.id * 350 + attempt * 250) % 1500;
            cerr << "[ChunkManager] Rate-limited (HTTP " << http_code << ") on chunk " 
                 << m_chunk.id << ". Backing off for " << backoff_seconds 
                 << "s (Attempt " << attempt << "/" << MAX_RETRIES << ")..." << endl;
            this_thread::sleep_for(chrono::milliseconds(backoff_seconds * 1000 + jitter_ms));
            backoff_seconds = min(16, backoff_seconds * 2);
            continue;
        }

        m_chunk.bytes_downloaded = ctx.bytes_written;
        m_chunk_manager.update_chunk_progress(m_chunk.id, ctx.bytes_written);

        current_end = m_chunk_manager.get_chunk_end_byte(m_chunk.id);
        if (current_end == 0) current_end = m_chunk.end_byte;
        expected_total_bytes = (current_end >= m_chunk.start_byte) ? (current_end - m_chunk.start_byte + 1) : 0;

        // Verify that all bytes for this chunk have actually been written and response was successful
        bool is_single_stream = !m_supports_ranges || is_full_file_single_chunk;
        if ((http_code == 206 || (http_code == 200 && is_single_stream) || (http_code >= 200 && http_code < 300)) && ctx.bytes_written >= expected_total_bytes) {
            m_chunk_manager.mark_chunk_done(m_chunk.id);
            return true;
        }

        // Bug 2 Fix: Handle boundary termination on stolen chunk gracefully
        if (res == CURLE_WRITE_ERROR) {
            if (ctx.current_offset > current_end || ctx.bytes_written >= expected_total_bytes) {
                m_chunk_manager.mark_chunk_done(m_chunk.id);
                return true;
            }
        }

        cerr << "[ChunkWorker Error] Attempt " << attempt << " failed for chunk " << m_chunk.id 
             << ": " << curl_easy_strerror(res) << " (HTTP " << http_code 
             << ", " << ctx.bytes_written << "/" << expected_total_bytes << " bytes)" << endl;

        if (attempt < MAX_RETRIES) {
            this_thread::sleep_for(chrono::seconds(1));
        }
    }

    if (m_pause_requested && m_pause_requested->load()) {
        return false;
    }

    m_chunk_manager.mark_chunk_failed(m_chunk.id);
    return false;
}