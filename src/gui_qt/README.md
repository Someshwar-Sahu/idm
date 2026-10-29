# Native Desktop GUI (`src/gui_qt`)

This module contains the pure native Qt 6 desktop interface, models, delegates, and dialogs.

## Components

| File | Purpose |
| :--- | :--- |
| [`main_qt.cpp`](main_qt.cpp) | Application entry point. Handles single-instance Mutex/lock file, CLI parameters (`--url`, `--tray`), system tray initialization, and Qt event loop startup. |
| [`MainWindow.h`](MainWindow.h) / [`.cpp`](MainWindow.cpp) | Primary dashboard window. Features categorized sidebar tree, top toolbar, real-time download table, search filter, and system tray integration. |
| [`DownloadTableModel.h`](DownloadTableModel.h) / [`.cpp`](DownloadTableModel.cpp) | High-performance `QAbstractTableModel` implementation supporting sorting, category filtering, search filtering, and live task updates. |
| [`SegmentedProgressWidget.h`](SegmentedProgressWidget.h) / [`.cpp`](SegmentedProgressWidget.cpp) | Custom Qt widget replicating classic IDM segmented chunk visualization, drawing individual thread download blocks with real-time gradient fills. |
| [`ProgressBarDelegate.h`](ProgressBarDelegate.h) / [`.cpp`](ProgressBarDelegate.cpp) | Custom table item delegate rendering inline styled progress bars in the main download table. |
| [`DownloadInfoDialog.h`](DownloadInfoDialog.h) / [`.cpp`](DownloadInfoDialog.cpp) | "Enter New Download URL" dialog allowing URL input, category selection, target directory selection, and immediate or delayed start. |
| [`DownloadStatusDialog.h`](DownloadStatusDialog.h) / [`.cpp`](DownloadStatusDialog.cpp) | Floating live progress dialog featuring speed gauge, ETA, segmented chunk widget, and pause/resume buttons. |
| [`DownloadCompleteDialog.h`](DownloadCompleteDialog.h) / [`.cpp`](DownloadCompleteDialog.cpp) | Completion popup offering "Open File" and "Open Folder" actions upon download finalization. |
| [`DownloadCancelledDialog.h`](DownloadCancelledDialog.h) / [`.cpp`](DownloadCancelledDialog.cpp) | Cancellation prompt with option to delete temporary downloaded files or preserve resume state. |
| [`OptionsDialog.h`](OptionsDialog.h) / [`.cpp`](OptionsDialog.cpp) | Comprehensive settings dialog (General, File Types, Save To, Connection limits, Proxy, and QoS tuning). |
| [`SchedulerDialog.h`](SchedulerDialog.h) / [`.cpp`](SchedulerDialog.cpp) | Queue scheduler configuration: start/stop times, weekly recurrence, and post-download power triggers. |
| [`SecurityWarningDialog.h`](SecurityWarningDialog.h) / [`.cpp`](SecurityWarningDialog.cpp) | Security alert dialog prompt for malicious or untrusted files, offering quarantine or explicit override. |
| [`resources.qrc`](resources.qrc) / [`resources.rc`](resources.rc) | Qt resource manifest and Windows PE binary icon metadata. |
