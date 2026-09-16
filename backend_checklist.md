# Backend Gap Analysis & Implementation Checklist

This document tracks all **mistakes/gaps identified in the current code** and serves as the **master step-by-step checklist of all 28 backend tasks** to complete before wrapping the frontend around the engine.

---

## Part 1: The 10 Mistakes & Design Gaps Found in Existing Code

1. **`MainWindow` Instant Process Death**:
   - `MainWindow::run_event_loop()` launches Edge asynchronously via `start msedge` and returns in 10ms. `main.cpp` immediately calls `queue.stop()` and the C++ engine terminates before any downloads can run.
2. **Static Chunk Allocation (No Dynamic Chunk Stealing)**:
   - `ChunkManager` divides chunks once at the start. If 7 threads finish and 1 thread crawls on a slow route, 7 threads sit completely idle waiting for the last thread (the classic "stuck at 90%" issue).
3. **No Pre-Flight Disk Space Verification**:
   - `DownloadTask` does not check free disk space on the target drive using `GetDiskFreeSpaceExA`. Large downloads will crash midway if the drive fills up.
4. **No Duplicate File Collision Handling**:
   - `DownloadTask` silently overwrites any file that already exists at the destination path instead of auto-renaming (`file (1).zip`) or prompting.
5. **Missing Token-Bucket Rate Limiting in `ChunkWorker`**:
   - `SettingsManager` defines `SpeedLimiterMode`, but `ChunkWorker.cpp` does not throttle libcurl transfers at all.
6. **No HTTP 429 (Rate-Limit) Backoff**:
   - If a CDN (Cloudflare/Akamai) throttles with HTTP 429 or 503, chunks fail immediately instead of backing off concurrency and retrying.
7. **Cache Sidecars Not Centralized**:
   - `.progress.json` files are placed in `./downloads/` instead of an isolated `Cache/` folder (`E:\IDM\Cache\`), leaving orphan clutter on cancelled tasks.
8. **Immutable URL in `DownloadTask` (Cannot Refresh Expired 24-Hour Links)**:
   - `m_url` cannot be updated. When hoster links (Google Drive, Rapidgator) expire with HTTP 403/410, there is no way to swap the URL and resume remaining chunks.
9. **Missing Antivirus Scanner Execution**:
   - No `AntivirusScanner` class exists; `MpCmdRun.exe` CLI and SmartScreen `IAttachmentExecute` are not called.
10. **Post-Queue OS Actions Not Wired**:
    - `PostQueueAction` enum exists, but does not call Windows APIs (`ExitWindowsEx`, `SetSystemPowerState`) or trigger Windows Action Center toasts.

---

## Part 2: Complete Master Backend Implementation Checklist (All 28 Tasks)

### 🔹 Phase 2A: Engine Core Hardening (`src/core/`)
- [ ] **Task 1: Dynamic Chunk Stealing in `ChunkManager`**
  - Implement `steal_slowest_chunk()` so idle threads cut active chunks with >10 MB remaining in half and finish in parallel.
  - *Verification Target*: `test_chunk_manager`
- [ ] **Task 2: Token-Bucket Speed Throttler in `ChunkWorker`**
  - Implement token-bucket byte rate limiter to enforce user-configured manual speed caps and Auto-QoS limits.
  - *Verification Target*: `test_chunk_worker`
- [ ] **Task 3: HTTP 429 / 503 Rate-Limit Exponential Backoff in `ChunkWorker`**
  - Detect CDN rate-limiting responses, apply exponential retry delay (2s, 4s), and dynamically reduce thread concurrency.
  - *Verification Target*: `test_chunk_worker`
- [ ] **Task 4: Pre-Flight Disk Space Verification in `DownloadTask`**
  - Add `GetDiskFreeSpaceExA` check before downloading starts; abort cleanly if free space < file size.
  - *Verification Target*: `test_download_task`
- [ ] **Task 5: Duplicate File Collision & Auto-Renaming in `FileWriter`**
  - Add `generate_unique_filepath()` to avoid overwriting existing files (e.g. `file (1).zip`).
  - *Verification Target*: `test_file_writer`
- [ ] **Task 6: Expired 24-Hour Link Refresh in `DownloadTask`**
  - Add `update_url(new_url)` for resuming expired hoster links without losing already downloaded chunks.
  - *Verification Target*: `test_download_task`
- [ ] **Task 7: Centralized Cache Directory Management in `DownloadTask` & `SettingsManager`**
  - Route all `.progress.json` sidecars to centralized `Cache/` directory (e.g. `E:\IDM\Cache\`) with auto-purge upon 100% completion.
  - *Verification Target*: `test_download_task`
- [ ] **Task 8: Automatic File Category Routing**
  - Route completed downloads into category subfolders (`Documents`, `Videos`, `Music`, `Compressed`, `Programs`) based on file extension.
  - *Verification Target*: `test_settings_manager`

---

### 🔹 Phase 2B: Antivirus & Link Security Engine (`src/security/`)
- [ ] **Task 9: Pre-Flight Link/URL Reputation Check (`IAttachmentExecute`)**
  - Use Windows COM interface (`CLSID_AttachmentServices`) to check links against Microsoft Defender SmartScreen before downloading.
  - *Verification Target*: `test_antivirus_scanner`
- [ ] **Task 10: Staging Cache Scan (`MpCmdRun.exe`) & Threat Parser**
  - Scan completed files in `Cache/` with `-DisableRemediation` (prevents silent deletion).
  - Capture malware names from stdout and evaluate exit codes (`0` clean, `2` threat).
  - *Verification Target*: `test_antivirus_scanner`
- [ ] **Task 11: False-Positive Override & Persistent Whitelist Memory**
  - Provide security prompt logic (`Quarantine` vs `Override & Keep`) and persist user-approved domains/hashes in whitelist.
  - *Verification Target*: `test_antivirus_scanner`
- [ ] **Task 12: In-Memory Chunk Buffer Scanning via Windows AMSI (`amsi.dll`)**
  - Deep buffer inspection using `AmsiScanBuffer` as chunks arrive in memory before flushing to disk.
  - *Verification Target*: `test_antivirus_scanner`

---

### 🔹 Phase 2C: Smart QoS, Hardware & Network Intelligence (`src/net/`)
- [ ] **Task 13: Smart Auto-QoS RTT Latency Monitor (`NetworkMonitor`)**
  - Background thread measuring network RTT (round-trip time).
  - Auto-throttle downloads by 20–30% during 4K streaming or Zoom calls to prevent lag spikes.
  - *Verification Target*: `test_network_monitor`
- [ ] **Task 14: Zero-Lag Gaming QoS Mode**
  - Detect active gaming processes (Steam, Epic, Riot) or Discord voice calls and automatically throttle downloads to protect gaming ping.
  - *Verification Target*: `test_network_monitor`
- [ ] **Task 15: Metered Wi-Fi & Mobile Hotspot Guard**
  - Detect metered Wi-Fi or phone hotspot connections via Windows Network List Manager API and auto-pause heavy downloads.
  - *Verification Target*: `test_network_monitor`
- [ ] **Task 16: Laptop Battery & Thermal Throttler Guard**
  - Use `GetSystemPowerStatus` to detect battery power or high CPU thermal load, automatically reducing worker threads from 32 to 4.
  - *Verification Target*: `test_network_monitor`
- [ ] **Task 17: Multi-Adapter Channel Bonding (`ChannelBonding`)**
  - Bind chunk sockets to alternate network adapters (Wi-Fi + Ethernet) via `CURLOPT_INTERFACE` to multiply bandwidth.
  - *Verification Target*: `test_channel_bonding`

---

### 🔹 Phase 2D: Queue Scheduler & Windows OS Integration (`src/queue/`)
- [ ] **Task 18: Post-Queue Native Windows Actions**
  - Implement `ExitWindowsEx` (Shutdown), `SetSystemPowerState` (Sleep/Hibernate), and `PlaySoundA`.
  - *Verification Target*: `test_download_queue`
- [ ] **Task 19: Windows 10/11 Native Action Center Toast Notifications**
  - Trigger native Windows notification toasts with interactive `[Open File]` and `[Open Folder]` buttons upon download completion.
  - *Verification Target*: `test_download_queue`

---

### 🔹 Phase 2E: Local C++ IPC Server & Keep-Alive Event Loop (`src/ipc/` & `src/gui/`)
- [ ] **Task 20: Embedded Zero-Dependency Local REST Server (`LocalServer`)**
  - Lightweight Winsock HTTP server listening on `127.0.0.1:port`.
  - Endpoints: `POST /api/add_download`, `POST /api/pause`, `POST /api/resume`, `POST /api/delete`, `GET /api/tasks`, `GET /api/chunks?id=...`, `POST /api/av_override`.
  - *Verification Target*: `test_local_server`
- [ ] **Task 21: `MainWindow` Keep-Alive Event Loop**
  - Block while the desktop window is open and shut down cleanly when closed.
  - *Verification Target*: `test_main_window`

---

### 🔹 Phase 2F: Media, Archives & Swarm Acceleration (`src/media/` & `src/tools/`)
- [ ] **Task 22: Multi-Mirror Swarm Downloading (Metalink Multiplexing)**
  - Split chunks across multiple mirror URLs simultaneously to bypass single-server bandwidth caps and multiply speed 3x.
  - *Verification Target*: `test_swarm_download`
- [ ] **Task 23: Universal Video & Audio Extraction Pipeline (`yt-dlp` & FFmpeg)**
  - Native backend pipeline supporting 1,000+ streaming sites with resolution selector (4K 60fps, 1080p, audio-only 320kbps MP3).
  - *Verification Target*: `test_media_extractor`
- [ ] **Task 24: Instant In-App Stream Preview**
  - Stream and play partially downloaded video/audio files directly from disk buffer while downloading.
  - *Verification Target*: `test_stream_preview`
- [ ] **Task 25: Automated Archive Unpacking & Integrity Verification**
  - Auto-extract `.zip`, `.rar`, `.7z` archives upon completion. Automatically verify clipboard MD5/SHA256 checksums.
  - *Verification Target*: `test_archive_verifier`

---

### 🔹 Phase 2G: Authentication, Browser Extension & Remote Access
- [ ] **Task 26: Browser Extension (Manifest V3) & Native Messaging Setup**
  - Manifest V3 extension (`manifest.json`, `background.js`) communicating with `idm_native_host.exe` via length-prefixed JSON, registered in Windows Registry.
  - *Verification Target*: `test_native_host`
- [ ] **Task 27: Site Logins Vault & Cookie Persistence (`src/security/Vault.h`)**
  - Encrypted storage for session cookies (`session_id`, `auth_token`, `cf_clearance`) and credentials for protected hosters (Google Drive, Mega, 1Fichier, private forums), auto-injected into libcurl headers.
  - *Verification Target*: `test_vault`
- [ ] **Task 28: Local Wi-Fi Remote Dashboard & Floating Desktop Mini-Dock**
  - Enable local network access so users can monitor/add downloads from their phone or tablet on the same Wi-Fi, plus a floating desktop mini-dock widget for drag-and-drop link catching.
  - *Verification Target*: `test_remote_dashboard`
