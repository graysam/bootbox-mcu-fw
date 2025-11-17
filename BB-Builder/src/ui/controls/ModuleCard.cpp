#include <BBBuilder/Controls/ModuleCard.h>
#include <BBBuilder/Controls/ControlTile.h>

#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QMimeData>
#include <QVBoxLayout>

namespace BBB {

ModuleCard::ModuleCard(const ModuleDescriptor &module, const QString &displayLabel, QWidget *parent)
    : QFrame(parent), module_(module), moduleId_(module.name), displayLabel_(displayLabel) {
    setObjectName(QStringLiteral("moduleCard"));
    setStyleSheet(QStringLiteral(
        "#moduleCard { background:rgba(13,27,42,0.95); border:1px solid rgba(255,255,255,0.05); border-radius:16px; }"
        "#moduleCard QLabel { color:#e5f0ff; }"));
    setAcceptDrops(true);

    bodyLayout_ = new QVBoxLayout(this);
    bodyLayout_->setContentsMargins(16, 12, 16, 12);
    bodyLayout_->setSpacing(10);

    auto *header = new QLabel(displayLabel_, this);
    header->setStyleSheet(QStringLiteral("font-size:16px; font-weight:600;"));
    bodyLayout_->addWidget(header);

    if (!module_.description.isEmpty()) {
        auto *desc = new QLabel(module_.description, this);
        desc->setWordWrap(true);
        desc->setStyleSheet(QStringLiteral("color:#9ab1c9; font-size:12px;"));
        bodyLayout_->addWidget(desc);
    }

    rebuildTiles();
}

void ModuleCard::setModule(const ModuleDescriptor &module, const QString &displayLabel) {
    module_ = module;
    moduleId_ = module.name;
    displayLabel_ = displayLabel;
    rebuildTiles();
}

void ModuleCard::rebuildTiles() {
    while (bodyLayout_->count() > 1) {
        auto item = bodyLayout_->takeAt(1);
        if (auto widget = item->widget()) widget->deleteLater();
        delete item;
    }

    for (const auto &ctrl : module_.controls) {
        auto *tile = new ControlTile(ctrl, this);
        connect(tile, &ControlTile::requestEdit, this, [this, tile](const ControlDescriptor &){
            emit tileSelected(tile);
        });
        connect(tile, &ControlTile::requestDelete, this, [this](const ControlDescriptor &descriptor){
            emit controlDeleteRequested(moduleId_, descriptor.id);
        });
        bodyLayout_->addWidget(tile);
    }
    bodyLayout_->addStretch();
}

void ModuleCard::mousePressEvent(QMouseEvent *event) {
    QFrame::mousePressEvent(event);
    emit moduleSelected(moduleId_);
}

void ModuleCard::contextMenuEvent(QContextMenuEvent *event) {
    QMenu menu(this);
    QAction *editAction = menu.addAction(tr("Edit panel"));
    QAction *duplicateAction = menu.addAction(tr("Duplicate panel"));
    QAction *removeAction = menu.addAction(tr("Remove panel"));
    QAction *chosen = menu.exec(event->globalPos());
    if (!chosen) return;
    if (chosen == editAction) {
        emit moduleSelected(moduleId_);
    } else if (chosen == duplicateAction) {
        emit duplicateRequested(moduleId_);
    } else if (chosen == removeAction) {
        emit removeRequested(moduleId_);
    }
}

void ModuleCard::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasFormat("application/x-bb-drag")) {
        event->acceptProposedAction();
    } else {
        QFrame::dragEnterEvent(event);
    }
}

void ModuleCard::dropEvent(QDropEvent *event) {
    if (!event->mimeData()->hasFormat("application/x-bb-drag")) {
        QFrame::dropEvent(event);
        return;
    }
    const QByteArray payload = event->mimeData()->data("application/x-bb-drag");
    if (!payload.isEmpty()) {
        emit dropPayloadRequested(moduleId_, payload);
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

} // namespace BBB
