# Media Stream Processing & Preview (`src/media`)

This module enables video/audio link extraction and in-progress media stream previewing.

## Components

| File | Purpose |
| :--- | :--- |
| [`MediaExtractor.h`](MediaExtractor.h) / [`.cpp`](MediaExtractor.cpp) | Direct media stream resolver. Parses video URLs, extracts direct download links from video hosting pages (YouTube, Vimeo, MP4, HLS/M3U8), and queries metadata (title, duration, quality formats). |
| [`StreamPreviewer.h`](StreamPreviewer.h) / [`.cpp`](StreamPreviewer.cpp) | Local HTTP streaming proxy that reads currently downloaded chunks from the sparse file writer, allowing external media players (VLC, Windows Media Player) to preview media files before the download completes. |
