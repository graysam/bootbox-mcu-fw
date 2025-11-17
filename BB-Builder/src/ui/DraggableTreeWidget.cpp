#include <BBBuilder/UI/DraggableTreeWidget.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>

namespace BBB {

DraggableTreeWidget::DraggableTreeWidget(QWidget *parent)
    : QTreeWidget(parent) {
    setDragEnabled(true);
}

QStringList DraggableTreeWidget::mimeTypes() const {
    return { QStringLiteral("application/x-bb-drag") };
}

Qt::DropActions DraggableTreeWidget::supportedDropActions() const {
    return Qt::CopyAction;
}

QMimeData *DraggableTreeWidget::mimeData(const QList<QTreeWidgetItem *> &items) const {
    if (items.isEmpty()) return nullptr;
    QTreeWidgetItem *item = items.first();
    if (!item) return nullptr;
    QString moduleName = item->data(0, Qt::UserRole).toString();
    QString controlId = item->data(0, Qt::UserRole + 1).toString();
    if (moduleName.isEmpty() && controlId.isEmpty()) return nullptr;
    QJsonObject obj;
    if (!controlId.isEmpty()) {
        obj.insert(QStringLiteral("kind"), QStringLiteral("control"));
        obj.insert(QStringLiteral("control"), controlId);
    } else {
        obj.insert(QStringLiteral("kind"), QStringLiteral("module"));
    }
    obj.insert(QStringLiteral("module"), moduleName);

    auto *mime = new QMimeData();
    mime->setData(QStringLiteral("application/x-bb-drag"), QJsonDocument(obj).toJson());
    mime->setText(item->text(0));
    return mime;
}

} // namespace BBB
