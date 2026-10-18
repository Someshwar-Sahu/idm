#include "DownloadStatusDialog.h"
#include "SegmentedProgressWidget.h"
#include "DownloadCompleteDialog.h"
#include "SecurityWarningDialog.h"
#include "../core/DownloadTask.h"
#include "../queue/DownloadQueue.h"
#include <filesystem>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFileInfo>
#include <QCloseEvent>
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
}

DownloadStatusDialog::DownloadStatusDialog(std::shared_ptr<DownloadTask> task, DownloadQueue* queue, QWidget* parent)
    : QDialog(parent), m_task(std::move(task)), m_queue(queue) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setWindowTitle(QStringLiteral("Downloading..."));
    setMinimumWidth(540);
    setStyleSheet(QStringLiteral(
        "QDialog { background: #1c1c20; color: #f4f4f5; }"
        "QLabel { font-size: 12px; color: #e4e4e7; }"
        "QProgressBar { background: #27272a; border: 1px solid #3f3f46; border-radius: 4px; text-align: center; color: white; font-weight: bold; height: 18px; }"
        "QProgressBar::chunk { background: #3b82f6; border-radius: 3px; }"
        "QPushButton { background: #27272a; color: #f4f4f5; border: 1px solid #3f3f46; border-radius: 5px; padding: 6px 16px; font-size: 12px; }"
        "QPushButton:hover { background: #3f3f46; border-color: #60a5fa; }"
        "QPushButton:pressed { background: #18181b; }"
    ));

    setupUi();

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &DownloadStatusDialog::onRefreshTick);
    m_timer->start(100);

    onRefreshTick();
}

DownloadStatusDialog::~DownloadStatusDialog() {
    if (m_timer) m_timer->stop();
}

void DownloadStatusDialog::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);

    m_fileLabel = new QLabel(this);
    m_fileLabel->setStyleSheet(QStringLiteral("font-weight: 600; font-size: 13px; color: #38bdf8;"));
    mainLayout->addWidget(m_fileLabel);

    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(6);

    grid->addWidget(new QLabel(QStringLiteral("Status:"), this), 0, 0);
    m_statusLabel = new QLabel(QStringLiteral("Connecting..."), this);
    grid->addWidget(m_statusLabel, 0, 1);

    grid->addWidget(new QLabel(QStringLiteral("File size:"), this), 1, 0);
    m_sizeLabel = new QLabel(QStringLiteral("-"), this);
    grid->addWidget(m_sizeLabel, 1, 1);

    grid->addWidget(new QLabel(QStringLiteral("Transfer rate:"), this), 2, 0);
    m_speedLabel = new QLabel(QStringLiteral("-"), this);
    grid->addWidget(m_speedLabel, 2, 1);

    grid->addWidget(new QLabel(QStringLiteral("Time left:"), this), 3, 0);
    m_etaLabel = new QLabel(QStringLiteral("-"), this);
    grid->addWidget(m_etaLabel, 3, 1);

    grid->addWidget(new QLabel(QStringLiteral("Resume capability:"), this), 4, 0);
    m_resumeLabel = new QLabel(QStringLiteral("-"), this);
    grid->addWidget(m_resumeLabel, 4, 1);

    mainLayout->addLayout(grid);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    mainLayout->addWidget(m_progressBar);

    mainLayout->addWidget(new QLabel(QStringLiteral("Connection Threads (Multi-Range Chunks):"), this));
    m_segmentedBar = new SegmentedProgressWidget(this);
    mainLayout->addWidget(m_segmentedBar);

    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch(1);

    m_overrideBtn = new QPushButton(QStringLiteral("Override & Keep"), this);
    m_overrideBtn->setStyleSheet(QStringLiteral("background: #7f1d1d; color: #fca5a5; font-weight: bold; border: 1px solid #ef4444;"));
    m_overrideBtn->setVisible(false);
    connect(m_overrideBtn, &QPushButton::clicked, this, &DownloadStatusDialog::onOverrideSecurityClicked);
    btnLayout->addWidget(m_overrideBtn);

    m_pauseResumeBtn = new QPushButton(QStringLiteral("Pause"), this);
    connect(m_pauseResumeBtn, &QPushButton::clicked, this, &DownloadStatusDialog::onPauseResumeClicked);
    btnLayout->addWidget(m_pauseResumeBtn);

    m_cancelBtn = new QPushButton(QStringLiteral("Cancel"), this);
    connect(m_cancelBtn, &QPushButton::clicked, this, &DownloadStatusDialog::onCancelClicked);
    btnLayout->addWidget(m_cancelBtn);

    m_hideBtn = new QPushButton(QStringLiteral("Hide"), this);
    connect(m_hideBtn, &QPushButton::clicked, this, &QDialog::hide);
    btnLayout->addWidget(m_hideBtn);

    mainLayout->addLayout(btnLayout);
}

void DownloadStatusDialog::onRefreshTick() {
    if (!m_task) return;

    QString fileName = QFileInfo(QString::fromStdString(m_task->get_destination_filepath())).fileName();
    m_fileLabel->setText(fileName);
    setWindowTitle(QStringLiteral("Downloading: ") + fileName);

    auto status = m_task->get_status();
    if (status == DownloadStatus::Downloading) {
        m_statusLabel->setText(QStringLiteral("<font color='#34d399'>Downloading</font>"));
        m_pauseResumeBtn->setText(QStringLiteral("Pause"));
        m_pauseResumeBtn->setEnabled(true);
    } else if (status == DownloadStatus::NotStarted) {
        m_statusLabel->setText(QStringLiteral("<font color='#60a5fa'>Connecting...</font>"));
        m_pauseResumeBtn->setText(QStringLiteral("Pause"));
        m_pauseResumeBtn->setEnabled(true);
    } else if (status == DownloadStatus::Probing) {
        m_statusLabel->setText(QStringLiteral("<font color='#60a5fa'>Connecting to server...</font>"));
        m_pauseResumeBtn->setText(QStringLiteral("Pause"));
        m_pauseResumeBtn->setEnabled(true);
    } else if (status == DownloadStatus::Paused) {
        m_statusLabel->setText(QStringLiteral("<font color='#fbbf24'>Paused</font>"));
        m_pauseResumeBtn->setText(QStringLiteral("Resume"));
        m_pauseResumeBtn->setEnabled(true);
    } else if (status == DownloadStatus::Cancelled) {
        m_statusLabel->setText(QStringLiteral("<font color='#fb923c'>Cancelled</font>"));
        m_pauseResumeBtn->setText(QStringLiteral("Resume"));
        m_pauseResumeBtn->setEnabled(true);
    } else if (status == DownloadStatus::Completed) {
        m_statusLabel->setText(QStringLiteral("<font color='#10b981'>Complete</font>"));
        m_overrideBtn->setVisible(false);
        m_pauseResumeBtn->setEnabled(false);
    } else if (status == DownloadStatus::Failed) {
        std::string err = m_task->get_last_error();
        bool isSecurityAlert = (err.find("Security Warning") != std::string::npos || err.find("Threat detected") != std::string::npos);
        if (isSecurityAlert) {
            m_statusLabel->setText(QStringLiteral("<font color='#ef4444'><b>Threat Detected!</b></font>"));
            m_overrideBtn->setVisible(true);
            m_pauseResumeBtn->setVisible(false);

            if (!m_securityDialogShown) {
                m_securityDialogShown = true;
                QString threatName = QString::fromStdString(err);
                size_t p = err.find("Threat detected (");
                if (p != std::string::npos) {
                    threatName = QString::fromStdString(err.substr(p + 17));
                    if (threatName.endsWith(")")) threatName.chop(1);
                }
                SecurityWarningDialog warnDlg(SecurityWarningDialog::Mode::PostDownload,
                                             QString::fromStdString(m_task->get_destination_filepath()),
                                             threatName,
                                             QStringLiteral("Target URL: %1").arg(QString::fromStdString(m_task->get_url())),
                                             this);
                if (warnDlg.exec() == QDialog::Accepted) {
                    onOverrideSecurityClicked();
                    return;
                } else {
                    if (m_queue) {
                        m_queue->remove_task(m_task->get_destination_filepath());
                    }
                    std::error_code ec;
                    std::filesystem::remove(m_task->get_destination_filepath(), ec);
                    close();
                    return;
                }
            }
        } else {
            std::string errStr = err.empty() ? "Server connection failed" : err;
            m_statusLabel->setText(QStringLiteral("<font color='#f87171'><b>Error:</b> %1</font>").arg(QString::fromStdString(errStr)));
            m_overrideBtn->setVisible(false);
            m_pauseResumeBtn->setText(QStringLiteral("Resume"));
            m_pauseResumeBtn->setEnabled(true);
        }
    }

    uint64_t total = m_task->get_total_size();
    uint64_t downloaded = m_task->get_downloaded_size();
    int pct = static_cast<int>(m_task->get_progress_pct());

    m_sizeLabel->setText(QStringLiteral("%1 / %2 (%3%)").arg(formatBytes(downloaded), formatBytes(total)).arg(pct));
    m_speedLabel->setText(formatBytes(static_cast<uint64_t>(m_task->get_speed_bps())) + QStringLiteral("/s"));
    m_etaLabel->setText(QString::fromStdString(m_task->get_eta()));
    m_resumeLabel->setText(m_task->supports_ranges() ? QStringLiteral("Yes") : QStringLiteral("No"));

    m_progressBar->setValue(pct);
    m_segmentedBar->updateTask(m_task.get());

    if (status == DownloadStatus::Completed && !m_completedPopupTriggered) {
        m_completedPopupTriggered = true;
        m_timer->stop();
        close();
        auto* compDlg = new DownloadCompleteDialog(m_task->get_destination_filepath(), total, parentWidget());
        compDlg->show();
    }
}

void DownloadStatusDialog::onOverrideSecurityClicked() {
    if (!m_task) return;
    m_task->override_security_and_complete();
    m_statusLabel->setText(QStringLiteral("<font color='#10b981'><b>Complete (Overridden)</b></font>"));
    m_overrideBtn->setVisible(false);
    m_completedPopupTriggered = true;
    m_timer->stop();
    close();
    auto* compDlg = new DownloadCompleteDialog(m_task->get_destination_filepath(), m_task->get_total_size(), parentWidget());
    compDlg->show();
}

void DownloadStatusDialog::onPauseResumeClicked() {
    if (!m_task) return;
    auto status = m_task->get_status();
    if (status == DownloadStatus::Downloading || status == DownloadStatus::NotStarted || status == DownloadStatus::Probing) {
        if (m_queue) {
            m_queue->pause_task(m_task.get());
        }
        m_task->pause();
        m_statusLabel->setText(QStringLiteral("<font color='#fbbf24'>Paused</font>"));
        m_pauseResumeBtn->setText(QStringLiteral("Resume"));
    } else if (status == DownloadStatus::Paused || status == DownloadStatus::Cancelled || status == DownloadStatus::Failed) {
        m_task->resume();
        if (m_queue) {
            m_queue->resume_task(m_task.get());
            m_queue->start();
        }
        m_statusLabel->setText(QStringLiteral("<font color='#60a5fa'>Connecting...</font>"));
        m_pauseResumeBtn->setText(QStringLiteral("Pause"));
    }
}

void DownloadStatusDialog::onCancelClicked() {
    if (!m_task) return;
    m_task->cancel();
    if (m_queue) {
        m_queue->pause_task(m_task.get());
    }
    close();
}
