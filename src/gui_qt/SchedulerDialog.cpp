#include "SchedulerDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QSplitter>
#include <QGroupBox>
#include <QInputDialog>
#include <QMessageBox>
#include <QHeaderView>
#include <QFileInfo>
#include <sstream>
#include <iomanip>

namespace {
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

QString statusToString(DownloadStatus status) {
    switch (status) {
        case DownloadStatus::NotStarted: return QStringLiteral("Queued");
        case DownloadStatus::Probing:    return QStringLiteral("Connecting");
        case DownloadStatus::Downloading:return QStringLiteral("Downloading");
        case DownloadStatus::Paused:     return QStringLiteral("Paused");
        case DownloadStatus::Completed:  return QStringLiteral("Complete");
        case DownloadStatus::Failed:     return QStringLiteral("Error");
        case DownloadStatus::Cancelled:  return QStringLiteral("Cancelled");
        default:                         return QStringLiteral("Unknown");
    }
}
}

SchedulerDialog::SchedulerDialog(std::map<QString, std::shared_ptr<DownloadQueue>>& queues,
                                 const QString& initialQueueName,
                                 QWidget* parent)
    : QDialog(parent), m_queues(queues) {
    setWindowTitle(QStringLiteral("IDM Scheduler & Queues"));
    resize(720, 520);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    setStyleSheet(QStringLiteral(
        "QDialog { background: #1c1c20; color: #f4f4f5; }"
        "QTreeWidget { background: #18181b; color: #f4f4f5; border: 1px solid #27272a; border-radius: 6px; padding: 4px; }"
        "QTreeWidget::item { padding: 6px; border-radius: 4px; }"
        "QTreeWidget::item:selected { background: #2563eb; color: #ffffff; }"
        "QTabWidget::pane { border: 1px solid #27272a; background: #18181b; border-radius: 6px; }"
        "QTabBar::tab { background: #27272a; color: #a1a1aa; padding: 8px 16px; border-top-left-radius: 6px; border-top-right-radius: 6px; margin-right: 2px; }"
        "QTabBar::tab:selected { background: #18181b; color: #38bdf8; font-weight: bold; border-bottom: 2px solid #38bdf8; }"
        "QGroupBox { color: #38bdf8; font-weight: bold; border: 1px solid #27272a; border-radius: 6px; margin-top: 12px; padding-top: 14px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }"
        "QLabel, QCheckBox { font-size: 12px; color: #e4e4e7; }"
        "QTimeEdit, QSpinBox { background: #27272a; color: #f4f4f5; border: 1px solid #3f3f46; border-radius: 4px; padding-left: 6px; padding-right: 20px; padding-top: 3px; padding-bottom: 3px; font-size: 12px; min-height: 20px; }"
        "QTimeEdit:focus, QSpinBox:focus { border-color: #3b82f6; }"
        "QTimeEdit::up-button, QSpinBox::up-button { subcontrol-origin: border; subcontrol-position: top right; width: 18px; height: 12px; border-left: 1px solid #3f3f46; border-bottom: 1px solid #3f3f46; border-top-right-radius: 3px; background: #2f2f35; }"
        "QTimeEdit::up-button:hover, QSpinBox::up-button:hover { background: #3f3f46; }"
        "QTimeEdit::up-button:pressed, QSpinBox::up-button:pressed { background: #18181b; }"
        "QTimeEdit::up-arrow, QSpinBox::up-arrow { width: 0px; height: 0px; border-left: 3px solid transparent; border-right: 3px solid transparent; border-bottom: 4px solid #f4f4f5; }"
        "QTimeEdit::down-button, QSpinBox::down-button { subcontrol-origin: border; subcontrol-position: bottom right; width: 18px; height: 12px; border-left: 1px solid #3f3f46; border-bottom-right-radius: 3px; background: #2f2f35; }"
        "QTimeEdit::down-button:hover, QSpinBox::down-button:hover { background: #3f3f46; }"
        "QTimeEdit::down-button:pressed, QSpinBox::down-button:pressed { background: #18181b; }"
        "QTimeEdit::down-arrow, QSpinBox::down-arrow { width: 0px; height: 0px; border-left: 3px solid transparent; border-right: 3px solid transparent; border-top: 4px solid #f4f4f5; }"
        "QTableWidget { background: #18181b; color: #f4f4f5; border: 1px solid #27272a; gridline-color: #27272a; }"
        "QHeaderView::section { background: #202025; color: #a1a1aa; border: 1px solid #27272a; padding: 4px; font-weight: 600; font-size: 11px; }"
        "QPushButton { background: #27272a; color: #f4f4f5; border: 1px solid #3f3f46; border-radius: 5px; padding: 6px 14px; font-size: 12px; font-weight: 500; }"
        "QPushButton:hover { background: #3f3f46; border-color: #60a5fa; }"
        "QPushButton:pressed { background: #18181b; }"
    ));

    setupUi();

    // Select initial queue or default to first available
    QTreeWidgetItem* targetItem = nullptr;
    for (int i = 0; i < m_queueTree->topLevelItemCount(); ++i) {
        auto* item = m_queueTree->topLevelItem(i);
        if (!initialQueueName.isEmpty() && item->text(0) == initialQueueName) {
            targetItem = item;
            break;
        }
    }
    if (!targetItem && m_queueTree->topLevelItemCount() > 0) {
        targetItem = m_queueTree->topLevelItem(0);
    }
    if (targetItem) {
        m_queueTree->setCurrentItem(targetItem);
    }
}

void SchedulerDialog::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(10);

    auto* splitter = new QSplitter(Qt::Horizontal, this);

    // Left Pane: Queues
    auto* leftContainer = new QWidget(this);
    auto* leftLayout = new QVBoxLayout(leftContainer);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(8);

    leftLayout->addWidget(new QLabel(QStringLiteral("<b>Queues</b>"), this));
    m_queueTree = new QTreeWidget(this);
    m_queueTree->setHeaderHidden(true);
    m_queueTree->setColumnCount(1);

    for (const auto& [name, q] : m_queues) {
        auto* item = new QTreeWidgetItem(m_queueTree, QStringList(name));
        item->setIcon(0, QIcon(":/icons/cat_queue.png"));
    }
    connect(m_queueTree, &QTreeWidget::currentItemChanged, this, &SchedulerDialog::onQueueSelected);
    leftLayout->addWidget(m_queueTree);

    auto* qBtnLayout = new QHBoxLayout();
    m_createQueueBtn = new QPushButton(QStringLiteral("Create queue"), this);
    connect(m_createQueueBtn, &QPushButton::clicked, this, &SchedulerDialog::onCreateQueueClicked);
    qBtnLayout->addWidget(m_createQueueBtn);

    m_deleteQueueBtn = new QPushButton(QStringLiteral("Delete"), this);
    connect(m_deleteQueueBtn, &QPushButton::clicked, this, &SchedulerDialog::onDeleteQueueClicked);
    qBtnLayout->addWidget(m_deleteQueueBtn);
    leftLayout->addLayout(qBtnLayout);

    splitter->addWidget(leftContainer);

    // Right Pane: Tabs
    m_tabs = new QTabWidget(this);

    // Tab 1: Schedule Tab
    auto* scheduleTab = new QWidget(this);
    auto* schedLayout = new QVBoxLayout(scheduleTab);
    schedLayout->setSpacing(12);

    auto* schedGroup = new QGroupBox(QStringLiteral("Timer & Schedule"), scheduleTab);
    auto* schedGrid = new QGridLayout(schedGroup);
    schedGrid->setHorizontalSpacing(12);
    schedGrid->setVerticalSpacing(10);

    m_enableStartCheck = new QCheckBox(QStringLiteral("Start download at:"), schedGroup);
    auto* startLayout = new QHBoxLayout();
    m_startTimeEdit = new QTimeEdit(QTime::currentTime(), schedGroup);
    m_startTimeEdit->setDisplayFormat(QStringLiteral("HH:mm"));
    startLayout->addWidget(m_startTimeEdit);

    m_useCurrentTimeBtn = new QPushButton(QStringLiteral("Current time"), schedGroup);
    m_useCurrentTimeBtn->setToolTip(QStringLiteral("Set start time to now"));
    connect(m_useCurrentTimeBtn, &QPushButton::clicked, this, &SchedulerDialog::onSetCurrentTimeClicked);
    startLayout->addWidget(m_useCurrentTimeBtn);

    m_add5MinBtn = new QPushButton(QStringLiteral("+5 min"), schedGroup);
    m_add5MinBtn->setToolTip(QStringLiteral("Start in 5 minutes"));
    connect(m_add5MinBtn, &QPushButton::clicked, this, &SchedulerDialog::onAdd5MinutesClicked);
    startLayout->addWidget(m_add5MinBtn);

    schedGrid->addWidget(m_enableStartCheck, 0, 0);
    schedGrid->addLayout(startLayout, 0, 1);

    m_enableStopCheck = new QCheckBox(QStringLiteral("Stop download at:"), schedGroup);
    auto* stopLayout = new QHBoxLayout();
    m_stopTimeEdit = new QTimeEdit(QTime::currentTime().addSecs(7200), schedGroup);
    m_stopTimeEdit->setDisplayFormat(QStringLiteral("HH:mm"));
    stopLayout->addWidget(m_stopTimeEdit);

    m_add1HourStopBtn = new QPushButton(QStringLiteral("+1 hour"), schedGroup);
    m_add1HourStopBtn->setToolTip(QStringLiteral("Stop 1 hour from start time"));
    connect(m_add1HourStopBtn, &QPushButton::clicked, this, &SchedulerDialog::onAdd1HourStopClicked);
    stopLayout->addWidget(m_add1HourStopBtn);

    schedGrid->addWidget(m_enableStopCheck, 1, 0);
    schedGrid->addLayout(stopLayout, 1, 1);

    schedGrid->addWidget(new QLabel(QStringLiteral("Retries on failure:"), schedGroup), 2, 0);
    m_retriesSpin = new QSpinBox(schedGroup);
    m_retriesSpin->setRange(1, 99);
    m_retriesSpin->setValue(10);
    schedGrid->addWidget(m_retriesSpin, 2, 1);

    schedLayout->addWidget(schedGroup);

    auto* concGroup = new QGroupBox(QStringLiteral("Queue Concurrency"), scheduleTab);
    auto* concLayout = new QHBoxLayout(concGroup);
    concLayout->addWidget(new QLabel(QStringLiteral("Download"), concGroup));
    m_scheduleConcurrentSpin = new QSpinBox(concGroup);
    m_scheduleConcurrentSpin->setRange(1, 10);
    m_scheduleConcurrentSpin->setValue(3);
    concLayout->addWidget(m_scheduleConcurrentSpin);
    concLayout->addWidget(new QLabel(QStringLiteral("file(s) at the same time (e.g. 3 of 5)"), concGroup));
    concLayout->addStretch(1);
    schedLayout->addWidget(concGroup);

    auto* postGroup = new QGroupBox(QStringLiteral("Actions on Completion"), scheduleTab);
    auto* postLayout = new QVBoxLayout(postGroup);
    postLayout->setSpacing(8);

    m_turnOffPcCheck = new QCheckBox(QStringLiteral("Turn off computer when downloads complete"), postGroup);
    postLayout->addWidget(m_turnOffPcCheck);

    m_sleepPcCheck = new QCheckBox(QStringLiteral("Put computer to sleep when downloads complete"), postGroup);
    postLayout->addWidget(m_sleepPcCheck);

    m_exitIdmCheck = new QCheckBox(QStringLiteral("Exit IDM when complete"), postGroup);
    postLayout->addWidget(m_exitIdmCheck);

    m_playSoundCheck = new QCheckBox(QStringLiteral("Play sound when complete"), postGroup);
    postLayout->addWidget(m_playSoundCheck);

    schedLayout->addWidget(postGroup);
    schedLayout->addStretch(1);
    m_tabs->addTab(scheduleTab, QStringLiteral("Schedule"));

    // Tab 2: Files in queue Tab
    auto* filesTab = new QWidget(this);
    auto* filesLayout = new QVBoxLayout(filesTab);
    filesLayout->setSpacing(10);

    auto* topFilesLayout = new QHBoxLayout();
    topFilesLayout->addWidget(new QLabel(QStringLiteral("Download"), filesTab));
    m_concurrentDownloadsSpin = new QSpinBox(filesTab);
    m_concurrentDownloadsSpin->setRange(1, 10);
    m_concurrentDownloadsSpin->setValue(3);
    topFilesLayout->addWidget(m_concurrentDownloadsSpin);
    topFilesLayout->addWidget(new QLabel(QStringLiteral("file(s) at the same time"), filesTab));
    topFilesLayout->addStretch(1);
    filesLayout->addLayout(topFilesLayout);

    connect(m_scheduleConcurrentSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int val) {
        if (m_concurrentDownloadsSpin && m_concurrentDownloadsSpin->value() != val) {
            m_concurrentDownloadsSpin->setValue(val);
        }
    });
    connect(m_concurrentDownloadsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int val) {
        if (m_scheduleConcurrentSpin && m_scheduleConcurrentSpin->value() != val) {
            m_scheduleConcurrentSpin->setValue(val);
        }
    });

    auto* tableAndBtns = new QHBoxLayout();
    m_filesTable = new QTableWidget(filesTab);
    m_filesTable->setColumnCount(4);
    m_filesTable->setHorizontalHeaderLabels({QStringLiteral("File Name"), QStringLiteral("Size"), QStringLiteral("Status"), QStringLiteral("Progress")});
    m_filesTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_filesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_filesTable->setSelectionMode(QAbstractItemView::SingleSelection);
    tableAndBtns->addWidget(m_filesTable, 1);

    auto* reorderLayout = new QVBoxLayout();
    m_moveUpBtn = new QPushButton(QStringLiteral("Move Up"), filesTab);
    connect(m_moveUpBtn, &QPushButton::clicked, this, &SchedulerDialog::onMoveUpClicked);
    reorderLayout->addWidget(m_moveUpBtn);

    m_moveDownBtn = new QPushButton(QStringLiteral("Move Down"), filesTab);
    connect(m_moveDownBtn, &QPushButton::clicked, this, &SchedulerDialog::onMoveDownClicked);
    reorderLayout->addWidget(m_moveDownBtn);

    m_removeFileBtn = new QPushButton(QStringLiteral("Remove"), filesTab);
    connect(m_removeFileBtn, &QPushButton::clicked, this, &SchedulerDialog::onRemoveFileClicked);
    reorderLayout->addWidget(m_removeFileBtn);
    reorderLayout->addStretch(1);

    tableAndBtns->addLayout(reorderLayout);
    filesLayout->addLayout(tableAndBtns);

    m_tabs->addTab(filesTab, QStringLiteral("Files in the queue"));

    splitter->addWidget(m_tabs);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({210, 510});

    mainLayout->addWidget(splitter);

    // Bottom Action Bar
    auto* bottomLayout = new QHBoxLayout();
    m_startNowBtn = new QPushButton(QStringLiteral("Start Now"), this);
    m_startNowBtn->setStyleSheet(QStringLiteral("background: #2563eb; color: white; font-weight: bold;"));
    connect(m_startNowBtn, &QPushButton::clicked, this, &SchedulerDialog::onStartNowClicked);
    bottomLayout->addWidget(m_startNowBtn);

    m_stopBtn = new QPushButton(QStringLiteral("Stop"), this);
    connect(m_stopBtn, &QPushButton::clicked, this, &SchedulerDialog::onStopQueueClicked);
    bottomLayout->addWidget(m_stopBtn);

    bottomLayout->addStretch(1);

    m_applyBtn = new QPushButton(QStringLiteral("Apply"), this);
    connect(m_applyBtn, &QPushButton::clicked, this, &SchedulerDialog::onApplyClicked);
    bottomLayout->addWidget(m_applyBtn);

    m_closeBtn = new QPushButton(QStringLiteral("Close"), this);
    connect(m_closeBtn, &QPushButton::clicked, this, &SchedulerDialog::accept);
    bottomLayout->addWidget(m_closeBtn);

    mainLayout->addLayout(bottomLayout);
}

void SchedulerDialog::accept() {
    if (!m_currentQueueName.isEmpty()) {
        saveQueueSettings(m_currentQueueName);
    }
    QDialog::accept();
}

void SchedulerDialog::closeEvent(QCloseEvent* event) {
    if (!m_currentQueueName.isEmpty()) {
        saveQueueSettings(m_currentQueueName);
    }
    QDialog::closeEvent(event);
}

void SchedulerDialog::onQueueSelected(QTreeWidgetItem* current, QTreeWidgetItem* previous) {
    if (previous) {
        saveQueueSettings(previous->text(0));
    }
    if (!current) return;
    QString name = current->text(0);
    m_currentQueueName = name;
    loadQueueSettings(name);

    // Disable deleting main or sync queue
    bool canDelete = (name != QStringLiteral("Main Download Queue") && name != QStringLiteral("Synchronization Queue"));
    m_deleteQueueBtn->setEnabled(canDelete);
}

void SchedulerDialog::loadQueueSettings(const QString& queueName) {
    auto it = m_queues.find(queueName);
    if (it == m_queues.end() || !it->second) return;

    auto q = it->second;
    auto sched = q->get_schedule();

    m_enableStartCheck->setChecked(sched.start_scheduled);
    if (!sched.start_time.empty()) {
        m_startTimeEdit->setTime(QTime::fromString(QString::fromStdString(sched.start_time), QStringLiteral("HH:mm")));
    } else {
        m_startTimeEdit->setTime(QTime::currentTime());
    }

    m_enableStopCheck->setChecked(sched.stop_scheduled);
    if (!sched.stop_time.empty()) {
        m_stopTimeEdit->setTime(QTime::fromString(QString::fromStdString(sched.stop_time), QStringLiteral("HH:mm")));
    } else {
        m_stopTimeEdit->setTime(QTime::currentTime().addSecs(7200));
    }

    m_retriesSpin->setValue(sched.max_retries > 0 ? sched.max_retries : 10);

    auto postAction = q->get_post_queue_action();
    m_turnOffPcCheck->setChecked(postAction == PostQueueAction::ShutdownPC);
    m_sleepPcCheck->setChecked(postAction == PostQueueAction::SleepPC);
    m_playSoundCheck->setChecked(postAction == PostQueueAction::PlaySound);

    int maxConcurrent = q->get_max_concurrent_tasks();
    m_concurrentDownloadsSpin->setValue(maxConcurrent);
    if (m_scheduleConcurrentSpin) {
        m_scheduleConcurrentSpin->setValue(maxConcurrent);
    }

    refreshFileList(queueName);
}

void SchedulerDialog::saveQueueSettings(const QString& queueName) {
    auto it = m_queues.find(queueName);
    if (it == m_queues.end() || !it->second) return;

    auto q = it->second;
    DownloadQueue::QueueSchedule sched;
    sched.start_scheduled = m_enableStartCheck->isChecked();
    sched.start_time = m_startTimeEdit->time().toString(QStringLiteral("HH:mm")).toStdString();
    sched.stop_scheduled = m_enableStopCheck->isChecked();
    sched.stop_time = m_stopTimeEdit->time().toString(QStringLiteral("HH:mm")).toStdString();
    sched.max_retries = m_retriesSpin->value();
    q->set_schedule(sched);

    if (m_turnOffPcCheck->isChecked()) {
        q->set_post_queue_action(PostQueueAction::ShutdownPC);
    } else if (m_sleepPcCheck->isChecked()) {
        q->set_post_queue_action(PostQueueAction::SleepPC);
    } else if (m_playSoundCheck->isChecked()) {
        q->set_post_queue_action(PostQueueAction::PlaySound);
    } else {
        q->set_post_queue_action(PostQueueAction::None);
    }

    int conc = m_scheduleConcurrentSpin ? m_scheduleConcurrentSpin->value() : m_concurrentDownloadsSpin->value();
    q->set_max_concurrent_tasks(conc);

    if (sched.start_scheduled || sched.stop_scheduled) {
        q->start_scheduler();
        QString nowStr = QTime::currentTime().toString(QStringLiteral("HH:mm"));
        if (sched.start_scheduled) {
            if (QString::fromStdString(sched.start_time) == nowStr) {
                // Scheduled for the current minute: make tasks runnable and start immediately!
                for (auto &t : q->get_tasks()) {
                    if (t && (t->get_status() == DownloadStatus::Paused || t->get_status() == DownloadStatus::NotStarted)) {
                        t->resume();
                        t->set_status(DownloadStatus::NotStarted);
                    }
                }
                q->start();
            } else {
                // Scheduled for future time: pause active downloads so they wait for timer
                q->pause();
            }
        }
    }

    emit queueScheduleChanged(queueName);
}

void SchedulerDialog::onSetCurrentTimeClicked() {
    m_startTimeEdit->setTime(QTime::currentTime());
    m_enableStartCheck->setChecked(true);
}

void SchedulerDialog::onAdd5MinutesClicked() {
    m_startTimeEdit->setTime(QTime::currentTime().addSecs(300));
    m_enableStartCheck->setChecked(true);
}

void SchedulerDialog::onAdd1HourStopClicked() {
    m_stopTimeEdit->setTime(m_startTimeEdit->time().addSecs(3600));
    m_enableStopCheck->setChecked(true);
}

void SchedulerDialog::refreshFileList(const QString& queueName) {
    m_filesTable->setRowCount(0);
    auto it = m_queues.find(queueName);
    if (it == m_queues.end() || !it->second) return;

    auto tasks = it->second->get_tasks();
    m_filesTable->setRowCount(static_cast<int>(tasks.size()));

    for (int row = 0; row < static_cast<int>(tasks.size()); ++row) {
        const auto& t = tasks[row];
        if (!t) continue;
        QString name = QFileInfo(QString::fromStdString(t->get_destination_filepath())).fileName();
        m_filesTable->setItem(row, 0, new QTableWidgetItem(name));
        m_filesTable->setItem(row, 1, new QTableWidgetItem(formatBytes(t->get_total_size())));
        m_filesTable->setItem(row, 2, new QTableWidgetItem(statusToString(t->get_status())));
        m_filesTable->setItem(row, 3, new QTableWidgetItem(QString::number(t->get_progress_pct(), 'f', 1) + QStringLiteral("%")));
    }
}

void SchedulerDialog::onCreateQueueClicked() {
    bool ok = false;
    QString queueName = QInputDialog::getText(
        this,
        QStringLiteral("Create Queue"),
        QStringLiteral("Enter new queue name:"),
        QLineEdit::Normal,
        QStringLiteral("New Download Queue"),
        &ok
    );

    if (!ok || queueName.trimmed().isEmpty()) return;
    queueName = queueName.trimmed();

    if (m_queues.find(queueName) != m_queues.end()) {
        QMessageBox::warning(this, QStringLiteral("Queue Exists"), QStringLiteral("A queue with this name already exists."));
        return;
    }

    auto newQ = std::make_shared<DownloadQueue>(queueName.toStdString(), 1);
    m_queues[queueName] = newQ;

    auto* item = new QTreeWidgetItem(m_queueTree, QStringList(queueName));
    item->setIcon(0, QIcon(":/icons/cat_queue.png"));
    m_queueTree->setCurrentItem(item);

    emit queueCreated(queueName);
}

void SchedulerDialog::onDeleteQueueClicked() {
    if (m_currentQueueName.isEmpty()) return;
    if (m_currentQueueName == QStringLiteral("Main Download Queue") || m_currentQueueName == QStringLiteral("Synchronization Queue")) {
        QMessageBox::warning(this, QStringLiteral("Cannot Delete"), QStringLiteral("Default queues cannot be deleted."));
        return;
    }

    auto ans = QMessageBox::question(
        this,
        QStringLiteral("Delete Queue"),
        QStringLiteral("Are you sure you want to delete the queue '%1'?").arg(m_currentQueueName),
        QMessageBox::Yes | QMessageBox::No
    );
    if (ans != QMessageBox::Yes) return;

    QString deletedName = m_currentQueueName;
    auto it = m_queues.find(deletedName);
    if (it != m_queues.end() && it->second) {
        it->second->stop();
        auto mainIt = m_queues.find(QStringLiteral("Main Download Queue"));
        if (mainIt != m_queues.end() && mainIt->second) {
            for (auto& task : it->second->get_tasks()) {
                if (task) {
                    task->set_queue_name("Main Download Queue");
                    mainIt->second->add_task(task);
                }
            }
        }
    }
    m_queues.erase(deletedName);

    // Remove from UI
    auto items = m_queueTree->findItems(deletedName, Qt::MatchExactly, 0);
    for (auto* item : items) {
        delete item;
    }

    emit queueDeleted(deletedName);

    if (m_queueTree->topLevelItemCount() > 0) {
        m_queueTree->setCurrentItem(m_queueTree->topLevelItem(0));
    }
}

void SchedulerDialog::onStartNowClicked() {
    if (m_currentQueueName.isEmpty()) return;
    saveQueueSettings(m_currentQueueName);
    auto it = m_queues.find(m_currentQueueName);
    if (it != m_queues.end() && it->second) {
        auto q = it->second;
        for (auto &t : q->get_tasks()) {
            if (t && (t->get_status() == DownloadStatus::Paused || t->get_status() == DownloadStatus::NotStarted)) {
                t->resume();
                t->set_status(DownloadStatus::NotStarted);
            }
        }
        q->start();
    }
    refreshFileList(m_currentQueueName);
    QMessageBox::information(this, QStringLiteral("Queue Started"),
                             QStringLiteral("Queue '%1' is now running (downloading %2 file(s) concurrently).")
                             .arg(m_currentQueueName)
                             .arg(m_concurrentDownloadsSpin->value()));
}

void SchedulerDialog::onStopQueueClicked() {
    if (m_currentQueueName.isEmpty()) return;
    auto it = m_queues.find(m_currentQueueName);
    if (it != m_queues.end() && it->second) {
        it->second->pause();
    }
    refreshFileList(m_currentQueueName);
}

void SchedulerDialog::onApplyClicked() {
    if (!m_currentQueueName.isEmpty()) {
        saveQueueSettings(m_currentQueueName);
    }
    accept();
}

void SchedulerDialog::onMoveUpClicked() {
    int row = m_filesTable->currentRow();
    if (row <= 0) return;
    auto it = m_queues.find(m_currentQueueName);
    if (it == m_queues.end() || !it->second) return;

    auto tasks = it->second->get_tasks();
    if (row < static_cast<int>(tasks.size())) {
        it->second->move_task_up(tasks[row]->get_destination_filepath());
        refreshFileList(m_currentQueueName);
        m_filesTable->selectRow(row - 1);
    }
}

void SchedulerDialog::onMoveDownClicked() {
    int row = m_filesTable->currentRow();
    if (row < 0 || row >= m_filesTable->rowCount() - 1) return;
    auto it = m_queues.find(m_currentQueueName);
    if (it == m_queues.end() || !it->second) return;

    auto tasks = it->second->get_tasks();
    if (row < static_cast<int>(tasks.size())) {
        it->second->move_task_down(tasks[row]->get_destination_filepath());
        refreshFileList(m_currentQueueName);
        m_filesTable->selectRow(row + 1);
    }
}

void SchedulerDialog::onRemoveFileClicked() {
    int row = m_filesTable->currentRow();
    if (row < 0) return;
    auto it = m_queues.find(m_currentQueueName);
    if (it == m_queues.end() || !it->second) return;

    auto tasks = it->second->get_tasks();
    if (row < static_cast<int>(tasks.size())) {
        auto t = tasks[row];
        if (t) {
            t->set_queue_name("");
        }
        it->second->remove_task(tasks[row]->get_destination_filepath());
        refreshFileList(m_currentQueueName);
        emit queueScheduleChanged(m_currentQueueName);
    }
}
