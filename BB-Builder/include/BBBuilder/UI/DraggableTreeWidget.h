#pragma once

#include <QTreeWidget>

namespace BBB {

class DraggableTreeWidget : public QTreeWidget {
    Q_OBJECT
public:
    explicit DraggableTreeWidget(QWidget *parent = nullptr);

protected:
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QList<QTreeWidgetItem *> &items) const override;
    Qt::DropActions supportedDropActions() const override;
};

} // namespace BBB
