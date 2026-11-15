#include "SwarmDownloadDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QStandardPaths>
#include <QFileInfo>
#include <QFile>
#include <QDateTime>
#include <QCloseEvent>
#include <curl/curl.h>

namespace {
QString formatBytes(quint64 bytes) {
    if (bytes >= 1024ULL * 1024 * 1024) {
        return QString::asprintf("%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    }
    if (bytes >= 1024ULL * 1024) {
        return QString::asprintf("%.2f MB", bytes / (1024.0 * 1024.0));
    }
    if (bytes >= 1024) {
        return QString::asprintf("%.2f KB", bytes / 1024.0);
    }
    return QString::asprintf("%llu B", bytes);
}

QString formatSpeed(double bytesPerSec) {
    if (bytesPerSec >= 1024.0 * 1024.0) {
        return QString::asprintf("%.2f MB/s", bytesPerSec / (1024.0 * 1024.0));
    }
    if (bytesPerSec >= 1024.0) {
        return QString::asprintf("%.2f KB/s", bytesPerSec / 1024.0);
    }
    return QString::asprintf("%.0f B/s", bytesPerSec);
}
} // namespace

SwarmDownloadDialog::SwarmDownloadDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(QStringLiteral("Swarm & Metalink Downloader"));
    resize(780, 620);
    setMinimumSize(680, 520);
    setModal(true);

    connect(this, &SwarmDownloadDialog::progressUpdated, this, &SwarmDownloadDialog::onProgressUpdated, Qt::QueuedConnection);
    connect(this, &SwarmDownloadDialog::downloadFinished, this, &SwarmDownloadDialog::onDownloadFinished, Qt::QueuedConnection);

    setupUi();
}

SwarmDownloadDialog::~SwarmDownloadDialog() {
    if (m_isDownloading.load() && m_downloader) {
        m_downloader->cancel();
    }
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
}

void SwarmDownloadDialog::closeEvent(QCloseEvent* event) {
    if (m_isDownloading.load()) {
        auto ret = QMessageBox::question(
            this,
            QStringLiteral("Swarm Download in Progress"),
            QStringLiteral("A swarm download is actively running. Do you want to cancel and exit?"),
            QMessageBox::Yes | QMessageBox::No
        );
        if (ret != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        if (m_downloader) {
            m_downloader->cancel();
        }
        if (m_workerThread.joinable()) {
            m_workerThread.join();
        }
    }
    QDialog::closeEvent(event);
}

void SwarmDownloadDialog::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(10);
    mainLayout->setContentsMargins(15, 15, 15, 15);

    // 1. Source Group
    auto* sourceGroup = new QGroupBox(QStringLiteral("Metalink Source & File Metadata"), this);
    auto* sourceLayout = new QGridLayout(sourceGroup);
    sourceLayout->setSpacing(8);

    sourceLayout->addWidget(new QLabel(QStringLiteral("Metalink URL / File:"), this), 0, 0);
    m_metalinkUrlEdit = new QLineEdit(this);
    m_metalinkUrlEdit->setPlaceholderText(QStringLiteral("https://example.com/release.metalink or select local file"));
    sourceLayout->addWidget(m_metalinkUrlEdit, 0, 1);

    m_fetchMetalinkBtn = new QPushButton(QStringLiteral("Fetch URL"), this);
    connect(m_fetchMetalinkBtn, &QPushButton::clicked, this, &SwarmDownloadDialog::onFetchMetalinkUrl);
    sourceLayout->addWidget(m_fetchMetalinkBtn, 0, 2);

    m_browseMetalinkBtn = new QPushButton(QStringLiteral("Browse File..."), this);
    connect(m_browseMetalinkBtn, &QPushButton::clicked, this, &SwarmDownloadDialog::onBrowseMetalinkFile);
    sourceLayout->addWidget(m_browseMetalinkBtn, 0, 3);

    sourceLayout->addWidget(new QLabel(QStringLiteral("Target Filename:"), this), 1, 0);
    m_filenameEdit = new QLineEdit(this);
    m_filenameEdit->setPlaceholderText(QStringLiteral("Filename parsed from Metalink"));
    sourceLayout->addWidget(m_filenameEdit, 1, 1);

    m_fileSizeLabel = new QLabel(QStringLiteral("Size: 0 B"), this);
    m_fileSizeLabel->setStyleSheet(QStringLiteral("font-weight: bold; color: #3b82f6;"));
    sourceLayout->addWidget(m_fileSizeLabel, 1, 2, 1, 2);

    sourceLayout->addWidget(new QLabel(QStringLiteral("Expected SHA-256:"), this), 2, 0);
    m_sha256Edit = new QLineEdit(this);
    m_sha256Edit->setPlaceholderText(QStringLiteral("Optional cryptographic hash for verification"));
    sourceLayout->addWidget(m_sha256Edit, 2, 1, 1, 3);

    mainLayout->addWidget(sourceGroup);

    // 2. Mirrors Table Group
    auto* mirrorGroup = new QGroupBox(QStringLiteral("Mirror Seed Pool (P2P-Accelerated HTTP Mirrors)"), this);
    auto* mirrorLayout = new QVBoxLayout(mirrorGroup);

    m_mirrorTable = new QTableWidget(this);
    m_mirrorTable->setColumnCount(5);
    m_mirrorTable->setHorizontalHeaderLabels({
        QStringLiteral("Mirror URL"),
        QStringLiteral("Priority"),
        QStringLiteral("Location"),
        QStringLiteral("Status"),
        QStringLiteral("Downloaded")
    });
    m_mirrorTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_mirrorTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_mirrorTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_mirrorTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_mirrorTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_mirrorTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_mirrorTable->setAlternatingRowColors(true);
    mirrorLayout->addWidget(m_mirrorTable);

    auto* mirrorBtnLayout = new QHBoxLayout();
    m_addMirrorBtn = new QPushButton(QStringLiteral("+ Add Mirror"), this);
    connect(m_addMirrorBtn, &QPushButton::clicked, this, &SwarmDownloadDialog::onAddMirror);
    mirrorBtnLayout->addWidget(m_addMirrorBtn);

    m_removeMirrorBtn = new QPushButton(QStringLiteral("- Remove Mirror"), this);
    connect(m_removeMirrorBtn, &QPushButton::clicked, this, &SwarmDownloadDialog::onRemoveMirror);
    mirrorBtnLayout->addWidget(m_removeMirrorBtn);

    m_clearMirrorsBtn = new QPushButton(QStringLiteral("Clear All"), this);
    connect(m_clearMirrorsBtn, &QPushButton::clicked, this, &SwarmDownloadDialog::onClearMirrors);
    mirrorBtnLayout->addWidget(m_clearMirrorsBtn);

    mirrorBtnLayout->addStretch();
    mirrorLayout->addLayout(mirrorBtnLayout);

    mainLayout->addWidget(mirrorGroup);

    // 3. Options Group
    auto* optGroup = new QGroupBox(QStringLiteral("Multiplex Strategy & Destination"), this);
    auto* optLayout = new QGridLayout(optGroup);
    optLayout->setSpacing(8);

    optLayout->addWidget(new QLabel(QStringLiteral("Multiplex Strategy:"), this), 0, 0);
    m_strategyCombo = new QComboBox(this);
    m_strategyCombo->addItem(QStringLiteral("Round Robin (Even Mirror Distribution)"));
    m_strategyCombo->addItem(QStringLiteral("Priority Weighted (High Priority Seeds First)"));
    m_strategyCombo->addItem(QStringLiteral("Fastest First (Adaptive Latency & Reliability)"));
    optLayout->addWidget(m_strategyCombo, 0, 1);

    optLayout->addWidget(new QLabel(QStringLiteral("Concurrent Connections:"), this), 0, 2);
    m_threadsSpin = new QSpinBox(this);
    m_threadsSpin->setRange(1, 32);
    m_threadsSpin->setValue(4);
    optLayout->addWidget(m_threadsSpin, 0, 3);

    optLayout->addWidget(new QLabel(QStringLiteral("Save To Directory / File:"), this), 1, 0);
    m_destPathEdit = new QLineEdit(this);
    QString defDownloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    m_destPathEdit->setText(defDownloads);
    optLayout->addWidget(m_destPathEdit, 1, 1, 1, 2);

    m_browseDestBtn = new QPushButton(QStringLiteral("Browse..."), this);
    connect(m_browseDestBtn, &QPushButton::clicked, this, &SwarmDownloadDialog::onBrowseDestination);
    optLayout->addWidget(m_browseDestBtn, 1, 3);

    mainLayout->addWidget(optGroup);

    // 4. Progress & Controls
    auto* progGroup = new QGroupBox(QStringLiteral("Transfer Status"), this);
    auto* progLayout = new QVBoxLayout(progGroup);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);
    progLayout->addWidget(m_progressBar);

    auto* statLayout = new QHBoxLayout();
    m_statusLabel = new QLabel(QStringLiteral("Status: Idle"), this);
    statLayout->addWidget(m_statusLabel);
    statLayout->addStretch();
    m_speedLabel = new QLabel(QStringLiteral("Speed: 0 B/s"), this);
    statLayout->addWidget(m_speedLabel);
    progLayout->addLayout(statLayout);

    mainLayout->addWidget(progGroup);

    // Buttons
    auto* btnLayout = new QHBoxLayout();
    m_startBtn = new QPushButton(QStringLiteral("Start Swarm Download"), this);
    m_startBtn->setStyleSheet(QStringLiteral("font-weight: bold; background-color: #2563eb; color: white; padding: 6px 14px; border-radius: 4px;"));
    connect(m_startBtn, &QPushButton::clicked, this, &SwarmDownloadDialog::onStartDownload);
    btnLayout->addWidget(m_startBtn);

    m_cancelBtn = new QPushButton(QStringLiteral("Cancel Download"), this);
    m_cancelBtn->setEnabled(false);
    connect(m_cancelBtn, &QPushButton::clicked, this, &SwarmDownloadDialog::onCancelDownload);
    btnLayout->addWidget(m_cancelBtn);

    btnLayout->addStretch();

    m_closeBtn = new QPushButton(QStringLiteral("Close"), this);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnLayout->addWidget(m_closeBtn);

    mainLayout->addLayout(btnLayout);
}

void SwarmDownloadDialog::refreshMirrorTable() {
    m_mirrorTable->setRowCount(0);
    for (const auto& m : m_metadata.mirrors) {
        int r = m_mirrorTable->rowCount();
        m_mirrorTable->insertRow(r);

        m_mirrorTable->setItem(r, 0, new QTableWidgetItem(QString::fromStdString(m.url)));
        m_mirrorTable->setItem(r, 1, new QTableWidgetItem(QString::number(m.priority)));
        m_mirrorTable->setItem(r, 2, new QTableWidgetItem(m.location.empty() ? QStringLiteral("Global") : QString::fromStdString(m.location)));
        m_mirrorTable->setItem(r, 3, new QTableWidgetItem(m.active ? QStringLiteral("Active") : QStringLiteral("Disabled")));
        m_mirrorTable->setItem(r, 4, new QTableWidgetItem(formatBytes(m.bytes_downloaded)));
    }
}

void SwarmDownloadDialog::parseAndApplyMetalink(const idm::MetalinkMetadata& meta) {
    m_metadata = meta;
    if (!meta.filename.empty()) {
        m_filenameEdit->setText(QString::fromStdString(meta.filename));
        QString currentDest = m_destPathEdit->text();
        QFileInfo fi(currentDest);
        if (fi.isDir()) {
            m_destPathEdit->setText(QDir(currentDest).filePath(QString::fromStdString(meta.filename)));
        } else {
            m_destPathEdit->setText(fi.dir().filePath(QString::fromStdString(meta.filename)));
        }
    }
    m_fileSize = meta.size;
    m_fileSizeLabel->setText(QStringLiteral("Size: ") + formatBytes(m_fileSize));

    if (!meta.sha256_hash.empty()) {
        m_sha256Edit->setText(QString::fromStdString(meta.sha256_hash));
    }
    refreshMirrorTable();
}

bool SwarmDownloadDialog::loadMetalinkContent(const QString& content, bool isJson) {
    std::string s = content.toStdString();
    idm::MetalinkMetadata meta;
    if (isJson || content.trimmed().startsWith(QLatin1Char('{'))) {
        meta = idm::SwarmDownloader::parse_metalink_json(s);
    } else {
        meta = idm::SwarmDownloader::parse_metalink_xml(s);
    }
    if (meta.is_valid || !meta.mirrors.empty()) {
        parseAndApplyMetalink(meta);
        return true;
    }
    return false;
}

bool SwarmDownloadDialog::addMirrorEntry(const QString& url, int priority, const QString& location) {
    if (url.trimmed().isEmpty()) return false;
    for (const auto& m : m_metadata.mirrors) {
        if (QString::fromStdString(m.url) == url.trimmed()) {
            return false; // Duplicate
        }
    }
    idm::Mirror m;
    m.id = static_cast<int>(m_metadata.mirrors.size() + 1);
    m.url = url.trimmed().toStdString();
    m.priority = priority > 0 ? priority : 1;
    m.location = location.toStdString();
    m.active = true;
    m_metadata.mirrors.push_back(m);
    refreshMirrorTable();
    return true;
}

void SwarmDownloadDialog::setDestinationPath(const QString& path) {
    m_destPathEdit->setText(path);
}

void SwarmDownloadDialog::setThreadCount(int count) {
    m_threadsSpin->setValue(count);
}

void SwarmDownloadDialog::setStrategyIndex(int index) {
    m_strategyCombo->setCurrentIndex(index);
}

int SwarmDownloadDialog::mirrorCount() const {
    return static_cast<int>(m_metadata.mirrors.size());
}

QString SwarmDownloadDialog::filename() const {
    return m_filenameEdit->text();
}

quint64 SwarmDownloadDialog::fileSize() const {
    return m_fileSize;
}

QString SwarmDownloadDialog::sha256Hash() const {
    return m_sha256Edit->text();
}

QString SwarmDownloadDialog::destinationPath() const {
    return m_destPathEdit->text();
}

QString SwarmDownloadDialog::lastStatus() const {
    return m_lastStatusText;
}

void SwarmDownloadDialog::onBrowseMetalinkFile() {
    QString filter = QStringLiteral("Metalink Files (*.metalink *.meta4 *.json);;All Files (*.*)");
    QString filePath = QFileDialog::getOpenFileName(this, QStringLiteral("Open Metalink Document"), QString(), filter);
    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("Error"), QStringLiteral("Could not read selected metalink file."));
        return;
    }

    QString content = QString::fromUtf8(file.readAll());
    file.close();

    bool isJson = filePath.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive);
    if (!loadMetalinkContent(content, isJson)) {
        QMessageBox::warning(this, QStringLiteral("Parsing Error"), QStringLiteral("Failed to parse valid metalink mirrors or metadata from file."));
    } else {
        m_metalinkUrlEdit->setText(filePath);
    }
}

void SwarmDownloadDialog::onFetchMetalinkUrl() {
    QString urlStr = m_metalinkUrlEdit->text().trimmed();
    if (urlStr.isEmpty() || (!urlStr.startsWith(QStringLiteral("http://")) && !urlStr.startsWith(QStringLiteral("https://")))) {
        QMessageBox::warning(this, QStringLiteral("Invalid URL"), QStringLiteral("Please enter a valid HTTP/HTTPS Metalink address."));
        return;
    }

    m_fetchMetalinkBtn->setEnabled(false);
    m_statusLabel->setText(QStringLiteral("Fetching Metalink descriptor..."));

    std::string url = urlStr.toStdString();
    std::string response;

    CURL* curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](char* ptr, size_t size, size_t nmemb, void* udata) -> size_t {
            auto* s = static_cast<std::string*>(udata);
            s->append(ptr, size * nmemb);
            return size * nmemb;
        });
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        if (res == CURLE_OK && !response.empty()) {
            bool isJson = urlStr.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive);
            if (loadMetalinkContent(QString::fromStdString(response), isJson)) {
                m_statusLabel->setText(QStringLiteral("Metalink descriptor fetched and parsed successfully."));
            } else {
                QMessageBox::warning(this, QStringLiteral("Parse Error"), QStringLiteral("Retrieved content is not a valid Metalink XML or JSON."));
                m_statusLabel->setText(QStringLiteral("Error: Invalid Metalink payload."));
            }
        } else {
            QMessageBox::warning(this, QStringLiteral("Fetch Error"), QStringLiteral("Could not download metalink from URL."));
            m_statusLabel->setText(QStringLiteral("Error fetching Metalink."));
        }
    }
    m_fetchMetalinkBtn->setEnabled(true);
}

void SwarmDownloadDialog::onAddMirror() {
    bool ok = false;
    QString url = QInputDialog::getText(this, QStringLiteral("Add Swarm Mirror"), QStringLiteral("Mirror URL:"), QLineEdit::Normal, QStringLiteral("https://"), &ok);
    if (!ok || url.trimmed().isEmpty()) return;

    int prio = QInputDialog::getInt(this, QStringLiteral("Mirror Priority"), QStringLiteral("Priority (1 = Highest, 5 = Lowest):"), 1, 1, 10, 1, &ok);
    if (!ok) prio = 1;

    QString loc = QInputDialog::getText(this, QStringLiteral("Mirror Location"), QStringLiteral("Region / Country code:"), QLineEdit::Normal, QStringLiteral("Global"), &ok);
    if (!ok || loc.trimmed().isEmpty()) loc = QStringLiteral("Global");

    addMirrorEntry(url, prio, loc);
}

void SwarmDownloadDialog::onRemoveMirror() {
    int r = m_mirrorTable->currentRow();
    if (r >= 0 && r < static_cast<int>(m_metadata.mirrors.size())) {
        m_metadata.mirrors.erase(m_metadata.mirrors.begin() + r);
        refreshMirrorTable();
    }
}

void SwarmDownloadDialog::onClearMirrors() {
    m_metadata.mirrors.clear();
    refreshMirrorTable();
}

void SwarmDownloadDialog::onBrowseDestination() {
    QString savePath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Save Swarm Download Target"),
        m_destPathEdit->text(),
        QStringLiteral("All Files (*.*)")
    );
    if (!savePath.isEmpty()) {
        m_destPathEdit->setText(savePath);
    }
}

void SwarmDownloadDialog::onStartDownload() {
    if (m_isDownloading.load()) return;

    if (m_metadata.mirrors.empty()) {
        QMessageBox::warning(this, QStringLiteral("No Mirrors"), QStringLiteral("Please add at least one active mirror URL to the swarm pool."));
        return;
    }

    QString dest = m_destPathEdit->text().trimmed();
    QFileInfo fi(dest);
    if (fi.isDir() || dest.isEmpty()) {
        QString fn = m_filenameEdit->text().trimmed();
        if (fn.isEmpty()) fn = QStringLiteral("swarm_download.bin");
        dest = QDir(dest).filePath(fn);
        m_destPathEdit->setText(dest);
    }

    quint64 totalSize = m_fileSize;
    if (totalSize == 0) {
        // If file size wasn't in metalink, prompt user or estimate
        bool ok = false;
        qint64 s = QInputDialog::getInt(this, QStringLiteral("Specify File Size"), QStringLiteral("Total size in MB:"), 100, 1, 1000000, 1, &ok);
        if (!ok) return;
        totalSize = static_cast<quint64>(s) * 1024ULL * 1024ULL;
        m_fileSize = totalSize;
        m_fileSizeLabel->setText(QStringLiteral("Size: ") + formatBytes(totalSize));
    }

    int threads = m_threadsSpin->value();
    m_downloader = std::make_unique<idm::SwarmDownloader>(dest.toStdString(), threads);

    // Apply strategy
    int stratIdx = m_strategyCombo->currentIndex();
    if (stratIdx == 1) {
        m_downloader->set_strategy(idm::MultiplexStrategy::PriorityWeighted);
    } else if (stratIdx == 2) {
        m_downloader->set_strategy(idm::MultiplexStrategy::FastestFirst);
    } else {
        m_downloader->set_strategy(idm::MultiplexStrategy::RoundRobin);
    }

    // Register all mirrors
    for (const auto& m : m_metadata.mirrors) {
        m_downloader->add_mirror(m.url, m.priority, m.location);
    }

    m_isDownloading.store(true);
    m_downloadComplete.store(false);
    m_startBtn->setEnabled(false);
    m_cancelBtn->setEnabled(true);
    m_statusLabel->setText(QStringLiteral("Downloading from %1 mirror(s)...").arg(m_metadata.mirrors.size()));
    m_lastStatusText = m_statusLabel->text();

    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }

    m_workerThread = std::thread([this, totalSize, threads]() {
        auto lastTime = std::chrono::steady_clock::now();
        uint64_t lastDownloaded = 0;

        bool ok = m_downloader->download(
            totalSize,
            threads * 2,
            [this, &lastTime, &lastDownloaded](uint64_t downloaded, uint64_t total, double) {
                auto now = std::chrono::steady_clock::now();
                auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTime).count();
                double speed = 0.0;
                if (elapsedMs >= 250) {
                    uint64_t diff = (downloaded >= lastDownloaded) ? (downloaded - lastDownloaded) : 0;
                    speed = (diff * 1000.0) / elapsedMs;
                    lastDownloaded = downloaded;
                    lastTime = now;
                }
                emit progressUpdated(downloaded, total, speed);
            }
        );

        QString finalMsg;
        if (ok) {
            finalMsg = QStringLiteral("Swarm download completed successfully.");
            std::string expHash = m_sha256Edit->text().trimmed().toStdString();
            if (!expHash.empty()) {
                bool integrityOk = m_downloader->verify_integrity(expHash);
                if (integrityOk) {
                    finalMsg += QStringLiteral(" SHA-256 integrity verified!");
                } else {
                    finalMsg = QStringLiteral("Warning: SHA-256 hash mismatch! Data may be corrupted.");
                    ok = false;
                }
            }
        } else {
            std::string err = m_downloader->get_last_error();
            finalMsg = QStringLiteral("Failed: ") + (err.empty() ? QStringLiteral("Transfer aborted") : QString::fromStdString(err));
        }

        m_isDownloading.store(false);
        m_downloadComplete.store(ok);
        emit downloadFinished(ok, finalMsg);
    });
}

void SwarmDownloadDialog::onCancelDownload() {
    if (m_downloader) {
        m_downloader->cancel();
        m_statusLabel->setText(QStringLiteral("Cancelling swarm download..."));
        m_lastStatusText = m_statusLabel->text();
    }
}

void SwarmDownloadDialog::onProgressUpdated(quint64 downloaded, quint64 total, double speedBps) {
    if (total > 0) {
        int pct = static_cast<int>((downloaded * 100ULL) / total);
        m_progressBar->setValue(pct);
    }
    m_speedLabel->setText(QStringLiteral("Speed: ") + formatSpeed(speedBps));
    m_statusLabel->setText(QStringLiteral("Downloaded: %1 / %2").arg(formatBytes(downloaded), formatBytes(total)));
    m_lastStatusText = m_statusLabel->text();

    if (m_downloader) {
        auto liveMirrors = m_downloader->get_mirrors();
        for (int i = 0; i < m_mirrorTable->rowCount() && i < static_cast<int>(liveMirrors.size()); ++i) {
            m_mirrorTable->setItem(i, 4, new QTableWidgetItem(formatBytes(liveMirrors[i].bytes_downloaded)));
            m_mirrorTable->setItem(i, 3, new QTableWidgetItem(liveMirrors[i].active ? QStringLiteral("Active") : QStringLiteral("Degraded")));
        }
    }
}

void SwarmDownloadDialog::onDownloadFinished(bool success, const QString& message) {
    m_startBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
    m_statusLabel->setText(message);
    m_lastStatusText = message;
    m_speedLabel->setText(QStringLiteral("Speed: 0 B/s"));

    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }

    if (success) {
        m_progressBar->setValue(100);
        QMessageBox::information(this, QStringLiteral("Swarm Download Complete"), message);
    } else {
        QMessageBox::warning(this, QStringLiteral("Swarm Download Result"), message);
    }
}
