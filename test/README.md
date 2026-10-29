# Automated Test Suites (`test/`)

This directory contains the 22 comprehensive unit and integration test suites validating all IDM subsystems.

## Active Test Suites (22 / 22)

| Executable Target | Source File | Subsystem Under Test |
| :--- | :--- | :--- |
| `test_threadpool` | [`test_threadpool.cpp`](test_threadpool.cpp) | ThreadPool concurrency, task dispatching, graceful shutdown |
| `test_chunk_manager` | [`test_chunk_manager.cpp`](test_chunk_manager.cpp) | Dynamic work-stealing algorithm, chunk bisection, progress tracking |
| `test_file_writer` | [`test_file_writer.cpp`](test_file_writer.cpp) | Sparse file pre-allocation, offset writes, disk boundary guards |
| `test_chunk_worker` | [`test_chunk_worker.cpp`](test_chunk_worker.cpp) | HTTP byte-range downloads, adaptive speed limiter, retry backoff |
| `test_resume_store` | [`test_resume_store.cpp`](test_resume_store.cpp) | Sidecar serialization, crash recovery, FNV-1a hash routing |
| `test_progress_tracker` | [`test_progress_tracker.cpp`](test_progress_tracker.cpp) | EWMA speed smoothing, bytes counter, ETA estimation |
| `test_download_task` | [`test_download_task.cpp`](test_download_task.cpp) | Full task lifecycle, HTTP HEAD/GET probes, security override |
| `test_settings_manager` | [`test_settings_manager.cpp`](test_settings_manager.cpp) | Settings persistence, category file type routing |
| `test_download_queue` | [`test_download_queue.cpp`](test_download_queue.cpp) | FIFO priority scheduling, concurrency caps, queue control |
| `test_native_host` | [`test_native_host.cpp`](test_native_host.cpp) | Chrome 32-bit Native Messaging protocol & JSON-RPC bridge |
| `test_local_server` | [`test_local_server.cpp`](test_local_server.cpp) | Async HTTP server, REST endpoints, JSON parsing guards |
| `test_antivirus_scanner` | [`test_antivirus_scanner.cpp`](test_antivirus_scanner.cpp) | SmartScreen, Defender CLI, AMSI scanning, quarantine isolation |
| `test_network_monitor` | [`test_network_monitor.cpp`](test_network_monitor.cpp) | Latency measurement, ping probing, Smart Auto-QoS |
| `test_power_monitor` | [`test_power_monitor.cpp`](test_power_monitor.cpp) | AC/battery detection, low-battery queue auto-pause |
| `test_channel_bonding` | [`test_channel_bonding.cpp`](test_channel_bonding.cpp) | Multi-adapter interface discovery & round-robin binding |
| `test_notification_manager`| [`test_notification_manager.cpp`](test_notification_manager.cpp) | Action Center XML toasts, interactive action routing |
| `test_swarm_download` | [`test_swarm_download.cpp`](test_swarm_download.cpp) | LAN P2P chunk discovery and data sharing |
| `test_media_extractor` | [`test_media_extractor.cpp`](test_media_extractor.cpp) | Video URL parsing, direct media stream URL extraction |
| `test_stream_preview` | [`test_stream_preview.cpp`](test_stream_preview.cpp) | Local HTTP streaming proxy for in-progress media playback |
| `test_archive_verifier` | [`test_archive_verifier.cpp`](test_archive_verifier.cpp) | Archive file header inspection and checksum integrity |
| `test_vault` | [`test_vault.cpp`](test_vault.cpp) | AES-256 encrypted credential store & domain matching |
| `test_remote_dashboard` | [`test_remote_dashboard.cpp`](test_remote_dashboard.cpp) | Remote web management server and REST API binding |

## Running All Tests

To execute the complete 22-suite test audit from PowerShell / bash:

```powershell
python "scratch/run_all_tests.py"
```
