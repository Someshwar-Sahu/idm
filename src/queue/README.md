# Download Queue & System Notifications (`src/queue`)

This module manages multi-task queue scheduling, sequential execution, and OS-native toast notifications.

## Components

| File | Purpose |
| :--- | :--- |
| [`DownloadQueue.h`](DownloadQueue.h) / [`.cpp`](DownloadQueue.cpp) | Manages task lifecycle, queue priorities, maximum concurrent active downloads, schedule timers, and post-queue power execution (system shutdown, sleep, or exit). |
| [`NotificationManager.h`](NotificationManager.h) / [`.cpp`](NotificationManager.cpp) | Native OS notification dispatcher: generates Windows 10/11 Action Center XML toasts with interactive buttons (`OpenFile`, `OpenFolder`), macOS AppleScript banners, and Linux `notify-send`. |
