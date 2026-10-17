#pragma once

#include <QDialog>
#include <QTreeWidget>
#include <QTabWidget>
#include <QTimeEdit>
#include <QCheckBox>
#include <QSpinBox>
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include <QCloseEvent>
#include <map>
#include <memory>
#include <string>

#include "../queue/DownloadQueue.h"

class SchedulerDialog : public QDialog {
    Q_OBJECT

public:
    explicit SchedulerDialog(const std::map<QString, std::shared_ptr<DownloadQueue>>& queues,
                            const QString& initialQueueName = QString(),
                            QWidget* parent = nullptr);
    ~SchedulerDialog() override = default;

    void accept() override;

protected:
    void closeEvent(QCloseEvent* event) override;

signals:
    void queueCreated(const QString& queueName);
    void queueDeleted(const QString& queueName);
    void queueScheduleChanged(const QString& queueName);

private slots:
    void onQueueSelected(QTreeWidgetItem* current, QTreeWidgetItem* previous);
    void onCreateQueueClicked();
    void onDeleteQueueClicked();
    void onStartNowClicked();
    void onStopQueueClicked();
    void onApplyClicked();
    void onMoveUpClicked();
    void onMoveDownClicked();
    void onRemoveFileClicked();
    void onSetCurrentTimeClicked();
    void onAdd5MinutesClicked();
    void onAdd1HourStopClicked();

private:
    void setupUi();
    void loadQueueSettings(const QString& queueName);
    void saveQueueSettings(const QString& queueName);
    void refreshFileList(const QString& queueName);

    std::map<QString, std::shared_ptr<DownloadQueue>> m_queues;
    QString m_currentQueueName;

    // UI Widgets
    QTreeWidget* m_queueTree{nullptr};
    QPushButton* m_createQueueBtn{nullptr};
    QPushButton* m_deleteQueueBtn{nullptr};

    QTabWidget* m_tabs{nullptr};

    // Tab 1: Schedule
    QCheckBox* m_enableStartCheck{nullptr};
    QTimeEdit* m_startTimeEdit{nullptr};
    QPushButton* m_useCurrentTimeBtn{nullptr};
    QPushButton* m_add5MinBtn{nullptr};

    QCheckBox* m_enableStopCheck{nullptr};
    QTimeEdit* m_stopTimeEdit{nullptr};
    QPushButton* m_add1HourStopBtn{nullptr};

    QSpinBox* m_scheduleConcurrentSpin{nullptr};
    QSpinBox* m_retriesSpin{nullptr};

    QCheckBox* m_turnOffPcCheck{nullptr};
    QCheckBox* m_sleepPcCheck{nullptr};
    QCheckBox* m_exitIdmCheck{nullptr};
    QCheckBox* m_playSoundCheck{nullptr};

    // Tab 2: Files in queue
    QSpinBox* m_concurrentDownloadsSpin{nullptr};
    QTableWidget* m_filesTable{nullptr};
    QPushButton* m_moveUpBtn{nullptr};
    QPushButton* m_moveDownBtn{nullptr};
    QPushButton* m_removeFileBtn{nullptr};

    // Bottom Action Buttons
    QPushButton* m_startNowBtn{nullptr};
    QPushButton* m_stopBtn{nullptr};
    QPushButton* m_applyBtn{nullptr};
    QPushButton* m_closeBtn{nullptr};
};
