cmake_minimum_required(VERSION 3.15)
project(idm_engine VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_TLS_VERIFY OFF)

if(WIN32)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
    set(PLATFORM_LIBS ole32 uuid shlwapi ws2_32 iphlpapi winmm powrprof shell32 crypt32 user32 comdlg32)
endif()

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
    if(WIN32)
        set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
    elseif(APPLE)
        set(CURL_USE_SECTRANSP ON CACHE BOOL "" FORCE)
    endif()
    FetchContent_Declare(
        curl
        URL https://github.com/curl/curl/archive/refs/tags/curl-8_5_0.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(curl)
    set(CURL_LIBRARIES libcurl)
endif()

include_directories(src src/core src/config src/queue src/native_host src/gui src/security src/net src/ipc src/media src/tools)

# Shared Core Engine Sources
set(CORE_SOURCES
    src/core/ChunkManager.cpp
    src/core/ChunkWorker.cpp
    src/core/DownloadTask.cpp
    src/core/FileWriter.cpp
    src/core/ProgressTracker.cpp
    src/core/ResumeStore.cpp
    src/core/ThreadPool.cpp
    src/security/AntivirusScanner.cpp
    src/config/SettingsManager.cpp
)

# ---------------------------------------------------------
# IDM Desktop Application
# ---------------------------------------------------------
set(PLATFORM_SOURCES "")
if(WIN32)
    set(PLATFORM_SOURCES src/gui/resources.rc)
endif()

add_executable(idm_app
    src/main.cpp
    src/gui/MainWindow.cpp
    src/queue/DownloadQueue.cpp
    src/ipc/LocalServer.cpp
    src/net/RemoteDashboard.cpp
    src/net/ChannelBonding.cpp
    src/net/NetworkMonitor.cpp
    src/net/PowerMonitor.cpp
    src/media/StreamPreviewer.cpp
    src/media/MediaExtractor.cpp
    src/security/Vault.cpp
    src/security/AntivirusScanner.cpp
    src/tools/ArchiveVerifier.cpp
    ${CORE_SOURCES}
    ${PLATFORM_SOURCES}
)
set_target_properties(idm_app PROPERTIES OUTPUT_NAME "IDMan")
target_link_libraries(idm_app PRIVATE ${CURL_LIBRARIES} nlohmann_json::nlohmann_json ${PLATFORM_LIBS})

add_custom_command(TARGET idm_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/ui"
        "$<TARGET_FILE_DIR:idm_app>/ui"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:idm_app>"
        "$<TARGET_FILE_DIR:idm_app>/idm_app.exe"
    COMMENT "Copying UI directory and idm_app.exe alias next to IDMan executable"
)

# ---------------------------------------------------------
# Unit Tests
# ---------------------------------------------------------
add_executable(test_threadpool test/test_threadpool.cpp src/core/ThreadPool.cpp)

add_executable(test_chunk_manager test/test_chunk_manager.cpp src/core/ChunkManager.cpp)

add_executable(test_file_writer test/test_file_writer.cpp src/core/FileWriter.cpp src/core/ThreadPool.cpp)

add_executable(test_chunk_worker test/test_chunk_worker.cpp src/core/ChunkWorker.cpp src/core/ChunkManager.cpp src/core/FileWriter.cpp)
target_link_libraries(test_chunk_worker PRIVATE ${CURL_LIBRARIES})

add_executable(test_resume_store test/test_resume_store.cpp src/core/ResumeStore.cpp src/core/ChunkManager.cpp)
target_link_libraries(test_resume_store PRIVATE nlohmann_json::nlohmann_json)

add_executable(test_progress_tracker test/test_progress_tracker.cpp src/core/ProgressTracker.cpp src/core/ChunkManager.cpp)

add_executable(test_download_task test/test_download_task.cpp ${CORE_SOURCES})
target_link_libraries(test_download_task PRIVATE ${CURL_LIBRARIES} nlohmann_json::nlohmann_json ${PLATFORM_LIBS})

add_executable(test_settings_manager test/test_settings_manager.cpp src/config/SettingsManager.cpp)
target_link_libraries(test_settings_manager PRIVATE nlohmann_json::nlohmann_json)

add_executable(test_download_queue test/test_download_queue.cpp src/queue/DownloadQueue.cpp ${CORE_SOURCES})
target_link_libraries(test_download_queue PRIVATE ${CURL_LIBRARIES} nlohmann_json::nlohmann_json ${PLATFORM_LIBS})

add_executable(test_native_host test/test_native_host.cpp src/native_host/NativeHost.cpp src/ipc/LocalServer.cpp src/queue/DownloadQueue.cpp ${CORE_SOURCES})
target_link_libraries(test_native_host PRIVATE ${CURL_LIBRARIES} nlohmann_json::nlohmann_json ${PLATFORM_LIBS})

add_executable(idm_native_host src/native_host/main_native_host.cpp src/native_host/NativeHost.cpp)
target_link_libraries(idm_native_host PRIVATE nlohmann_json::nlohmann_json ${PLATFORM_LIBS})

add_executable(test_main_window test/test_main_window.cpp src/gui/MainWindow.cpp src/ipc/LocalServer.cpp src/queue/DownloadQueue.cpp ${CORE_SOURCES})
target_link_libraries(test_main_window PRIVATE ${CURL_LIBRARIES} nlohmann_json::nlohmann_json ${PLATFORM_LIBS})

add_executable(test_antivirus_scanner test/test_antivirus_scanner.cpp src/security/AntivirusScanner.cpp)
target_link_libraries(test_antivirus_scanner PRIVATE nlohmann_json::nlohmann_json ${PLATFORM_LIBS})

add_executable(test_network_monitor test/test_network_monitor.cpp src/net/NetworkMonitor.cpp)
target_link_libraries(test_network_monitor PRIVATE ${CURL_LIBRARIES} ${PLATFORM_LIBS})

add_executable(test_power_monitor test/test_power_monitor.cpp src/net/PowerMonitor.cpp)
target_link_libraries(test_power_monitor PRIVATE ${PLATFORM_LIBS})

add_executable(test_channel_bonding test/test_channel_bonding.cpp src/net/ChannelBonding.cpp)
target_link_libraries(test_channel_bonding PRIVATE ${CURL_LIBRARIES} ${PLATFORM_LIBS})

add_executable(test_notification_manager test/test_notification_manager.cpp src/queue/NotificationManager.cpp)
target_link_libraries(test_notification_manager PRIVATE ${PLATFORM_LIBS})

add_executable(test_local_server test/test_local_server.cpp src/ipc/LocalServer.cpp src/queue/DownloadQueue.cpp ${CORE_SOURCES})
target_link_libraries(test_local_server PRIVATE ${CURL_LIBRARIES} nlohmann_json::nlohmann_json ${PLATFORM_LIBS})

add_executable(test_swarm_download test/test_swarm_download.cpp src/net/SwarmDownloader.cpp ${CORE_SOURCES})
target_link_libraries(test_swarm_download PRIVATE ${CURL_LIBRARIES} nlohmann_json::nlohmann_json ${PLATFORM_LIBS})

add_executable(test_media_extractor test/test_media_extractor.cpp src/media/MediaExtractor.cpp )
target_link_libraries(test_media_extractor PRIVATE nlohmann_json::nlohmann_json ${PLATFORM_LIBS})

add_executable(test_stream_preview test/test_stream_preview.cpp src/media/StreamPreviewer.cpp src/ipc/LocalServer.cpp src/queue/DownloadQueue.cpp ${CORE_SOURCES})
target_link_libraries(test_stream_preview PRIVATE ${CURL_LIBRARIES} nlohmann_json::nlohmann_json ${PLATFORM_LIBS})

add_executable(test_archive_verifier test/test_archive_verifier.cpp src/tools/ArchiveVerifier.cpp src/security/AntivirusScanner.cpp)
target_link_libraries(test_archive_verifier PRIVATE nlohmann_json::nlohmann_json ${PLATFORM_LIBS})

add_executable(test_vault test/test_vault.cpp src/security/Vault.cpp ${CORE_SOURCES})
target_link_libraries(test_vault PRIVATE ${CURL_LIBRARIES} nlohmann_json::nlohmann_json ${PLATFORM_LIBS})

add_executable(test_remote_dashboard test/test_remote_dashboard.cpp src/net/RemoteDashboard.cpp src/ipc/LocalServer.cpp src/queue/DownloadQueue.cpp ${CORE_SOURCES})
target_link_libraries(test_remote_dashboard PRIVATE ${CURL_LIBRARIES} nlohmann_json::nlohmann_json ${PLATFORM_LIBS})