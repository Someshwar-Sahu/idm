#include "ResumeStore.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdio>
#include <iostream>
#include <filesystem>
#include <sstream>

using namespace std;
using json = nlohmann::json;

string ResumeStore::get_sidecar_filepath(const string &destination_filepath, const string &cache_directory){
    if(cache_directory.empty()){
        return destination_filepath + ".progress.json";
    }
    namespace fs = std::filesystem;
    try{
        fs::path cache_path(cache_directory);
        std::error_code ec;
        fs::create_directories(cache_path, ec);

        fs::path dest(destination_filepath);
        string filename = dest.filename().string();
        if(filename.empty()){
            filename = "download";
        }

        // Bug 16 Fix: 64-bit FNV-1a path hash to eliminate 32-bit collisions across folders/drives
        uint64_t path_hash = 14695981039346656037ULL;
        for (char c : destination_filepath) {
            path_hash ^= static_cast<uint8_t>(c);
            path_hash *= 1099511628211ULL;
        }
        stringstream ss;
        ss << hex << std::setfill('0') << std::setw(16) << path_hash;

        string sidecar_name = filename +"_"+ss.str()+".progress.json";
        return (cache_path / sidecar_name).string();
    } catch(...){
        return destination_filepath + ".progress.json";
    }
}

bool ResumeStore::save(const string &sidecar_filepath,
    const string &url,
    uint64_t total_size, 
    const string &etag, 
    const ChunkManager &chunk_manager,
    bool supports_ranges){
        try{
            json j;
            j["url"] = url;
            j["total_size"] = total_size;
            j["etag"] = etag;
            j["supports_ranges"] = supports_ranges;

            auto chunks = chunk_manager.get_all_chunks();
            j["chunk_count"] = chunks.size();

            json chunks_json = json::array();
            for(const auto &chunk : chunks){
                json c;
                c["id"] = chunk.id;
                c["start_byte"] = chunk.start_byte;
                c["end_byte"] = chunk.end_byte;
                c["bytes_downloaded"] = chunk.bytes_downloaded;
                c["state"] = Chunk::state_to_string(chunk.state);
                c["retry_count"] = chunk.retry_count;
                chunks_json.push_back(c);
            }
            j["chunks"] = chunks_json;

            string temp_filepath = sidecar_filepath + ".tmp";
            ofstream file(temp_filepath);
            if(!file.is_open()){
                return false;
            }

            file<<j.dump(4);
            file.close();

            std::error_code ec;
            std::filesystem::rename(temp_filepath, sidecar_filepath, ec);
            return !ec;
        } catch (const std::exception &e){
            cerr<<"[ResumeStore Error] Failed to save sidecar JSON: "<<e.what()<<endl;
            return false;
        }
}

bool ResumeStore::load(const string &sidecar_filepath,
    string &out_url,
    uint64_t &out_total_size,
    string &out_etag,
    ChunkManager &out_chunk_manager,
    bool &out_supports_ranges){
        if(!exists(sidecar_filepath)){
            return false;
        }

        try{
            ifstream file(sidecar_filepath);
            if(!file.is_open()){
                return false;
            }
            
            json j;
            file>>j;
            file.close();

            out_url = j.value("url", "");
            out_total_size = j.value("total_size", static_cast<uint64_t>(0));
            out_etag = j.value("etag", "");

            if(!j.contains("chunks") || !j["chunks"].is_array()){
                return false;
            }

            std::vector<Chunk> loaded_chunks;
            for(const auto &c_json: j["chunks"]){
                Chunk chunk;
                chunk.id = c_json.value("id", -1);
                chunk.start_byte = c_json.value("start_byte", static_cast<uint64_t>(0));
                chunk.end_byte = c_json.value("end_byte", static_cast<uint64_t>(0));
                chunk.bytes_downloaded = c_json.value("bytes_downloaded", static_cast<uint64_t>(0));
                string state_str = c_json.value("state", "Pending");
                if (state_str == "Done") {
                    chunk.state = ChunkState::Done;
                } else {
                    chunk.state = ChunkState::Pending; // InProgress and Failed are reset to Pending for retry/resumption
                }
                chunk.retry_count = c_json.value("retry_count", 0);
                if(chunk.id >= 0){
                    loaded_chunks.push_back(chunk);
                }
            }

            out_chunk_manager.restore_chunks(out_total_size, loaded_chunks);
            out_supports_ranges = j.value("supports_ranges", loaded_chunks.size() > 1);
            return true;
        } catch(const std::exception &e){
            cerr<<"[ResumeStore Error] Failed to parse sidecar JSON: "<<e.what()<<endl;
            return false;
        }
}

bool ResumeStore::exists(const string &sidecar_filepath){
    ifstream file(sidecar_filepath);
    return file.good();
}

bool ResumeStore::remove(const string &sidecar_filepath){
    if(exists(sidecar_filepath)){
        return std::remove(sidecar_filepath.c_str()) == 0;
    }
    return true;
}