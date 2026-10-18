#pragma once

#include <QMainWindow>
#include <QTableView>
#include <QTreeWidget>
#include <QToolBar>
#include <QStatusBar>
#include <QLabel>
#include <QSystemTrayIcon>
#include <QSortFilterProxyModel>
#include <QTimer>
#include <map>
#include <memory>

#include "DownloadTableModel.h"
#include "ProgressBarDelegate.h"
#include "../queue/DownloadQueue.h"
#include "../net/NetworkMonitor.h"
#include "../net/PowerMonitor.h"
#include "../security/Vault.h"

class SettingsManager;

class TaskFilterProxyModel : public QSortFilterProxyModel {
public:
    explicit TaskFilterProxyModel(QObject* parent = nullptr) : QSortFilterProxyModel(parent) {}

    void setQueueMode(bool isQueue, const QString& filterText) {
        m_isQueueFilter = isQueue;
        m_filterText = filterText;
        invalidateRowsFilter();
    }

    bool isQueueMode() const { return m_isQueueFilter; }
    QString filterText() const { return m_filterText; }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    bool m_isQueueFilter{false};
    QString m_filterText;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(DownloadQueue* queue, SettingsManager* settings = nullptr, QWidget* parent = nullptr);
    ~MainWindow() override;

    void attach_settings(SettingsManager* settings) { m_settings = settings; }

    std::shared_ptr<DownloadQueue> getQueue(const QString& name = QString()) const;
    void createQueue(const QString& name);
    void deleteQueue(const QString& name);
    void refreshQueuesInSidebar();

public slots:
    void show_tray_notification(const QString& title, const QString& message);
    void restore_or_launch_window();
    void onDownloadIntercepted(const QString& url, const QString& filename, const QString& cookies, const QString& referer, const QString& userAgent, quint64 totalSize = 0);

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;

private slots:
    // Toolbar & Action Slots
    void onAddUrlClicked();
    void onResumeClicked();
    void onPauseClicked();
    void onStopAllClicked();
    void onDeleteClicked();
    void onRemoveFromListClicked();
    void onDeleteFileClicked();
    void onOptionsClicked();
    void onSchedulerClicked();
    void onStartQueueClicked();
    void onStopQueueClicked();

    // Table & Sidebar Interactions
    void onTableDoubleClicked(const QModelIndex& index);
    void onTableContextMenuRequested(const QPoint& pos);
    void onSidebarContextMenuRequested(const QPoint& pos);
    void onCategorySelected(QTreeWidgetItem* current, QTreeWidgetItem* previous);
    void onCreateQueueTriggered();
    void onMoveTaskToQueue(const QString& targetQueue);

    // Advanced Feature Slots
    void onStreamPreviewClicked();
    void onRefreshUrlClicked();
    void onVerifyChecksumClicked();
    void onExtractArchiveClicked();
    void onSiteLoginsClicked();
    void onSetSpeedMode(int mode);

    // Live UI Refresh
    void onRefreshTimerTick();

    // System Tray
    void onTrayIconActivated(QSystemTrayIcon::ActivationReason reason);

private:
    void setupUi();
    void createMenuBar();
    void createToolBar();
    void createSidebar();
    void createTableView();
    void createStatusBar();
    void createTrayIcon();
    std::shared_ptr<DownloadQueue> findQueueForTask(const std::shared_ptr<DownloadTask>& task) const;

    DownloadQueue* m_queue{nullptr};
    std::map<QString, std::shared_ptr<DownloadQueue>> m_queues;
    QTreeWidgetItem* m_queuesRootItem{nullptr};
    SettingsManager* m_settings{nullptr};
    DownloadTableModel* m_tableModel{nullptr};
    TaskFilterProxyModel* m_proxyModel{nullptr};
    ProgressBarDelegate* m_progressDelegate{nullptr};

    // UI Widgets
    QTreeWidget* m_sidebarTree{nullptr};
    QTableView* m_tableView{nullptr};
    QSystemTrayIcon* m_trayIcon{nullptr};
    QTimer* m_refreshTimer{nullptr};

    // Status bar labels
    QLabel* m_statusLeftLabel{nullptr};
    QLabel* m_statusSpeedLabel{nullptr};

    // Intelligence Engines
    std::unique_ptr<NetworkMonitor> m_networkMonitor;
    std::unique_ptr<PowerMonitor> m_powerMonitor;
    idm::Vault m_vault;

    // Active Category / Queue Filter
    QString m_selectedCategory{"All Downloads"};
    bool m_isQueueFilter{false};
};

