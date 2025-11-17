#include "MainWindow.h"

#include <algorithm>

#include <BBBuilder/Controls/ControlTile.h>
#include <BBBuilder/Controls/ModuleCard.h>
#include <BBBuilder/Controls/InspectorPanel.h>
#include <BBBuilder/UI/PreviewPane.h>
#include <BBBuilder/UI/DraggableTreeWidget.h>

#include <QAction>
#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QDrag>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QStatusBar>
#include <QSet>
#include <QTabWidget>
#include <QTextEdit>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace BBB {

namespace {
QString listStyleSheet() {
    return QStringLiteral(
        "QListWidget, QTreeWidget { background-color:#0d1b2a; color:#e5f0ff; border:1px solid #1f2a44; }"
        "QListWidget::item, QTreeWidget::item { padding:8px; }"
        "QListWidget::item:selected, QTreeWidget::item:selected { background:#1f3356; }"
        "QTreeWidget::branch { background:transparent; }");
}
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), project_(std::make_unique<Project>()) {
    setupUi();
    refreshStatus();
}

void MainWindow::setupUi() {
    setWindowTitle(tr("BB Builder"));
    resize(1200, 760);

    auto *fileMenu = menuBar()->addMenu(tr("&File"));
    auto *newAction = fileMenu->addAction(tr("New Project"));
    auto *openAction = fileMenu->addAction(tr("Open Project"));
    auto *saveAction = fileMenu->addAction(tr("Save Project"));
    fileMenu->addSeparator();
    auto *importAction = fileMenu->addAction(tr("Import SigmaStudio Export"));
    auto *exportAction = fileMenu->addAction(tr("Create Bundle"));
    fileMenu->addSeparator();
    auto *quitAction = fileMenu->addAction(tr("Quit"));

    auto *editMenu = menuBar()->addMenu(tr("&Edit"));
    auto *undoAction = editMenu->addAction(tr("Undo"));
    undoAction->setShortcut(QKeySequence::Undo);
    undoAction->setEnabled(true);
    editMenu->addAction(tr("Redo"))->setEnabled(false);

    auto *viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(tr("Toggle grid"))->setEnabled(false);
    viewMenu->addAction(tr("Dark/light preview"))->setEnabled(false);

    auto *toolsMenu = menuBar()->addMenu(tr("&Tools"));
    toolsMenu->addAction(tr("Bundle validator"))->setEnabled(false);
    toolsMenu->addAction(tr("Parameter analyzer"))->setEnabled(false);

    auto *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("Documentation"))->setEnabled(false);

    connect(newAction, &QAction::triggered, this, &MainWindow::createNewProject);
    connect(openAction, &QAction::triggered, this, &MainWindow::openProject);
    connect(saveAction, &QAction::triggered, this, &MainWindow::saveProject);
    connect(importAction, &QAction::triggered, this, &MainWindow::importSigmaStudio);
    connect(exportAction, &QAction::triggered, this, &MainWindow::exportBundle);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);
    connect(undoAction, &QAction::triggered, this, &MainWindow::undoLastChange);

    auto *toolBar = addToolBar(tr("Layout Tools"));
    toolBar->setMovable(false);
    auto *addPanelAction = toolBar->addAction(tr("Add Panel"));
    connect(addPanelAction, &QAction::triggered, this, &MainWindow::addSelectedModulesToLayout);
    auto *alignAction = toolBar->addAction(tr("Align"));
    alignAction->setEnabled(false);
    auto *gridAction = toolBar->addAction(tr("Show Grid"));
    gridAction->setEnabled(false);
    auto *previewToggle = toolBar->addAction(tr("Preview Mode"));
    previewToggle->setEnabled(false);

    tabWidget_ = new QTabWidget(this);
    setCentralWidget(tabWidget_);

    auto *importTab = new QWidget(this);
    auto *importLayout = new QVBoxLayout(importTab);
    auto *modulesLabel = new QLabel(tr("Detected modules"), importTab);
    importLayout->addWidget(modulesLabel);

    auto *algLabel = new QLabel(tr("Algorithm blocks"), importTab);
    importLayout->addWidget(algLabel);
    algorithmList_ = new QListWidget(importTab);
    algorithmList_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    algorithmList_->setStyleSheet(listStyleSheet());
    importLayout->addWidget(algorithmList_, 1);

    auto *paramLabel = new QLabel(tr("Assignable parameters"), importTab);
    importLayout->addWidget(paramLabel);
    parameterList_ = new QListWidget(importTab);
    parameterList_->setSelectionMode(QAbstractItemView::SingleSelection);
    parameterList_->setStyleSheet(listStyleSheet());
    importLayout->addWidget(parameterList_, 1);

    addToLayoutButton_ = new QPushButton(tr("Add to layout"), importTab);
    auto *importBtnRow = new QHBoxLayout();
    importBtnRow->addStretch();
    importBtnRow->addWidget(addToLayoutButton_);
    importLayout->addLayout(importBtnRow);

    auto *layoutTab = new QWidget(this);
    layoutTab->setStyleSheet(QStringLiteral(
        "QLineEdit, QTextEdit, QComboBox, QSpinBox { background-color:#101e31; color:#e5f0ff; border:1px solid rgba(255,255,255,0.12); border-radius:8px; padding:4px 8px; }"
        "QComboBox QAbstractItemView { background-color:#0b1624; color:#e5f0ff; }"));
    auto *layoutSplit = new QHBoxLayout(layoutTab);
    layoutSplit->setSpacing(16);

    auto *layoutPanel = new QWidget(layoutTab);
    auto *layoutPanelLayout = new QVBoxLayout(layoutPanel);

    auto *projectMetaFrame = new QFrame(layoutPanel);
    projectMetaFrame->setObjectName("projectMetaFrame");
    projectMetaFrame->setStyleSheet(QStringLiteral(
        "#projectMetaFrame { background:rgba(13,27,42,0.7); border:1px solid rgba(255,255,255,0.06); border-radius:12px; }"
        "#projectMetaFrame QLabel { color:#e5f0ff; font-weight:600; }"
        "#projectMetaFrame QLineEdit, #projectMetaFrame QTextEdit { background:rgba(255,255,255,0.05); color:#e5f0ff; border:1px solid rgba(255,255,255,0.1); border-radius:8px; }"));
    auto *projectMetaLayout = new QVBoxLayout(projectMetaFrame);
    auto *projectMetaLabel = new QLabel(tr("Project metadata"), projectMetaFrame);
    projectNameEdit_ = new QLineEdit(projectMetaFrame);
    projectNameEdit_->setPlaceholderText(tr("Project name"));
    projectAuthorEdit_ = new QLineEdit(projectMetaFrame);
    projectAuthorEdit_->setPlaceholderText(tr("Author / team"));
    projectDescriptionEdit_ = new QTextEdit(projectMetaFrame);
    projectDescriptionEdit_->setPlaceholderText(tr("Short description or notes"));
    projectDescriptionEdit_->setFixedHeight(70);
    projectMetaApplyButton_ = new QPushButton(tr("Apply project info"), projectMetaFrame);
    connect(projectMetaApplyButton_, &QPushButton::clicked, this, &MainWindow::applyProjectMetadata);
    projectMetaLayout->addWidget(projectMetaLabel);
    projectMetaLayout->addWidget(projectNameEdit_);
    projectMetaLayout->addWidget(projectAuthorEdit_);
    projectMetaLayout->addWidget(projectDescriptionEdit_);
    projectMetaLayout->addWidget(projectMetaApplyButton_, 0, Qt::AlignRight);
    layoutPanelLayout->addWidget(projectMetaFrame);

    auto *paletteLabel = new QLabel(tr("Controls palette"), layoutPanel);
    paletteLabel->setStyleSheet(QStringLiteral("font-weight:600; margin-top:8px;"));
    layoutPanelLayout->addWidget(paletteLabel);
    controlPalette_ = new QWidget(layoutPanel);
    auto *paletteLayout = new QHBoxLayout(controlPalette_);
    paletteLayout->setSpacing(8);
    paletteLayout->setContentsMargins(0, 0, 0, 0);
    auto makePaletteButton = [this](const QString &text, const QString &type, const QString &tooltip) {
        auto *btn = new QToolButton(controlPalette_);
        btn->setText(text);
        btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
        btn->setCursor(Qt::OpenHandCursor);
        btn->setProperty("paletteType", type);
        btn->setToolTip(tooltip);
        btn->setStyleSheet(QStringLiteral("QToolButton { background-color:rgba(126,196,255,0.1); color:#e5f0ff; border:1px solid rgba(126,196,255,0.3); border-radius:8px; padding:6px 10px; }"
                                          "QToolButton:hover { background-color:rgba(126,196,255,0.2); }"));
        paletteButtons_.append(btn);
        return btn;
    };
    makePaletteButton(tr("Slider"), QStringLiteral("slider"), tr("Add a slider control"));
    makePaletteButton(tr("Knob"), QStringLiteral("knob"), tr("Add a rotary knob"));
    makePaletteButton(tr("Toggle"), QStringLiteral("toggle"), tr("Add a toggle switch"));
    makePaletteButton(tr("Meter"), QStringLiteral("meter"), tr("Add a meter/monitor"));
    makePaletteButton(tr("Label"), QStringLiteral("label"), tr("Add a text label"));
    makePaletteButton(tr("Heading"), QStringLiteral("heading"), tr("Add a module heading"));
    makePaletteButton(tr("Empty Panel"), QStringLiteral("module"), tr("Add a new module card"));
    paletteLayout->addStretch();
    for (auto *btn : paletteButtons_) {
        connect(btn, &QToolButton::pressed, this, [this, btn]() {
            const QString type = btn->property("paletteType").toString();
            startPaletteDrag(type);
        });
    }
    layoutPanelLayout->addWidget(controlPalette_);

    auto *unassignedLabel = new QLabel(tr("Unassigned modules & controls"), layoutPanel);
    unassignedLabel->setStyleSheet(QStringLiteral("font-weight:600; margin-top:12px;"));
    layoutPanelLayout->addWidget(unassignedLabel);
    unassignedTree_ = new DraggableTreeWidget(layoutPanel);
    unassignedTree_->setObjectName(QStringLiteral("unassignedTree"));
    unassignedTree_->setHeaderHidden(true);
    unassignedTree_->setStyleSheet(listStyleSheet());
    unassignedTree_->setSelectionMode(QAbstractItemView::SingleSelection);
    unassignedTree_->setMaximumHeight(220);
    layoutPanelLayout->addWidget(unassignedTree_);

    layoutPanelLayout->addWidget(new QLabel(tr("Layout order (drag controls to reorder)"), layoutTab));
    layoutList_ = new QListWidget(layoutPanel);
    layoutList_->setDragDropMode(QAbstractItemView::InternalMove);
    layoutList_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    layoutList_->setStyleSheet(listStyleSheet());
    layoutPanelLayout->addWidget(layoutList_, 1);
    removeFromLayoutButton_ = new QPushButton(tr("Remove selected"), layoutPanel);
    auto *layoutButtons = new QHBoxLayout();
    layoutButtons->addWidget(removeFromLayoutButton_);
    layoutButtons->addStretch();
    layoutPanelLayout->addLayout(layoutButtons);

    inspector_ = new InspectorPanel(layoutTab);

    layoutSplit->addWidget(layoutPanel, 2);
    layoutSplit->addWidget(inspector_, 3);

    auto *previewTab = new QWidget(this);
    auto *previewLayout = new QVBoxLayout(previewTab);
    previewInfo_ = new QLabel(tr("Import a SigmaStudio export to begin."), previewTab);
    previewLayout->addWidget(previewInfo_);
    previewScroll_ = new QScrollArea(previewTab);
    previewScroll_->setWidgetResizable(true);
    previewCanvas_ = new QWidget(previewScroll_);
    previewCardsLayout_ = new QVBoxLayout(previewCanvas_);
    previewCardsLayout_->setContentsMargins(0, 0, 0, 0);
    previewCardsLayout_->setSpacing(12);
    previewCardsLayout_->addStretch();
    previewScroll_->setWidget(previewCanvas_);
    previewLayout->addWidget(previewScroll_, 1);

    auto *livePreviewTab = new QWidget(this);
    auto *livePreviewLayout = new QVBoxLayout(livePreviewTab);
    livePreview_ = new PreviewPane(livePreviewTab);
    livePreviewLayout->addWidget(livePreview_);

    tabWidget_->addTab(importTab, tr("Import"));
    tabWidget_->addTab(layoutTab, tr("Layout & Presets"));
    tabWidget_->addTab(previewTab, tr("Canvas"));
    tabWidget_->addTab(livePreviewTab, tr("Preview (Live)"));

    statusLabel_ = new QLabel(this);
    statusBar()->addPermanentWidget(statusLabel_);

    connect(addToLayoutButton_, &QPushButton::clicked, this, &MainWindow::addSelectedModulesToLayout);
    connect(removeFromLayoutButton_, &QPushButton::clicked, this, &MainWindow::removeSelectedLayoutItems);
    connect(algorithmList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) {
        addSelectedModulesToLayout();
    });
    connect(algorithmList_, &QListWidget::currentRowChanged, this, [this](int){
        handleAlgorithmSelectionChanged();
    });
    connect(layoutList_, &QListWidget::itemSelectionChanged, this, &MainWindow::handleLayoutSelectionChanged);
    connect(layoutList_->model(), &QAbstractItemModel::rowsMoved, this, [this](const QModelIndex &, int from, int, const QModelIndex &, int to){
        project_->moveLayoutModule(from, to > from ? to - 1 : to);
        refreshPreview();
    });
    connect(inspector_, &InspectorPanel::moduleUpdated, this, &MainWindow::applyModuleUpdate);
    connect(inspector_, &InspectorPanel::controlUpdated, this, &MainWindow::applyControlUpdate);
    connect(inspector_, &InspectorPanel::controlDeleted, this, &MainWindow::deleteControl);
}

void MainWindow::refreshStatus() {
    QString summary = project_->meta().name.isEmpty()
                           ? tr("Untitled project")
                           : project_->meta().name;
    if (!currentProjectPath_.isEmpty()) {
        summary += tr(" (%1)").arg(QFileInfo(currentProjectPath_).fileName());
    }
    if (!project_->programBinaryPath().isEmpty()) {
        summary += tr(" | Binary: %1").arg(QFileInfo(project_->programBinaryPath()).fileName());
    }
    summary += tr(" | %n module(s)", nullptr, project_->modules().size());
    summary.prepend(project_->isDirty() ? QStringLiteral("● ") : QStringLiteral("○ "));
    statusLabel_->setText(summary);

    refreshAlgorithmLists();
    refreshLayoutList();
    refreshPreview();
    refreshProjectMetaFields();
}

void MainWindow::createNewProject() {
    project_->clear();
    project_->meta().name = tr("New Project");
    project_->setDirty(true);
    undoStack_.clear();
    currentProjectPath_.clear();
    if (inspector_) inspector_->clearSelection();
    refreshStatus();
}

void MainWindow::openProject() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Open Project"), QString(), tr("BB Builder Project (*.bbproj)"));
    if (path.isEmpty()) return;
    QString error;
    if (!project_->loadFromFile(path, &error)) {
        QMessageBox::warning(this, tr("Load failed"), error.isEmpty() ? tr("Unable to open project.") : error);
        return;
    }
    currentProjectPath_ = path;
    undoStack_.clear();
    if (inspector_) inspector_->clearSelection();
    refreshStatus();
    statusBar()->showMessage(tr("Loaded %1").arg(QFileInfo(path).fileName()), 4000);
}

void MainWindow::saveProject() {
    QString targetPath = currentProjectPath_;
    if (targetPath.isEmpty()) {
        targetPath = QFileDialog::getSaveFileName(this, tr("Save Project"), QString(), tr("BB Builder Project (*.bbproj)"));
        if (targetPath.isEmpty()) return;
    }
    QString error;
    if (!project_->saveToFile(targetPath, &error)) {
        QMessageBox::warning(this, tr("Save failed"), error.isEmpty() ? tr("Unable to save project.") : error);
        return;
    }
    currentProjectPath_ = targetPath;
    project_->setDirty(false);
    refreshStatus();
    statusBar()->showMessage(tr("Saved %1").arg(QFileInfo(targetPath).fileName()), 4000);
}

void MainWindow::importSigmaStudio() {
    const QString path = QFileDialog::getExistingDirectory(this, tr("Select SigmaStudio Export Folder"));
    if (path.isEmpty()) return;
    auto result = parser_.parseFromPath(path);
    if (!result.has_value()) {
        QMessageBox::warning(this, tr("Import failed"), tr("Unable to parse export at %1").arg(path));
        return;
    }
    project_->modules() = result->modules;
    if (!result->programBinaryPath.isEmpty()) {
        project_->setProgramBinaryPath(result->programBinaryPath);
    }
    project_->setDirty(true);
    if (project_->meta().name.isEmpty()) {
        project_->meta().name = QFileInfo(path).completeBaseName();
    }

    QString info = result->message;
    if (!result->programBinaryPath.isEmpty()) {
        info += tr("\nProgram: %1").arg(QFileInfo(result->programBinaryPath).fileName());
    } else {
        info += tr("\nProgram binary not found in export folder.");
    }
    if (!result->interfaceXmlPath.isEmpty()) {
        info += tr("\nSchematic XML: %1").arg(QFileInfo(result->interfaceXmlPath).fileName());
    }
    previewInfo_->setText(info);
    undoStack_.clear();
    currentProjectPath_.clear();
    if (inspector_) inspector_->clearSelection();
    refreshStatus();
}

void MainWindow::exportBundle() {
    const QString output = QFileDialog::getSaveFileName(this, tr("Create Bundle"), QString(), tr("BOOTBOX bundle (*.bbx)"));
    if (output.isEmpty()) return;
    if (project_->programBinaryPath().isEmpty()) {
        QMessageBox::warning(this, tr("Missing binary"), tr("Set a SigmaStudio program binary before exporting a bundle."));
        return;
    }
    if (project_->layout().isEmpty() && project_->modules().isEmpty()) {
        QMessageBox::warning(this, tr("No controls"), tr("Add at least one module/control to the layout before exporting."));
        return;
    }
    if (!bundleWriter_.writeBundle(*project_, output, BundleWriter::Options{})) {
        QMessageBox::warning(this, tr("Export failed"), bundleWriter_.lastError());
        return;
    }
    QMessageBox::information(this, tr("Bundle created"), tr("Bundle exported to %1").arg(output));
    statusBar()->showMessage(tr("Bundle written to %1").arg(QFileInfo(output).fileName()), 4000);
}

void MainWindow::addSelectedModulesToLayout() {
    if (!algorithmList_) return;
    const auto selected = algorithmList_->selectedItems();
    bool inserted = false;
    const auto &algs = project_->algorithms();
    for (QListWidgetItem *item : selected) {
        if (!item) continue;
        const int algIndex = item->data(Qt::UserRole).toInt();
        if (algIndex < 0 || algIndex >= algs.size()) continue;
        const auto &alg = algs.at(algIndex);
        const QString label = alg.friendlyName.isEmpty() ? alg.cellName : alg.friendlyName;
        project_->addLayoutModule(alg.moduleName, label);
        inserted = true;
    }
    if (inserted) {
        refreshLayoutList();
        refreshPreview();
    }
}

void MainWindow::removeSelectedLayoutItems() {
    const auto selected = layoutList_->selectedItems();
    QList<int> indexes;
    for (QListWidgetItem *item : selected) {
        indexes << layoutList_->row(item);
    }
    std::sort(indexes.begin(), indexes.end(), std::greater<int>());
    for (int idx : indexes) {
        project_->removeLayoutModule(idx);
        delete layoutList_->takeItem(idx);
    }
    refreshLayoutList();
    refreshPreview();
    handleLayoutSelectionChanged();
}

void MainWindow::handleLayoutSelectionChanged() {
    if (!inspector_) return;
    auto selected = layoutList_->selectedItems();
    if (selected.isEmpty()) {
        inspector_->clearSelection();
        return;
    }
    const QString moduleName = selected.first()->data(Qt::UserRole).toString();
    const ModuleDescriptor *module = project_->findModule(moduleName);
    const auto *entry = findLayoutEntry(moduleName);
    if (!module || !entry) {
        inspector_->clearSelection();
        return;
    }
    inspector_->showModule(moduleName, *module, entry->displayLabel);
}

void MainWindow::deleteControl(const QString &moduleName, const QString &controlId) {
    auto *module = project_->findModule(moduleName);
    if (!module) return;
    auto it = std::find_if(module->controls.begin(), module->controls.end(),
                           [&](const ControlDescriptor &ctrl){ return ctrl.id == controlId; });
    if (it == module->controls.end()) return;
    const int index = static_cast<int>(std::distance(module->controls.begin(), it));
    ControlDescriptor removed = *it;
    if (!project_->removeControl(moduleName, controlId)) return;
    undoStack_.append({[this, moduleName, removed, index]() {
        if (auto *module = project_->findModule(moduleName)) {
            const int insertIndex = std::min(index, static_cast<int>(module->controls.size()));
            module->controls.insert(module->controls.begin() + insertIndex, removed);
            module->dirty = true;
            project_->setDirty(true);
            refreshStatus();
            refreshPreview();
            handleLayoutSelectionChanged();
        }
    }});
    refreshStatus();
    refreshPreview();
    handleLayoutSelectionChanged();
    if (inspector_) inspector_->clearSelection();
    refreshUnassignedList();
}

void MainWindow::handleControlDropPayload(const QString &moduleId, const QString &controlId, const QByteArray &payload) {
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) return;
    const QJsonObject obj = doc.object();
    const QString kind = obj.value(QStringLiteral("kind")).toString();
    if (kind == QLatin1String("palette")) {
        applyPaletteToControl(moduleId, controlId, obj.value(QStringLiteral("type")).toString());
    } else if (kind == QLatin1String("control")) {
        remapControlFromModule(moduleId,
                               controlId,
                               obj.value(QStringLiteral("module")).toString(),
                               obj.value(QStringLiteral("control")).toString());
    }
}

void MainWindow::duplicateModulePanel(const QString &moduleId) {
    const ModuleDescriptor *module = project_->findModule(moduleId);
    if (!module) return;
    const QString label = module->name;
    project_->addLayoutModule(moduleId, label);
    refreshLayoutList();
    refreshPreview();
}

void MainWindow::removeModulePanel(const QString &moduleId) {
    auto &layoutEntries = project_->layout();
    for (int i = 0; i < layoutEntries.size(); ++i) {
        if (layoutEntries[i].moduleName == moduleId) {
            project_->removeLayoutModule(i);
            if (layoutList_ && i < layoutList_->count()) {
                delete layoutList_->takeItem(i);
            }
            break;
        }
    }
    refreshLayoutList();
    refreshPreview();
    handleLayoutSelectionChanged();
}

void MainWindow::applyProjectMetadata() {
    if (!project_) return;
    auto &meta = project_->meta();
    QString name = projectNameEdit_ ? projectNameEdit_->text().trimmed() : meta.name;
    QString author = projectAuthorEdit_ ? projectAuthorEdit_->text().trimmed() : meta.author;
    QString description = projectDescriptionEdit_ ? projectDescriptionEdit_->toPlainText().trimmed() : meta.description;
    bool changed = false;
    if (meta.name != name) {
        meta.name = name;
        changed = true;
    }
    if (meta.author != author) {
        meta.author = author;
        changed = true;
    }
    if (meta.description != description) {
        meta.description = description;
        changed = true;
    }
    if (!changed) return;
    project_->setDirty(true);
    refreshStatus();
    statusBar()->showMessage(tr("Project metadata updated"), 2500);
}

int MainWindow::findModuleIndexByName(const QString &name) const {
    const auto &modules = project_->modules();
    for (int i = 0; i < modules.size(); ++i) {
        if (modules[i].name == name) return i;
    }
    return -1;
}

void MainWindow::refreshLayoutList() {
    layoutList_->blockSignals(true);
    layoutList_->clear();
    for (const auto &entry : project_->layout()) {
        const ModuleDescriptor *module = project_->findModule(entry.moduleName);
        int controlCount = module ? module->controls.size() : 0;
        QListWidgetItem *item = new QListWidgetItem(QStringLiteral("%1 (%2 controls)")
                                                         .arg(entry.displayLabel)
                                                         .arg(controlCount));
        item->setData(Qt::UserRole, entry.moduleName);
        item->setData(Qt::UserRole + 1, entry.displayLabel);
        layoutList_->addItem(item);
    }
    layoutList_->blockSignals(false);
    refreshUnassignedList();
}

void MainWindow::refreshUnassignedList() {
    if (!unassignedTree_) return;
    unassignedTree_->blockSignals(true);
    unassignedTree_->clear();
    auto *modulesRoot = new QTreeWidgetItem(QStringList(tr("Modules not on canvas")));
    modulesRoot->setFlags((modulesRoot->flags() & ~Qt::ItemIsSelectable) & ~Qt::ItemIsDragEnabled);
    unassignedTree_->addTopLevelItem(modulesRoot);
    QSet<QString> placed;
    for (const auto &entry : project_->layout()) {
        placed.insert(entry.moduleName);
    }
    bool hasUnassigned = false;
    for (const auto &module : project_->modules()) {
        if (placed.contains(module.name)) continue;
        hasUnassigned = true;
        auto *moduleItem = new QTreeWidgetItem(modulesRoot, QStringList(module.name));
        moduleItem->setData(0, Qt::UserRole, module.name);
        moduleItem->setExpanded(false);
        for (const auto &ctrl : module.controls) {
            auto *ctrlItem = new QTreeWidgetItem(moduleItem, QStringList(QStringLiteral("• %1").arg(ctrl.label)));
            ctrlItem->setData(0, Qt::UserRole, module.name);
            ctrlItem->setData(0, Qt::UserRole + 1, ctrl.id);
        }
    }
    modulesRoot->setHidden(!hasUnassigned);
    modulesRoot->setExpanded(true);
    unassignedTree_->blockSignals(false);
}

void MainWindow::refreshAlgorithmLists() {
    if (!algorithmList_) return;
    algorithmList_->blockSignals(true);
    algorithmList_->clear();
    const auto &algs = project_->algorithms();
    for (int i = 0; i < algs.size(); ++i) {
        const auto &alg = algs.at(i);
        QString friendly = alg.friendlyName.isEmpty() ? alg.cellName : alg.friendlyName;
        QString text = friendly;
        if (alg.instanceIndex > 0) {
            text += tr(" (inst %1)").arg(alg.instanceIndex + 1);
        }
        auto *item = new QListWidgetItem(text, algorithmList_);
        item->setData(Qt::UserRole, i);
        item->setToolTip(QStringLiteral("%1\nCell: %2\nAlgorithm: %3")
                             .arg(friendly, alg.cellName, alg.symbol));
    }
    algorithmList_->blockSignals(false);
    handleAlgorithmSelectionChanged();
}

void MainWindow::handleAlgorithmSelectionChanged() {
    if (!parameterList_) return;
    parameterList_->clear();
    if (!algorithmList_ || !algorithmList_->currentItem()) return;
    const int index = algorithmList_->currentItem()->data(Qt::UserRole).toInt();
    const auto &algs = project_->algorithms();
    if (index < 0 || index >= algs.size()) return;
    const auto &alg = algs.at(index);
    for (const auto &controlId : alg.controlIds) {
        const ControlDescriptor *ctrl = project_->findControl(alg.moduleName, controlId);
        if (!ctrl) continue;
        QString label = ctrl->label.isEmpty() ? controlId : ctrl->label;
        QString detail = QStringLiteral("@0x%1").arg(QString::number(ctrl->address, 16));
        auto *item = new QListWidgetItem(QStringLiteral("%1 %2").arg(label, detail), parameterList_);
        item->setToolTip(QStringLiteral("%1\n%2").arg(label, detail));
    }
}

void MainWindow::refreshPreview() {
    if (!previewCardsLayout_) return;
    while (previewCardsLayout_->count() > 1) {
        auto item = previewCardsLayout_->takeAt(0);
        if (auto widget = item->widget()) widget->deleteLater();
        delete item;
    }

    if (project_->layout().isEmpty()) {
        auto *placeholder = new QLabel(tr("[empty layout] Add modules from the Import tab."), previewCanvas_);
        placeholder->setStyleSheet(QStringLiteral("color:#9ab1c9; padding:12px;"));
        previewCardsLayout_->insertWidget(previewCardsLayout_->count() - 1, placeholder);
        renderLivePreview();
        return;
    }

    for (const auto &entry : project_->layout()) {
        const ModuleDescriptor *module = project_->findModule(entry.moduleName);
        if (!module) continue;
        auto *card = new ModuleCard(*module, entry.displayLabel, previewCanvas_);
        connect(card, &ModuleCard::moduleSelected, this, [this](const QString &moduleId){
            for (int row = 0; row < layoutList_->count(); ++row) {
                QListWidgetItem *item = layoutList_->item(row);
                if (item->data(Qt::UserRole).toString() == moduleId) {
                    layoutList_->setCurrentItem(item);
                    break;
                }
            }
        });
        connect(card, &ModuleCard::duplicateRequested, this, &MainWindow::duplicateModulePanel);
        connect(card, &ModuleCard::removeRequested, this, &MainWindow::removeModulePanel);
        connect(card, &ModuleCard::controlDeleteRequested, this, &MainWindow::deleteControl);
        connect(card, &ModuleCard::dropPayloadRequested, this, &MainWindow::handleModuleDropPayload);
        connect(card, &ModuleCard::tileSelected, this, [this, moduleId = entry.moduleName](ControlTile *tile){
            if (!tile) return;
            for (int row = 0; row < layoutList_->count(); ++row) {
                QListWidgetItem *item = layoutList_->item(row);
                if (item->data(Qt::UserRole).toString() == moduleId) {
                    layoutList_->setCurrentItem(item);
                    break;
                }
            }
            if (inspector_) {
                inspector_->focusControl(moduleId, tile->controlId());
            }
        });
        const QString moduleId = entry.moduleName;
        const auto tiles = card->findChildren<ControlTile *>();
        for (ControlTile *tile : tiles) {
            if (!tile) continue;
            const QString controlId = tile->controlId();
            connect(tile, &ControlTile::dropPayload, this, [this, moduleId, controlId](const QByteArray &payload) {
                handleControlDropPayload(moduleId, controlId, payload);
            });
        }
        previewCardsLayout_->insertWidget(previewCardsLayout_->count() - 1, card);
    }
    renderLivePreview();
}

void MainWindow::applyModuleUpdate(const QString &moduleName, const QString &displayLabel, const QString &description) {
    const LayoutModule *entry = findLayoutEntry(moduleName);
    const ModuleDescriptor *module = project_->findModule(moduleName);
    if (!module || !entry) return;
    const QString prevLabel = entry->displayLabel;
    const QString prevDescription = module->description;
    if (prevLabel == displayLabel && prevDescription == description) return;
    project_->updateLayoutLabel(moduleName, displayLabel);
    project_->updateModuleDescription(moduleName, description);
    undoStack_.append({[this, moduleName, prevLabel, prevDescription]() {
        project_->updateLayoutLabel(moduleName, prevLabel);
        project_->updateModuleDescription(moduleName, prevDescription);
        refreshLayoutList();
        refreshPreview();
        handleLayoutSelectionChanged();
    }});
    refreshLayoutList();
    refreshPreview();
    handleLayoutSelectionChanged();
}

void MainWindow::applyControlUpdate(const QString &moduleName, const QString &previousId, const ControlDescriptor &control) {
    const ControlDescriptor *prevPtr = project_->findControl(moduleName, previousId);
    if (!prevPtr) return;
    ControlDescriptor previous = *prevPtr;
    if (!project_->updateControl(moduleName, previousId, control)) return;
    undoStack_.append({[this, moduleName, previous, control]() {
        project_->updateControl(moduleName, control.id, previous);
        refreshPreview();
        handleLayoutSelectionChanged();
    }});
    refreshPreview();
    handleLayoutSelectionChanged();
}

void MainWindow::undoLastChange() {
    if (undoStack_.isEmpty()) return;
    auto command = undoStack_.takeLast();
    if (command.undo) {
        command.undo();
    }
}

const LayoutModule* MainWindow::findLayoutEntry(const QString &moduleName) const {
    for (const auto &entry : project_->layout()) {
        if (entry.moduleName == moduleName) {
            return &entry;
        }
    }
    return nullptr;
}

void MainWindow::refreshProjectMetaFields() {
    if (!projectNameEdit_ || !project_) return;
    const auto &meta = project_->meta();
    projectNameEdit_->setText(meta.name);
    if (projectAuthorEdit_) projectAuthorEdit_->setText(meta.author);
    if (projectDescriptionEdit_) projectDescriptionEdit_->setText(meta.description);
}

void MainWindow::handleModuleDropPayload(const QString &moduleId, const QByteArray &payload) {
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) return;
    const QJsonObject obj = doc.object();
    const QString kind = obj.value(QStringLiteral("kind")).toString();
    if (kind == QLatin1String("palette")) {
        createControlFromPalette(moduleId, obj.value(QStringLiteral("type")).toString());
    } else if (kind == QLatin1String("control")) {
        addControlFromModule(moduleId,
                             obj.value(QStringLiteral("module")).toString(),
                             obj.value(QStringLiteral("control")).toString());
    } else if (kind == QLatin1String("module")) {
        duplicateModulePanel(obj.value(QStringLiteral("module")).toString());
    }
}

void MainWindow::startPaletteDrag(const QString &controlType) {
    if (controlType.isEmpty()) return;
    auto *drag = new QDrag(this);
    auto *mime = new QMimeData();
    QJsonObject obj;
    obj.insert(QStringLiteral("kind"), QStringLiteral("palette"));
    obj.insert(QStringLiteral("type"), controlType);
    mime->setData(QStringLiteral("application/x-bb-drag"), QJsonDocument(obj).toJson());
    drag->setMimeData(mime);
    drag->exec(Qt::CopyAction);
}

QString MainWindow::generateControlId(const ModuleDescriptor &module, const QString &base) const {
    QString sanitizedBase = base;
    sanitizedBase.replace(' ', '_');
    if (sanitizedBase.isEmpty()) sanitizedBase = QStringLiteral("control");
    QSet<QString> existing;
    for (const auto &ctrl : module.controls) {
        existing.insert(ctrl.id);
    }
    QString candidate = sanitizedBase;
    int suffix = 1;
    while (existing.contains(candidate)) {
        candidate = QStringLiteral("%1_%2").arg(sanitizedBase).arg(++suffix);
    }
    return candidate;
}

void MainWindow::createControlFromPalette(const QString &moduleId, const QString &controlType) {
    if (moduleId.isEmpty() || controlType.isEmpty()) return;
    auto *module = project_->findModule(moduleId);
    if (!module) return;
    ControlDescriptor descriptor;
    descriptor.type = controlType.toLower();
    descriptor.id = generateControlId(*module, descriptor.type);
    descriptor.label = descriptor.id;
    descriptor.unit.clear();
    descriptor.step = 0.1;
    descriptor.min = 0.0;
    descriptor.max = 1.0;
    descriptor.defaultValue = 0.0;
    descriptor.address = 0;
    descriptor.format = QStringLiteral("fixed5.23");
    if (descriptor.type == QLatin1String("slider") || descriptor.type == QLatin1String("knob")) {
        descriptor.min = -60.0;
        descriptor.max = 12.0;
        descriptor.defaultValue = 0.0;
        descriptor.unit = QStringLiteral("dB");
        descriptor.step = 0.5;
    } else if (descriptor.type == QLatin1String("toggle")) {
        descriptor.min = 0.0;
        descriptor.max = 1.0;
        descriptor.step = 1.0;
    } else if (descriptor.type == QLatin1String("meter")) {
        descriptor.unit = QStringLiteral("dB");
        descriptor.min = -80.0;
        descriptor.max = 12.0;
        descriptor.defaultValue = -12.0;
    } else if (descriptor.type == QLatin1String("label") || descriptor.type == QLatin1String("heading")) {
        descriptor.unit.clear();
        descriptor.min = 0.0;
        descriptor.max = 0.0;
        descriptor.step = 1.0;
        descriptor.defaultValue = 0.0;
        descriptor.format = QStringLiteral("raw");
    } else if (descriptor.type == QLatin1String("module")) {
        project_->addLayoutModule(moduleId, module->name);
        refreshLayoutList();
        refreshPreview();
        return;
    }
    module->controls.push_back(descriptor);
    module->dirty = true;
    project_->setDirty(true);
    refreshStatus();
    refreshPreview();
    handleLayoutSelectionChanged();
}

void MainWindow::addControlFromModule(const QString &targetModule, const QString &sourceModule, const QString &controlId) {
    if (targetModule.isEmpty() || sourceModule.isEmpty() || controlId.isEmpty()) return;
    auto *target = project_->findModule(targetModule);
    const auto *source = project_->findModule(sourceModule);
    if (!target || !source) return;
    const ControlDescriptor *control = project_->findControl(sourceModule, controlId);
    if (!control) return;
    ControlDescriptor copy = *control;
    copy.id = generateControlId(*target, control->id);
    target->controls.push_back(copy);
    target->dirty = true;
    project_->setDirty(true);
    refreshStatus();
    refreshPreview();
    handleLayoutSelectionChanged();
}

void MainWindow::applyPaletteToControl(const QString &moduleId, const QString &controlId, const QString &controlType) {
    if (moduleId.isEmpty() || controlId.isEmpty() || controlType.isEmpty()) return;
    ControlDescriptor *control = project_->findControl(moduleId, controlId);
    if (!control) return;
    ControlDescriptor updated = *control;
    const QString type = controlType.toLower();
    updated.type = type;
    if (type == QLatin1String("slider") || type == QLatin1String("knob")) {
        updated.min = -60.0;
        updated.max = 12.0;
        updated.unit = QStringLiteral("dB");
        updated.step = 0.5;
        updated.defaultValue = 0.0;
    } else if (type == QLatin1String("toggle")) {
        updated.min = 0.0;
        updated.max = 1.0;
        updated.unit.clear();
        updated.step = 1.0;
        updated.defaultValue = 0.0;
    } else if (type == QLatin1String("meter")) {
        updated.min = -80.0;
        updated.max = 12.0;
        updated.unit = QStringLiteral("dB");
        updated.step = 0.5;
        updated.defaultValue = -12.0;
    } else if (type == QLatin1String("label") || type == QLatin1String("heading")) {
        updated.unit.clear();
        updated.min = 0.0;
        updated.max = 0.0;
        updated.step = 1.0;
        updated.defaultValue = 0.0;
        updated.address = 0;
        updated.format = QStringLiteral("raw");
    }
    if (project_->updateControl(moduleId, controlId, updated)) {
        refreshPreview();
    }
}

void MainWindow::remapControlFromModule(const QString &targetModule, const QString &controlId, const QString &sourceModule, const QString &sourceControl) {
    if (targetModule.isEmpty() || controlId.isEmpty() || sourceModule.isEmpty() || sourceControl.isEmpty()) return;
    const ControlDescriptor *sourceDesc = project_->findControl(sourceModule, sourceControl);
    ControlDescriptor *target = project_->findControl(targetModule, controlId);
    if (!sourceDesc || !target) return;
    ControlDescriptor updated = *target;
    updated.label = sourceDesc->label;
    updated.description = sourceDesc->description;
    updated.unit = sourceDesc->unit;
    updated.min = sourceDesc->min;
    updated.max = sourceDesc->max;
    updated.step = sourceDesc->step;
    updated.defaultValue = sourceDesc->defaultValue;
    updated.address = sourceDesc->address;
    updated.byteWidth = sourceDesc->byteWidth;
    updated.format = sourceDesc->format;
    updated.readOnly = sourceDesc->readOnly;
    updated.type = sourceDesc->type;
    if (project_->updateControl(targetModule, controlId, updated)) {
        refreshPreview();
    }
}

QJsonObject MainWindow::buildPreviewSchema() const {
    QJsonObject root;
    root.insert(QStringLiteral("project"), project_->meta().name);
    QJsonArray modules;
    for (const auto &entry : project_->layout()) {
        const ModuleDescriptor *module = project_->findModule(entry.moduleName);
        if (!module) continue;
        QJsonObject modObj;
        modObj.insert(QStringLiteral("name"), module->name);
        modObj.insert(QStringLiteral("title"), entry.displayLabel.isEmpty() ? module->name : entry.displayLabel);
        modObj.insert(QStringLiteral("description"), module->description);
        QJsonArray controls;
        for (const auto &ctrl : module->controls) {
            QJsonObject ctrlObj;
            ctrlObj.insert(QStringLiteral("id"), ctrl.id);
            ctrlObj.insert(QStringLiteral("label"), ctrl.label);
            ctrlObj.insert(QStringLiteral("type"), ctrl.type);
            ctrlObj.insert(QStringLiteral("unit"), ctrl.unit);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            ctrlObj.insert(QStringLiteral("min"), ctrl.min);
            ctrlObj.insert(QStringLiteral("max"), ctrl.max);
            ctrlObj.insert(QStringLiteral("step"), ctrl.step);
            ctrlObj.insert(QStringLiteral("default"), ctrl.defaultValue);
#else
            ctrlObj.insert(QStringLiteral("min"), QJsonValue(ctrl.min));
            ctrlObj.insert(QStringLiteral("max"), QJsonValue(ctrl.max));
            ctrlObj.insert(QStringLiteral("step"), QJsonValue(ctrl.step));
            ctrlObj.insert(QStringLiteral("default"), QJsonValue(ctrl.defaultValue));
#endif
            ctrlObj.insert(QStringLiteral("address"), static_cast<qint64>(ctrl.address));
            ctrlObj.insert(QStringLiteral("format"), ctrl.format);
            controls.append(ctrlObj);
        }
        modObj.insert(QStringLiteral("controls"), controls);
        modules.append(modObj);
    }
    root.insert(QStringLiteral("modules"), modules);
    return root;
}

void MainWindow::renderLivePreview() {
    if (!livePreview_) return;
    livePreview_->setSchema(buildPreviewSchema());
}

} // namespace BBB
