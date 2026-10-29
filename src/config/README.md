# Application Configuration (`src/config`)

This module manages user settings persistence, default file paths, and category file type definitions.

## Components

| File | Purpose |
| :--- | :--- |
| [`SettingsManager.h`](SettingsManager.h) / [`.cpp`](SettingsManager.cpp) | Manages application preferences (max concurrent connections, speed limiter, cache directory, auto-start, file type associations). Serializes and deserializes configurations to `settings.json`. |
