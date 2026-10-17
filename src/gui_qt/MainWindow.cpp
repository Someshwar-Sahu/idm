#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "MainWindow.h"
#include "DownloadTableModel.h"
#include "ProgressBarDelegate.h"
#include "DownloadInfoDialog.h"
#include "DownloadStatusDialog.h"
#include "DownloadCompleteDialog.h"
#include "SchedulerDialog.h"
#include "OptionsDialog.h"
#include "../queue/DownloadQueue.h"
#include "../config/SettingsManager.h"

#include <QMenuBar>
#include <QMenu>
#include <QToolBar>
#include <QToolButton>
#include <QStatusBar>
#include <QSplitter>
#include <QHeaderView>
#include <QMessageBox>
#include <QFileInfo>
#include <QInputDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QClipboard>
#include <QGuiApplication>
#include <QCloseEvent>
#include <QStyle>
#include <QPainter>
#include <QColor>
#include <sstream>
#include <iomanip>
#include <filesystem>

namespace {

QIcon loadIdmIcon(const QString& name) {
    QIcon icon;
    icon.addFile(QStringLiteral(":/icons/") + name + QStringLiteral(".png"), QSize(32, 32), QIcon::Normal, QIcon::Off);
    icon.addFile(QStringLiteral(":/icons/") + name + QStringLiteral("_hot.png"), QSize(32, 32), QIcon::Active, QIcon::Off);
    if (icon.isNull()) {
        icon.addFile(QStringLiteral("src/gui_qt/icons/") + name + QStringLiteral(".png"), QSize(32, 32), QIcon::Normal, QIcon::Off);
        icon.addFile(QStringLiteral("src/gui_qt/icons/") + name + QStringLiteral("_hot.png"), QSize(32, 32), QIcon::Active, QIcon::Off);
    }
    return icon;
}

QString formatBytes(uint64_t bytes) {
    if (bytes == 0) return QStringLiteral("0 B");
    static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unitIndex = 0;
    double size = static_cast<double>(bytes);
    while (size >= 1024.0 && unitIndex < 4) {
        size /= 1024.0;
        unitIndex++;
    }
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(unitIndex == 0 ? 0 : 2) << size << " " << units[unitIndex];
    return QString::fromStdString(ss.str());
}

} // anonymous namespace

MainWindow::MainWindow(DownloadQueue* queue, SettingsManager* settings, QWidget* parent)
    : QMainWindow(parent), m_queue(queue), m_settings(settings) {
    setWindowTitle(QStringLiteral("Internet Download Manager 6.42"));
    resize(1100, 620);
    QIcon appIcon(":/idm.ico");
    if (appIcon.isNull()) appIcon = QIcon("src/gui_qt/idm.ico");
    setWindowIcon(appIcon);

    if (queue) {
        m_queues[QStringLiteral("Main Download Queue")] = std::shared_ptr<DownloadQueue>(queue, [](DownloadQueue*){});
    } else {
        m_queues[QStringLiteral("Main Download Queue")] = std::make_shared<DownloadQueue>("Main Download Queue", 3);
    }
    m_queues[QStringLiteral("Synchronization Queue")] = std::make_shared<DownloadQueue>("Synchronization Queue", 1);

    setupUi();
    createTrayIcon();

    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, &MainWindow::onRefreshTimerTick);
    m_refreshTimer->start(100);
}

MainWindow::~MainWindow() {
    if (m_refreshTimer) {
        m_refreshTimer->stop();
    }
}

void MainWindow::setupUi() {
    setStyleSheet(QStringLiteral(
        "QMainWindow { background: #18181b; color: #f4f4f5; }"
        "QMenuBar { background: #18181b; color: #e4e4e7; font-size: 12px; border-bottom: 1px solid #27272a; padding: 2px 4px; }"
        "QMenuBar::item { padding: 4px 8px; border-radius: 4px; }"
        "QMenuBar::item:selected { background: #27272a; }"
        "QMenu { background: #1f1f23; color: #f4f4f5; border: 1px solid #2e2e33; border-radius: 6px; padding: 4px; }"
        "QMenu::item { padding: 6px 24px; border-radius: 4px; font-size: 12px; }"
        "QMenu::item:selected { background: #2563eb; color: #ffffff; }"
        "QMenu::separator { height: 1px; background: #2e2e33; margin: 4px 0; }"
        "QSplitter::handle { background: #27272a; width: 1px; }"
        "QTreeWidget { background: #18181b; color: #e4e4e7; border: none; border-right: 1px solid #27272a; font-size: 12px; padding: 6px; }"
        "QTreeWidget::item { padding: 5px 8px; margin: 1px 0; border-radius: 5px; }"
        "QTreeWidget::item:hover { background: #27272a; }"
        "QTreeWidget::item:selected { background: #27272a; color: #38bdf8; font-weight: 600; border-left: 2px solid #38bdf8; }"
        "QTableView { background: #18181b; alternate-background-color: #1e1e22; color: #f4f4f5; border: none; gridline-color: #27272a; font-size: 12px; selection-background-color: #1e293b; selection-color: #38bdf8; }"
        "QHeaderView::section { background: #202025; color: #a1a1aa; border: none; border-right: 1px solid #27272a; border-bottom: 1px solid #27272a; padding: 6px 8px; font-weight: 600; font-size: 11px; }"
        "QStatusBar { background: #18181b; color: #a1a1aa; border-top: 1px solid #27272a; font-size: 11px; padding: 2px 8px; }"
    ));

    createMenuBar();
    createToolBar();
    createStatusBar();

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    createTableView();
    createSidebar();

    splitter->addWidget(m_sidebarTree);
    splitter->addWidget(m_tableView);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({210, 890});

    setCentralWidget(splitter);
}

void MainWindow::createMenuBar() {
    QMenu* tasksMenu = menuBar()->addMenu(QStringLiteral("&Tasks"));
    tasksMenu->addAction(style()->standardIcon(QStyle::SP_FileIcon), QStringLiteral("Add &new download..."), QKeySequence::New, this, &MainWindow::onAddUrlClicked);
    tasksMenu->addAction(style()->standardIcon(QStyle::SP_MediaPlay), QStringLiteral("&Resume download"), this, &MainWindow::onResumeClicked);
    tasksMenu->addAction(style()->standardIcon(QStyle::SP_MediaPause), QStringLiteral("&Stop download"), this, &MainWindow::onPauseClicked);
    tasksMenu->addAction(style()->standardIcon(QStyle::SP_MediaStop), QStringLiteral("Stop &all"), this, &MainWindow::onStopAllClicked);
    tasksMenu->addSeparator();
    tasksMenu->addAction(style()->standardIcon(QStyle::SP_TrashIcon), QStringLiteral("Remove from IDM (keep file)"), this, &MainWindow::onRemoveFromListClicked);
    tasksMenu->addAction(style()->standardIcon(QStyle::SP_DialogDiscardButton), QStringLiteral("Delete file from disk..."), this, &MainWindow::onDeleteFileClicked);
    tasksMenu->addSeparator();
    tasksMenu->addAction(style()->standardIcon(QStyle::SP_DialogCloseButton), QStringLiteral("E&xit"), QKeySequence::Quit, qApp, &QCoreApplication::quit);

    QMenu* downloadsMenu = menuBar()->addMenu(QStringLiteral("&Downloads"));
    downloadsMenu->addAction(style()->standardIcon(QStyle::SP_FileDialogDetailedView), QStringLiteral("&Options..."), this, &MainWindow::onOptionsClicked);
    downloadsMenu->addAction(style()->standardIcon(QStyle::SP_ArrowRight), QStringLiteral("&Scheduler..."), this, &MainWindow::onSchedulerClicked);
    downloadsMenu->addSeparator();
    downloadsMenu->addAction(style()->standardIcon(QStyle::SP_MediaPlay), QStringLiteral("Start &Queue"), this, &MainWindow::onStartQueueClicked);
    downloadsMenu->addAction(style()->standardIcon(QStyle::SP_MediaPause), QStringLiteral("Stop Q&ueue"), this, &MainWindow::onStopQueueClicked);

    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
    viewMenu->addAction(QStringLiteral("&Refresh"), QKeySequence::Refresh, this, [this]() {
        if (m_tableModel) m_tableModel->refreshTasks();
    });

    QMenu* helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));
    helpMenu->addAction(QStringLiteral("&About IDM..."), this, [this]() {
        QMessageBox::about(this, QStringLiteral("About Internet Download Manager"),
                           QStringLiteral("<b>Internet Download Manager (Native Qt)</b><br>"
                                          "Version 6.42 C++ Engine<br>"
                                          "High-performance multithreaded download accelerator."));
    });
}

void MainWindow::createToolBar() {
    QToolBar* toolbar = addToolBar(QStringLiteral("MainToolbar"));
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(28, 28));
    toolbar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);

    toolbar->setStyleSheet(QStringLiteral(
        "QToolBar { background: #1c1c20; border-bottom: 1px solid #27272a; spacing: 4px; padding: 4px 8px; }"
        "QToolButton { background: transparent; color: #e4e4e7; border: 1px solid transparent; border-radius: 6px; "
        "              padding: 6px 10px; font-size: 11px; font-weight: 500; min-width: 56px; }"
        "QToolButton:hover { background: #27272a; border-color: #3f3f46; color: #ffffff; }"
        "QToolButton:pressed { background: #18181b; border-color: #3b82f6; }"
    ));

    auto* actAdd = toolbar->addAction(loadIdmIcon(QStringLiteral("add_url")), QStringLiteral("Add URL"), this, &MainWindow::onAddUrlClicked);
    actAdd->setToolTip(QStringLiteral("Start a new download"));

    auto* actResume = toolbar->addAction(loadIdmIcon(QStringLiteral("resume")), QStringLiteral("Resume"), this, &MainWindow::onResumeClicked);
    actResume->setToolTip(QStringLiteral("Resume selected download"));

    auto* actStop = toolbar->addAction(loadIdmIcon(QStringLiteral("stop")), QStringLiteral("Stop"), this, &MainWindow::onPauseClicked);
    actStop->setToolTip(QStringLiteral("Pause selected download"));

    auto* actStopAll = toolbar->addAction(loadIdmIcon(QStringLiteral("stop_all")), QStringLiteral("Stop All"), this, &MainWindow::onStopAllClicked);
    actStopAll->setToolTip(QStringLiteral("Pause all active downloads"));

    auto* actDelete = toolbar->addAction(loadIdmIcon(QStringLiteral("delete")), QStringLiteral("Delete"), this, &MainWindow::onDeleteClicked);
    actDelete->setToolTip(QStringLiteral("Delete or remove selected download"));
    auto* deleteButton = qobject_cast<QToolButton*>(toolbar->widgetForAction(actDelete));
    if (deleteButton) {
        deleteButton->setPopupMode(QToolButton::MenuButtonPopup);
        auto* delMenu = new QMenu(deleteButton);
        delMenu->addAction(style()->standardIcon(QStyle::SP_TrashIcon), QStringLiteral("Remove from IDM (Keep file)"), this, &MainWindow::onRemoveFromListClicked);
        delMenu->addAction(style()->standardIcon(QStyle::SP_DialogDiscardButton), QStringLiteral("Delete file from disk..."), this, &MainWindow::onDeleteFileClicked);
        deleteButton->setMenu(delMenu);
    }

    toolbar->addSeparator();

    auto* actStartQueue = toolbar->addAction(loadIdmIcon(QStringLiteral("resume")), QStringLiteral("Start Queue"), this, &MainWindow::onStartQueueClicked);
    actStartQueue->setToolTip(QStringLiteral("Start queue downloads (respecting queue concurrency limit)"));

    auto* actStopQueue = toolbar->addAction(loadIdmIcon(QStringLiteral("stop")), QStringLiteral("Stop Queue"), this, &MainWindow::onStopQueueClicked);
    actStopQueue->setToolTip(QStringLiteral("Stop downloads in current queue"));

    toolbar->addSeparator();

    auto* actOptions = toolbar->addAction(loadIdmIcon(QStringLiteral("options")), QStringLiteral("Options"), this, &MainWindow::onOptionsClicked);
    actOptions->setToolTip(QStringLiteral("Configure settings and connections"));

    auto* actScheduler = toolbar->addAction(loadIdmIcon(QStringLiteral("scheduler")), QStringLiteral("Scheduler"), this, &MainWindow::onSchedulerClicked);
    actScheduler->setToolTip(QStringLiteral("Queue scheduler and timer"));
}

void MainWindow::createSidebar() {
    m_sidebarTree = new QTreeWidget(this);
    m_sidebarTree->setHeaderHidden(true);
    m_sidebarTree->setRootIsDecorated(true);
    m_sidebarTree->setAnimated(true);
    m_sidebarTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_sidebarTree, &QTreeWidget::customContextMenuRequested, this, &MainWindow::onSidebarContextMenuRequested);

    auto* catRoot = new QTreeWidgetItem(m_sidebarTree, QStringList(QStringLiteral("Categories")));
    catRoot->setExpanded(true);
    catRoot->setFlags(catRoot->flags() & ~Qt::ItemIsSelectable);

    const QStringList categories = {
        QStringLiteral("All Downloads"),
        QStringLiteral("Compressed"),
        QStringLiteral("Documents"),
        QStringLiteral("Music"),
        QStringLiteral("Programs"),
        QStringLiteral("Video")
    };
    const QStringList catIcons = {
        QStringLiteral("cat_all"),
        QStringLiteral("cat_compressed"),
        QStringLiteral("cat_documents"),
        QStringLiteral("cat_music"),
        QStringLiteral("cat_programs"),
        QStringLiteral("cat_video")
    };

    for (int i = 0; i < categories.size(); ++i) {
        auto* item = new QTreeWidgetItem(catRoot, QStringList(categories[i]));
        item->setIcon(0, loadIdmIcon(catIcons[i]));
    }

    m_queuesRootItem = new QTreeWidgetItem(m_sidebarTree, QStringList(QStringLiteral("Queues")));
    m_queuesRootItem->setExpanded(true);
    m_queuesRootItem->setFlags(m_queuesRootItem->flags() & ~Qt::ItemIsSelectable);

    refreshQueuesInSidebar();

    connect(m_sidebarTree, &QTreeWidget::currentItemChanged, this, &MainWindow::onCategorySelected);

    if (catRoot->childCount() > 0) {
        m_sidebarTree->setCurrentItem(catRoot->child(0));
    }
}

void MainWindow::createTableView() {
    m_tableModel = new DownloadTableModel(m_queue, this);
    m_tableModel->attachQueues(&m_queues);

    m_proxyModel = new QSortFilterProxyModel(this);
    m_proxyModel->setSourceModel(m_tableModel);
    m_proxyModel->setFilterKeyColumn(DownloadTableModel::ColCategory);
    m_proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);

    m_tableView = new QTableView(this);
    m_tableView->setModel(m_proxyModel);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableView->setAlternatingRowColors(true);
    m_tableView->setSortingEnabled(true);
    m_tableView->verticalHeader()->setVisible(false);
    m_tableView->setContextMenuPolicy(Qt::CustomContextMenu);

    m_progressDelegate = new ProgressBarDelegate(this);
    m_tableView->setItemDelegateForColumn(DownloadTableModel::ColProgress, m_progressDelegate);

    m_tableView->horizontalHeader()->setSectionResizeMode(DownloadTableModel::ColFileName, QHeaderView::Stretch);
    m_tableView->setColumnWidth(DownloadTableModel::ColSize, 85);
    m_tableView->setColumnWidth(DownloadTableModel::ColStatus, 100);
    m_tableView->setColumnWidth(DownloadTableModel::ColProgress, 140);
    m_tableView->setColumnWidth(DownloadTableModel::ColSpeed, 90);
    m_tableView->setColumnWidth(DownloadTableModel::ColEta, 90);
    m_tableView->setColumnWidth(DownloadTableModel::ColCategory, 90);
    m_tableView->setColumnWidth(DownloadTableModel::ColQueue, 120);
    m_tableView->setColumnWidth(DownloadTableModel::ColLastTry, 80);

    connect(m_tableView, &QTableView::doubleClicked, this, &MainWindow::onTableDoubleClicked);
    connect(m_tableView, &QTableView::customContextMenuRequested, this, &MainWindow::onTableContextMenuRequested);
}

void MainWindow::createStatusBar() {
    m_statusLeftLabel = new QLabel(QStringLiteral(" Ready"), this);
    m_statusSpeedLabel = new QLabel(QStringLiteral("Speed: 0 KB/s  |  Threads: 0 "), this);

    statusBar()->addWidget(m_statusLeftLabel, 1);
    statusBar()->addPermanentWidget(m_statusSpeedLabel);
}

void MainWindow::createTrayIcon() {
    QIcon trayIcon(":/idm.ico");
    if (trayIcon.isNull()) trayIcon = QIcon("src/gui_qt/idm.ico");
    m_trayIcon = new QSystemTrayIcon(trayIcon, this);

    auto* trayMenu = new QMenu(this);
    trayMenu->addAction(QStringLiteral("Open IDM"), this, [this]() {
        showNormal();
        activateWindow();
    });
    trayMenu->addAction(QStringLiteral("Add Download..."), this, &MainWindow::onAddUrlClicked);
    trayMenu->addSeparator();
    trayMenu->addAction(QStringLiteral("Stop All"), this, &MainWindow::onStopAllClicked);
    trayMenu->addSeparator();
    trayMenu->addAction(QStringLiteral("Exit"), qApp, &QCoreApplication::quit);

    m_trayIcon->setContextMenu(trayMenu);
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &MainWindow::onTrayIconActivated);
    m_trayIcon->show();
}

std::shared_ptr<DownloadQueue> MainWindow::getQueue(const QString& name) const {
    if (name.isEmpty() || name == QStringLiteral("Main Download Queue")) {
        auto it = m_queues.find(QStringLiteral("Main Download Queue"));
        if (it != m_queues.end()) return it->second;
    }
    auto it = m_queues.find(name);
    if (it != m_queues.end()) return it->second;
    return nullptr;
}

std::shared_ptr<DownloadQueue> MainWindow::findQueueForTask(const std::shared_ptr<DownloadTask>& task) const {
    if (!task) return nullptr;
    std::string qName = task->get_queue_name();
    if (!qName.empty()) {
        auto it = m_queues.find(QString::fromStdString(qName));
        if (it != m_queues.end()) return it->second;
    }
    for (const auto& [name, q] : m_queues) {
        if (!q) continue;
        for (const auto& t : q->get_tasks()) {
            if (t && t->get_destination_filepath() == task->get_destination_filepath()) {
                return q;
            }
        }
    }
    return getQueue();
}

void MainWindow::createQueue(const QString& name) {
    QString qName = name.trimmed();
    if (qName.isEmpty()) return;
    if (m_queues.find(qName) != m_queues.end()) {
        QMessageBox::warning(this, QStringLiteral("Queue Exists"), QStringLiteral("A queue with this name already exists."));
        return;
    }
    m_queues[qName] = std::make_shared<DownloadQueue>(qName.toStdString(), 1);
    refreshQueuesInSidebar();
}

void MainWindow::deleteQueue(const QString& name) {
    if (name == QStringLiteral("Main Download Queue") || name == QStringLiteral("Synchronization Queue")) {
        QMessageBox::warning(this, QStringLiteral("Cannot Delete"), QStringLiteral("Default queues cannot be deleted."));
        return;
    }
    auto it = m_queues.find(name);
    if (it != m_queues.end()) {
        auto mainQ = getQueue(QStringLiteral("Main Download Queue"));
        if (it->second && mainQ) {
            auto tasks = it->second->get_tasks();
            for (auto& t : tasks) {
                if (t) {
                    it->second->remove_task(t->get_destination_filepath());
                    t->set_queue_name("Main Download Queue");
                    mainQ->add_task(t);
                }
            }
            it->second->stop();
        }
        m_queues.erase(it);
        refreshQueuesInSidebar();
        if (m_tableModel) m_tableModel->refreshTasks();
    }
}

void MainWindow::refreshQueuesInSidebar() {
    if (!m_queuesRootItem) return;
    qDeleteAll(m_queuesRootItem->takeChildren());

    for (const auto& [qName, q] : m_queues) {
        auto* qItem = new QTreeWidgetItem(m_queuesRootItem, QStringList(qName));
        qItem->setIcon(0, loadIdmIcon(QStringLiteral("cat_queue")));
    }
    m_queuesRootItem->setExpanded(true);
}

void MainWindow::onCreateQueueTriggered() {
    bool ok = false;
    QString name = QInputDialog::getText(
        this,
        QStringLiteral("Create Queue"),
        QStringLiteral("Enter new queue name:"),
        QLineEdit::Normal,
        QStringLiteral("New Download Queue"),
        &ok
    );
    if (ok && !name.trimmed().isEmpty()) {
        createQueue(name.trimmed());
    }
}

void MainWindow::onMoveTaskToQueue(const QString& targetQueue) {
    auto current = m_tableView->currentIndex();
    if (!current.isValid()) return;
    int sourceRow = m_proxyModel->mapToSource(current).row();
    auto task = m_tableModel->getTask(sourceRow);
    if (!task) return;

    QString oldQueueName = QString::fromStdString(task->get_queue_name());
    if (oldQueueName == targetQueue) return;

    auto oldQ = findQueueForTask(task);
    auto newQ = getQueue(targetQueue);
    if (!newQ) return;

    if (oldQ) {
        oldQ->remove_task(task->get_destination_filepath());
    }
    task->set_queue_name(targetQueue.toStdString());
    newQ->add_task(task);
    if (m_tableModel) m_tableModel->refreshTasks();
}

void MainWindow::onSidebarContextMenuRequested(const QPoint& pos) {
    QTreeWidgetItem* item = m_sidebarTree->itemAt(pos);
    if (!item) return;

    if (item == m_queuesRootItem) {
        QMenu menu(this);
        menu.addAction(style()->standardIcon(QStyle::SP_FileDialogNewFolder), QStringLiteral("Create new queue..."), this, &MainWindow::onCreateQueueTriggered);
        menu.exec(m_sidebarTree->viewport()->mapToGlobal(pos));
        return;
    }

    if (item->parent() == m_queuesRootItem) {
        QString qName = item->text(0);
        auto q = getQueue(qName);

        QMenu menu(this);
        auto* actStart = menu.addAction(style()->standardIcon(QStyle::SP_MediaPlay), QStringLiteral("Start queue"));
        connect(actStart, &QAction::triggered, this, [q]() {
            if (q) q->start();
        });

        auto* actStop = menu.addAction(style()->standardIcon(QStyle::SP_MediaPause), QStringLiteral("Stop queue"));
        connect(actStop, &QAction::triggered, this, [q]() {
            if (q) q->pause();
        });

        menu.addSeparator();

        auto* actSchedule = menu.addAction(loadIdmIcon(QStringLiteral("scheduler")), QStringLiteral("Schedule queue..."));
        connect(actSchedule, &QAction::triggered, this, [this, qName]() {
            SchedulerDialog dlg(m_queues, qName, this);
            connect(&dlg, &SchedulerDialog::queueCreated, this, [this](const QString&) {
                refreshQueuesInSidebar();
            });
            connect(&dlg, &SchedulerDialog::queueDeleted, this, [this](const QString&) {
                refreshQueuesInSidebar();
            });
            connect(&dlg, &SchedulerDialog::queueScheduleChanged, this, [this](const QString&) {
                if (m_tableModel) m_tableModel->refreshTasks();
            });
            dlg.exec();
            if (m_tableModel) m_tableModel->refreshTasks();
        });

        menu.addSeparator();

        auto* actDelete = menu.addAction(style()->standardIcon(QStyle::SP_TrashIcon), QStringLiteral("Delete queue"));
        bool canDelete = (qName != QStringLiteral("Main Download Queue") && qName != QStringLiteral("Synchronization Queue"));
        actDelete->setEnabled(canDelete);
        connect(actDelete, &QAction::triggered, this, [this, qName]() {
            deleteQueue(qName);
        });

        menu.addSeparator();
        menu.addAction(style()->standardIcon(QStyle::SP_FileDialogNewFolder), QStringLiteral("Create new queue..."), this, &MainWindow::onCreateQueueTriggered);

        menu.exec(m_sidebarTree->viewport()->mapToGlobal(pos));
    }
}

void MainWindow::onCategorySelected(QTreeWidgetItem* current, QTreeWidgetItem*) {
    if (!current || !m_proxyModel) return;
    QString cat = current->text(0);
    m_selectedCategory = cat;

    if (current->parent() == m_queuesRootItem) {
        m_isQueueFilter = true;
        m_proxyModel->setFilterKeyColumn(DownloadTableModel::ColQueue);
        m_proxyModel->setFilterFixedString(cat);
    } else {
        m_isQueueFilter = false;
        m_proxyModel->setFilterKeyColumn(DownloadTableModel::ColCategory);
        if (cat == QStringLiteral("All Downloads") || cat == QStringLiteral("Categories") || cat == QStringLiteral("Queues")) {
            m_proxyModel->setFilterRegularExpression(QString());
        } else {
            m_proxyModel->setFilterFixedString(cat);
        }
    }
}

void MainWindow::onAddUrlClicked() {
    QString clipText = QGuiApplication::clipboard()->text().trimmed();
    QString defaultUrl = (clipText.startsWith(QStringLiteral("http://")) || clipText.startsWith(QStringLiteral("https://"))) ? clipText : QString();
    QStringList qNames;
    for (const auto& [qName, q] : m_queues) qNames << qName;
    DownloadInfoDialog dlg(defaultUrl, qNames, m_settings, this);
    if (dlg.exec() == QDialog::Accepted) {
        std::string url = dlg.getUrl();
        std::string fullPath = dlg.getDestinationPath();
        bool startNow = dlg.shouldStartImmediately();
        std::string qName = dlg.getSelectedQueue();
        if (url.empty() || fullPath.empty()) return;

        int connections = m_settings ? m_settings->get_settings().default_connections_per_task : 8;
        uint64_t speedLimit = (m_settings && m_settings->get_settings().speed_mode == SpeedLimiterMode::ManualCap)
                              ? m_settings->get_settings().manual_speed_limit_bps : 0;
        std::string cacheDir = m_settings ? m_settings->get_settings().cache_directory : "";
        bool avEnabled = m_settings ? m_settings->get_settings().antivirus_scan_enabled : true;

        auto task = std::make_shared<DownloadTask>(url, fullPath, connections, speedLimit, "", "", "", cacheDir);
        task->set_queue_name(qName);
        task->set_antivirus_scan_enabled(avEnabled);
        if (dlg.isSecurityOverride()) {
            task->set_security_override(true);
        }
        task->set_status(DownloadStatus::NotStarted);
        auto targetQueue = getQueue(QString::fromStdString(qName));
        if (targetQueue) {
            targetQueue->add_task(task);
            if (startNow) {
                targetQueue->start();
            }
        } else if (m_queue) {
            m_queue->add_task(task);
            if (startNow) m_queue->start();
        }
        m_tableModel->addTask(task);
        m_tableModel->refreshTasks();
        if (startNow) {
            auto* statusDlg = new DownloadStatusDialog(task, targetQueue ? targetQueue.get() : m_queue, this);
            statusDlg->show();
        }
    }
}

void MainWindow::onResumeClicked() {
    auto current = m_tableView->currentIndex();
    if (!current.isValid()) return;
    int sourceRow = m_proxyModel->mapToSource(current).row();
    auto task = m_tableModel->getTask(sourceRow);
    if (!task) return;

    auto q = findQueueForTask(task);
    if (q) {
        q->resume_task(task->get_destination_filepath());
        q->start();
    }
    m_tableModel->refreshTasks();
}

void MainWindow::onPauseClicked() {
    auto current = m_tableView->currentIndex();
    if (!current.isValid()) return;
    int sourceRow = m_proxyModel->mapToSource(current).row();
    auto task = m_tableModel->getTask(sourceRow);
    if (!task) return;

    auto q = findQueueForTask(task);
    if (q) {
        q->pause_task(task->get_destination_filepath());
    }
    m_tableModel->refreshTasks();
}

void MainWindow::onStopAllClicked() {
    for (const auto& [name, q] : m_queues) {
        if (q) q->pause();
    }
    if (m_tableModel) m_tableModel->refreshTasks();
}

void MainWindow::onDeleteClicked() {
    auto current = m_tableView->currentIndex();
    if (!current.isValid()) return;
    int sourceRow = m_proxyModel->mapToSource(current).row();
    auto task = m_tableModel->getTask(sourceRow);
    if (!task) return;

    QString fileName = QFileInfo(QString::fromStdString(task->get_destination_filepath())).fileName();
    if (fileName.isEmpty()) fileName = QStringLiteral("this download");

    QMessageBox msgBox(this);
    msgBox.setWindowTitle(QStringLiteral("Delete Download"));
    msgBox.setText(QStringLiteral("<b>Choose how you want to remove \"%1\":</b>").arg(fileName));
    msgBox.setInformativeText(QStringLiteral(
        "• <b>Remove from IDM:</b> Removes item from the list only. The file on disk will be kept intact.\n\n"
        "• <b>Delete File from Disk:</b> Permanently deletes the file from disk and removes it from IDM."
    ));
    msgBox.setIcon(QMessageBox::Question);

    QPushButton* removeListBtn = msgBox.addButton(QStringLiteral("Remove from IDM"), QMessageBox::ActionRole);
    QPushButton* deleteFileBtn = msgBox.addButton(QStringLiteral("Delete File from Disk"), QMessageBox::DestructiveRole);
    msgBox.addButton(QMessageBox::Cancel);
    msgBox.setDefaultButton(removeListBtn);

    msgBox.exec();

    if (msgBox.clickedButton() == removeListBtn) {
        onRemoveFromListClicked();
    } else if (msgBox.clickedButton() == deleteFileBtn) {
        if (task->get_status() == DownloadStatus::Downloading || task->get_status() == DownloadStatus::Probing) {
            task->pause();
        }
        auto q = findQueueForTask(task);
        if (q) {
            q->remove_task(task->get_destination_filepath());
        }
        std::string destPath = task->get_destination_filepath();
        m_tableModel->removeTask(destPath);

        std::error_code ec;
        if (!destPath.empty()) {
            std::filesystem::remove(destPath, ec);
            std::string sidecar = destPath + ".idm_sidecar";
            std::filesystem::remove(sidecar, ec);
        }
        statusBar()->showMessage(QStringLiteral("Permanently deleted '%1' from disk and removed from IDM").arg(fileName), 3000);
    }
}

void MainWindow::onRemoveFromListClicked() {
    auto current = m_tableView->currentIndex();
    if (!current.isValid()) return;
    int sourceRow = m_proxyModel->mapToSource(current).row();
    auto task = m_tableModel->getTask(sourceRow);
    if (!task) return;

    QString fileName = QFileInfo(QString::fromStdString(task->get_destination_filepath())).fileName();
    if (fileName.isEmpty()) fileName = QStringLiteral("this download");

    if (task->get_status() == DownloadStatus::Downloading || task->get_status() == DownloadStatus::Probing) {
        task->pause();
    }
    auto q = findQueueForTask(task);
    if (q) {
        q->remove_task(task->get_destination_filepath());
    }
    m_tableModel->removeTask(task->get_destination_filepath());
    statusBar()->showMessage(QStringLiteral("Removed '%1' from IDM (file kept on disk)").arg(fileName), 3000);
}

void MainWindow::onDeleteFileClicked() {
    auto current = m_tableView->currentIndex();
    if (!current.isValid()) return;
    int sourceRow = m_proxyModel->mapToSource(current).row();
    auto task = m_tableModel->getTask(sourceRow);
    if (!task) return;

    QString fileName = QFileInfo(QString::fromStdString(task->get_destination_filepath())).fileName();
    if (fileName.isEmpty()) fileName = QStringLiteral("this download");

    auto ans = QMessageBox::question(this, QStringLiteral("Delete File from Disk"),
        QStringLiteral("Are you sure you want to permanently delete '%1' from your hard drive and remove it from IDM?\n\nThis cannot be undone.")
        .arg(fileName),
        QMessageBox::Yes | QMessageBox::No);

    if (ans == QMessageBox::Yes) {
        if (task->get_status() == DownloadStatus::Downloading || task->get_status() == DownloadStatus::Probing) {
            task->pause();
        }
        auto q = findQueueForTask(task);
        if (q) {
            q->remove_task(task->get_destination_filepath());
        }
        std::string destPath = task->get_destination_filepath();
        m_tableModel->removeTask(destPath);

        std::error_code ec;
        if (!destPath.empty()) {
            std::filesystem::remove(destPath, ec);
            std::string sidecar = destPath + ".idm_sidecar";
            std::filesystem::remove(sidecar, ec);
        }
        statusBar()->showMessage(QStringLiteral("Permanently deleted '%1' from disk").arg(fileName), 3000);
    }
}

void MainWindow::onOptionsClicked() {
    OptionsDialog dlg(m_settings, this);
    connect(&dlg, &OptionsDialog::settingsApplied, this, [this]() {
        if (m_settings) {
            auto s = m_settings->get_settings();
            uint64_t speedLimit = (s.speed_mode == SpeedLimiterMode::ManualCap) ? s.manual_speed_limit_bps : 0;
            for (auto& [name, q] : m_queues) {
                if (q) {
                    q->set_max_concurrent_tasks(s.max_concurrent_download);
                    for (auto& task : q->get_tasks()) {
                        if (task) {
                            task->set_speed_limit(speedLimit);
                            task->set_thread_count(s.default_connections_per_task);
                            task->set_antivirus_scan_enabled(s.antivirus_scan_enabled);
                            if (!s.cache_directory.empty()) {
                                task->set_cache_directory(s.cache_directory);
                            }
                        }
                    }
                }
            }
            if (m_queue) {
                m_queue->set_max_concurrent_tasks(s.max_concurrent_download);
            }
            statusBar()->showMessage(QStringLiteral("Settings applied successfully"), 3000);
        }
        if (m_tableModel) m_tableModel->refreshTasks();
    });
    dlg.exec();
}

void MainWindow::onSchedulerClicked() {
    QString initialQ = m_isQueueFilter ? m_selectedCategory : QStringLiteral("Main Download Queue");
    SchedulerDialog dlg(m_queues, initialQ, this);
    connect(&dlg, &SchedulerDialog::queueCreated, this, [this](const QString&) {
        refreshQueuesInSidebar();
    });
    connect(&dlg, &SchedulerDialog::queueDeleted, this, [this](const QString&) {
        refreshQueuesInSidebar();
    });
    connect(&dlg, &SchedulerDialog::queueScheduleChanged, this, [this](const QString&) {
        if (m_tableModel) m_tableModel->refreshTasks();
    });
    dlg.exec();
    if (m_tableModel) m_tableModel->refreshTasks();
}

void MainWindow::onStartQueueClicked() {
    auto q = getQueue(m_isQueueFilter ? m_selectedCategory : QStringLiteral("Main Download Queue"));
    if (q) {
        for (auto &t : q->get_tasks()) {
            if (t && (t->get_status() == DownloadStatus::Paused || t->get_status() == DownloadStatus::NotStarted)) {
                t->resume();
                t->set_status(DownloadStatus::NotStarted);
            }
        }
        q->start();
    }
    if (m_tableModel) m_tableModel->refreshTasks();
}

void MainWindow::onStopQueueClicked() {
    auto q = getQueue(m_isQueueFilter ? m_selectedCategory : QStringLiteral("Main Download Queue"));
    if (q) {
        q->pause();
    }
    if (m_tableModel) m_tableModel->refreshTasks();
}

void MainWindow::onTableDoubleClicked(const QModelIndex& index) {
    if (!index.isValid()) return;
    int sourceRow = m_proxyModel->mapToSource(index).row();
    auto task = m_tableModel->getTask(sourceRow);
    if (!task) return;

    if (task->get_status() == DownloadStatus::Completed) {
        std::error_code ec;
        if (!task->get_destination_filepath().empty() && !std::filesystem::exists(task->get_destination_filepath(), ec)) {
            QString fn = QFileInfo(QString::fromStdString(task->get_destination_filepath())).fileName();
            auto ans = QMessageBox::question(this, QStringLiteral("File Missing on Disk"),
                QStringLiteral("The file \"%1\" was moved, renamed, or deleted from disk.\n\nPath: %2\n\nWould you like to re-download this file now?")
                .arg(fn, QString::fromStdString(task->get_destination_filepath())),
                QMessageBox::Yes | QMessageBox::No);
            if (ans == QMessageBox::Yes) {
                task->set_status(DownloadStatus::NotStarted);
                task->resume();
                auto q = findQueueForTask(task);
                if (q) {
                    q->resume_task(task->get_destination_filepath());
                    q->start();
                }
                m_tableModel->refreshTasks();
            }
            return;
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(task->get_destination_filepath())));
    } else {
        auto q = findQueueForTask(task);
        auto* statusDlg = new DownloadStatusDialog(task, q ? q.get() : m_queue, this);
        statusDlg->show();
    }
}

void MainWindow::onTableContextMenuRequested(const QPoint& pos) {
    auto index = m_tableView->indexAt(pos);
    if (!index.isValid()) return;
    int sourceRow = m_proxyModel->mapToSource(index).row();
    auto task = m_tableModel->getTask(sourceRow);
    if (!task) return;

    bool isCompleted = (task->get_status() == DownloadStatus::Completed);
    std::error_code ec;
    bool fileExists = !task->get_destination_filepath().empty() && std::filesystem::exists(task->get_destination_filepath(), ec);
    bool isMissing = isCompleted && !fileExists;

    QMenu menu(this);
    if (!isCompleted) {
        menu.addAction(style()->standardIcon(QStyle::SP_MediaPlay), QStringLiteral("Resume"), this, &MainWindow::onResumeClicked);
        menu.addAction(style()->standardIcon(QStyle::SP_MediaPause), QStringLiteral("Pause"), this, &MainWindow::onPauseClicked);
        menu.addSeparator();
    }

    if (isMissing) {
        menu.addAction(style()->standardIcon(QStyle::SP_BrowserReload), QStringLiteral("Re-download missing file"), this, [this, task]() {
            task->set_status(DownloadStatus::NotStarted);
            task->resume();
            auto q = findQueueForTask(task);
            if (q) {
                q->resume_task(task->get_destination_filepath());
                q->start();
            }
            if (m_tableModel) m_tableModel->refreshTasks();
        });
        menu.addSeparator();
    }

    menu.addAction(style()->standardIcon(QStyle::SP_TrashIcon), QStringLiteral("Remove from IDM (keep file)"), this, &MainWindow::onRemoveFromListClicked);
    menu.addAction(style()->standardIcon(QStyle::SP_DialogDiscardButton), QStringLiteral("Delete file from disk..."), this, &MainWindow::onDeleteFileClicked);

    std::string err = task->get_last_error();
    if (task->get_status() == DownloadStatus::Failed && (err.find("Security Warning") != std::string::npos || err.find("Threat detected") != std::string::npos)) {
        menu.addAction(style()->standardIcon(QStyle::SP_MessageBoxCritical), QStringLiteral("Security Alert: Override & Allow"), this, [this, task]() {
            task->override_security_and_complete();
            if (m_tableModel) m_tableModel->refreshTasks();
        });
    }

    menu.addSeparator();

    auto* moveMenu = menu.addMenu(QStringLiteral("Move to queue"));
    for (const auto& [qName, q] : m_queues) {
        auto* act = moveMenu->addAction(qName, this, [this, qName]() {
            onMoveTaskToQueue(qName);
        });
        if (task->get_queue_name() == qName.toStdString()) {
            act->setCheckable(true);
            act->setChecked(true);
        }
    }
    moveMenu->addSeparator();
    moveMenu->addAction(QStringLiteral("Create new queue..."), this, &MainWindow::onCreateQueueTriggered);

    if (!task->get_queue_name().empty()) {
        menu.addAction(style()->standardIcon(QStyle::SP_DialogCancelButton), QStringLiteral("Remove from queue"), this, [this, task]() {
            auto q = findQueueForTask(task);
            if (q) {
                q->remove_task(task->get_destination_filepath());
            }
            task->set_queue_name("");
            if (m_tableModel) m_tableModel->refreshTasks();
        });
    }

    if (isCompleted && fileExists) {
        menu.addSeparator();
        menu.addAction(QStringLiteral("Open"), this, [task]() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(task->get_destination_filepath())));
        });
        menu.addAction(QStringLiteral("Open Folder"), this, [task]() {
            std::string dir = std::filesystem::path(task->get_destination_filepath()).parent_path().string();
            QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(dir)));
        });
    }

    menu.exec(m_tableView->viewport()->mapToGlobal(pos));
}

void MainWindow::show_tray_notification(const QString& title, const QString& message) {
    if (m_trayIcon && m_trayIcon->isVisible()) {
        m_trayIcon->showMessage(title, message, QSystemTrayIcon::Information, 3000);
    }
}

void MainWindow::restore_or_launch_window() {
    if (isMinimized()) {
        showNormal();
    }
    show();
    raise();
    activateWindow();
#ifdef _WIN32
    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (hwnd) {
        ShowWindow(hwnd, SW_RESTORE);
        SetForegroundWindow(hwnd);
    }
#endif
}

void MainWindow::onDownloadIntercepted(const QString& url, const QString& filename, const QString& cookies, const QString& referer, const QString& userAgent, quint64 totalSize) {
    if (m_settings && !m_settings->is_interception_allowed(url.toStdString(), userAgent.toStdString())) {
        return;
    }

    // Only use 'this' as parent if main window is already visible.
    // If IDM is in the tray, leave the main window in the tray and show dialogs independently.
    QWidget* parentWnd = isVisible() ? this : nullptr;

    bool showDialog = m_settings ? m_settings->get_settings().show_start_download_dialog : true;
    if (showDialog) {
        QStringList qNames;
        for (const auto& [qName, q] : m_queues) qNames << qName;
        DownloadInfoDialog dlg(url, qNames, m_settings, parentWnd);
        if (!filename.isEmpty()) {
            dlg.setCustomFilename(filename);
        }
        if (totalSize > 0) {
            dlg.setFileSize(totalSize);
        }
        dlg.setHttpContext(cookies, referer, userAgent);

        dlg.show();
        dlg.raise();
        dlg.activateWindow();
#ifdef _WIN32
        HWND hwnd = reinterpret_cast<HWND>(dlg.winId());
        if (hwnd) {
            SetForegroundWindow(hwnd);
        }
#endif

        if (dlg.exec() == QDialog::Accepted) {
            std::string destUrl = dlg.getUrl();
            std::string fullPath = dlg.getDestinationPath();
            bool startNow = dlg.shouldStartImmediately();
            std::string qName = dlg.getSelectedQueue();
            if (destUrl.empty() || fullPath.empty()) return;

            int connections = m_settings ? m_settings->get_settings().default_connections_per_task : 8;
            uint64_t speedLimit = (m_settings && m_settings->get_settings().speed_mode == SpeedLimiterMode::ManualCap)
                                  ? m_settings->get_settings().manual_speed_limit_bps : 0;
            std::string cacheDir = m_settings ? m_settings->get_settings().cache_directory : "";
            bool avEnabled = m_settings ? m_settings->get_settings().antivirus_scan_enabled : true;

            auto task = std::make_shared<DownloadTask>(
                destUrl, fullPath, connections, speedLimit,
                referer.toStdString(), cookies.toStdString(), userAgent.toStdString(), cacheDir
            );
            task->set_queue_name(qName);
            task->set_antivirus_scan_enabled(avEnabled);
            if (dlg.isSecurityOverride()) {
                task->set_security_override(true);
            }
            task->set_status(DownloadStatus::NotStarted);
            auto targetQueue = getQueue(QString::fromStdString(qName));
            if (targetQueue) {
                targetQueue->add_task(task);
                if (startNow) {
                    targetQueue->start();
                }
            } else if (m_queue) {
                m_queue->add_task(task);
                if (startNow) m_queue->start();
            }
            m_tableModel->addTask(task);
            m_tableModel->refreshTasks();
            if (startNow) {
                auto* statusDlg = new DownloadStatusDialog(task, targetQueue ? targetQueue.get() : m_queue, parentWnd);
                statusDlg->show();
                statusDlg->raise();
                statusDlg->activateWindow();
            }
        }
    } else {
        std::string fname = filename.toStdString();
        if (fname.empty()) fname = DownloadTask::extract_filename_from_url(url.toStdString());
        std::string cat = m_settings ? m_settings->determine_category_for_filename(fname) : "General";
        std::string cat_dir = m_settings ? m_settings->get_category_path(cat) : "C:/Downloads";
        std::string fullPath = cat_dir + "/" + fname;

        int connections = m_settings ? m_settings->get_settings().default_connections_per_task : 8;
        uint64_t speedLimit = (m_settings && m_settings->get_settings().speed_mode == SpeedLimiterMode::ManualCap)
                              ? m_settings->get_settings().manual_speed_limit_bps : 0;
        std::string cacheDir = m_settings ? m_settings->get_settings().cache_directory : "";
        bool avEnabled = m_settings ? m_settings->get_settings().antivirus_scan_enabled : true;

        auto task = std::make_shared<DownloadTask>(
            url.toStdString(), fullPath, connections, speedLimit,
            referer.toStdString(), cookies.toStdString(), userAgent.toStdString(), cacheDir
        );
        task->set_queue_name("Main Download Queue");
        task->set_antivirus_scan_enabled(avEnabled);
        auto mainQ = getQueue(QStringLiteral("Main Download Queue"));
        if (mainQ) {
            mainQ->add_task(task);
            mainQ->start();
        } else if (m_queue) {
            m_queue->add_task(task);
            m_queue->start();
        }
        m_tableModel->addTask(task);
        m_tableModel->refreshTasks();

        auto* statusDlg = new DownloadStatusDialog(task, mainQ ? mainQ.get() : m_queue, parentWnd);
        statusDlg->show();
        statusDlg->raise();
        statusDlg->activateWindow();
    }
}

void MainWindow::onRefreshTimerTick() {
    double totalSpeed = 0.0;
    int activeCount = 0;
    int totalThreads = 0;
    size_t totalTasks = 0;

    auto allTasks = m_tableModel ? m_tableModel->getAllTasks() : std::vector<std::shared_ptr<DownloadTask>>();
    totalTasks = allTasks.size();
    for (const auto& task : allTasks) {
        if (task && task->get_status() == DownloadStatus::Downloading) {
            totalSpeed += task->get_speed_bps();
            activeCount++;
            totalThreads += task->get_thread_count();
        }
    }

    if (activeCount > 0) {
        m_statusLeftLabel->setText(QStringLiteral(" Downloading: %1 file(s)").arg(activeCount));
    } else {
        m_statusLeftLabel->setText(QStringLiteral(" Ready (%1 items)").arg(totalTasks));
    }

    m_statusSpeedLabel->setText(QStringLiteral("Speed: %1/s  |  Threads: %2 ")
                                .arg(formatBytes(static_cast<uint64_t>(totalSpeed)))
                                .arg(totalThreads));

    static int s_lastActiveCount = 0;
    static size_t s_lastTaskCount = 0;
    static int s_tick = 0;
    s_tick++;
    if (activeCount > 0 || s_lastActiveCount > 0 || totalTasks != s_lastTaskCount || (s_tick % 5 == 0)) {
        if (m_tableModel) m_tableModel->refreshTasks();
    }
    s_lastActiveCount = activeCount;
    s_lastTaskCount = totalTasks;
}

void MainWindow::onTrayIconActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
        if (isVisible()) {
            hide();
        } else {
            showNormal();
            activateWindow();
        }
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (m_trayIcon && m_trayIcon->isVisible()) {
        hide();
        event->ignore();
    } else {
        if (m_refreshTimer) {
            m_refreshTimer->stop();
        }
        event->accept();
        qApp->quit();
    }
}

void MainWindow::changeEvent(QEvent* event) {
    if (event->type() == QEvent::WindowStateChange) {
        if (isMinimized()) {
        }
    }
    QMainWindow::changeEvent(event);
}
