# Acceleration Core (`src/core`)

This module forms the multithreaded network engine and data pipeline of the IDM accelerator.

## Components

| File | Purpose |
| :--- | :--- |
| [`DownloadTask.h`](DownloadTask.h) / [`.cpp`](DownloadTask.cpp) | Core task controller. Manages URL probing (HTTP HEAD/GET), range detection, worker thread pool lifecycle, dynamic speed limiting, state persistence, and file finalization. |
| [`ChunkManager.h`](ChunkManager.h) / [`.cpp`](ChunkManager.cpp) | Manages byte-range boundaries, chunk state transitions (`Pending`, `InProgress`, `Done`, `Failed`), and the **Dynamic Work-Stealing Algorithm** (bisection of slowest lagging chunks). |
| [`ChunkWorker.h`](ChunkWorker.h) / [`.cpp`](ChunkWorker.cpp) | Worker thread execution unit using `libcurl`. Implements HTTP byte-range requests (`Range: bytes=X-Y`), connection timeout guards, credential injection, and channel bonding interface binding. |
| [`FileWriter.h`](FileWriter.h) / [`.cpp`](FileWriter.cpp) | Thread-safe, non-blocking disk I/O. Supports sparse file pre-allocation (`SetFileValidData` / `posix_fallocate`) and arbitrary offset writes (`write_at_offset`). |
| [`ProgressTracker.h`](ProgressTracker.h) / [`.cpp`](ProgressTracker.cpp) | Thread-safe EWMA progress tracking, instantaneous/smoothed speed calculations, and real-time ETA estimation. |
| [`ResumeStore.h`](ResumeStore.h) / [`.cpp`](ResumeStore.cpp) | Atomic JSON sidecar serialization (`.progress.json`), crash recovery, 64-bit FNV-1a path hashing, and centralized cache directory management. |
| [`ThreadPool.h`](ThreadPool.h) / [`.cpp`](ThreadPool.cpp) | Lightweight, header-backed worker thread pool for managing asynchronous background tasks. |
| [`Chunk.h`](Chunk.h) | Data structures and enums defining individual chunk offsets, byte counts, and execution states. |
