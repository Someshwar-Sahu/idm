#include "ProgressBarDelegate.h"
#include "DownloadTableModel.h"
#include <QApplication>
#include <QPainter>
#include <QStyleOptionProgressBar>

ProgressBarDelegate::ProgressBarDelegate(QObject* parent)
    : QStyledItemDelegate(parent) {
}

void ProgressBarDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    if (index.column() == DownloadTableModel::ColProgress) {
        QString statusText = index.sibling(index.row(), DownloadTableModel::ColStatus).data(Qt::DisplayRole).toString();
        bool isMissing = (statusText == QStringLiteral("File missing"));

        if (isMissing) {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            QRect rect = option.rect.adjusted(4, 4, -4, -4);
            painter->setPen(QColor(63, 63, 70));
            painter->setBrush(QColor(30, 30, 34));
            painter->drawRoundedRect(rect, 4, 4);
            painter->setPen(QColor(161, 161, 170));
            QFont font = option.font;
            font.setPointSize(8);
            painter->setFont(font);
            painter->drawText(rect, Qt::AlignCenter, QStringLiteral("Missing on disk"));
            painter->restore();
            return;
        }

        double progress = index.data(Qt::UserRole).toDouble();
        if (progress < 0.0) progress = 0.0;
        if (progress > 100.0) progress = 100.0;

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        // Center progress bar pill vertically within the table cell
        int barHeight = 16;
        int barY = option.rect.top() + (option.rect.height() - barHeight) / 2;
        QRect barRect(option.rect.left() + 6, barY, option.rect.width() - 12, barHeight);

        // 1. Background track
        painter->setPen(QColor(46, 46, 51)); // #2e2e33 subtle outline
        painter->setBrush(QColor(24, 24, 27)); // #18181b dark background
        painter->drawRoundedRect(barRect, 4, 4);

        // 2. Progress fill
        int fillWidth = static_cast<int>((barRect.width()) * (progress / 100.0));
        if (fillWidth > 0) {
            QRect fillRect(barRect.left(), barRect.top(), fillWidth, barRect.height());

            QColor startColor, endColor;
            if (statusText == QStringLiteral("Complete") || progress >= 99.99) {
                startColor = QColor(16, 185, 129); // #10b981 emerald
                endColor   = QColor(5, 150, 105);  // #059669
            } else if (statusText == QStringLiteral("Paused")) {
                startColor = QColor(245, 158, 11); // #f59e0b amber
                endColor   = QColor(217, 119, 6);  // #d97706
            } else if (statusText == QStringLiteral("Cancelled")) {
                startColor = QColor(249, 115, 22); // #f97316 orange
                endColor   = QColor(194, 65, 12);  // #c2410c
            } else if (statusText.startsWith(QStringLiteral("Error")) || statusText == QStringLiteral("Threat Blocked")) {
                startColor = QColor(239, 68, 68);  // #ef4444 red
                endColor   = QColor(185, 28, 28);  // #b91c1c
            } else {
                startColor = QColor(14, 165, 233); // #0ea5e9 sky blue
                endColor   = QColor(37, 99, 235);  // #2563eb blue
            }

            QLinearGradient grad(barRect.topLeft(), barRect.topRight());
            grad.setColorAt(0.0, startColor);
            grad.setColorAt(1.0, endColor);

            painter->setPen(Qt::NoPen);
            painter->setBrush(grad);

            // Clip to fillWidth while drawing full rounded pill for smooth corners
            painter->setClipRect(fillRect);
            painter->drawRoundedRect(barRect, 4, 4);
            painter->setClipping(false);
        }

        // 3. Crisp centered percentage text
        QFont font = option.font;
        font.setPointSize(8);
        font.setBold(true);
        painter->setFont(font);

        QString text = QString::number(progress, 'f', 1) + QStringLiteral("%");
        painter->setPen(QColor(244, 244, 245)); // crisp white/light gray
        painter->drawText(barRect, Qt::AlignCenter, text);

        painter->restore();
    } else {
        QStyledItemDelegate::paint(painter, option, index);
    }
}
