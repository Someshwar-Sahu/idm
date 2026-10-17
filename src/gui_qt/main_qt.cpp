#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
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
#include <curl/curl.h>

int main(int argc, char* argv[]) {
#ifdef _WIN32
    HANDLE hMutex = CreateMutexA(NULL, TRUE, "Global\\IDMan_SingleInstance_Mutex_Qt");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existingWnd = FindWindowA(NULL, "Internet Download Manager 6.42");
        if (existingWnd) {
            ShowWindow(existingWnd, SW_RESTORE);
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
        return 0;
    }
#endif

    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    app.setApplicationName(QStringLiteral("Internet Download Manager"));
    QIcon appIcon(":/idm.ico");
    if (appIcon.isNull()) appIcon = QIcon("src/gui_qt/idm.ico");
    app.setWindowIcon(appIcon);

    auto settings = std::make_unique<SettingsManager>();
    settings->load("settings.json");

    int maxConcurrent = settings->get_settings().max_concurrent_download;
    if (maxConcurrent <= 0) maxConcurrent = 3;

    auto queue = std::make_unique<DownloadQueue>("Main Download Queue", maxConcurrent);
    queue->start();

    // Start local REST server on 127.0.0.1:8080 for browser extension communication
    auto server = std::make_unique<idm::LocalServer>();
    server->attach_queue(queue.get());
    server->attach_settings(settings.get());

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

    server->set_app_terminator([&app]() {
        QMetaObject::invokeMethod(&app, "quit", Qt::QueuedConnection);
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
    return ret;
}
