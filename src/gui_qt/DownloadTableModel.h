#pragma once

#include <QAbstractTableModel>
#include <QString>
#include <map>
#include <string>
#include <vector>
#include <memory>

#include "../queue/DownloadQueue.h"
#include "../core/DownloadTask.h"

class DownloadTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        ColFileName = 0,
        ColSize,
        ColStatus,
        ColProgress,
        ColSpeed,
        ColEta,
        ColCategory,
        ColQueue,
        ColLastTry,
        ColCount
    };
    explicit DownloadTableModel(DownloadQueue* queue, QObject* parent = nullptr);

    void attachQueues(const std::map<QString, std::shared_ptr<DownloadQueue>>* queues);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    std::shared_ptr<DownloadTask> getTask(int row) const;
    void addTask(const std::shared_ptr<DownloadTask>& task);
    bool removeTask(const std::string& destination_filepath);
    const std::vector<std::shared_ptr<DownloadTask>>& getAllTasks() const { return m_masterTasks; }

public slots:
    void refreshTasks();
    void onTaskProgressUpdated(const std::string& taskId);

private:
    DownloadQueue* m_queue{nullptr};
    const std::map<QString, std::shared_ptr<DownloadQueue>>* m_queues{nullptr};
    std::vector<std::shared_ptr<DownloadTask>> m_masterTasks;
    std::vector<std::shared_ptr<DownloadTask>> m_tasks;
};