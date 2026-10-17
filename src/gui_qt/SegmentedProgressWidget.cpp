#include "SegmentedProgressWidget.h"
#include <QPainter>
#include <QPaintEvent>
#include <cmath>
#include "../core/DownloadTask.h"

SegmentedProgressWidget::SegmentedProgressWidget(QWidget* parent)
    : QWidget(parent) {
    setMinimumHeight(24);
    setMaximumHeight(28);
}

void SegmentedProgressWidget::updateTask(DownloadTask* task) {
    if (!task) return;
    m_totalProgress = task->get_progress_pct();

    auto rawChunks = task->get_chunks();
    m_chunks.clear();
    uint64_t total = task->get_total_size();

    if (rawChunks.empty() || total == 0) {
        ChunkBar bar;
        bar.startPct = 0.0;
        bar.endPct = 1.0;
        bar.downloadedPct = m_totalProgress / 100.0;
        bar.isDone = (m_totalProgress >= 100.0);
        bar.isActive = (m_totalProgress > 0 && m_totalProgress < 100.0);
        m_chunks.push_back(bar);
    } else {
        for (const auto& ch : rawChunks) {
            ChunkBar bar;
            bar.startPct = static_cast<double>(ch.start_byte) / static_cast<double>(total);
            bar.endPct = static_cast<double>(ch.end_byte + 1) / static_cast<double>(total);
            uint64_t chunkLen = (ch.end_byte >= ch.start_byte) ? (ch.end_byte - ch.start_byte + 1) : 1;
            bar.downloadedPct = static_cast<double>(ch.bytes_downloaded) / static_cast<double>(chunkLen);
            bar.isDone = (ch.bytes_downloaded >= chunkLen || ch.state == ChunkState::Done);
            bar.isActive = (!bar.isDone && ch.bytes_downloaded > 0);
            m_chunks.push_back(bar);
        }
    }
    update();
}

void SegmentedProgressWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QRect r = rect().adjusted(1, 1, -1, -1);
    // Background track
    p.setBrush(QColor(28, 28, 34));
    p.setPen(QPen(QColor(50, 50, 60), 1));
    p.drawRoundedRect(r, 4, 4);

    if (m_chunks.empty()) return;

    int totalW = r.width();
    for (size_t i = 0; i < m_chunks.size(); ++i) {
        const auto& ch = m_chunks[i];
        int xStart = r.left() + static_cast<int>(ch.startPct * totalW);
        int xEnd = r.left() + static_cast<int>(ch.endPct * totalW);
        int chW = std::max(2, xEnd - xStart);

        // Chunk container border divider
        if (i > 0) {
            p.setPen(QColor(18, 18, 22));
            p.drawLine(xStart, r.top(), xStart, r.bottom());
        }

        // Filled downloaded slice
        int fillW = static_cast<int>(ch.downloadedPct * chW);
        if (fillW > 0) {
            QRect fillRect(xStart, r.top() + 1, fillW, r.height() - 2);
            if (ch.isDone) {
                // Completed: Emerald Green
                p.fillRect(fillRect, QColor(16, 185, 129));
            } else if (ch.isActive) {
                // Active: Vibrant Cyan / Blue
                p.fillRect(fillRect, QColor(59, 130, 246));
            } else {
                p.fillRect(fillRect, QColor(100, 116, 139));
            }
        }
    }
}
