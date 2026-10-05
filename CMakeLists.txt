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

include_directories(src src/core src/config src/queue src/native_host src/gui src/security)

# ThreadPool Unit Test
add_executable(test_threadpool test/test_threadpool.cpp src/core/ThreadPool.cpp)
target_link_libraries(test_threadpool PRIVATE ${CMAKE_THREAD_LIBS_INIT})

add_executable(test_chunk_manager test/test_chunk_manager.cpp src/core/ChunkManager.cpp)
target_link_libraries(test_chunk_manager PRIVATE ${CMAKE_THREAD_LIBS_INIT})

add_executable(test_file_writer test/test_file_writer.cpp src/core/FileWriter.cpp src/core/ThreadPool.cpp)
target_link_libraries(test_file_writer PRIVATE ${CMAKE_THREAD_LIBS_INIT})

add_executable(test_chunk_worker test/test_chunk_worker.cpp src/core/ChunkWorker.cpp src/core/ChunkManager.cpp src/core/FileWriter.cpp)
target_link_libraries(test_chunk_worker PRIVATE ${CURL_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})

add_executable(test_resume_store test/test_resume_store.cpp src/core/ResumeStore.cpp src/core/ChunkManager.cpp)
target_link_libraries(test_resume_store PRIVATE nlohmann_json::nlohmann_json ${CMAKE_THREAD_LIBS_INIT})

add_executable(test_progress_tracker test/test_progress_tracker.cpp src/core/ProgressTracker.cpp src/core/ChunkManager.cpp)
target_link_libraries(test_progress_tracker PRIVATE ${CMAKE_THREAD_LIBS_INIT})

add_executable(test_download_task 
    test/test_download_task.cpp 
    src/core/DownloadTask.cpp 
    src/core/ChunkWorker.cpp 
    src/core/ChunkManager.cpp 
    src/core/FileWriter.cpp 
    src/core/ThreadPool.cpp 
    src/core/ProgressTracker.cpp 
    src/core/ResumeStore.cpp
    src/security/AntivirusScanner.cpp
)
target_link_libraries(test_download_task PRIVATE ${CURL_LIBRARIES} nlohmann_json::nlohmann_json ${CMAKE_THREAD_LIBS_INIT})
if (WIN32)
    target_link_libraries(test_download_task PRIVATE ole32 uuid shlwapi)
endif()

target_link_libraries(test_download_task PRIVATE ${CURL_LIBRARIES} nlohmann_json::nlohmann_json ${CMAKE_THREAD_LIBS_INIT})

# SettingsManager Unit Test
add_executable(test_settings_manager test/test_settings_manager.cpp src/config/SettingsManager.cpp)
target_link_libraries(test_settings_manager PRIVATE nlohmann_json::nlohmann_json ${CMAKE_THREAD_LIBS_INIT})

# DownloadQueue Unit Test
add_executable(test_download_queue 
    test/test_download_queue.cpp 
    src/queue/DownloadQueue.cpp 
    src/core/DownloadTask.cpp 
    src/core/ChunkWorker.cpp 
    src/core/ChunkManager.cpp 
    src/core/FileWriter.cpp 
    src/core/ThreadPool.cpp 
    src/core/ProgressTracker.cpp 
    src/core/ResumeStore.cpp
    src/security/AntivirusScanner.cpp
)
target_link_libraries(test_download_queue PRIVATE ${CURL_LIBRARIES} nlohmann_json::nlohmann_json ${CMAKE_THREAD_LIBS_INIT})
if (WIN32)
    target_link_libraries(test_download_queue PRIVATE ole32 uuid shlwapi)
endif()

# NativeHost Unit Test
add_executable(test_native_host test/test_native_host.cpp src/native_host/NativeHost.cpp)
target_link_libraries(test_native_host PRIVATE nlohmann_json::nlohmann_json ${CMAKE_THREAD_LIBS_INIT})

# MainWindow Unit Test
add_executable(test_main_window test/test_main_window.cpp src/gui/MainWindow.cpp)
target_link_libraries(test_main_window PRIVATE nlohmann_json::nlohmann_json ${CMAKE_THREAD_LIBS_INIT})

# IDM Main Desktop Application Executable
add_executable(idm_app 
    src/main.cpp 
    src/gui/MainWindow.cpp 
    src/queue/DownloadQueue.cpp 
    src/config/SettingsManager.cpp
    src/core/DownloadTask.cpp 
    src/core/ChunkWorker.cpp 
    src/core/ChunkManager.cpp 
    src/core/FileWriter.cpp 
    src/core/ThreadPool.cpp 
    src/core/ProgressTracker.cpp 
    src/core/ResumeStore.cpp
    src/security/AntivirusScanner.cpp
)
target_link_libraries(idm_app PRIVATE ${CURL_LIBRARIES} nlohmann_json::nlohmann_json ${CMAKE_THREAD_LIBS_INIT})
if (WIN32)
    target_link_libraries(idm_app PRIVATE ole32 uuid shlwapi)
endif()

# AntivirusScanner Unit Test (Task 9)
add_executable(test_antivirus_scanner test/test_antivirus_scanner.cpp src/security/AntivirusScanner.cpp)
target_link_libraries(test_antivirus_scanner PRIVATE nlohmann_json::nlohmann_json ${CMAKE_THREAD_LIBS_INIT})
if (WIN32)
    target_link_libraries(test_antivirus_scanner PRIVATE ole32 uuid shlwapi)
endif()

