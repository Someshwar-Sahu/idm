#include "ChunkWorker.h"
#include <iostream>
#include<sstream>

using namespace std;

ChunkWorker::ChunkWorker(const string &url, Chunk chunk, ChunkManager &chunk_manager, FileWriter &file_writer)
    : m_url(url),
      m_chunk(chunk),
      m_chunk_manager(chunk_manager),
      m_file_writer(file_writer) {
    m_curl = curl_easy_init();
}

ChunkWorker::~ChunkWorker() {
    if(m_curl != nullptr){
        curl_easy_cleanup(m_curl);
        m_curl = nullptr;
    }
}

size_t ChunkWorker::write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t total_bytes = size*nmemb;
    if(userdata == nullptr || total_bytes == 0){
        return 0;
    }
    WriteContext *ctx = static_cast<WriteContext*>(userdata);

    bool ok = ctx->file_writer->write_at_offset(ctx->current_offset, ptr, total_bytes);
    if(!ok){
        return 0;
    }

    ctx->current_offset += total_bytes;

    ctx->chunk_manager->update_chunk_progress(ctx->chunk_id, ctx->current_offset);

    return total_bytes;
}

bool ChunkWorker::download() {
    if(m_curl == nullptr){
        m_chunk_manager.mark_chunk_failed(m_chunk.id);
        return false;
    }

    curl_easy_setopt(m_curl, CURLOPT_URL, m_url.c_str());

    curl_easy_setopt(m_curl, CURLOPT_FOLLOWLOCATION, 1L);

    curl_easy_setopt(m_curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(m_curl, CURLOPT_SSL_VERIFYHOST, 0L);

    curl_easy_setopt(m_curl, CURLOPT_USERAGENT, "IDM_Engine/1.0");


    stringstream range_ss;
    range_ss << (m_chunk.start_byte + m_chunk.bytes_downloaded)<< "-"<< m_chunk.end_byte;
    string range_str = range_ss.str();
    curl_easy_setopt(m_curl, CURLOPT_RANGE, range_str.c_str());

    WriteContext ctx;
    ctx.file_writer = &m_file_writer;
    ctx.chunk_manager = &m_chunk_manager;
    ctx.chunk_id = m_chunk.id;
    ctx.current_offset = m_chunk.start_byte + m_chunk.bytes_downloaded;
    ctx.end_byte = m_chunk.end_byte;

    curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, &ctx);

    curl_easy_setopt(m_curl, CURLOPT_FAILONERROR, 1L);

    CURLcode res = curl_easy_perform(m_curl);

    if (res == CURLE_OK) {
        m_chunk_manager.mark_chunk_done(m_chunk.id);
        return true;
    } else {
        cerr << "[ChunkWorker Error] libcurl failed: " << curl_easy_strerror(res) << endl;
        m_chunk_manager.mark_chunk_failed(m_chunk.id);
        return false;
    }

}