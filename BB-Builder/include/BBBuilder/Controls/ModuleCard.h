#pragma once

#include <QFrame>

#include <BBBuilder/Project.h>

QT_BEGIN_NAMESPACE
class QVBoxLayout;
class QContextMenuEvent;
class QDragEnterEvent;
class QDropEvent;
class QMimeData;
QT_END_NAMESPACE

namespace BBB {

class ControlTile;

class ModuleCard : public QFrame {
    Q_OBJECT
public:
    explicit ModuleCard(const ModuleDescriptor &module, const QString &displayLabel, QWidget *parent = nullptr);

    void setModule(const ModuleDescriptor &module, const QString &displayLabel);
    QString moduleId() const { return moduleId_; }
    QString displayLabel() const { return displayLabel_; }

signals:
    void tileSelected(ControlTile *tile);
    void moduleSelected(const QString &moduleId);
    void duplicateRequested(const QString &moduleId);
    void removeRequested(const QString &moduleId);
    void controlDeleteRequested(const QString &moduleId, const QString &controlId);
    void dropPayloadRequested(const QString &moduleId, const QByteArray &payload);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void rebuildTiles();

    ModuleDescriptor module_;
    QString moduleId_;
    QString displayLabel_;
    QVBoxLayout *bodyLayout_ = nullptr;
};

} // namespace BBB
