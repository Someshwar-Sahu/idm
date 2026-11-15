#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include <QTableWidget>
#include <QComboBox>
#include <QSpinBox>
#include <QProgressBar>
#include <QPushButton>
#include <QGroupBox>
#include <memory>
#include <thread>
#include <atomic>
#include "../net/SwarmDownloader.h"

class SwarmDownloadDialog : public QDialog {
    Q_OBJECT

public:
    explicit SwarmDownloadDialog(QWidget* parent = nullptr);
    ~SwarmDownloadDialog() override;

    // Helper to programmatically load metalink content (useful for testing)
    bool loadMetalinkContent(const QString& content, bool isJson = false);
    bool addMirrorEntry(const QString& url, int priority = 1, const QString& location = QStringLiteral("Global"));
    void setDestinationPath(const QString& path);
    void setThreadCount(int count);
    void setStrategyIndex(int index);

    // Direct accessors for verification & testing
    int mirrorCount() const;
    QString filename() const;
    quint64 fileSize() const;
    QString sha256Hash() const;
    QString destinationPath() const;
    bool isDownloading() const { return m_isDownloading.load(); }
    bool isDownloadComplete() const { return m_downloadComplete.load(); }
    QString lastStatus() const;

signals:
    void progressUpdated(quint64 downloaded, quint64 total, double speedBps);
    void downloadFinished(bool success, const QString& message);

private slots:
    void onBrowseMetalinkFile();
    void onFetchMetalinkUrl();
    void onAddMirror();
    void onRemoveMirror();
    void onClearMirrors();
    void onBrowseDestination();
    void onStartDownload();
    void onCancelDownload();
    void onProgressUpdated(quint64 downloaded, quint64 total, double speedBps);
    void onDownloadFinished(bool success, const QString& message);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void setupUi();
    void refreshMirrorTable();
    void parseAndApplyMetalink(const idm::MetalinkMetadata& meta);

    // Source inputs
    QLineEdit* m_metalinkUrlEdit{nullptr};
    QPushButton* m_fetchMetalinkBtn{nullptr};
    QPushButton* m_browseMetalinkBtn{nullptr};
    QLineEdit* m_filenameEdit{nullptr};
    QLabel* m_fileSizeLabel{nullptr};
    QLineEdit* m_sha256Edit{nullptr};

    // Mirror pool
    QTableWidget* m_mirrorTable{nullptr};
    QPushButton* m_addMirrorBtn{nullptr};
    QPushButton* m_removeMirrorBtn{nullptr};
    QPushButton* m_clearMirrorsBtn{nullptr};

    // Config options
    QComboBox* m_strategyCombo{nullptr};
    QSpinBox* m_threadsSpin{nullptr};
    QLineEdit* m_destPathEdit{nullptr};
    QPushButton* m_browseDestBtn{nullptr};

    // Execution & Progress
    QProgressBar* m_progressBar{nullptr};
    QLabel* m_statusLabel{nullptr};
    QLabel* m_speedLabel{nullptr};
    QPushButton* m_startBtn{nullptr};
    QPushButton* m_cancelBtn{nullptr};
    QPushButton* m_closeBtn{nullptr};

    // State
    idm::MetalinkMetadata m_metadata;
    std::unique_ptr<idm::SwarmDownloader> m_downloader;
    std::thread m_workerThread;
    std::atomic<bool> m_isDownloading{false};
    std::atomic<bool> m_downloadComplete{false};
    quint64 m_fileSize{0};
    QString m_lastStatusText;
};

