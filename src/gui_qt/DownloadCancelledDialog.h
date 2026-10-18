#pragma once
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <memory>
#include <string>

class DownloadTask;
class DownloadQueue;
class DownloadTableModel;

class DownloadCancelledDialog : public QDialog {
    Q_OBJECT
public:
    DownloadCancelledDialog(std::shared_ptr<DownloadTask> task,
                            DownloadQueue* queue,
                            DownloadTableModel* model,
                            QWidget* parent = nullptr);

private slots:
    void onResumeClicked();
    void onOpenFolderClicked();
    void onDiscardCacheClicked();
    void onDeleteClicked();

private:
    void setupUi();

    std::shared_ptr<DownloadTask> m_task;
    DownloadQueue* m_queue{nullptr};
    DownloadTableModel* m_model{nullptr};

    QLabel* m_progLabel{nullptr};
    QLabel* m_diskUsageLabel{nullptr};
    QProgressBar* m_pBar{nullptr};
    QPushButton* m_discardCacheBtn{nullptr};
};
