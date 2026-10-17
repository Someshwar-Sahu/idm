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

        QStyleOptionProgressBar barOption;
        barOption.rect = option.rect.adjusted(4, 4, -4, -4);
        barOption.minimum = 0;
        barOption.maximum = 100;
        barOption.progress = static_cast<int>(progress);
        barOption.text = QString::number(progress, 'f', 1) + QStringLiteral("%");
        barOption.textVisible = true;
        barOption.textAlignment = Qt::AlignCenter;

        QApplication::style()->drawControl(QStyle::CE_ProgressBar, &barOption, painter);
    } else {
        QStyledItemDelegate::paint(painter, option, index);
    }
}
