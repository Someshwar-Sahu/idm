#include "DownloadTableModel.h"
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <QColor>
#include <QFont>
#include "../core/DownloadTask.h"
#include "../queue/DownloadQueue.h"

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

QString formatSpeed(double bytesPerSec) {
    if (bytesPerSec <= 0.0) return QStringLiteral("0 KB/s");
    return formatBytes(static_cast<uint64_t>(bytesPerSec)) + QStringLiteral("/s");
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

QString categorizeExtension(const std::string& extRaw) {
    std::string ext = extRaw;
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".zip" || ext == ".rar" || ext == ".7z" || ext == ".tar" || ext == ".gz") return QStringLiteral("Compressed");
    if (ext == ".pdf" || ext == ".doc" || ext == ".docx" || ext == ".txt" || ext == ".xlsx") return QStringLiteral("Documents");
    if (ext == ".mp3" || ext == ".wav" || ext == ".flac" || ext == ".aac") return QStringLiteral("Music");
    if (ext == ".exe" || ext == ".msi" || ext == ".bat" || ext == ".apk") return QStringLiteral("Programs");
    if (ext == ".mp4" || ext == ".mkv" || ext == ".avi" || ext == ".mov" || ext == ".webm") return QStringLiteral("Video");
    return QStringLiteral("General");
}

} // anonymous namespace

DownloadTableModel::DownloadTableModel(DownloadQueue* queue, QObject* parent)
    : QAbstractTableModel(parent), m_queue(queue) {
    refreshTasks();
}

int DownloadTableModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(m_tasks.size());
}

int DownloadTableModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return ColCount;
}

QVariant DownloadTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        switch (section) {
            case ColFileName: return QStringLiteral("File Name");
            case ColSize:     return QStringLiteral("Size");
            case ColStatus:   return QStringLiteral("Status");
            case ColProgress: return QStringLiteral("Progress");
            case ColSpeed:    return QStringLiteral("Speed");
            case ColEta:      return QStringLiteral("Time Left");
            case ColCategory: return QStringLiteral("Category");
            case ColQueue:    return QStringLiteral("Queue");
            case ColLastTry:  return QStringLiteral("Last Try");
            default: break;
        }
    }
    return QVariant();
}

QVariant DownloadTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_tasks.size()))
        return QVariant();

    const auto& task = m_tasks[index.row()];
    if (!task) return QVariant();

    bool isMissing = false;
    if (task->get_status() == DownloadStatus::Completed) {
        std::error_code ec;
        if (!task->get_destination_filepath().empty() && !std::filesystem::exists(task->get_destination_filepath(), ec)) {
            isMissing = true;
        }
    }

    if (role == Qt::ForegroundRole) {
        if (isMissing) {
            return QColor(161, 161, 170); // muted gray #a1a1aa
        }
        if (index.column() == ColStatus) {
            auto st = task->get_status();
            if (st == DownloadStatus::Completed) return QColor(52, 211, 153); // #34d399 green
            if (st == DownloadStatus::Downloading) return QColor(96, 165, 250); // #60a5fa blue
            if (st == DownloadStatus::Paused) return QColor(251, 191, 36); // #fbbf24 amber
            if (st == DownloadStatus::Failed) return QColor(248, 113, 113); // #f87171 red
        }
    }

    if (role == Qt::FontRole) {
        if (isMissing) {
            QFont f;
            f.setStrikeOut(true);
            return f;
        }
    }

    if (role == Qt::ToolTipRole) {
        if (isMissing) {
            return QStringLiteral("The file was moved, renamed, or deleted from disk:\n%1\n\n(Double-click row to redownload)")
                   .arg(QString::fromStdString(task->get_destination_filepath()));
        }
        if (task->get_status() == DownloadStatus::Failed && !task->get_last_error().empty()) {
            return QString::fromStdString(task->get_last_error());
        }
        return QString::fromStdString(task->get_destination_filepath());
    }

    if (role == Qt::TextAlignmentRole) {
        if (index.column() == ColSize || index.column() == ColSpeed || index.column() == ColEta)
            return QVariant(Qt::AlignRight | Qt::AlignVCenter);
        if (index.column() == ColProgress || index.column() == ColStatus)
            return QVariant(Qt::AlignCenter);
        return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
    }

    if (role == Qt::UserRole && index.column() == ColProgress) {
        return task->get_progress_pct();
    }

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case ColFileName: {
                std::string dest = task->get_destination_filepath();
                std::string name = std::filesystem::path(dest).filename().string();
                if (name.empty()) name = DownloadTask::extract_filename_from_url(task->get_url());
                if (isMissing) {
                    return QString::fromStdString(name) + QStringLiteral(" [File moved/deleted]");
                }
                return QString::fromStdString(name);
            }
            case ColSize: {
                uint64_t total = task->get_total_size();
                if (total == 0) return QStringLiteral("Unknown");
                return formatBytes(total);
            }
            case ColStatus: {
                if (isMissing) {
                    return QStringLiteral("File missing");
                }
                if (task->get_status() == DownloadStatus::Failed) {
                    std::string err = task->get_last_error();
                    if (err.find("Threat detected") != std::string::npos || err.find("Security Warning") != std::string::npos) {
                        return QStringLiteral("Threat Blocked");
                    }
                    return QStringLiteral("Error");
                }
                return statusToString(task->get_status());
            }
            case ColProgress:
                if (isMissing) return QStringLiteral("Missing");
                return QString::number(task->get_progress_pct(), 'f', 1) + QStringLiteral("%");
            case ColSpeed:
                if (isMissing) return QStringLiteral("-");
                if (task->get_status() == DownloadStatus::Downloading)
                    return formatSpeed(task->get_speed_bps());
                return QStringLiteral("-");
            case ColEta:
                if (isMissing) return QStringLiteral("-");
                if (task->get_status() == DownloadStatus::Downloading) {
                    std::string eta = task->get_eta();
                    return eta.empty() ? QStringLiteral("-") : QString::fromStdString(eta);
                }
                return QStringLiteral("-");
            case ColCategory: {
                std::string ext = std::filesystem::path(task->get_destination_filepath()).extension().string();
                return categorizeExtension(ext);
            }
            case ColQueue: {
                std::string qn = task->get_queue_name();
                return qn.empty() ? QStringLiteral("-") : QString::fromStdString(qn);
            }
            case ColLastTry:
                return QStringLiteral("Queued");
            default:
                break;
        }
    }
    return QVariant();
}

void DownloadTableModel::attachQueues(const std::map<QString, std::shared_ptr<DownloadQueue>>* queues) {
    m_queues = queues;
    refreshTasks();
}

std::shared_ptr<DownloadTask> DownloadTableModel::getTask(int row) const {
    if (row >= 0 && row < static_cast<int>(m_tasks.size())) {
        return m_tasks[row];
    }
    return nullptr;
}

void DownloadTableModel::addTask(const std::shared_ptr<DownloadTask>& task) {
    if (!task) return;
    for (const auto& t : m_masterTasks) {
        if (t && (t->get_destination_filepath() == task->get_destination_filepath() || t->get_url() == task->get_url())) {
            return;
        }
    }
    m_masterTasks.push_back(task);
    refreshTasks();
}

bool DownloadTableModel::removeTask(const std::string& destination_filepath) {
    for (auto it = m_masterTasks.begin(); it != m_masterTasks.end(); ++it) {
        if (*it && ((*it)->get_destination_filepath() == destination_filepath || (*it)->get_url() == destination_filepath)) {
            m_masterTasks.erase(it);
            refreshTasks();
            return true;
        }
    }
    return false;
}

void DownloadTableModel::refreshTasks() {
    // Sync any tasks from m_queues or m_queue into m_masterTasks if not already tracked
    if (m_queues) {
        for (const auto& [name, q] : *m_queues) {
            if (q) {
                for (const auto& t : q->get_tasks()) {
                    if (!t) continue;
                    bool exists = false;
                    for (const auto& mt : m_masterTasks) {
                        if (mt && (mt->get_destination_filepath() == t->get_destination_filepath() || mt->get_url() == t->get_url())) {
                            exists = true;
                            break;
                        }
                    }
                    if (!exists) {
                        m_masterTasks.push_back(t);
                    }
                }
            }
        }
    } else if (m_queue) {
        for (const auto& t : m_queue->get_tasks()) {
            if (!t) continue;
            bool exists = false;
            for (const auto& mt : m_masterTasks) {
                if (mt && (mt->get_destination_filepath() == t->get_destination_filepath() || mt->get_url() == t->get_url())) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                m_masterTasks.push_back(t);
            }
        }
    }

    std::vector<std::shared_ptr<DownloadTask>> current = m_masterTasks;

    if (current.size() != m_tasks.size()) {
        beginResetModel();
        m_tasks = current;
        endResetModel();
    } else {
        m_tasks = current;
        if (!m_tasks.empty()) {
            emit dataChanged(index(0, 0), index(static_cast<int>(m_tasks.size()) - 1, ColCount - 1));
        }
    }
}

void DownloadTableModel::onTaskProgressUpdated(const std::string& taskId) {
    for (int row = 0; row < static_cast<int>(m_tasks.size()); ++row) {
        if (m_tasks[row] && m_tasks[row]->get_destination_filepath() == taskId) {
            emit dataChanged(index(row, 0), index(row, ColCount - 1));
            break;
        }
    }
}
