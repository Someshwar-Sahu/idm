#pragma once

#include <QStyledItemDelegate>

class ProgressBarDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit ProgressBarDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};
