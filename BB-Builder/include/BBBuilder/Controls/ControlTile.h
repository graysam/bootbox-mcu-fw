#pragma once

#include <QFrame>

#include <BBBuilder/Project.h>

QT_BEGIN_NAMESPACE
class QLabel;
class QContextMenuEvent;
class QDragEnterEvent;
class QDropEvent;
class QMimeData;
QT_END_NAMESPACE

namespace BBB {

class ControlTile : public QFrame {
    Q_OBJECT
public:
    explicit ControlTile(const ControlDescriptor &descriptor, QWidget *parent = nullptr);

    QString controlId() const { return descriptor_.id; }
    void setDescriptor(const ControlDescriptor &descriptor);
    const ControlDescriptor &descriptor() const { return descriptor_; }

signals:
    void requestEdit(const ControlDescriptor &descriptor);
    void requestDelete(const ControlDescriptor &descriptor);
    void dropPayload(const QByteArray &payload);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void buildUi();
    QString iconForType(const QString &type) const;

    ControlDescriptor descriptor_;
    QLabel *titleLabel_ = nullptr;
    QLabel *valueLabel_ = nullptr;
    QLabel *glyphLabel_ = nullptr;
    QLabel *descriptionLabel_ = nullptr;
    QLabel *addressBadge_ = nullptr;
    QLabel *formatBadge_ = nullptr;
};

} // namespace BBB
