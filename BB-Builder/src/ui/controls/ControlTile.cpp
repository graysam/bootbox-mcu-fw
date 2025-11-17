#include <BBBuilder/Controls/ControlTile.h>

#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>

namespace BBB {

ControlTile::ControlTile(const ControlDescriptor &descriptor, QWidget *parent)
    : QFrame(parent), descriptor_(descriptor) {
    setObjectName(QStringLiteral("controlTile"));
    setCursor(Qt::PointingHandCursor);
    setMinimumHeight(56);
    setStyleSheet(QStringLiteral(
        "#controlTile { border: 1px solid rgba(255,255,255,0.08);"
        " border-radius: 12px; background-color: rgba(255,255,255,0.02); }"));
    setAcceptDrops(true);
    buildUi();
}

void ControlTile::setDescriptor(const ControlDescriptor &descriptor) {
    descriptor_ = descriptor;
    buildUi();
}

void ControlTile::buildUi() {
    delete layout();
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(12, 8, 12, 8);
    row->setSpacing(12);

    if (!glyphLabel_) {
        glyphLabel_ = new QLabel(this);
        glyphLabel_->setAlignment(Qt::AlignCenter);
        glyphLabel_->setFixedWidth(36);
        glyphLabel_->setStyleSheet(QStringLiteral("color:#7ec4ff; font-size:18px;"));
    }
    glyphLabel_->setText(iconForType(descriptor_.type));

    if (!titleLabel_) titleLabel_ = new QLabel(this);
    if (!valueLabel_) valueLabel_ = new QLabel(this);
    if (!descriptionLabel_) {
        descriptionLabel_ = new QLabel(this);
        descriptionLabel_->setStyleSheet(QStringLiteral("color:#9ab1c9; font-size:11px;"));
        descriptionLabel_->setWordWrap(true);
    }

    titleLabel_->setText(QStringLiteral("%1 (%2)").arg(descriptor_.label, descriptor_.type));
    titleLabel_->setStyleSheet(QStringLiteral("color:#e5f0ff; font-weight:600;"));

    QString valueText;
    if (!descriptor_.unit.isEmpty()) {
        valueText = QStringLiteral("%1 %2")
                        .arg(QString::number(descriptor_.defaultValue, 'f', descriptor_.step < 1 ? 2 : 0))
                        .arg(descriptor_.unit);
    } else {
        valueText = QString::number(descriptor_.defaultValue, 'f', descriptor_.step < 1 ? 2 : 0);
    }
    valueLabel_->setText(valueText);
    valueLabel_->setStyleSheet(QStringLiteral("color:#9ab1c9;"));

    if (descriptor_.description.trimmed().isEmpty()) {
        descriptionLabel_->setVisible(false);
    } else {
        descriptionLabel_->setVisible(true);
        descriptionLabel_->setText(descriptor_.description.trimmed());
    }

    auto *textCol = new QVBoxLayout();
    textCol->setContentsMargins(0, 0, 0, 0);
    textCol->addWidget(titleLabel_);
    if (descriptionLabel_->isVisible()) {
        textCol->addWidget(descriptionLabel_);
    }

    if (!addressBadge_) {
        addressBadge_ = new QLabel(this);
        addressBadge_->setStyleSheet(QStringLiteral("background:rgba(126,196,255,0.12); color:#9ed1ff; border-radius:6px; padding:2px 6px; font-size:10px;"));
    }
    if (!formatBadge_) {
        formatBadge_ = new QLabel(this);
        formatBadge_->setStyleSheet(QStringLiteral("background:rgba(255,255,255,0.08); color:#d0daff; border-radius:6px; padding:2px 6px; font-size:10px;"));
    }
    if (descriptor_.address) {
        addressBadge_->setText(QStringLiteral("0x%1").arg(descriptor_.address, 0, 16).toUpper());
        addressBadge_->setVisible(true);
    } else {
        addressBadge_->setText(tr("UI-only"));
        addressBadge_->setVisible(true);
    }
    formatBadge_->setText(descriptor_.format.isEmpty() ? QStringLiteral("fixed5.23") : descriptor_.format);
    auto *badgeRow = new QHBoxLayout();
    badgeRow->setContentsMargins(0, 0, 0, 0);
    badgeRow->setSpacing(6);
    badgeRow->addWidget(addressBadge_);
    badgeRow->addWidget(formatBadge_);
    badgeRow->addStretch();
    textCol->addLayout(badgeRow);

    row->addWidget(glyphLabel_);
    row->addLayout(textCol, 1);
    row->addWidget(valueLabel_, 0, Qt::AlignRight | Qt::AlignVCenter);
}

void ControlTile::mousePressEvent(QMouseEvent *event) {
    QFrame::mousePressEvent(event);
    emit requestEdit(descriptor_);
}

void ControlTile::paintEvent(QPaintEvent *event) {
    QFrame::paintEvent(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QColor border(255, 255, 255, 25);
    painter.setPen(QPen(border, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(rect().adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);
}

QString ControlTile::iconForType(const QString &type) const {
    const QString lower = type.toLower();
    if (lower.contains(QStringLiteral("toggle"))) return QStringLiteral("⏻");
    if (lower.contains(QStringLiteral("knob"))) return QStringLiteral("◉");
    if (lower.contains(QStringLiteral("fader"))) return QStringLiteral("▮");
    if (lower.contains(QStringLiteral("meter"))) return QStringLiteral("▤");
    return QStringLiteral("⇅");
}

void ControlTile::contextMenuEvent(QContextMenuEvent *event) {
    QMenu menu(this);
    QAction *editAction = menu.addAction(tr("Edit control"));
    QAction *deleteAction = menu.addAction(tr("Delete control"));
    QAction *chosen = menu.exec(event->globalPos());
    if (!chosen) return;
    if (chosen == editAction) {
        emit requestEdit(descriptor_);
    } else if (chosen == deleteAction) {
        emit requestDelete(descriptor_);
    }
}

void ControlTile::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasFormat("application/x-bb-drag")) {
        event->acceptProposedAction();
    } else {
        QFrame::dragEnterEvent(event);
    }
}

void ControlTile::dropEvent(QDropEvent *event) {
    if (!event->mimeData()->hasFormat("application/x-bb-drag")) {
        QFrame::dropEvent(event);
        return;
    }
    const QByteArray payload = event->mimeData()->data("application/x-bb-drag");
    if (!payload.isEmpty()) {
        emit dropPayload(payload);
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

} // namespace BBB
