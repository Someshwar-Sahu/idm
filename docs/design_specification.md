# IDM-Style Download Manager — Phase 1: Backend Core Engine

**Stack:** C++17, libcurl, CMake (cross-platform build)
**Goal of Phase 1:** A working, testable command-line download engine — no GUI yet. Prove multi-connection chunked downloading, resume, and merging work correctly before touching Qt.

---

## 1. Scope of Phase 1

In scope:
- Single file download, split into N parallel chunks over HTTP/HTTPS
- Server-capability detection (does it support `Accept-Ranges`?)
- Fallback to single-connection download when ranges aren't supported
- Resume support (persist chunk progress to disk)
- Basic retry on chunk failure
- Console progress output (percentage + speed per chunk + total)

Out of scope (later phases):
- GUI (Phase 3)
- Multi-file queue (Phase 4)
- Speed limiter (Phase 5)
- Browser hook (Phase 6)

---

## 2. High-Level Architecture

```
                    ┌─────────────────────┐
                    │   DownloadTask       │  (represents ONE file download)
                    │  - url, filename     │
                    │  - total size        │
                    │  - chunk list        │
                    └──────────┬───────────┘
                               │ owns
                               ▼
                    ┌─────────────────────┐
                    │   ChunkManager       │  splits file into byte ranges,
                    │                      │  tracks chunk state
                    └──────────┬───────────┘
                               │ dispatches jobs to
                               ▼
                    ┌─────────────────────┐
                    │   ThreadPool         │  fixed-size pool of workers
                    │  (N worker threads)  │
                    └──────────┬───────────┘
                               │ each worker runs
                               ▼
                    ┌─────────────────────┐
                    │   ChunkWorker        │  owns one CURL easy handle,
                    │  (libcurl easy call) │  downloads its byte range
                    └──────────┬───────────┘
                               │ writes via
                               ▼
                    ┌─────────────────────┐
                    │   FileWriter         │  pwrite/seek+write at correct
                    │                      │  offset in the output file
                    └──────────┬───────────┘
                               │ progress reported to
                               ▼
                    ┌─────────────────────┐
                    │   ProgressTracker    │  aggregates per-chunk bytes,
                    │                      │  prints total % + speed
                    └─────────────────────┘

                    ┌─────────────────────┐
                    │   ResumeStore        │  reads/writes a .progress
                    │  (JSON on disk)      │  sidecar file per download
                    └─────────────────────┘
```

---

## 3. Core Classes (interface-level, no implementation yet)

### 3.1 `DownloadTask`
Represents one download job end-to-end.

Responsibilities:
- Holds URL, destination path, total file size, number of chunks
- Owns the `ChunkManager` and the list of `Chunk` structs
- Entry points: `start()`, `pause()`, `resume()`, `cancel()`
- On construction, does a `HEAD` request (via libcurl) to get:
  - `Content-Length`
  - Whether `Accept-Ranges: bytes` is present
  - Final resolved URL (after redirects)

### 3.2 `Chunk` (plain data struct)
```
struct Chunk {
    int id;
    uint64_t start_byte;
    uint64_t end_byte;
    uint64_t bytes_downloaded;   // for resume + progress
    enum State { Pending, InProgress, Done, Failed } state;
};
```

### 3.3 `ChunkManager`
Responsibilities:
- Given total size + desired chunk count, compute chunk boundaries
- Track state of each chunk (pending/in-progress/done/failed)
- Provide next-job-to-do (for retry logic and thread pool pull model)
- Detect "all chunks done" condition to trigger merge/finalize

### 3.4 `ThreadPool`
Generic reusable thread pool (not download-specific), so it can be reused later for other parallel work.

Responsibilities:
- Fixed number of worker threads at construction
- `enqueue(std::function<void()> job)` interface
- Threads pull from a job queue (mutex + condition variable)
- Graceful shutdown

### 3.5 `ChunkWorker`
Responsibilities:
- Given a `Chunk`, perform the actual libcurl request with `Range: bytes=start-end`
- Use `CURLOPT_WRITEFUNCTION` callback to stream bytes directly to `FileWriter` (avoid buffering whole chunk in RAM)
- On failure (timeout, connection reset): report back to `ChunkManager` for retry, up to a max retry count
- Update `bytes_downloaded` on the chunk as data arrives (for both progress and resume)

### 3.6 `FileWriter`
Responsibilities:
- Pre-allocate the output file to full size before download starts (avoids fragmentation, allows random-offset writes)
- Thread-safe write-at-offset (each worker writes only its own byte range, so no overlap — but still needs a safe file handle strategy: either one `pwrite`-style call per platform, or one file handle per thread opened in read/write mode)
- Cross-platform note: Windows uses `_pwrite`/`SetFilePointerEx`+`WriteFile`; POSIX uses `pwrite()`. Abstract this behind one function so the rest of the code doesn't care about OS.

### 3.7 `ResumeStore`
Responsibilities:
- Serializes chunk state (id, start, end, bytes_downloaded, state) to a JSON sidecar file, e.g. `filename.part.json`
- On startup, if a sidecar file exists for the target file, offer to resume: reconstruct `ChunkManager` state from it instead of starting fresh
- Deletes the sidecar file once download completes and merge is confirmed successful

### 3.8 `ProgressTracker`
Responsibilities:
- Polls or receives callbacks with bytes-downloaded-per-chunk
- Computes: overall percentage, instantaneous speed (rolling window), ETA
- Prints to console (Phase 1) — later phases will feed this to the GUI via signals

---

## 4. Data Flow (single download, happy path)

1. User provides URL + output path via CLI args
2. `DownloadTask` issues `HEAD` request → gets size + range support
3. If no range support → single `ChunkWorker` downloads whole file, done
4. If range support:
   a. `ChunkManager` divides file into N chunks (N = CLI arg or default, e.g. 8)
   b. `FileWriter` pre-allocates output file to full size
   c. Each chunk is enqueued as a job in `ThreadPool`
   d. Each `ChunkWorker` downloads its range, writing bytes as they stream in, updating `ResumeStore` periodically (e.g. every N KB or every second)
   e. `ProgressTracker` aggregates and prints status
5. When all chunks report `Done`, verify total bytes written == expected size
6. Delete the `.progress.json` sidecar
7. Exit

### Resume path
1. User re-runs the same command
2. `DownloadTask` checks for existing `.progress.json`
3. If found and the server's `Content-Length`/ETag still matches what's stored → reconstruct chunk states, skip chunks already `Done`, resume `InProgress`/`Pending` ones from their last `bytes_downloaded` offset (adjust the `Range` header's start accordingly)
4. If server file has changed (different ETag/size) → warn user, restart fresh

---

## 5. Threading Model Notes

- **Thread pool, not one-thread-per-chunk**: even with e.g. 32 chunks, only a fixed number of worker threads (e.g. 8) actually run concurrently; extra chunks queue up. This avoids oversubscribing CPU/network with huge chunk counts.
- **libcurl thread-safety**: use one `CURL*` easy handle per thread (never share a handle across threads). Global libcurl init (`curl_global_init`) happens once at program start, before any threads spawn.
- **Alternative considered**: libcurl's `multi` interface (single-threaded event loop managing many connections). This is actually closer to how real IDM/aria2 do it internally — worth a design note for a possible v2 refactor, but Phase 1 uses the simpler thread-per-job model since the goal is to *learn threading explicitly*.

---

## 6. Resume File Format (`<filename>.progress.json`)

```json
{
  "url": "https://example.com/file.zip",
  "total_size": 104857600,
  "etag": "\"abc123\"",
  "chunk_count": 8,
  "chunks": [
    { "id": 0, "start": 0, "end": 13107199, "downloaded": 13107199, "state": "Done" },
    { "id": 1, "start": 13107200, "end": 26214399, "downloaded": 4200000, "state": "InProgress" }
  ]
}
```

---

## 7. Project Folder Structure (proposed)

```
download-manager/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── DownloadTask.h / .cpp
│   ├── ChunkManager.h / .cpp
│   ├── Chunk.h
│   ├── ThreadPool.h / .cpp
│   ├── ChunkWorker.h / .cpp
│   ├── FileWriter.h / .cpp
│   ├── ResumeStore.h / .cpp
│   └── ProgressTracker.h / .cpp
├── third_party/           (or use CMake FetchContent for libcurl + a JSON lib)
├── tests/
│   └── (unit tests per class, later)
└── README.md
```

Dependencies to bring in via CMake `FetchContent` or system package:
- `libcurl` (networking)
- A JSON library for the resume file — `nlohmann/json` is the common lightweight choice

---

## 8. Build Order Within Phase 1

1. `ThreadPool` (generic, testable on its own with dummy jobs)
2. `Chunk` + `ChunkManager` (pure logic, no networking — unit-testable)
3. `FileWriter` (pre-allocate + offset write, test with dummy data)
4. `ChunkWorker` (libcurl integration — first point where real networking appears)
5. `DownloadTask` (wires everything together)
6. `ResumeStore` (JSON persistence)
7. `ProgressTracker` (console output)
8. `main.cpp` (CLI argument parsing, wiring)

This order lets you test each piece in isolation before the networking complexity enters — the thread pool and chunk math can be verified with fake/mocked work first.

---

## 9. Open Questions / Decisions Still Needed for Phase 1

- Default chunk count (fixed at 8, or scale with file size?)
- Max retries per chunk before failing the whole download
- Progress update frequency (time-based vs byte-count-based)
- Whether to verify final file integrity (checksum) if server provides one (e.g. `Content-MD5` header) — not all servers send this

---

*Next step once this doc is confirmed: hand this to your build tool (Antigravity) starting with `ThreadPool` and `ChunkManager`, since they have no external dependencies and are the easiest to verify first.*

---
---

# Phase 2: GUI Layer (Qt Widgets)

**Goal:** Wrap the working Phase 1 engine in a desktop UI. The engine stays a separate library/module — the GUI is a thin client on top, communicating via Qt signals/slots.

## 2.1 Why keep engine and GUI separate

The Phase 1 classes (`DownloadTask`, `ChunkManager`, etc.) should have **zero Qt dependency** — pure C++/libcurl. This means:
- The engine can be unit-tested and run headless (CLI) forever, even after GUI exists
- GUI becomes replaceable later (e.g. swap Qt Widgets for a web UI) without touching engine code
- Threading stays clean: engine's worker threads emit progress via a thread-safe callback; GUI thread only ever touches Qt objects on the main thread

## 2.2 Bridging engine threads → GUI thread

Qt Widgets are **not thread-safe** — only the main/GUI thread may touch widgets. Since `ChunkWorker`s run on background threads, progress cannot be pushed directly into a `QProgressBar`.

Bridge approach:
- Wrap `DownloadTask` in a `QObject`-derived adapter class, e.g. `DownloadTaskController`
- `ChunkWorker`/`ProgressTracker` callbacks call a thread-safe method that does `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` (or emits a signal — Qt signals across threads are automatically queued when connected with `Qt::AutoConnection`/`Qt::QueuedConnection`)
- GUI slots receive these signals on the main thread and update widgets safely

## 2.3 Core GUI Classes

### `MainWindow`
- Holds a `QListView`/`QTableView` of active + completed downloads (one row per `DownloadTask`)
- Toolbar: "Add URL", "Pause", "Resume", "Cancel", "Remove"
- Menu: settings (default download folder, default chunk count, max concurrent downloads — ties into Phase 3)

### `DownloadItemModel` (subclass of `QAbstractTableModel` or `QAbstractListModel`)
- Backs the list view
- Columns: filename, size, progress %, speed, ETA, status
- Updates when a `DownloadTaskController` emits `progressChanged()`

### `DownloadTaskController` (QObject wrapper per download)
- Owns one `DownloadTask` (engine object)
- Exposes signals: `progressChanged(int taskId, qint64 bytesDone, qint64 total)`, `speedChanged(...)`, `stateChanged(...)`, `finished(...)`, `failed(QString error)`
- Exposes slots: `pause()`, `resume()`, `cancel()` — forwards to the engine object

### `AddDownloadDialog`
- Simple dialog: paste URL, choose destination folder, optionally override chunk count
- On confirm, does the `HEAD` request (via engine) to show file size/filename preview before starting

## 2.4 Data Flow (GUI mode)

1. User pastes URL in `AddDownloadDialog`
2. `MainWindow` creates a `DownloadTaskController`, which creates the underlying `DownloadTask`
3. Controller starts the task; engine spins up `ChunkManager` + `ThreadPool` as before
4. Engine-side progress callbacks → controller's thread-safe signal emission → `DownloadItemModel` updates → view repaints the row
5. Pause/Resume/Cancel buttons call controller slots → forwarded into engine (engine needs `pause()`/`resume()` support added if not already present from Phase 1 — worth revisiting `ChunkManager`/`ThreadPool` to support a cooperative pause flag checked between chunk writes)

## 2.5 Open Questions for Phase 2
- Single window with all downloads, or per-download detail view on double-click?
- Persist window state (which downloads were listed) across app restarts — likely reuse the `.progress.json` sidecar files to repopulate the list on launch
- Dark/light theme — Qt supports platform-native styling by default, decide if custom stylesheet needed

---
---

# Phase 3: Queue Manager (Multi-Download Support)

**Goal:** Support many simultaneous/queued downloads with a global concurrency limit, independent of how many chunks each individual download uses internally.

## 3.1 Why this needs its own layer

Phase 1's `ThreadPool` handles chunks *within one* download. Phase 3 adds a layer *above* that: deciding **which downloads** are allowed to run their chunk-threads right now, so 20 queued downloads with 8 chunks each don't try to spawn 160 threads at once.

## 3.2 Core Classes

### `DownloadQueueManager`
- Holds an ordered list of `DownloadTask`/`DownloadTaskController` objects with states: `Queued`, `Active`, `Paused`, `Completed`, `Failed`
- Config: `maxConcurrentDownloads` (e.g. 3 active at a time)
- When an active download finishes/fails/is removed, promotes the next `Queued` item to `Active`
- Supports reordering (drag to reprioritize) and manual "start now" override (bump a queued item ahead)

### Global vs per-download thread budget
- Introduce a **shared** `ThreadPool` (or a `ThreadPool` per active download, sized smaller, e.g. 4 chunks each instead of 8) so total system threads stay bounded even with several downloads active at once
- Simplest v1 approach: total_worker_threads = maxConcurrentDownloads × chunksPerDownload, hard-capped at some sane number (e.g. 32) — refine later if needed

## 3.3 Data Flow

1. User adds multiple URLs → all enter `Queued` state in `DownloadQueueManager`
2. Manager promotes up to `maxConcurrentDownloads` items to `Active`, starting their engines
3. GUI list shows all items regardless of state, with a status column distinguishing Queued/Active/Paused/Done
4. As active items complete, queue manager auto-promotes next queued item

## 3.4 Open Questions for Phase 3
- Priority levels (high/normal/low) or just simple FIFO + manual reorder?
- Should paused downloads free up their concurrency slot immediately for a queued item to start?

---
---

# Phase 4: Speed Limiter

**Goal:** Cap total (or per-download) bandwidth usage so the download manager doesn't saturate the user's whole connection.

## 4.1 Approach: Token Bucket

Standard, simple, well-understood algorithm:
- A "bucket" holds tokens (1 token ≈ 1 byte, or 1 KB for coarser granularity)
- Tokens are added to the bucket at a fixed rate (the speed limit, e.g. 500 KB/s → 500 tokens/sec)
- Before writing a downloaded block, a worker must "spend" tokens equal to the block size; if insufficient tokens are available, it sleeps until enough accumulate
- Bucket has a max capacity (burst allowance) so short bursts above the limit are tolerated but sustained rate is capped

## 4.2 Where it plugs in

- **Global limiter**: one shared `TokenBucket` instance that *all* `ChunkWorker`s across *all* active downloads draw from — enforces a system-wide cap
- **Per-download limiter**: optionally, each `DownloadTask` can have its own `TokenBucket` in addition to the global one, for "limit this specific download to X KB/s"
- `ChunkWorker`'s libcurl write callback checks the bucket(s) before accepting more data — this is a natural place to throttle, since it's already the per-chunk data path

## 4.3 Core Classes

### `TokenBucket`
- Thread-safe (mutex-protected or atomic-based)
- Methods: `consume(size_t bytes)` (blocks/sleeps if not enough tokens), `refill()` (called periodically, e.g. by a dedicated timer thread or lazily computed based on elapsed time since last consume)

## 4.4 Open Questions for Phase 4
- Should the limiter be togglable at runtime from the GUI (a slider), requiring the bucket's rate to be adjustable live?
- Unlimited-by-default with opt-in cap, or always-on with a high default?

---
---

# Phase 5: Browser Hook (Hardest, Do Last)

**Goal:** When the user clicks a downloadable file link in their browser, intercept it and hand it to this app instead of the browser's built-in downloader.

## 5.1 Why this is hard

Browsers don't let arbitrary native apps intercept downloads directly for security reasons. The real mechanism used by IDM and similar tools has two cooperating pieces:

1. **A browser extension** (WebExtension API — works across Chrome/Edge/Firefox with minor manifest differences) that listens for download events (`chrome.downloads.onCreated` / `browser.downloads.onCreated`, or intercepting link clicks) and captures the URL + metadata
2. **Native messaging host**: a small executable (registered with the OS/browser via a native messaging manifest JSON) that the extension talks to over stdin/stdout using a defined message protocol. This is how the extension hands the captured URL off to your actual C++ download manager app.

## 5.2 Core Pieces

### Browser Extension (JavaScript, WebExtension API)
- `manifest.json` declaring permissions (`downloads`, `nativeMessaging`)
- Background script: listens for new downloads, optionally cancels the browser's own download (`chrome.downloads.cancel`) and instead sends the URL to the native host
- Minimal popup UI optional (toggle "send to Download Manager" on/off)

### Native Messaging Host (small C++ or Python helper executable)
- Registered via a manifest file placed in a browser-specific directory (path differs per OS and browser) pointing to the executable path
- Reads length-prefixed JSON messages from stdin (this is the WebExtension native messaging wire format), extracts the URL
- Either launches the main download manager app with the URL as an argument, or communicates with an already-running instance (e.g. via a local socket / named pipe / D-Bus depending on OS) so a single running app instance receives it

### Main App: "listen for external adds"
- The main download manager app needs an IPC endpoint (local TCP socket on localhost, or platform-native IPC) so the native messaging host can hand off URLs to an already-running instance, rather than spawning duplicate app instances

## 5.3 Data Flow

1. User clicks a download link in browser
2. Extension's background script catches the event, cancels native browser download
3. Extension sends `{url, filename, referrer}` to native messaging host over the native messaging protocol
4. Native host forwards it to the running download manager app via local IPC (or starts the app with the URL as a launch argument if not running)
5. App's `DownloadQueueManager` receives the new URL and queues it exactly like a manually-added download

## 5.4 Open Questions for Phase 5
- Support just Chrome/Edge first (shared manifest format, Chromium-based), add Firefox later (slightly different manifest key names)?
- IPC mechanism for "already running instance" hand-off: local TCP socket (simplest, cross-platform) vs OS-native IPC (more idiomatic per-OS but more code)

---

# Overall Phase Summary

| Phase | Deliverable | Depends on |
|---|---|---|
| 1 | CLI multi-threaded chunked download engine + resume | — |
| 2 | Qt GUI wrapping the engine | Phase 1 |
| 3 | Multi-download queue with concurrency limits | Phase 1 (Phase 2 optional but natural to combine) |
| 4 | Global/per-download speed limiter | Phase 1 |
| 5 | Browser extension + native messaging hook | Phase 1–3 (needs a running app + IPC endpoint to hand URLs to) |

Recommended build order stays: **1 → 2 → 3 → 4 → 5**, since each later phase either extends the engine's public interface (pause/resume, limiter hooks) or wraps it (GUI, queue) without requiring earlier phases to be redesigned — as long as Phase 1's classes are kept engine-only with no Qt/GUI assumptions baked in.
