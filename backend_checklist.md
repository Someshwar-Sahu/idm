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
- [x] **Task 1: Dynamic Chunk Stealing in `ChunkManager`** ✅
  - Implemented `steal_slowest_chunk()` so idle threads cut active chunks with >10 MB remaining in half and finish in parallel.
  - *Verification Target*: `test_chunk_manager` (PASSED ✅)
- [x] **Task 2: Token-Bucket Speed Throttler in `ChunkWorker` & `DownloadTask`** ✅
  - Implemented token-bucket byte rate limiter via `CURLOPT_MAX_RECV_SPEED_LARGE` and dynamically partitioned total speed limit evenly across worker threads (`worker_speed = total_limit / worker_count`).
  - *Verification Target*: `test_chunk_worker` (PASSED ✅), `test_download_task` (PASSED ✅)
- [x] **Task 3: HTTP 429 / 503 Rate-Limit Exponential Backoff in `ChunkWorker`** ✅
  - Detect CDN rate-limiting responses (HTTP 429 / 503), apply exponential retry backoff delay (2s, 4s, 8s), and track chunk retry counts.
  - *Verification Target*: `test_chunk_worker` (PASSED ✅)
- [x] **Task 4: Pre-Flight Disk Space Verification in `DownloadTask`** ✅
  - Implemented `GetDiskFreeSpaceExA` native volume query with parent traversal and safe root guards; pre-flight aborts cleanly before allocating or downloading if free space < file size.
  - *Verification Target*: `test_download_task` (PASSED ✅)
- [x] **Task 5: Duplicate File Collision & Auto-Renaming in `FileWriter`** ✅
  - Implemented zero-latency `file_exists()` and `generate_unique_filepath()` sequence numbering (`(1)`, `(2)`) with extension-less file support, enabling instantaneous GUI duplicate prompt checks and auto-renaming.
  - *Verification Target*: `test_file_writer` (PASSED ✅)
- [x] **Task 6: Expired 24-Hour Link Refresh in `DownloadTask`** ✅
  - Implemented `update_url()` with pre-flight server validation and file size compatibility guards, plus session context forwarding (`Referer`, `Cookies`, `User-Agent`) to bypass hoster link expiry and anti-hotlinking.
  - *Verification Target*: `test_download_task` (PASSED ✅)
- [x] **Task 7: Centralized Cache Directory Management in `DownloadTask` & `SettingsManager`** ✅
  - Routed `.progress.json` sidecars to centralized `Cache/` directory with path-hashed uniqueness, keeping user download folders clutter-free with automatic purge upon 100% completion and cancellation.
  - *Verification Target*: `test_download_task` (PASSED ✅), `test_resume_store` (PASSED ✅)
- [x] **Task 8: Automatic File Category Routing** ✅
  - Route completed downloads into category subfolders (`Documents`, `Videos`, `Music`, `Compressed`, `Programs`) based on file extension. Full pipeline wiring into `DownloadTask`, `main.cpp` IPC dispatcher, and `SettingsManager`.
  - *Verification Target*: `test_settings_manager` (PASSED ✅), `test_download_task` (PASSED ✅), `idm_app` (PASSED ✅)
- [x] **Task 8B: Small File Bypass & Browser Fallback Threshold (`SettingsManager` & Extension)** ✅
  - Configurable threshold (default 10–20 MB); files below threshold bypass IDM and download via browser unless user overrides (Alt-click, right-click "Download with IDM", or GUI Add URL). Also auto-use single thread for small files. Full wiring into `NativeHost` and `main.cpp`.
  - *Verification Target*: `test_settings_manager` (PASSED ✅), `test_native_host` (PASSED ✅), `idm_app` (PASSED ✅)

---

### 🔹 Phase 2B: Antivirus & Link Security Engine (`src/security/`)
- [x] **Task 9: Pre-Flight Link/URL Reputation Check (`IAttachmentExecute`) & MOTW** ✅
  - Uses Windows COM interface (`CLSID_AttachmentServices`) to pre-check URLs against SmartScreen and attaches native Windows Mark-of-the-Web (ZoneId=3 Alternate Data Stream) upon download completion. Fully wired into `DownloadTask`, `DownloadQueue`, and `idm_app`.
  - *Verification Target*: `test_antivirus_scanner` (PASSED ✅), `test_download_task` (PASSED ✅), `test_download_queue` (PASSED ✅), `idm_app` (PASSED ✅)
- [x] **Task 10: Staging Cache Scan (`MpCmdRun.exe` / `spctl`) & Threat Parser** ✅
  - Scan completed files in `Cache/` with `-DisableRemediation` (prevents silent deletion).
  - Capture malware names from stdout and evaluate exit codes (`0` clean, `2`/`3` threat). Added cross-platform macOS/Linux scanner parity.
  - *Verification Target*: `test_antivirus_scanner` (PASSED ✅)
- [x] **Task 11: False-Positive Override & Persistent Whitelist Memory** ✅
  - Provide security prompt logic (`Quarantine` vs `Override & Keep`) and persist user-approved domains/hashes in whitelist.
  - Implemented zero-dependency FIPS 180-2 SHA-256 calculation, persistent whitelist JSON storage, quarantine file isolation with permission revocation, metadata sidecar generation, and safe restore/deletion lifecycle.
  - *Verification Target*: `test_antivirus_scanner` (PASSED ✅)
- [x] **Task 12: In-Memory Chunk Buffer Scanning via Windows AMSI (`amsi.dll`) & Heuristics** ✅
  - Deep buffer inspection using `AmsiScanBuffer` as chunks arrive in memory before flushing to disk.
  - Implemented dynamic runtime loading of `amsi.dll` with cross-platform heuristic pattern inspection (EICAR detection) and clean fallback.
  - *Verification Target*: `test_antivirus_scanner` (PASSED ✅)

---

### 🔹 Phase 2C: Smart QoS, Hardware & Network Intelligence (`src/net/`)
- [x] **Task 13: Smart Auto-QoS RTT Latency Monitor (`NetworkMonitor`)** ✅
  - Background thread measuring network RTT (round-trip time).
  - Auto-throttle downloads by 20–30% during 4K streaming or Zoom calls to prevent lag spikes.
  - *Verification Target*: `test_network_monitor` (PASSED ✅)
- [x] **Task 14: Zero-Lag Gaming QoS Mode** ✅
  - Detect active gaming processes (Steam, Epic, Riot) or Discord voice calls and automatically throttle downloads to protect gaming ping.
  - Implemented cross-platform active process enumeration (Windows Toolhelp32 & macOS libproc) with configurable monitored games and automatic 90% throttle / restore lifecycle.
  - *Verification Target*: `test_network_monitor` (PASSED ✅)
- [x] **Task 15: Metered Wi-Fi & Mobile Hotspot Guard** ✅
  - Detect metered Wi-Fi or phone hotspot connections via Windows Network List Manager / SSID heuristic patterns and auto-pause or throttle heavy downloads.
  - Implemented configurable policies (`AutoPause`, `Throttle`, `Allow`), dynamic SSID pattern matching, and state-change callbacks.
  - *Verification Target*: `test_network_monitor` (PASSED ✅)
- [x] **Task 16: Laptop Battery & Thermal Throttler Guard (`PowerMonitor`)** ✅
  - Cross-platform power querying using native Windows `GetSystemPowerStatus` and `GetSystemTimes` (plus macOS `IOPowerSources` and CPU load fallbacks).
  - Dynamically throttles worker threads from 32 down to 4 in `AggressiveEco` battery mode, clamps to 2 in emergency critical battery (< 15%), and throttles under heavy thermal/CPU loads (> 85%).
  - Fully decoupled background monitor thread with state callbacks and deterministic mock providers.
  - *Verification Target*: `test_power_monitor` (PASSED ✅)
- [x] **Task 17: Multi-Adapter Channel Bonding (`ChannelBonding`)** ✅
  - Native adapter discovery via Windows `GetAdaptersAddresses` (and macOS `getifaddrs`), identifying adapter types (Ethernet, Wi-Fi), MACs, and link speeds.
  - Multi-socket binding via libcurl's `CURLOPT_INTERFACE` with Round-Robin, Weighted Ratio (3:1 proportional throughput), and Failover Primary modes.
  - Automatic degradation handling, hot-unplug failover rerouting, and per-adapter byte transfer tracking.
  - *Verification Target*: `test_channel_bonding` (PASSED ✅)

---

### 🔹 Phase 2D: Queue Scheduler & Windows OS Integration (`src/queue/`)
- [x] **Task 18: Post-Queue Native Windows Actions** ✅
  - Native Win32 PC Shutdown (`ExitWindowsEx` with `SE_SHUTDOWN_NAME` privilege acquisition), Sleep/Hibernate (`SetSystemPowerState` / `SetSuspendState`), and completion chime (`PlaySoundA` with `winmm`).
  - Thread-safe deadlock-free queue dispatch with pluggable `NativeActionExecutor` mock for deterministic test interception and dry-run validation.
  - *Verification Target*: `test_download_queue` (PASSED ✅)
- [x] **Task 19: Windows 10/11 Native Action Center Toast Notifications** ✅
  - Trigger native Windows notification toasts with interactive `[Open File]` and `[Open Folder]` buttons upon download completion.
  - *Verification Target*: `test_notification_manager` (PASSED ✅)

---

### 🔹 Phase 2E: Local C++ IPC Server & Keep-Alive Event Loop (`src/ipc/` & `src/gui/`)
- [x] **Task 20: Embedded Zero-Dependency Local REST Server (`LocalServer`)** ✅
  - Lightweight Winsock HTTP server listening on `127.0.0.1:port`.
  - Endpoints: `POST /api/add_download`, `POST /api/pause`, `POST /api/resume`, `POST /api/delete`, `GET /api/tasks`, `GET /api/chunks?id=...`, `POST /api/av_override`.
  - *Verification Target*: `test_local_server` (PASSED ✅)
- [x] **Task 21: `MainWindow` Keep-Alive Event Loop** ✅
  - Block while the desktop window is open and shut down cleanly when closed.
  - Headless/live modes, child process tracking (`CreateProcessA`/`TerminateProcess`), `LocalServer` auto-teardown, and thread unblocking.
  - *Verification Target*: `test_main_window` (PASSED ✅)

---

### 🔹 Phase 2F: Media, Archives & Swarm Acceleration (`src/media/` & `src/tools/`)
- [x] **Task 22: Multi-Mirror Swarm Downloading (Metalink Multiplexing)** ✅
  - Split chunks across multiple mirror URLs simultaneously to bypass single-server bandwidth caps and multiply speed 3x.
  - Metalink XML/JSON parsers with substring guards, dynamic mirror registration & health demotion, round-robin & priority multiplexing, live failover, and SHA-256 bit-exact assembly verification.
  - *Verification Target*: `test_swarm_download` (PASSED ✅)
- [x] **Task 23: Universal Video & Audio Extraction Pipeline (`yt-dlp` & FFmpeg)** ✅
  - Native backend pipeline supporting 1,000+ streaming sites with resolution selector (4K 60fps, 1080p, audio-only 320kbps MP3).
  - Stream format matrix parsing, smart presets, real-time line-by-line stdout progress tracker, cancellation lifecycle, and mock process execution.
  - *Verification Target*: `test_media_extractor` (PASSED ✅)
- [x] **Task 24: Instant In-App Stream Preview (`StreamPreviewer`)** ✅
  - Stream and play partially downloaded video/audio files directly from disk buffer while downloading via non-exclusive read-shared handles.
  - Multi-container magic header analysis (MP4, WebM, MKV, WAV, MP3), MP4 fast-start `moov` atom placement verification, contiguous playable range estimation across active chunks, readiness threshold gates, and dynamic loopback HTTP 206 Partial Content server routing.
  - *Verification Target*: `test_stream_preview` (PASSED ✅)
- [x] **Task 25: Automated Archive Unpacking & Integrity Verification (`ArchiveVerifier`)** ✅
  - Auto-extract `.zip`, `.rar`, `.7z`, `.tar`, `.gz`, `.bz2` archives upon completion into canonical subfolders with Zip-Slip path traversal protection.
  - Full RFC 1321 MD5, RFC 3174 SHA-1, and SHA-256 streaming file digest computation, automatic clipboard hex checksum extraction, and platform extraction command dispatching.
  - *Verification Target*: `test_archive_verifier` (PASSED ✅)
---

### 🔹 Phase 2G: Authentication, Browser Extension & Remote Access
- [x] **Task 26: Browser Extension (Manifest V3) & Native Messaging Setup** ✅
  - Manifest V3 extension bundle (`manifest.json`, `background.js`, `popup.html`, `popup.js`, `com.idm.native_host.json`) intercepting browser downloads, harvesting domain cookies and Referers.
  - Native messaging host binary (`idm_native_host.exe`) with 4-byte stdio protocol framing, Chrome/Edge HKCU registry installation/uninstallation, and real-time loopback TCP forwarding to `LocalServer` (`POST /api/add_download`).
  - *Verification Target*: `test_native_host` (PASSED ✅)
- [ ] **Task 27: Site Logins Vault & Cookie Persistence (`src/security/Vault.h`)**
  - Encrypted storage for session cookies (`session_id`, `auth_token`, `cf_clearance`) and credentials for protected hosters (Google Drive, Mega, 1Fichier, private forums), auto-injected into libcurl headers.
  - *Verification Target*: `test_vault`
- [ ] **Task 28: Local Wi-Fi Remote Dashboard & Floating Desktop Mini-Dock**
  - Enable local network access so users can monitor/add downloads from their phone or tablet on the same Wi-Fi, plus a floating desktop mini-dock widget for drag-and-drop link catching.
  - *Verification Target*: `test_remote_dashboard`
