#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <iostream>
static LONG WINAPI GlobalCrashHandler(EXCEPTION_POINTERS* ep) {
    std::cerr << "\n============================================\n";
    std::cerr << "[CRASH DETECTED]\n";
    std::cerr << "Exception Code: 0x" << std::hex << ep->ExceptionRecord->ExceptionCode << std::dec << "\n";
    std::cerr << "Exception Address: " << ep->ExceptionRecord->ExceptionAddress << "\n";
    std::cerr << "============================================\n" << std::endl;
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

#include <QApplication>
#include <QIcon>
#include <QMetaObject>
#include <memory>
#include <iostream>
#include "MainWindow.h"
#include "../queue/DownloadQueue.h"
#include "../ipc/LocalServer.h"
#include "../config/SettingsManager.h"
#include "../media/StreamPreviewer.h"
#include "../net/RemoteDashboard.h"
#include "../security/AntivirusScanner.h"
#include <curl/curl.h>

int main(int argc, char* argv[]) {
    // Bug #12 Fix: Initialize libcurl globally before any worker threads or curl_easy_init calls
    curl_global_init(CURL_GLOBAL_ALL);

#ifdef _WIN32
    SetUnhandledExceptionFilter(GlobalCrashHandler);
    HANDLE hMutex = CreateMutexA(NULL, TRUE, "Global\\IDMan_SingleInstance_Mutex_Qt");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existingWnd = FindWindowA(NULL, "Internet Download Manager 6.42");
        if (existingWnd) {
            ShowWindowAsync(existingWnd, SW_RESTORE);
            SetForegroundWindow(existingWnd);
        }
        // Also ping REST API to restore window if minimized to system tray
        CURL* curl = curl_easy_init();
        if (curl) {
            curl_easy_setopt(curl, CURLOPT_URL, "http://127.0.0.1:8080/api/show");
            curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 500L);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](void*, size_t s, size_t n, void*) -> size_t { return s * n; });
            curl_easy_perform(curl);
            curl_easy_cleanup(curl);
        }
        CloseHandle(hMutex);
        curl_global_cleanup();
        return 0;
    }
#endif

    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    app.setApplicationName(QStringLiteral("Internet Download Manager"));
    QIcon appIcon(":/idm.ico");
    if (appIcon.isNull()) appIcon = QIcon("src/gui_qt/idm.ico");
    app.setWindowIcon(appIcon);

    app.setStyleSheet(QStringLiteral(
        "QSpinBox, QTimeEdit {"
        "  background: #27272a; color: #f4f4f5; border: 1px solid #3f3f46; border-radius: 4px;"
        "  padding-left: 6px; padding-right: 20px; padding-top: 3px; padding-bottom: 3px;"
        "  font-size: 12px; min-height: 20px;"
        "}"
        "QSpinBox:focus, QTimeEdit:focus { border-color: #3b82f6; }"
        "QSpinBox::up-button, QTimeEdit::up-button {"
        "  subcontrol-origin: border; subcontrol-position: top right; width: 18px; height: 12px;"
        "  border-left: 1px solid #3f3f46; border-bottom: 1px solid #3f3f46; border-top-right-radius: 3px;"
        "  background: #2f2f35;"
        "}"
        "QSpinBox::up-button:hover, QTimeEdit::up-button:hover { background: #3f3f46; }"
        "QSpinBox::up-button:pressed, QTimeEdit::up-button:pressed { background: #18181b; }"
        "QSpinBox::up-arrow, QTimeEdit::up-arrow {"
        "  width: 0px; height: 0px; border-left: 3px solid transparent; border-right: 3px solid transparent;"
        "  border-bottom: 4px solid #f4f4f5;"
        "}"
        "QSpinBox::down-button, QTimeEdit::down-button {"
        "  subcontrol-origin: border; subcontrol-position: bottom right; width: 18px; height: 12px;"
        "  border-left: 1px solid #3f3f46; border-bottom-right-radius: 3px;"
        "  background: #2f2f35;"
        "}"
        "QSpinBox::down-button:hover, QTimeEdit::down-button:hover { background: #3f3f46; }"
        "QSpinBox::down-button:pressed, QTimeEdit::down-button:pressed { background: #18181b; }"
        "QSpinBox::down-arrow, QTimeEdit::down-arrow {"
        "  width: 0px; height: 0px; border-left: 3px solid transparent; border-right: 3px solid transparent;"
        "  border-top: 4px solid #f4f4f5;"
        "}"
    ));

    auto settings = std::make_unique<SettingsManager>("settings.json");
    settings->load();
    AntivirusScanner::load_whitelist();

    int maxConcurrent = settings->get_settings().max_concurrent_download;
    if (maxConcurrent <= 0) maxConcurrent = 3;

    auto queue = std::make_unique<DownloadQueue>("Main Download Queue", maxConcurrent);
    queue->start();

    // Start local REST server on 127.0.0.1:8080 for browser extension communication
    auto server = std::make_unique<idm::LocalServer>();
    server->attach_queue(queue.get());
    server->attach_settings(settings.get());

    auto dashboard = std::make_unique<idm::RemoteDashboard>();
    dashboard->attach_to_server(*server, queue.get());

    MainWindow mainWindow(queue.get(), settings.get());

    // Wire thread-safe GUI invocation from background LocalServer threads
    server->set_tray_notifier([&mainWindow](const std::string& title, const std::string& message) {
        QMetaObject::invokeMethod(&mainWindow, "show_tray_notification", Qt::QueuedConnection,
                                  Q_ARG(QString, QString::fromStdString(title)),
                                  Q_ARG(QString, QString::fromStdString(message)));
    });

    server->set_window_restorer([&mainWindow]() {
        QMetaObject::invokeMethod(&mainWindow, "restore_or_launch_window", Qt::QueuedConnection);
    });

    server->set_app_terminator([&mainWindow]() {
        QMetaObject::invokeMethod(&mainWindow, "onExitApp", Qt::QueuedConnection);
    });

    server->set_intercept_notifier([&mainWindow](const idm::LocalServer::InterceptRequest& req) {
        QMetaObject::invokeMethod(&mainWindow, "onDownloadIntercepted", Qt::QueuedConnection,
                                  Q_ARG(QString, QString::fromStdString(req.url)),
                                  Q_ARG(QString, QString::fromStdString(req.filename)),
                                  Q_ARG(QString, QString::fromStdString(req.cookies)),
                                  Q_ARG(QString, QString::fromStdString(req.referer)),
                                  Q_ARG(QString, QString::fromStdString(req.user_agent)),
                                  Q_ARG(quint64, static_cast<quint64>(req.total_size)));
    });

    idm::StreamPreviewer::attach_to_server(*server, queue.get());
    server->start("127.0.0.1", 8080);

    mainWindow.show();

    int ret = app.exec();
    server->stop();
    queue->stop();

#ifdef _WIN32
    if (hMutex) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }
#endif
    curl_global_cleanup();
    return ret;
}
