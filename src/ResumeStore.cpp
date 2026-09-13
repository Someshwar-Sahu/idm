#include "ResumeStore.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdio>
#include <iostream>

using namespace std;
using json = nlohmann::json;

string ResumeStore::get_sidecar_filepath(const string &destination_filepath){
    return destination_filepath + ".progress.json";
}

bool ResumeStore::save(const string &sidecar_filepath,
    const string &url,
    uint64_t total_size, 
    const string &etag, 
    const ChunkManager &chunk_manager){
        try{
            json j;
            j["url"] = url;
            j["total_size"] = total_size;
            j["etag"] = etag;

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

            ofstream file(sidecar_filepath);
            if(!file.is_open()){
                return false;
            }

            file<<j.dump(4);
            file.close();
            return true;
        } catch (const std::exception &e){
            cerr<<"[ResumeStore Error] Failed to save sidecar JSON: "<<e.what()<<endl;
            return false;
        }
}

bool ResumeStore::load(const string &sidecar_filepath,
    string &out_url,
    uint64_t &out_total_size,
    string &out_etag,
    ChunkManager &out_chunk_manager){
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

            int chunk_count = j.value("chunk_count", 0);
            out_chunk_manager.init(out_total_size, chunk_count);

            for(const auto &c_json: j["chunks"]){
                int id = c_json.value("id", -1);
                uint64_t downloaded = c_json.value("bytes_downloaded", static_cast<uint64_t>(0));
                string state_str = c_json.value("state", "Pending");

                if(id>=0){
                    out_chunk_manager.update_chunk_progress(id, downloaded);
                    if(state_str  == "Done"){
                        out_chunk_manager.mark_chunk_done(id);
                    }
                }
            }
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