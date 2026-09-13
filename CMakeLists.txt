cmake_minimum_required(VERSION 3.15)
project(idm_engine VERSION 1.0.0 LANGUAGES CXX)

# Disable SSL verification for FetchContent downloads on local MinGW
set(CMAKE_TLS_VERIFY OFF)

# Link MinGW runtime (POSIX threads & C++ standard library) statically
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# 1. nlohmann_json
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(json)

# 2. libcurl (Find system installed CURL, or FetchContent fallback)
find_package(CURL QUIET)

if(NOT CURL_FOUND)
    message(STATUS "Local libcurl not found. Fetching libcurl source automatically...")
    set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE) # Enable Windows native HTTPS/SSL
    FetchContent_Declare(
        curl
        URL https://github.com/curl/curl/archive/refs/tags/curl-8_5_0.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(curl)
    set(CURL_LIBRARIES libcurl)
endif()

include_directories(src)

# ThreadPool Unit Test
add_executable(test_threadpool test/test_threadpool.cpp src/ThreadPool.cpp)
target_link_libraries(test_threadpool PRIVATE ${CMAKE_THREAD_LIBS_INIT})

add_executable(test_chunk_manager test/test_chunk_manager.cpp src/ChunkManager.cpp)
target_link_libraries(test_chunk_manager PRIVATE ${CMAKE_THREAD_LIBS_INIT})

add_executable(test_file_writer test/test_file_writer.cpp src/FileWriter.cpp src/ThreadPool.cpp)
target_link_libraries(test_file_writer PRIVATE ${CMAKE_THREAD_LIBS_INIT})

add_executable(test_chunk_worker test/test_chunk_worker.cpp src/ChunkWorker.cpp src/ChunkManager.cpp src/FileWriter.cpp)
target_link_libraries(test_chunk_worker PRIVATE ${CURL_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})

add_executable(test_resume_store test/test_resume_store.cpp src/ResumeStore.cpp src/ChunkManager.cpp)
target_link_libraries(test_resume_store PRIVATE nlohmann_json::nlohmann_json ${CMAKE_THREAD_LIBS_INIT})
