#pragma once
#include <QWidget>
#include <vector>
#include <cstdint>

class DownloadTask;

class SegmentedProgressWidget : public QWidget {
    Q_OBJECT
public:
    explicit SegmentedProgressWidget(QWidget* parent = nullptr);
    void updateTask(DownloadTask* task);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    struct ChunkBar {
        double startPct{0.0};
        double endPct{0.0};
        double downloadedPct{0.0};
        bool isDone{false};
        bool isActive{false};
    };
    std::vector<ChunkBar> m_chunks;
    double m_totalProgress{0.0};
};
