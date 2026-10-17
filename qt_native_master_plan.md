# Internet Download Manager (IDM) — Qt 6 Native Desktop Architecture Plan
> **Target Platforms:** Windows 10/11 & macOS (Apple Silicon / Intel)  
> **Framework:** C++20 + Qt 6 (Widgets) — 100% Native OS controls, zero browser overhead.

---

## Architecture Blueprint Overview

```
┌────────────────────────────────────────────────────────────────────────┐
│                        IDM NATIVE QT ARCHITECTURE                      │
├────────────────────────────────────────────────────────────────────────┤
│                                                                        │
│  [Browser Extension] ──(Native Messaging / Stdio)──► [NativeHost.exe]  │
│                                                              │         │
│                                                       (Local IPC)      │
│                                                              ▼         │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │                    Qt 6 Presentation Layer                       │  │
│  │  • MainWindow (QMainWindow, QToolBar, QStatusBar)                │  │
│  │  • CategoryTreeWidget (All, Compressed, Video, Music, etc.)      │  │
│  │  • DownloadTableView (QAbstractTableModel + QStyledItemDelegate) │  │
│  │  • Native Context Menu (QMenu: Pause, Resume, Redownload, etc.)  │  │
│  │  • SystemTray (QSystemTrayIcon: Windows Tray & Mac Status Bar)   │  │
│  │  • SegmentedProgressWidget (Custom QPainter 60 FPS Chunk Bar)    │  │
│  │  • Dialogs (DownloadInfoDialog, DownloadStatusDialog, Complete)  │  │
│  └──────────────────────────────────┬───────────────────────────────┘  │
│                                     │ Direct C++ Signals & Slots       │
│                                     ▼ (Zero Serialization, 0ms lag)    │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │                   High-Performance C++ Core                      │  │
│  │  • DownloadQueue & ThreadPool (Concurrency & Scheduling)         │  │
│  │  • DownloadTask & ChunkWorker (libcurl multi-range engine)       │  │
│  │  • ChunkManager & Dynamic Stealing (Donor boundary sync)         │  │
│  │  • FileWriter (Zero-copy sparse writes & preallocation)          │  │
│  │  • AntivirusScanner (Pre-flight URL reputation & in-memory AMSI) │  │
│  │  • ResumeStore (Crash-proof sidecars & ETag validation)          │  │
│  └──────────────────────────────────────────────────────────────────┘  │
└────────────────────────────────────────────────────────────────────────┘
```

---

## Phase 0: Workspace Cleanup & Directory Preparation

### Objectives:
1. Purge all test download files (`Wireshark*.exe`, `.bin`, temp sidecars) to free disk space and eliminate project clutter.
2. Create the clean directory structure for the Qt presentation layer: `src/gui_qt/`.
3. Preserve all working progress zips and core engine files.

### 1. Cleanup Commands (PowerShell):
```powershell
# Remove all Wireshark test files and sidecars
Remove-Item -Path "Wireshark-4.6.9-x64*.exe", "Wireshark-4.6.9-x64*.progress.json" -Force -ErrorAction SilentlyContinue

# Remove leftover test binaries
Remove-Item -Path "paused_dummy.bin" -Force -ErrorAction SilentlyContinue

# Create new Qt source directory
New-Item -ItemType Directory -Path "src/gui_qt" -Force
```

### 2. Files to Retire (Once Qt UI is wired):
* `ui/` directory: All HTML, CSS, and JS files (`index.html`, `download_status.html`, `download_info.html`, `app.js`, `styles.css`).
* `src/gui/MainWindow.cpp` & `src/gui/MainWindow.h` (the old `msedge.exe --app` launcher).

---

## Phase 1: Qt 6 Toolchain & CMake Configuration

### Objectives:
1. Install Qt 6 libraries and developer headers for MinGW / Clang / MSVC.
2. Configure `CMakeLists.txt` using official Qt 6 macros (`find_package`, `qt_add_executable`, `AUTOMOC`, `AUTOUIC`, `AUTORCC`).

### 1. Fast Qt 6 Installation via `aqtinstall`:
```powershell
# Install aqtinstall via Python pip (clean, official community installer)
pip install aqtinstall

# Install Qt 6.7 (Widgets, Gui, Core) for MinGW (Windows)
python -m aqt install-qt windows desktop 6.7.2 win64_mingw -O C:\Qt

# On macOS, install via Homebrew:
# brew install qt@6
```

### 2. CMakeLists.txt Configuration Snippet:
```cmake
cmake_minimum_required(VERSION 3.20)
project(IDM LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable automatic Qt Meta-Object Compiler, UI compiler, and Resource compiler
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Point CMake to Qt directory if not in PATH
list(APPEND CMAKE_PREFIX_PATH "C:/Qt/6.7.2/mingw_64")

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

# Source files for Qt Presentation Layer
set(QT_GUI_SOURCES
    src/gui_qt/main.cpp
    src/gui_qt/MainWindow.cpp
    src/gui_qt/MainWindow.h
    src/gui_qt/DownloadTableModel.cpp
    src/gui_qt/DownloadTableModel.h
    src/gui_qt/DownloadInfoDialog.cpp
    src/gui_qt/DownloadInfoDialog.h
    src/gui_qt/DownloadStatusDialog.cpp
    src/gui_qt/DownloadStatusDialog.h
    src/gui_qt/DownloadCompleteDialog.cpp
    src/gui_qt/DownloadCompleteDialog.h
    src/gui_qt/SegmentedProgressWidget.cpp
    src/gui_qt/SegmentedProgressWidget.h
)

# Build Native IDM Application Target
qt_add_executable(idm_app
    ${QT_GUI_SOURCES}
    ${CORE_SOURCES}
    ${SECURITY_SOURCES}
    ${CONFIG_SOURCES}
)

target_link_libraries(idm_app PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    ${CURL_LIBRARIES}
    nlohmann_json::nlohmann_json
)
```

---

## Phase 2: Core C++ Engine & Qt Bridge (Signals & Slots)

### Objectives:
Connect the multithreaded C++ engine directly to the Qt UI thread with microsecond responsiveness and zero JSON overhead.

### Architecture:
* `DownloadTask` and `DownloadQueue` run on background threads.
* They emit Qt signals or invoke callbacks posted to Qt event loop via `QMetaObject::invokeMethod()`.
* `DownloadTableModel` (`QAbstractTableModel`) handles the list of downloads efficiently without UI stutters.

### Code Snippet (`DownloadTableModel.h`):
```cpp
#pragma once
#include <QAbstractTableModel>
#include <memory>
#include <vector>
#include "../../core/DownloadTask.h"
#include "../../queue/DownloadQueue.h"

class DownloadTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        ColFileName = 0,
        ColSize,
        ColStatus,
        ColProgress,
        ColSpeed,
        ColEta,
        ColCategory,
        ColLastTry,
        ColCount
    };

    explicit DownloadTableModel(idm::DownloadQueue* queue, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

public slots:
    void refreshTasks();
    void onTaskProgressUpdated(const QString& taskId, uint64_t downloadedBytes, double speedBps);

private:
    idm::DownloadQueue* m_queue{nullptr};
    std::vector<std::shared_ptr<idm::DownloadTask>> m_tasks;
};
```

---

## Phase 3: MainWindow, Sidebar & Custom Progress Delegate

### Objectives:
Replicate IDM's iconic desktop layout:
1. **Toolbar:** Large styled icons (*Add URL, Resume, Stop, Stop All, Delete, Options, Scheduler*).
2. **Left Sidebar (`QTreeWidget`):** Categories with auto-filtering (*All, Compressed, Documents, Music, Programs, Video*).
3. **Center Grid (`QTableView`):**
   * Uses `QStyledItemDelegate` to paint authentic dual-tone graphical progress bars with text overlays directly inside table cells.
4. **Status Bar (`QStatusBar`):** Shows active queue state, concurrent worker threads, and global download speed.

### Code Snippet (`ProgressBarDelegate.cpp`):
```cpp
void ProgressBarDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    if (index.column() == DownloadTableModel::ColProgress) {
        int progress = index.data(Qt::UserRole).toInt();
        
        QStyleOptionProgressBar barOption;
        barOption.rect = option.rect.adjusted(4, 4, -4, -4);
        barOption.minimum = 0;
        barOption.maximum = 100;
        barOption.progress = progress;
        barOption.text = QString("%1%").arg(progress);
        barOption.textVisible = true;
        barOption.textAlignment = Qt::AlignCenter;

        QApplication::style()->drawControl(QStyle::CE_ProgressBar, &barOption, painter);
    } else {
        QStyledItemDelegate::paint(painter, option, index);
    }
}
```

---

## Phase 4: Authentic System Dialogs (`QDialog`)

### 1. `DownloadInfoDialog`:
* Triggered before a download begins.
* Pre-fills URL from clipboard, probes size via `HEAD`, determines default category by extension, and suggests unique filename.
* Allows user to change category, browse folder with native `QFileDialog`, or schedule for later.
* Buttons: **Start Download**, **Download Later**, **Cancel**.

### 2. `DownloadStatusDialog`:
* Active download progress window.
* **Tab 1: Download Status:** Shows live transfer rate, ETA, resume capability, and overall progress.
* **Segmented Connection Bar (`SegmentedProgressWidget`):**
  * Custom `QWidget` painting real-time colored chunk boundaries using `QPainter`.
  * Red/Green/Blue color bands indicate chunk download states at 60 FPS.
* **Tab 2: Speed Limiter:** Live slider and spinbox to restrict bandwidth.
* **Tab 3: Options on Completion:** Checkboxes for *Turn off computer* and *Exit IDM*.
* **Security Threat Warning:** Red alert banner that appears only if a threat is flagged, with direct **Quarantine** and **Override & Keep** actions.

### 3. `DownloadCompleteDialog`:
* Pops up on 100% completion.
* Displays file icon, final size, and destination path.
* Buttons: **Open**, **Open Folder** (selects file in Explorer on Windows or Finder on macOS), **Close**.

---

## Phase 5: System Tray & Dual-Minimization (Windows + macOS)

### Objectives:
* Standard minimize (`_`): Minimizes to taskbar, visible in `Alt+Tab`.
* Close (`X`): Minimizes to hidden notification area (system tray on Windows / menu bar status icon on macOS) without interrupting active downloads.

### Code Snippet (`MainWindow.cpp`):
```cpp
void MainWindow::closeEvent(QCloseEvent* event) {
    if (m_settings->get_settings().minimize_to_tray_on_close && m_trayIcon->isVisible()) {
        hide(); // Removes from taskbar and Alt+Tab
        m_trayIcon->showMessage("IDM Active", "Downloads continue in the background.", QSystemTrayIcon::Information, 1500);
        event->ignore();
    } else {
        event->accept();
    }
}
```

---

## Phase 6: Native Context Menus & Keyboard Shortcuts

### Objectives:
* Standard right-click menu (`QMenu`) on table rows:
  * ▷ **Resume** (`Enter`)
  * ⏸ **Pause** (`Space`)
  * ↻ **Redownload**
  * 🗑️ **Delete** (`Delete`)
  * 📂 **Open File**
  * 📁 **Open Folder**
  * ℹ️ **Properties** (`Alt+Enter`)
* Zero browser options (*No Inspect, No QR Code, No AdBlock*).

---

## Phase 7: Settings & Advanced Modules Integration

### Dialog: `SettingsDialog` (`QDialog` with tabs)
1. **General Tab:**
   * Browser integration checkboxes.
   * Dual-minimization preferences.
   * Clipboard monitoring toggle.
2. **File Types & Categories Tab:**
   * Editable category rules (Extensions list + destination folder).
   * Wildcard `*` catch-all for `General`.
3. **Connection & Speed Limiter Tab:**
   * Default connections per task (1 to 32).
   * Global speed limit toggle and rate.
   * Multi-NIC Channel Bonding toggle (Wi-Fi + Ethernet aggregation).
4. **Antivirus & Security Tab:**
   * Path to antivirus executable (`MpCmdRun.exe` or custom scanner).
   * Pre-flight URL reputation toggle.
   * In-memory AMSI inspection toggle.
   * Whitelist manager dialog.
5. **Site Logins (Vault) Tab:**
   * Manage saved passwords and cookies per domain.

---

## Phase 8: Verification & Cross-Platform Testing

| Step | Test Suite / Target | Success Criteria |
| :--- | :--- | :--- |
| **8.1** | Unit Tests (`test_download_task`, `test_download_queue`) | 100% PASS with zero regressions. |
| **8.2** | Native GUI Launch on Windows | Launches instantaneously, uses < 40 MB RAM, authentic Windows styling. |
| **8.3** | Native GUI Launch on macOS | Native Cocoa/Aqua styling, Mac menu bar, Finder reveal. |
| **8.4** | Browser Extension Link Capture | Clicking downloads in Chrome/Edge opens native Qt `DownloadInfoDialog`. |
| **8.5** | Antivirus Quarantine & Override | EICAR detection flags alert; override marks complete without layout glitches. |
