# IDM (Internet Download Manager) Master Application Specification & Context Handoff

> **CRITICAL INSTRUCTIONS FOR AI AGENT (MANDATORY RULE)**
> - **DO NOT edit or modify code files directly using file-writing/editing tools.** The user wants to write all code themselves to learn hands-on.
> - **ALWAYS provide clear code snippets**, build/run terminal commands, and **deep line-by-line explanations** for one file at a time.
> - **Obey User Control & Hands-On Learning**: Let the user write the code, run the build commands, and verify test outputs themselves.

---

## 1. Project Context & Workspace Index

- **Workspace Path**: `e:\Projects\idm`
- **Compiler / Toolchain**: MinGW `g++` 15.2.0, `cmake` 4.2.3, `ninja` 1.13.2 on Windows.
- **Third-Party Libraries**: `libcurl` (with Windows Schannel SSL support), `nlohmann_json` via FetchContent.

### Modular Project Directory Structure:
```
e:\Projects\idm\
├── CMakeLists.txt
├── src/
│   ├── core/                    # Core engine components (ThreadPool, ChunkManager, FileWriter, ChunkWorker, ProgressTracker, ResumeStore, DownloadTask)
│   │   ├── ThreadPool.h / .cpp
│   │   ├── Chunk.h
│   │   ├── ChunkManager.h / .cpp
│   │   ├── FileWriter.h / .cpp
│   │   ├── ChunkWorker.h / .cpp
│   │   ├── ProgressTracker.h / .cpp
│   │   ├── ResumeStore.h / .cpp
│   │   └── DownloadTask.h / .cpp
│   │
│   ├── config/                  # Configuration & settings management
│   │   └── SettingsManager.h / .cpp
│   │
│   ├── queue/                   # Multi-queue scheduler & auto-shutdown engine
│   │   └── DownloadQueue.h / .cpp
│   │
│   ├── security/                # Windows Defender & SmartScreen security pipeline
│   │   └── AntivirusScanner.h / .cpp
│   │
│   ├── net/                     # Auto-QoS RTT monitor & Channel Bonding
│   │   ├── NetworkMonitor.h / .cpp
│   │   └── ChannelBonding.h / .cpp
│   │
│   ├── ipc/                     # Local C++ REST server bridging C++ to UI
│   │   └── LocalServer.h / .cpp
│   │
│   ├── native_host/             # Browser Native Messaging Host binary
│   │   └── NativeHost.h / .cpp
│   │
│   └── gui/                     # Desktop GUI launcher & bridge
│       └── MainWindow.h / .cpp
│
├── ui/                          # 100% Local HTML5/CSS3/JS Frontend
│   ├── index.html
│   ├── css/
│   │   ├── styles.css
│   │   └── components.css
│   ├── js/
│   │   ├── app.js
│   │   ├── chunk_visualizer.js
│   │   └── settings.js
│   └── assets/                  # Icons, sound alerts (.wav), images
│
└── test/                        # Unit test suite
    ├── test_threadpool.cpp
    ├── test_chunk_manager.cpp
    ├── test_file_writer.cpp
    ├── test_chunk_worker.cpp
    ├── test_resume_store.cpp
    ├── test_progress_tracker.cpp
    ├── test_download_task.cpp
    ├── test_settings_manager.cpp
    ├── test_download_queue.cpp
    ├── test_antivirus_scanner.cpp
    ├── test_native_host.cpp
    └── test_main_window.cpp
```

### Core Engine Files (`src/`):
- [`CMakeLists.txt`](file:///e:/Projects/idm/CMakeLists.txt) — Global build configuration with static MinGW runtime linking (`-static`).
- [`src/core/ThreadPool.h`](file:///e:/Projects/idm/src/core/ThreadPool.h) & [`.cpp`](file:///e:/Projects/idm/src/core/ThreadPool.cpp) — Multi-threaded worker pool & thread-safe task queue.
- [`src/core/Chunk.h`](file:///e:/Projects/idm/src/core/Chunk.h) — `Chunk` struct and `ChunkState` enum (`Pending`, `InProgress`, `Done`, `Failed`).
- [`src/core/ChunkManager.h`](file:///e:/Projects/idm/src/core/ChunkManager.h) & [`.cpp`](file:///e:/Projects/idm/src/core/ChunkManager.cpp) — Byte-range chunking & thread-safe state tracker.
- [`src/core/FileWriter.h`](file:///e:/Projects/idm/src/core/FileWriter.h) & [`.cpp`](file:///e:/Projects/idm/src/core/FileWriter.cpp) — Thread-safe pre-allocated offset disk writer (`WriteFile` / `pwrite`).
- [`src/core/ChunkWorker.h`](file:///e:/Projects/idm/src/core/ChunkWorker.h) & [`.cpp`](file:///e:/Projects/idm/src/core/ChunkWorker.cpp) — libcurl HTTP/HTTPS Range chunk downloader.
- [`src/core/ResumeStore.h`](file:///e:/Projects/idm/src/core/ResumeStore.h) & [`.cpp`](file:///e:/Projects/idm/src/core/ResumeStore.cpp) — JSON sidecar persistence (`.progress.json`) for pause/resume.
- [`src/core/ProgressTracker.h`](file:///e:/Projects/idm/src/core/ProgressTracker.h) & [`.cpp`](file:///e:/Projects/idm/src/core/ProgressTracker.cpp) — Exponential Moving Average (EMA) speed filter & ETA calculator.
- [`src/core/DownloadTask.h`](file:///e:/Projects/idm/src/core/DownloadTask.h) & [`.cpp`](file:///e:/Projects/idm/src/core/DownloadTask.cpp) — Master orchestrator (HTTP HEAD probing, thread pool dispatch, progress monitoring).

---

## 2. Desktop GUI & UX Design Specifications

### Architecture & Local Offline Execution
- **100% Offline & Local Disk Loading**: All UI files (`index.html`, `styles.css`, `app.js`, vector icons, fonts) are stored **locally on your computer disk**. The interface loads instantly in milliseconds without requiring any internet connection.
- **HTML5/CSS3 Visual Freedom**: Unlocks unlimited styling capabilities — dark glassmorphism, glowing status badges, smooth CSS transitions, custom dark scrollbars, animated per-chunk visual progress bars, and responsive layouts.
- **C++ Engine Communication**: The HTML5/CSS3 frontend communicates directly with our C++ backend engine via Windows WebView2 / local IPC RPC calls.

### Aesthetic System
- **Modern Dark & Light Themes**: Sleek dark mode palette (Slate/Zinc charcoal `#0f172a`, card surfaces `#1e293b`, primary accent `#3b82f6`), subtle glassmorphism card surfaces, custom scrollbars, and modern typography (Inter / Roboto).
- **Responsive Layout**: Resizable main window with collapsable sidebar and flexible data grid.

### Top Menu Bar & Quick Action Toolbar
```
┌──────────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│  Tasks    File    Downloads    View    Options    Scheduler    Help                                              │
├──────────────────────────────────────────────────────────────────────────────────────────────────────────────────┤
│  [+] Add URL   [▶] Resume   [⏸] Pause   [◼] Stop All   [🗑] Delete   [⚙] Options   [📅] Scheduler                  │
│                                                                                                                  │
│  [🟢 Interception: ON / 🔴 OFF]   │   Speed Limiter: [ ⚡ Max Uncapped ⯆ | 🧠 Smart Auto-QoS ⯆ | 🐢 Manual Cap ⯆ ] │
└──────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

#### Quick-Action Toolbar Buttons:
- **`[+] Add URL`**: Manual URL input dialog (supports custom URL, referrer, cookie strings).
- **`[▶] Resume`**: Resume selected download.
- **`[⏸] Pause`**: Pause selected download.
- **`[◼] Stop All`**: Stop all active downloading tasks.
- **`[🗑] Delete`**: Remove selected entry or file.
- **`[⚙] Options`**: Opens the Tabbed Configuration Window.
- **`[📅] Scheduler`**: Opens Queue & Scheduler Manager.
- **`[🟢 Interception Switch]`**: One-click global toggle for browser link interception.
- **`[Speed Limiter Selector]`**: Switch between Uncapped, Smart Auto-QoS (latency sensing), and Manual Speed Cap.

---

### Main Window Layout (Sidebar & Central Data Grid)
```
┌───────────────────────────┬──────────────────────────────────────────────────────────────────────────────────────┐
│  CATEGORIES               │  DOWNLOADS LIST                                                                      │
├───────────────────────────┼──────────────────────────────────────────────────────────────────────────────────────┤
│ 📋 All Downloads          │ File Name       Category    Size     Status       Progress          Speed     ETA    │
│ ⏳ Unfinished             │──────────────────────────────────────────────────────────────────────────────────────│
│ ✅ Finished               │ MKV_Movie.mkv   Video       2.59GB   Downloading  [=======>  ] 45%  52.4MB/s  00:12  │
│                           │ Setup_v2.exe    Programs    120MB    Completed    [==========]100%   --        --     │
│  CATEGORIES:              │ Game_Data.zip   Compressed  14.2GB   Paused       [===>      ] 22%   --        --     │
│ 📁 Documents (.pdf, .doc) │                                                                                      │
│ 🎬 Videos (.mp4, .mkv)    │                                                                                      │
│ 🎵 Music (.mp3, .wav)     │                                                                                      │
│ 📦 Compressed (.zip,.7z)  │                                                                                      │
│ 💻 Programs (.exe, .iso)  │                                                                                      │
└───────────────────────────┴──────────────────────────────────────────────────────────────────────────────────────┘
```

#### Right-Click Context Menu (File Item):
- 📁 **Open File** / **Open Folder**
- ▶ **Resume** / ⏸ **Pause**
- 🔄 **Redownload / Refresh Expired Download Link**
- 🗑 **Delete File & Record**
- 📋 **Copy URL to Clipboard**
- ℹ️ **File Properties / Details**

---

### Iconic Per-Chunk Visualizer Dialog
When clicking on an active download (or opening its Progress Dialog), a detailed modal opens displaying:
- Total Progress, Downloaded Bytes / Total Bytes, Speed, and ETA.
- **Per-Chunk Visual Progress Bars**: Renders individual progress bars for every thread worker ($1 \dots N$ threads), showing active byte transfers per chunk in real time!

---

### System Tray Background Daemon
- Closing the main GUI window minimizes the application to the Windows System Tray.
- System Tray Context Menu:
  - `[✓] Interception Enabled`
  - `[⚡] Speed Limiter: Max Uncapped / 🧠 Smart Auto-QoS / 🐢 Manual Cap`
  - `⏸ Pause All Downloads`
  - `▶ Resume All Downloads`
  - `📂 Open Downloads Folder`
  - `🚪 Exit IDM`

---

## 3. Real Binary Payload Interception & The 6 Download Scenarios

> [!IMPORTANT]
> ### Core Rationale & User Reasoning (Why DOM Scraping Fails & Header Interception Wins)
> 1. **Daily Ad Script Changes**: File hosters and link shorteners (e.g. `links.ol-am.top`, `healthshield`, `rvpaste`, `financeehelp`) constantly change their button IDs, CSS selectors, DOM structures, countdown timers, and popunder ad scripts daily. Automating DOM button clicks is a fragile trap.
> 2. **Fake Download Buttons vs Real Final Download Link**: Shortener pages contain multiple deceptive ad buttons ("Download Now", "Start Download", "Click Here") that simply open ad popups or new browser windows. Not every button is a real download button.
> 3. **Multi-Step Human Verification & Logins**: Shortener pages often require multi-step human verification (Cloudflare Turnstile, image captchas, login walls) before revealing the final download URL.
> 4. **The Definitive IDM Solution**: Instead of fighting an endless battle writing fragile scrapers, IDM lets the user navigate through human verifications and ad pages naturally in their normal browser. IDM **completely ignores all intermediate `text/html` response pages**, preventing false popups on fake ad buttons. IDM triggers ONLY when the user reaches the final step and clicks the genuine link that streams a real binary payload (`Content-Disposition: attachment`, `application/octet-stream`, `.zip`, `.exe`, `.mp4`, `.pdf`).

```
  User Clicks Button / Link in Browser
                  │
                  ▼
  Inspect HTTP Response Headers (Extension / Core Engine)
                  │
  ┌───────────────┴─────────────────────────────┐
  ▼                                             ▼
[Content-Type: text/html]             [Binary Payload File Stream]
(Fake Ad Button / Intermediate Page)   • Content-Disposition: attachment; filename=...
        │                              • Content-Type: application/octet-stream, video/mp4...
        ▼                              • Extensions: .zip, .mkv, .mp4, .exe, .iso, .pdf
IGNORE (Browser displays page normally)         │
                                               ▼
                                   🚀 INTERCEPT IN IDM!
                                   (Pops up Start Download Dialog
                                    & Handlers Multi-Threaded Engine)
```

### Detailed Breakdown of the 6 Download Scenarios:

#### Scenario 1: Standard Direct Binary Downloads (`.zip`, `.exe`, `.iso`, `.pdf`, `.mp4`)
- **Flow**: User clicks a direct file link in Chrome/Brave/Firefox.
- **Handling**: Extension inspects response headers (`Content-Disposition: attachment` or matching binary MIME/extension).
- **Action**: Cancels native browser download, extracts URL, Cookies, User-Agent, and Referer, sends to IDM via Native Messaging, pops up "Start Download" dialog.

#### Scenario 2: Link Shorteners, Multi-Step Ad Pages & Human Verification (Cloudflare, Captchas, Logins)
- **Flow**: User navigates sites with shortener redirects, popunder ads, captchas, or login walls (`links.ol-am.top`, `healthshield`, `rvpaste`, `financeehelp`).
- **Handling**: Intermediate pages return `Content-Type: text/html`.
- **Action**: IDM **completely ignores** all intermediate `text/html` pages and fake ad buttons. The user completes human verification / captchas naturally inside their normal browser. Only when the final link streams an actual binary file payload does IDM catch it automatically!

#### Scenario 3: Protected Hosters & Premium Accounts (Google Drive, Mega, Rapidgator, Private Forums)
- **Flow**: Direct file links require user authentication cookies (`session_id`, `auth_token`, `cf_clearance`).
- **Handling**: Browser extension mirrors session cookies automatically. For standalone IDM URL additions, an **Embedded WebView Login Window** opens for one-time user login.
- **Action**: Credentials and cookies are stored in IDM's encrypted **Site Logins Vault** and automatically injected into libcurl HTTP headers (`Cookie: ...`).

#### Scenario 4: Expired Temporary Download Links (24-Hour Token Timeout)
- **Flow**: Download pauses or fails with HTTP 403 Forbidden / 410 Gone due to expired token.
- **Handling**: User right-clicks task in GUI -> **"Refresh Download Address"**.
- **Action**: IDM opens browser to grab a new generated link, updates URL in `DownloadTask` and `ResumeStore`, and continues chunk downloads **without losing existing downloaded data**.

#### Scenario 5: Web Video & Audio Stream Grabber (HLS / DASH / HTML5 Video)
- **Flow**: User plays video on web page (YouTube, Vimeo, news portals).
- **Handling**: Content script detects HTML5 `<video>` elements and video stream segments (`.m3u8`, `.mpd`, `.ts`).
- **Action**: Displays floating **"Download Video with IDM"** overlay button on top-right of player, offering resolution options (1080p, 720p, audio MP3).

#### Scenario 6: Manual URL Additions, Clipboard Monitoring & Drag-and-Drop
- **Flow**: User copies link to clipboard, clicks `[+] Add URL`, or drags link onto IDM window.
- **Handling**: Clipboard listener detects file extension URLs. Manual dialog allows custom HTTP Headers (Referer, Cookie, User-Agent).
- **Action**: Probes server with HTTP `HEAD` request to inspect Range headers and pre-allocate file before downloading.

---

## 4. Embedded Sign-In Browser Popup & Site Logins Vault

- **Embedded WebView Auth Window**: For protected websites/hosters (Google Drive, Mega, private forums), the app pops up a mini embedded WebView window.
- **One-Time Login**: The user logs in once inside the popup window.
- **Auto Cookie Extractor & Vault**: The app automatically captures session cookies (`session_id`, `auth_token`, `cf_clearance`) and saves them in IDM's encrypted **Site Logins Vault**.
- **Automatic Header Injection**: All future `DownloadTask` requests to that domain automatically inject the saved cookies into libcurl.

---

## 5. Expired Download Link Refresh Workflow

- **The Problem**: Temporary download URLs (e.g. Google Drive, Rapidgator, 1Fichier) expire after 24 hours.
- **The IDM Solution**: Right-clicking a paused download and selecting **"Refresh Download Address"** allows pasting/grabbing a new download URL. The engine updates the URL in `DownloadTask` and `ResumeStore`, and resumes downloading the remaining chunks **without losing already downloaded data**!

---

## 6. Centralized Cache & Multi-Drive Directory Management

### Centralized Cache System
- **Drive-Independent Installation**: App can be installed on any drive (`C:`, `D:`, `E:`, or portable USB drive).
- **Isolated `Cache/` Directory**: All in-progress chunk buffers and `.progress.json` sidecars are centralized in a single folder (e.g., `E:\IDM\Cache\`).
- **Automated Cache Purging**:
  - Automatically deletes cache files immediately when a download reaches 100% completion.
  - Startup garbage collection automatically cleans up orphaned cache files from cancelled tasks.

### Multi-Drive Folder Menu
- Top Menu Bar (`File -> Open Folders`) provides instant shortcuts to:
  - Configured download folders across multiple drives (`D:\Downloads`, `E:\Videos`, `E:\Games`).
  - Centralized Cache Folder (`E:\IDM\Cache`).
  - One-click **"Clear Cache Now"** button displaying total cache size freed.

---

## 7. Speed Limiter Engine & Control System

The Speed Limiter is accessible from **4 locations**:
1. **Main GUI Header Dropdown**: Quick selector `[ ⚡ Max Uncapped | 🧠 Smart Auto-QoS | 🐢 Manual Cap ]`.
2. **System Tray Right-Click Menu**: Quick access preset toggles.
3. **`Downloads` Top Menu**: `Downloads -> Speed Limiter -> Turn On / Turn Off / Settings...`
4. **`Options` Window (`Connection` Tab)**: Global speed limit input box (in KB/s or MB/s) and task-level caps.

```
 ┌─────────────────────────────────────────────────────────────────────────────┐
 │                      SPEED LIMITER OPERATING MODES                         │
 ├─────────────────────────────────────────────────────────────────────────────┤
 │ 1. 🚀 Max Speed (100% Uncapped): Open full bandwidth across 32 threads.    │
 │                                                                             │
 │ 2. 🧠 Smart Auto-QoS (Streaming Mode):                                      │
 │    • Continuously monitors system-wide network round-trip time (RTT).       │
 │    • If active 4K video streaming (YouTube/Netflix) or Zoom calls are      │
 │      detected, IDM automatically throttles chunk downloads by 20–30% to    │
 │      prevent lag, then ramps back to 100% when the video buffer fills.      │
 │                                                                             │
 │ 3. 🐢 Manual Cap Mode:                                                      │
 │    • User-defined global speed limit (e.g. 500 KB/s, 2.0 MB/s, 5.0 MB/s).  │
 │    • Applies token bucket rate limiting across active ChunkWorker           │
 │      threads without dropping TCP connections.                              │
 └─────────────────────────────────────────────────────────────────────────────┘
```

---

## 8. Smart Antivirus Security & False-Positive Override

- **Pre-Flight Link/URL Reputation Check**:
  - Uses the Windows `IAttachmentExecute` COM interface (`#include <shobjidl.h>`) with `CLSID_AttachmentServices`.
  - Calls `SetSource(url)` and `CheckPolicy()` before downloading begins.
  - If Microsoft Defender SmartScreen identifies the link as phishing or malware, download pauses immediately and notifies the user before fetching any bytes.
- **Post-Download Staging Scan via Windows Defender CLI (`MpCmdRun.exe`)**:
  - Downloaded file is kept in the isolated cache buffer (`E:\IDM\Cache\staging_task.bin`).
  - Invokes `MpCmdRun.exe -Scan -ScanType 3 -File "<cached_file>" -DisableRemediation`.
  - `-DisableRemediation` ensures Windows Defender acts as an inspector and does **not** silently delete or quarantine the file behind IDM's back.
  - Evaluates exit codes:
    - Exit Code `0`: Clean. File moves automatically to final directory (`Downloads/`).
    - Exit Code `2`: Threat found. Captures malware signature name from stdout (e.g. `Trojan:Win32/Wacatac.B!ml`).
- **False-Positive Override Dialog**:
  - If a threat is flagged (Exit Code `2`), the GUI displays a prompt:
    `[ 🛡️ Quarantine & Delete ]` vs `[ ⚠️ Override & Keep File Anyway ]`.
  - Overriding moves the file to the destination folder and adds the domain/hash to a persistent **Trusted Whitelist** so the user isn't repeatedly alerted for approved files.
- **In-Memory Buffer Scanning via Windows AMSI (`amsi.dll`)**:
  - Optional real-time scanning of chunk buffers using `AmsiScanBuffer()` before flushing to disk.

---

## 9. Batch Downloader, Clipboard Monitor & Queue Scheduler

- **Batch Pattern Grabber**: Parse and download URL lists using regex range patterns (e.g., `https://site.com/file_[001-100].zip`).
- **Clipboard Monitoring**: Automatically detects valid download URLs copied in Windows clipboard and pops up an "Add Download" notification.
- **Multi-Queue Engine**: Main Queue, Synchronization Queue, and Custom User Queues with configurable file concurrency.
- **Time-Based Scheduler & Auto-Shutdown**: Schedule start/stop times for queues, with post-download actions: **Turn Off Computer**, **Sleep**, **Hibernate**, or **Play Notification Sound**.

---

## 10. Tabbed Settings Manager Specifications

- **General**: Windows startup toggle, browser integration checkboxes, hotkey overrides (`ALT` force, `SHIFT` bypass).
- **File Types**: Extensions list for automatic interception, excluded domains list.
- **Connection**: Max connections selector (1–32), global speed cap settings, speed limiter mode selection.
- **Save To**: Default category folders (`Documents`, `Videos`, `Music`, `Programs`, `Compressed`) and Cache directory path.
- **Downloads**: UI notification dialog toggles, Antivirus scanner executable path & arguments.
- **Proxy / SOCKS**: System proxy, manual HTTP/HTTPS/SOCKS5 proxy, PAC script URL.
- **Site Logins**: Encrypted credential storage for protected sites and hosters.

---

## 11. Browser Extension & Native Messaging Host Architecture

- **Manifest V3 Extension**: Installed in Chrome, Brave, Edge, Firefox.
- **Native Messaging Host**: Lightweight executable `idm_native_host.exe` installed in registry (`HKCU\Software\Google\Chrome\NativeMessagingHosts\com.idm.native_host`).
- **Communication Protocol**: Inter-process communication via `stdin`/`stdout` using 32-bit length-prefixed JSON messages containing URL, Cookies, User-Agent, Referer, and Page Title.

---

## 12. File Category Auto-Routing & Custom Filter Rules

- Automatically routes completed downloads into category subfolders:
  - 📁 **Documents**: `.pdf`, `.doc`, `.docx`, `.xls`, `.txt` -> `Downloads/Documents`
  - 🎬 **Videos**: `.mp4`, `.mkv`, `.avi`, `.webm`, `.mov` -> `Downloads/Videos`
  - 🎵 **Music**: `.mp3`, `.flac`, `.wav`, `.aac` -> `Downloads/Music`
  - 📦 **Compressed**: `.zip`, `.rar`, `.7z`, `.tar`, `.gz` -> `Downloads/Compressed`
  - 💻 **Programs**: `.exe`, `.msi`, `.iso`, `.dmg` -> `Downloads/Programs`
- Custom user routing rules can be added per file extension or domain name in Settings.

---

## 13. Post-Download Actions & Audio Notifications System

- Configurable actions when individual downloads or entire queues complete:
  - 🔔 **Play Custom Notification Sound** (`.wav`).
  - 📂 **Open Destination Folder / Highlight File**.
  - 🛡️ **Trigger Antivirus Security Isolation Scan**.
  - ⚡ **Auto Shutdown / Sleep / Hibernate PC**.

---

## 14. Next-Gen "Better Than IDM" Innovations

1. **Multi-Mirror Swarm Downloading (Metalink Multiplexing)**: Split chunks across multiple mirror URLs simultaneously to multiply speed 3x.
2. **Universal Video & Audio Extractor (`yt-dlp` & FFmpeg Pipeline)**: Native backend pipeline supporting 1,000+ streaming sites (YouTube, Twitch, TikTok, Instagram, Twitter/X) with resolution selector (4K 60fps, 1080p, audio-only 320kbps MP3).
3. **Instant In-App Stream Preview**: Watch videos or listen to audio files in an embedded player while they are still downloading.
4. **Zero-Lag Gaming QoS Mode**: Auto-detect when games or voice calls are active and automatically throttle background downloads to preserve ping.
5. **Metered Network & Mobile Hotspot Guard**: Detect metered Wi-Fi or phone hotspot connections via Windows Network List Manager API and auto-pause heavy downloads.
6. **Laptop Battery & Thermal Guard**: Reduce worker thread count (32 threads down to 4) when running on battery or high CPU temperatures.
7. **Automated Archive Unpacking & Integrity Verification**: Auto-extract archives upon completion and auto-verify clipboard MD5/SHA256 checksums.
8. **Local Wi-Fi Remote Control Dashboard**: Access download queue from phone or tablet on the same local Wi-Fi network.
9. **Floating Desktop Mini-Dock**: Compact, semi-transparent desktop widget showing real-time aggregate speed and serving as a drag-and-drop link target.
