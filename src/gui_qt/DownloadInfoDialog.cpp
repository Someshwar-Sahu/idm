#include "DownloadInfoDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFileDialog>
#include <QStandardPaths>
#include <QDir>
#include <QMessageBox>
#include <QApplication>
#include <filesystem>
#include "../core/DownloadTask.h"
#include "../security/AntivirusScanner.h"
#include "SecurityWarningDialog.h"

#include <thread>

DownloadInfoDialog::DownloadInfoDialog(const QString& initialUrl, const QStringList& queues, SettingsManager* settings, QWidget* parent)
    : QDialog(parent), m_settings(settings), m_availableQueues(queues) {
    m_probeCancelled = std::make_shared<std::atomic<bool>>(false);
    setWindowTitle(QStringLiteral("Enter New Download Address"));
    setMinimumWidth(560);
    setModal(true);

    setupUi();

    if (!initialUrl.isEmpty()) {
        m_urlEdit->setText(initialUrl);
        autoDetectFileNameAndCategory(initialUrl);
        probeUrlAsync(initialUrl);
    }
}

DownloadInfoDialog::~DownloadInfoDialog() {
    if (m_probeCancelled) {
        m_probeCancelled->store(true);
    }
}

void DownloadInfoDialog::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);

    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(10);

    // Row 0: URL
    grid->addWidget(new QLabel(QStringLiteral("Address (URL):"), this), 0, 0);
    m_urlEdit = new QLineEdit(this);
    m_urlEdit->setPlaceholderText(QStringLiteral("https://example.com/file.zip"));
    connect(m_urlEdit, &QLineEdit::textChanged, this, &DownloadInfoDialog::onUrlChanged);
    grid->addWidget(m_urlEdit, 0, 1, 1, 2);

    // Row 1: Category
    grid->addWidget(new QLabel(QStringLiteral("Category:"), this), 1, 0);
    m_categoryCombo = new QComboBox(this);
    m_categoryCombo->addItems({
        QStringLiteral("General"),
        QStringLiteral("Compressed"),
        QStringLiteral("Documents"),
        QStringLiteral("Music"),
        QStringLiteral("Programs"),
        QStringLiteral("Video")
    });
    connect(m_categoryCombo, &QComboBox::currentTextChanged, this, &DownloadInfoDialog::onCategoryChanged);
    grid->addWidget(m_categoryCombo, 1, 1, 1, 2);

    // Row 2: Queue
    grid->addWidget(new QLabel(QStringLiteral("Download Queue:"), this), 2, 0);
    m_queueCombo = new QComboBox(this);
    if (m_availableQueues.isEmpty()) {
        m_queueCombo->addItems({
            QStringLiteral("Main Download Queue"),
            QStringLiteral("Synchronization Queue")
        });
    } else {
        m_queueCombo->addItems(m_availableQueues);
    }
    grid->addWidget(m_queueCombo, 2, 1, 1, 2);

    // Row 3: Save As
    grid->addWidget(new QLabel(QStringLiteral("Save As:"), this), 3, 0);
    m_savePathEdit = new QLineEdit(this);
    grid->addWidget(m_savePathEdit, 3, 1);

    m_browseBtn = new QPushButton(QStringLiteral("..."), this);
    m_browseBtn->setFixedWidth(36);
    connect(m_browseBtn, &QPushButton::clicked, this, &DownloadInfoDialog::onBrowseClicked);
    grid->addWidget(m_browseBtn, 3, 2);

    // Row 4: File size
    grid->addWidget(new QLabel(QStringLiteral("File size:"), this), 4, 0);
    m_fileSizeLabel = new QLabel(QStringLiteral("Checking..."), this);
    m_fileSizeLabel->setStyleSheet(QStringLiteral("color: #60a5fa; font-weight: 500;"));
    grid->addWidget(m_fileSizeLabel, 4, 1, 1, 2);

    // Row 5: Sound option
    m_muteSoundCheck = new QCheckBox(QStringLiteral("Mute completion sound and notifications for this download"), this);
    grid->addWidget(m_muteSoundCheck, 5, 1, 1, 2);

    mainLayout->addLayout(grid);
    mainLayout->addSpacing(10);

    // Bottom Action Buttons
    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    m_startBtn = new QPushButton(QStringLiteral("Start Download"), this);
    m_startBtn->setDefault(true);
    m_startBtn->setStyleSheet(QStringLiteral("QPushButton { font-weight: bold; padding: 6px 14px; }"));
    connect(m_startBtn, &QPushButton::clicked, this, &DownloadInfoDialog::onStartClicked);
    btnLayout->addWidget(m_startBtn);

    m_laterBtn = new QPushButton(QStringLiteral("Download Later"), this);
    m_laterBtn->setStyleSheet(QStringLiteral("QPushButton { padding: 6px 14px; }"));
    connect(m_laterBtn, &QPushButton::clicked, this, &DownloadInfoDialog::onDownloadLaterClicked);
    btnLayout->addWidget(m_laterBtn);

    m_cancelBtn = new QPushButton(QStringLiteral("Cancel"), this);
    m_cancelBtn->setStyleSheet(QStringLiteral("QPushButton { padding: 6px 14px; }"));
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(m_cancelBtn);

    mainLayout->addLayout(btnLayout);
}

void DownloadInfoDialog::autoDetectFileNameAndCategory(const QString& urlStr) {
    if (urlStr.trimmed().isEmpty()) return;

    std::string url = urlStr.trimmed().toStdString();
    std::string filename = DownloadTask::extract_filename_from_url(url);
    if (filename.empty() || filename == "download.bin") {
        QUrl qurl(urlStr.trimmed());
        QString path = qurl.path();
        if (!path.isEmpty()) {
            QString name = QFileInfo(path).fileName();
            if (!name.isEmpty()) filename = name.toStdString();
        }
    }
    if (filename.empty()) filename = "download.bin";

    std::string ext = std::filesystem::path(filename).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    QString category = QStringLiteral("General");
    if (ext == ".zip" || ext == ".rar" || ext == ".7z" || ext == ".tar" || ext == ".gz" || ext == ".bz2" || ext == ".iso" || ext == ".xz") {
        category = QStringLiteral("Compressed");
    } else if (ext == ".pdf" || ext == ".doc" || ext == ".docx" || ext == ".txt" || ext == ".xlsx" || ext == ".pptx" || ext == ".odt" || ext == ".csv" || ext == ".epub") {
        category = QStringLiteral("Documents");
    } else if (ext == ".mp3" || ext == ".wav" || ext == ".flac" || ext == ".aac" || ext == ".ogg" || ext == ".m4a" || ext == ".wma") {
        category = QStringLiteral("Music");
    } else if (ext == ".exe" || ext == ".msi" || ext == ".bat" || ext == ".apk" || ext == ".dmg" || ext == ".pkg" || ext == ".deb") {
        category = QStringLiteral("Programs");
    } else if (ext == ".mp4" || ext == ".mkv" || ext == ".avi" || ext == ".mov" || ext == ".webm" || ext == ".flv" || ext == ".wmv" || ext == ".m4v") {
        category = QStringLiteral("Video");
    }

    m_categoryCombo->blockSignals(true);
    m_categoryCombo->setCurrentText(category);
    m_categoryCombo->blockSignals(false);

    QString downloadFolder;
    if (m_settings) {
        downloadFolder = QString::fromStdString(m_settings->get_settings().default_download_directory);
    }
    if (downloadFolder.isEmpty()) {
        downloadFolder = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    }
    QString targetDir = downloadFolder + QStringLiteral("/") + category;
    QDir().mkpath(targetDir);

    QString fullPath = targetDir + QStringLiteral("/") + QString::fromStdString(filename);
    m_savePathEdit->setText(fullPath);
}

void DownloadInfoDialog::onUrlChanged(const QString& newUrl) {
    if (m_savePathEdit->text().isEmpty() || m_savePathEdit->text().endsWith(QStringLiteral("/download.bin"))) {
        autoDetectFileNameAndCategory(newUrl);
    }
}

void DownloadInfoDialog::onBrowseClicked() {
    QString currentPath = m_savePathEdit->text();
    QString selectedFile = QFileDialog::getSaveFileName(this, QStringLiteral("Select Destination File"), currentPath);
    if (!selectedFile.isEmpty()) {
        m_savePathEdit->setText(selectedFile);
    }
}

void DownloadInfoDialog::onCategoryChanged(const QString& category) {
    QString currentPath = m_savePathEdit->text().trimmed();
    if (currentPath.isEmpty()) return;
    QFileInfo fi(currentPath);
    QString filename = fi.fileName();
    if (filename.isEmpty()) filename = QStringLiteral("download.bin");

    QString downloadFolder;
    if (m_settings) {
        downloadFolder = QString::fromStdString(m_settings->get_settings().default_download_directory);
    }
    if (downloadFolder.isEmpty()) {
        downloadFolder = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    }
    QString targetDir = downloadFolder + QStringLiteral("/") + category;
    QDir().mkpath(targetDir);
    m_savePathEdit->setText(targetDir + QStringLiteral("/") + filename);
}

void DownloadInfoDialog::setCustomFilename(const QString& filename) {
    if (filename.isEmpty() || filename == "download.bin") return;

    std::string ext = std::filesystem::path(filename.toStdString()).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    QString category = QStringLiteral("General");
    if (ext == ".zip" || ext == ".rar" || ext == ".7z" || ext == ".tar" || ext == ".gz" || ext == ".bz2" || ext == ".iso" || ext == ".xz") {
        category = QStringLiteral("Compressed");
    } else if (ext == ".pdf" || ext == ".doc" || ext == ".docx" || ext == ".txt" || ext == ".xlsx" || ext == ".pptx" || ext == ".odt" || ext == ".csv" || ext == ".epub") {
        category = QStringLiteral("Documents");
    } else if (ext == ".mp3" || ext == ".wav" || ext == ".flac" || ext == ".aac" || ext == ".ogg" || ext == ".m4a" || ext == ".wma") {
        category = QStringLiteral("Music");
    } else if (ext == ".exe" || ext == ".msi" || ext == ".bat" || ext == ".apk" || ext == ".dmg" || ext == ".pkg" || ext == ".deb") {
        category = QStringLiteral("Programs");
    } else if (ext == ".mp4" || ext == ".mkv" || ext == ".avi" || ext == ".mov" || ext == ".webm" || ext == ".flv" || ext == ".wmv" || ext == ".m4v") {
        category = QStringLiteral("Video");
    }

    m_categoryCombo->blockSignals(true);
    m_categoryCombo->setCurrentText(category);
    m_categoryCombo->blockSignals(false);

    QString downloadFolder;
    if (m_settings) {
        downloadFolder = QString::fromStdString(m_settings->get_settings().default_download_directory);
    }
    if (downloadFolder.isEmpty()) {
        downloadFolder = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    }
    QString targetDir = downloadFolder + QStringLiteral("/") + category;
    QDir().mkpath(targetDir);

    m_savePathEdit->setText(targetDir + QStringLiteral("/") + filename);
}

void DownloadInfoDialog::setFileSize(uint64_t bytes) {
    if (bytes == 0) {
        m_fileSizeLabel->setText(QStringLiteral("Unknown / Dynamic size"));
        return;
    }
    double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    QString sizeStr;
    if (mb >= 1024.0) {
        sizeStr = QStringLiteral("%1 GB (%2 bytes)").arg(QString::number(mb / 1024.0, 'f', 2)).arg(bytes);
    } else {
        sizeStr = QStringLiteral("%1 MB (%2 bytes)").arg(QString::number(mb, 'f', 2)).arg(bytes);
    }
    m_fileSizeLabel->setText(sizeStr);
}

void DownloadInfoDialog::setHttpContext(const QString& cookies, const QString& referer, const QString& userAgent) {
    m_cookies = cookies;
    m_referer = referer;
    m_userAgent = userAgent;
}

void DownloadInfoDialog::probeUrlAsync(const QString& urlStr) {
    std::string url = urlStr.trimmed().toStdString();
    if (url.empty() || (!urlStr.startsWith("http://") && !urlStr.startsWith("https://"))) {
        m_fileSizeLabel->setText(QStringLiteral("-"));
        return;
    }
    m_fileSizeLabel->setText(QStringLiteral("Querying server..."));

    if (m_probeCancelled) {
        m_probeCancelled->store(true);
    }
    m_probeCancelled = std::make_shared<std::atomic<bool>>(false);
    auto cancelled = m_probeCancelled;
    QPointer<DownloadInfoDialog> safeThis(this);

    std::string referer = m_referer.toStdString();
    std::string cookies = m_cookies.toStdString();
    std::string userAgent = m_userAgent.toStdString();

    std::string initialFilename = DownloadTask::extract_filename_from_url(url);
    if (initialFilename.empty() || initialFilename == "/" || initialFilename == "\\") {
        initialFilename = "download.bin";
    }

    std::thread([safeThis, cancelled, url, referer, cookies, userAgent, initialFilename]() {
        DownloadTask task(url, initialFilename, 1, 0, referer, cookies, userAgent);
        bool ok = task.probe_url();
        if (cancelled->load()) return;
        uint64_t total = task.get_total_size();
        std::string probedFilename = QFileInfo(QString::fromStdString(task.get_destination_filepath())).fileName().toStdString();
        bool resumeOk = task.supports_ranges();

        if (cancelled->load()) return;

        QMetaObject::invokeMethod(qApp, [safeThis, cancelled, ok, total, probedFilename, resumeOk]() {
            if (cancelled->load() || !safeThis) return;
            if (ok && total > 0) {
                safeThis->setFileSize(total);
                if (resumeOk) {
                    safeThis->m_fileSizeLabel->setText(safeThis->m_fileSizeLabel->text() + QStringLiteral("  [Resume: Yes]"));
                } else {
                    safeThis->m_fileSizeLabel->setText(safeThis->m_fileSizeLabel->text() + QStringLiteral("  [Resume: No]"));
                }
            } else {
                safeThis->m_fileSizeLabel->setText(QStringLiteral("Unknown (Streaming / Dynamic)"));
            }

            if (!probedFilename.empty() && probedFilename != "tmp.bin" && probedFilename != "download.bin" && probedFilename.rfind("tmp.", 0) != 0) {
                safeThis->setCustomFilename(QString::fromStdString(probedFilename));
            }
        }, Qt::QueuedConnection);
    }).detach();
}

void DownloadInfoDialog::onStartClicked() {
    if (m_probeCancelled) {
        m_probeCancelled->store(true);
    }
    QString urlStr = m_urlEdit->text().trimmed();
    if (urlStr.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Invalid URL"), QStringLiteral("Please enter a valid URL address."));
        return;
    }

    // Pre-flight security check before starting download (if enabled)
    bool avEnabled = m_settings ? m_settings->get_settings().antivirus_scan_enabled : true;
    if (avEnabled) {
        auto rep = AntivirusScanner::check_url_reputation(urlStr.toStdString(), m_savePathEdit->text().trimmed().toStdString());
        if (!rep.is_safe || rep.prompt_recommended) {
            SecurityWarningDialog warnDlg(SecurityWarningDialog::Mode::PreDownload,
                                         urlStr,
                                         QString::fromStdString(rep.details),
                                         m_savePathEdit->text().trimmed(),
                                         this);
            if (warnDlg.exec() != QDialog::Accepted) {
                // User cancelled download
                return;
            }
            m_securityOverride = true;
        }
    }

    m_startImmediately = true;
    accept();
}

void DownloadInfoDialog::onDownloadLaterClicked() {
    if (m_probeCancelled) {
        m_probeCancelled->store(true);
    }
    QString urlStr = m_urlEdit->text().trimmed();
    if (urlStr.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Invalid URL"), QStringLiteral("Please enter a valid URL address."));
        return;
    }

    bool avEnabled = m_settings ? m_settings->get_settings().antivirus_scan_enabled : true;
    if (avEnabled) {
        auto rep = AntivirusScanner::check_url_reputation(urlStr.toStdString(), m_savePathEdit->text().trimmed().toStdString());
        if (!rep.is_safe || rep.prompt_recommended) {
            SecurityWarningDialog warnDlg(SecurityWarningDialog::Mode::PreDownload,
                                         urlStr,
                                         QString::fromStdString(rep.details),
                                         m_savePathEdit->text().trimmed(),
                                         this);
            if (warnDlg.exec() != QDialog::Accepted) {
                return;
            }
            m_securityOverride = true;
        }
    }

    m_startImmediately = false;
    accept();
}

std::string DownloadInfoDialog::getUrl() const {
    return m_urlEdit->text().trimmed().toStdString();
}

std::string DownloadInfoDialog::getDestinationPath() const {
    return m_savePathEdit->text().trimmed().toStdString();
}

std::string DownloadInfoDialog::getCategory() const {
    return m_categoryCombo->currentText().toStdString();
}

bool DownloadInfoDialog::shouldStartImmediately() const {
    return m_startImmediately;
}

bool DownloadInfoDialog::isSoundMuted() const {
    return m_muteSoundCheck->isChecked();
}

void DownloadInfoDialog::setAvailableQueues(const QStringList& queues) {
    m_availableQueues = queues;
    if (m_queueCombo) {
        QString current = m_queueCombo->currentText();
        m_queueCombo->clear();
        m_queueCombo->addItems(queues);
        int idx = m_queueCombo->findText(current);
        if (idx >= 0) {
            m_queueCombo->setCurrentIndex(idx);
        }
    }
}

std::string DownloadInfoDialog::getSelectedQueue() const {
    if (m_queueCombo) {
        return m_queueCombo->currentText().toStdString();
    }
    return "Main Download Queue";
}

