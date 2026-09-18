#include "ChunkWorker.h"
#include <iostream>
#include <thread>
#include<chrono>
#include<sstream>

using namespace std;

ChunkWorker::ChunkWorker(const string &url, Chunk chunk, ChunkManager &chunk_manager, FileWriter &file_writer, uint64_t max_bytes_per_sec)
    : m_url(url),
      m_chunk(chunk),
      m_chunk_manager(chunk_manager),
      m_file_writer(file_writer),
      m_max_bytes_per_sec(max_bytes_per_sec) {
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
    size_t total_bytes = size*nmemb;
    if(userdata == nullptr || total_bytes == 0){
        return 0;
    }
    WriteContext *ctx = static_cast<WriteContext*>(userdata);

    uint64_t dynamic_end_byte = ctx->chunk_manager->get_chunk_end_byte(ctx->chunk_id);
    if(dynamic_end_byte >0 && ctx->current_offset> dynamic_end_byte){
        return 0;
    }

    if(dynamic_end_byte >0 && (ctx->current_offset + total_bytes -1) > dynamic_end_byte){
        total_bytes = static_cast<size_t>(dynamic_end_byte-ctx->current_offset +1);
    }

    bool ok = ctx->file_writer->write_at_offset(ctx->current_offset, ptr, total_bytes);
    if(!ok){
        return 0;
    }

    ctx->current_offset += total_bytes;
    ctx->bytes_written += total_bytes;

    ctx->chunk_manager->update_chunk_progress(ctx->chunk_id, ctx->bytes_written);

    return total_bytes;
}

bool ChunkWorker::download() {
    if(m_curl == nullptr){
        m_chunk_manager.mark_chunk_failed(m_chunk.id);
        return false;
    }

    constexpr int MAX_RETRIES = 3;
    int attempt =0;
    int backoff_seconds = 2;

    while(attempt < MAX_RETRIES){
        attempt++;
        curl_easy_setopt(m_curl, CURLOPT_URL, m_url.c_str());
        curl_easy_setopt(m_curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(m_curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(m_curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(m_curl, CURLOPT_USERAGENT, "IDM_Engine/1.0");

        if(m_max_bytes_per_sec >0){
            curl_easy_setopt(m_curl, CURLOPT_MAX_RECV_SPEED_LARGE,static_cast<curl_off_t>(m_max_bytes_per_sec));
        }

        uint64_t total_size = m_chunk_manager.get_total_file_size();
        bool is_full_file_single_chunk = (m_chunk.start_byte == 0 && 
                                        m_chunk.bytes_downloaded == 0 && 
                                        total_size > 0 && 
                                        m_chunk.end_byte == total_size - 1);

        if (!is_full_file_single_chunk) {
            stringstream range_ss;
            range_ss << (m_chunk.start_byte + m_chunk.bytes_downloaded) << "-" << m_chunk.end_byte;
            string range_str = range_ss.str();
            curl_easy_setopt(m_curl, CURLOPT_RANGE, range_str.c_str());
        }

        WriteContext ctx;
        ctx.file_writer = &m_file_writer;
        ctx.chunk_manager = &m_chunk_manager;
        ctx.chunk_id = m_chunk.id;
        ctx.current_offset = m_chunk.start_byte + m_chunk.bytes_downloaded;
        ctx.end_byte = m_chunk.end_byte;
        ctx.bytes_written = m_chunk.bytes_downloaded;

        curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, &ctx);

        curl_easy_setopt(m_curl, CURLOPT_FAILONERROR, 0L);

        CURLcode res = curl_easy_perform(m_curl);

        long http_code = 0;
        curl_easy_getinfo(m_curl, CURLINFO_RESPONSE_CODE, &http_code);

        if (http_code == 429 || http_code == 503){
            cerr<<"[ChunkManager] Rate-limited (HTTP "<<http_code<<") on chunk "<<m_chunk.id<<". Backing off for "<<backoff_seconds<<"s (Attempt "<<attempt<<"/"<<MAX_RETRIES<<")..."<<endl;
            this_thread::sleep_for(chrono::seconds(backoff_seconds));
            backoff_seconds *= 2;
            continue;
        }

        if(res == CURLE_OK ||res == CURLE_WRITE_ERROR){
            uint64_t current_end = m_chunk_manager.get_chunk_end_byte(m_chunk.id);
            if(current_end == 0) current_end = m_chunk.end_byte;

            if(ctx.current_offset > current_end){
                m_chunk_manager.mark_chunk_done(m_chunk.id);
                return true;
            }else if(res == CURLE_OK &&(http_code >=200 && http_code <300)){
                m_chunk_manager.mark_chunk_done(m_chunk.id);
                return true;
            }
        }

        cerr<<"[ChunkWorker Error] Attempt "<<attempt<< " failed: "<<curl_easy_strerror(res)<<" (HTTP "<<http_code<<")"<<endl;

        if(attempt<MAX_RETRIES){
            this_thread::sleep_for(chrono::seconds(1));
        }
    }

    m_chunk_manager.mark_chunk_failed(m_chunk.id);
    return false;
}