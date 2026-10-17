#pragma once
#include <QDialog>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <memory>

class DownloadTask;
class DownloadQueue;
class SegmentedProgressWidget;

class DownloadStatusDialog : public QDialog {
    Q_OBJECT
public:
    DownloadStatusDialog(std::shared_ptr<DownloadTask> task, DownloadQueue* queue, QWidget* parent = nullptr);
    ~DownloadStatusDialog() override;

private slots:
    void onRefreshTick();
    void onPauseResumeClicked();
    void onCancelClicked();
    void onOverrideSecurityClicked();

private:
    void setupUi();

    std::shared_ptr<DownloadTask> m_task;
    DownloadQueue* m_queue{nullptr};

    QLabel* m_fileLabel{nullptr};
    QLabel* m_statusLabel{nullptr};
    QLabel* m_sizeLabel{nullptr};
    QLabel* m_speedLabel{nullptr};
    QLabel* m_etaLabel{nullptr};
    QLabel* m_resumeLabel{nullptr};

    QProgressBar* m_progressBar{nullptr};
    SegmentedProgressWidget* m_segmentedBar{nullptr};

    QPushButton* m_overrideBtn{nullptr};
    QPushButton* m_pauseResumeBtn{nullptr};
    QPushButton* m_cancelBtn{nullptr};
    QPushButton* m_hideBtn{nullptr};

    QTimer* m_timer{nullptr};
    bool m_completedPopupTriggered{false};
    bool m_securityDialogShown{false};
};
