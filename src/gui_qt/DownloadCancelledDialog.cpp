#include "DownloadCancelledDialog.h"
#include "DownloadStatusDialog.h"
#include "DownloadTableModel.h"
#include "../core/DownloadTask.h"
#include "../queue/DownloadQueue.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QDesktopServices>
#include <QUrl>
#include <QProcess>
#include <QDir>
#include <QMessageBox>
#include <sstream>
#include <iomanip>
#include <filesystem>

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
}

DownloadCancelledDialog::DownloadCancelledDialog(std::shared_ptr<DownloadTask> task,
                                                 DownloadQueue* queue,
                                                 DownloadTableModel* model,
                                                 QWidget* parent)
    : QDialog(parent), m_task(std::move(task)), m_queue(queue), m_model(model) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    
    QString fileName = m_task ? QFileInfo(QString::fromStdString(m_task->get_destination_filepath())).fileName() : QStringLiteral("Download");
    setWindowTitle(QStringLiteral("Download Cancelled - ") + fileName);
    setMinimumWidth(500);

    setStyleSheet(QStringLiteral(
        "QDialog { background: #1c1c20; color: #f4f4f5; }"
        "QLabel { font-size: 12px; color: #e4e4e7; }"
        "QProgressBar { background: #27272a; border: 1px solid #3f3f46; border-radius: 4px; text-align: center; color: white; font-weight: bold; height: 18px; }"
        "QProgressBar::chunk { background: #f97316; border-radius: 3px; }"
        "QPushButton { background: #27272a; color: #f4f4f5; border: 1px solid #3f3f46; border-radius: 5px; padding: 6px 16px; font-size: 12px; font-weight: 500; }"
        "QPushButton:hover { background: #3f3f46; border-color: #60a5fa; }"
        "QPushButton:pressed { background: #18181b; }"
    ));

    setupUi();
}

void DownloadCancelledDialog::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);

    auto* headerLabel = new QLabel(QStringLiteral("⏹  Download Cancelled"), this);
    headerLabel->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: 700; color: #fb923c; padding: 4px 0;"));
    mainLayout->addWidget(headerLabel);

    if (!m_task) return;

    QFileInfo fi(QString::fromStdString(m_task->get_destination_filepath()));
    auto* nameLabel = new QLabel(QStringLiteral("<b>File:</b> ") + fi.fileName(), this);
    nameLabel->setStyleSheet(QStringLiteral("color: #38bdf8; font-weight: 600; font-size: 13px;"));
    mainLayout->addWidget(nameLabel);

    uint64_t total = m_task->get_total_size();
    uint64_t downloaded = m_task->get_downloaded_size();
    double pct = m_task->get_progress_pct();

    m_progLabel = new QLabel(QStringLiteral("<b>Downloaded:</b> %1 / %2 (%3%)")
        .arg(formatBytes(downloaded), formatBytes(total), QString::number(pct, 'f', 1)), this);
    mainLayout->addWidget(m_progLabel);

    m_pBar = new QProgressBar(this);
    m_pBar->setRange(0, 100);
    m_pBar->setValue(static_cast<int>(pct));
    m_pBar->setFormat(QString::number(pct, 'f', 1) + QStringLiteral("% (Cancelled)"));
    mainLayout->addWidget(m_pBar);

    std::string dest = m_task->get_destination_filepath();
    std::error_code ec;
    uint64_t diskBytes = (!dest.empty() && std::filesystem::exists(dest, ec)) ? std::filesystem::file_size(dest, ec) : 0;

    m_diskUsageLabel = new QLabel(QStringLiteral("💾 <b>Partial Cache on Disk:</b> %1 (Preserved for fast resuming)")
        .arg(formatBytes(diskBytes)), this);
    m_diskUsageLabel->setStyleSheet(QStringLiteral("color: #a1a1aa; font-size: 11px;"));
    mainLayout->addWidget(m_diskUsageLabel);

    auto* pathLabel = new QLabel(QStringLiteral("<b>Folder:</b> ") + fi.absolutePath(), this);
    pathLabel->setWordWrap(true);
    mainLayout->addWidget(pathLabel);

    auto* urlLabel = new QLabel(QStringLiteral("<b>URL:</b> ") + QString::fromStdString(m_task->get_url()), this);
    urlLabel->setWordWrap(true);
    urlLabel->setStyleSheet(QStringLiteral("color: #94a3b8; font-size: 11px;"));
    mainLayout->addWidget(urlLabel);

    mainLayout->addSpacing(6);

    auto* btnLayout = new QHBoxLayout();

    auto* resumeBtn = new QPushButton(QStringLiteral("Resume Download"), this);
    resumeBtn->setStyleSheet(QStringLiteral("background: #2563eb; color: white; font-weight: 600;"));
    connect(resumeBtn, &QPushButton::clicked, this, &DownloadCancelledDialog::onResumeClicked);
    btnLayout->addWidget(resumeBtn);

    m_discardCacheBtn = new QPushButton(QStringLiteral("Clear Cache (Free Space)"), this);
    m_discardCacheBtn->setToolTip(QStringLiteral("Deletes partial downloaded file and cache to free disk space immediately, keeping task in list"));
    m_discardCacheBtn->setStyleSheet(QStringLiteral("background: #3f3f46; color: #f4f4f5; font-weight: 500;"));
    connect(m_discardCacheBtn, &QPushButton::clicked, this, &DownloadCancelledDialog::onDiscardCacheClicked);
    btnLayout->addWidget(m_discardCacheBtn);

    auto* folderBtn = new QPushButton(QStringLiteral("Open Folder"), this);
    connect(folderBtn, &QPushButton::clicked, this, &DownloadCancelledDialog::onOpenFolderClicked);
    btnLayout->addWidget(folderBtn);

    auto* deleteBtn = new QPushButton(QStringLiteral("Delete"), this);
    deleteBtn->setStyleSheet(QStringLiteral("background: #7f1d1d; color: #fca5a5; border-color: #ef4444;"));
    connect(deleteBtn, &QPushButton::clicked, this, &DownloadCancelledDialog::onDeleteClicked);
    btnLayout->addWidget(deleteBtn);

    btnLayout->addStretch(1);

    auto* closeBtn = new QPushButton(QStringLiteral("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnLayout->addWidget(closeBtn);

    mainLayout->addLayout(btnLayout);
}

void DownloadCancelledDialog::onDiscardCacheClicked() {
    if (!m_task) return;
    std::string dest = m_task->get_destination_filepath();
    std::error_code ec;
    uint64_t diskBytes = (!dest.empty() && std::filesystem::exists(dest, ec)) ? std::filesystem::file_size(dest, ec) : 0;

    if (diskBytes == 0 && m_task->get_downloaded_size() == 0) {
        QMessageBox::information(this, QStringLiteral("No Cache Used"), QStringLiteral("This download has no cached data on disk."));
        return;
    }

    auto ans = QMessageBox::question(this, QStringLiteral("Clear Download Cache"),
        QStringLiteral("Free %1 of disk space by discarding the partial file?\n\nThe download will remain in your list with 0% progress so you can download it later.")
        .arg(formatBytes(diskBytes)),
        QMessageBox::Yes | QMessageBox::No);

    if (ans != QMessageBox::Yes) return;

    m_task->clear_cache_and_partial_file();

    if (m_model) {
        m_model->refreshTasks();
    }

    if (m_progLabel) {
        m_progLabel->setText(QStringLiteral("<b>Downloaded:</b> 0 B / %1 (0.0%)")
            .arg(formatBytes(m_task->get_total_size())));
    }
    if (m_pBar) {
        m_pBar->setValue(0);
        m_pBar->setFormat(QStringLiteral("0% (Cache Cleared)"));
    }
    if (m_diskUsageLabel) {
        m_diskUsageLabel->setText(QStringLiteral("💾 <b>Partial Cache on Disk:</b> 0 B (Space freed)"));
        m_diskUsageLabel->setStyleSheet(QStringLiteral("color: #34d399; font-size: 11px; font-weight: 500;"));
    }
    if (m_discardCacheBtn) {
        m_discardCacheBtn->setEnabled(false);
    }

    QMessageBox::information(this, QStringLiteral("Space Freed"),
        QStringLiteral("Successfully freed %1 of disk space!").arg(formatBytes(diskBytes)));
}

void DownloadCancelledDialog::onResumeClicked() {
    if (!m_task) {
        accept();
        return;
    }

    m_task->resume(); // resets status to NotStarted
    if (m_queue) {
        m_queue->resume_task(m_task.get());
        m_queue->start();
    }
    if (m_model) {
        m_model->refreshTasks();
    }

    // Open active download status dialog
    auto* statusDlg = new DownloadStatusDialog(m_task, m_queue, parentWidget());
    statusDlg->show();

    accept();
}

void DownloadCancelledDialog::onOpenFolderClicked() {
    if (!m_task) return;
    std::string path = m_task->get_destination_filepath();
#ifdef _WIN32
    QString nativePath = QDir::toNativeSeparators(QString::fromStdString(path));
    QStringList args;
    args << QStringLiteral("/select,") << nativePath;
    QProcess::startDetached(QStringLiteral("explorer.exe"), args);
#else
    QFileInfo fi(QString::fromStdString(path));
    QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
#endif
}

void DownloadCancelledDialog::onDeleteClicked() {
    if (!m_task) {
        accept();
        return;
    }

    auto ans = QMessageBox::question(this, QStringLiteral("Delete Download"),
        QStringLiteral("Are you sure you want to delete this cancelled download?\n\nDo you also want to remove the partially downloaded file from disk?"),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);

    if (ans == QMessageBox::Cancel) return;

    std::string path = m_task->get_destination_filepath();

    if (ans == QMessageBox::Yes) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    if (m_queue) {
        m_queue->remove_task(path);
    }
    if (m_model) {
        m_model->removeTask(path);
        m_model->refreshTasks();
    }

    accept();
}
