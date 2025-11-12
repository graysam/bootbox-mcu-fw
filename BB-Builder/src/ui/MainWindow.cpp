#include "MainWindow.h"

#include <QAction>
#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QVBoxLayout>

namespace BBB {

namespace {
QString listStyleSheet() {
    return QStringLiteral(
        "QListWidget { background-color:#0d1b2a; color:#e5f0ff; border:1px solid #1f2a44; }"
        "QListWidget::item { padding:8px; }"
        "QListWidget::item:selected { background:#1f3356; }");
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

    connect(newAction, &QAction::triggered, this, &MainWindow::createNewProject);
    connect(openAction, &QAction::triggered, this, &MainWindow::openProject);
    connect(saveAction, &QAction::triggered, this, &MainWindow::saveProject);
    connect(importAction, &QAction::triggered, this, &MainWindow::importSigmaStudio);
    connect(exportAction, &QAction::triggered, this, &MainWindow::exportBundle);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    tabWidget_ = new QTabWidget(this);
    setCentralWidget(tabWidget_);

    auto *importTab = new QWidget(this);
    auto *importLayout = new QVBoxLayout(importTab);
    auto *modulesLabel = new QLabel(tr("Detected modules"), importTab);
    importLayout->addWidget(modulesLabel);

    moduleList_ = new QListWidget(importTab);
    moduleList_->setObjectName("moduleList");
    moduleList_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    moduleList_->setStyleSheet(listStyleSheet());
    importLayout->addWidget(moduleList_, 1);

    addToLayoutButton_ = new QPushButton(tr("Add to layout"), importTab);
    auto *importBtnRow = new QHBoxLayout();
    importBtnRow->addStretch();
    importBtnRow->addWidget(addToLayoutButton_);
    importLayout->addLayout(importBtnRow);

    auto *layoutTab = new QWidget(this);
    auto *layoutLayout = new QVBoxLayout(layoutTab);
    layoutLayout->addWidget(new QLabel(tr("Layout order (drag controls to reorder)"), layoutTab));
    layoutList_ = new QListWidget(layoutTab);
    layoutList_->setDragDropMode(QAbstractItemView::InternalMove);
    layoutList_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    layoutList_->setStyleSheet(listStyleSheet());
    layoutLayout->addWidget(layoutList_, 1);
    removeFromLayoutButton_ = new QPushButton(tr("Remove selected"), layoutTab);
    auto *layoutButtons = new QHBoxLayout();
    layoutButtons->addWidget(removeFromLayoutButton_);
    layoutButtons->addStretch();
    layoutLayout->addLayout(layoutButtons);

    auto *previewTab = new QWidget(this);
    auto *previewLayout = new QVBoxLayout(previewTab);
    previewInfo_ = new QLabel(tr("Import a SigmaStudio export to begin."), previewTab);
    previewLayout->addWidget(previewInfo_);
    previewList_ = new QListWidget(previewTab);
    previewList_->setStyleSheet(listStyleSheet());
    previewLayout->addWidget(previewList_, 1);

    tabWidget_->addTab(importTab, tr("Import"));
    tabWidget_->addTab(layoutTab, tr("Layout & Presets"));
    tabWidget_->addTab(previewTab, tr("Preview"));

    statusLabel_ = new QLabel(this);
    statusBar()->addPermanentWidget(statusLabel_);

    connect(addToLayoutButton_, &QPushButton::clicked, this, &MainWindow::addSelectedModulesToLayout);
    connect(removeFromLayoutButton_, &QPushButton::clicked, this, &MainWindow::removeSelectedLayoutItems);
    connect(moduleList_, &QListWidget::itemDoubleClicked, this, &MainWindow::handleModuleDoubleClicked);
    connect(layoutList_->model(), &QAbstractItemModel::rowsMoved, this, [this](const QModelIndex &, int from, int, const QModelIndex &, int to){
        project_->moveLayoutModule(from, to > from ? to - 1 : to);
        refreshPreview();
    });
}

void MainWindow::refreshStatus() {
    QString summary = project_->meta().name.isEmpty()
                           ? tr("Untitled project")
                           : project_->meta().name;
    if (!project_->programBinaryPath().isEmpty()) {
        summary += tr(" | Binary: %1").arg(QFileInfo(project_->programBinaryPath()).fileName());
    }
    summary += tr(" | %n module(s)", nullptr, project_->modules().size());
    statusLabel_->setText(summary);

    moduleList_->clear();
    for (const auto &module : project_->modules()) {
        QListWidgetItem *header = new QListWidgetItem(QStringLiteral("▶ %1 (%2 controls)")
                                                          .arg(module.name)
                                                          .arg(module.controls.size()));
        header->setData(Qt::UserRole, module.name);
        QFont f = header->font();
        f.setBold(true);
        header->setFont(f);
        moduleList_->addItem(header);
        for (const auto &ctrl : module.controls) {
            QListWidgetItem *item = new QListWidgetItem(QStringLiteral("   • %1 @0x%2")
                                                            .arg(ctrl.label)
                                                            .arg(QString::number(ctrl.address, 16)));
            item->setFlags(Qt::ItemIsEnabled);
            moduleList_->addItem(item);
        }
    }
    refreshLayoutList();
    refreshPreview();
}

void MainWindow::createNewProject() {
    project_->clear();
    project_->meta().name = tr("New Project");
    project_->setDirty(true);
    refreshStatus();
}

void MainWindow::openProject() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Open Project"), QString(), tr("BB Builder Project (*.bbproj)"));
    if (path.isEmpty()) return;
    QMessageBox::information(this, tr("Not Implemented"), tr("Project loading is not implemented yet."));
}

void MainWindow::saveProject() {
    const QString path = QFileDialog::getSaveFileName(this, tr("Save Project"), QString(), tr("BB Builder Project (*.bbproj)"));
    if (path.isEmpty()) return;
    QMessageBox::information(this, tr("Not Implemented"), tr("Project saving is not implemented yet."));
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
    refreshStatus();
}

void MainWindow::exportBundle() {
    const QString output = QFileDialog::getSaveFileName(this, tr("Create Bundle"), QString(), tr("BOOTBOX bundle (*.bbx)"));
    if (output.isEmpty()) return;
    if (!bundleWriter_.writeBundle(*project_, output, BundleWriter::Options{})) {
        QMessageBox::warning(this, tr("Export failed"), bundleWriter_.lastError());
        return;
    }
    QMessageBox::information(this, tr("Bundle created"), tr("Bundle exported to %1").arg(output));
}

void MainWindow::addSelectedModulesToLayout() {
    const auto selected = moduleList_->selectedItems();
    bool inserted = false;
    for (QListWidgetItem *item : selected) {
        const QString moduleName = item->data(Qt::UserRole).toString();
        if (moduleName.isEmpty()) continue;
        project_->addLayoutModule(moduleName, moduleName);
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
    refreshPreview();
}

void MainWindow::handleModuleDoubleClicked(QListWidgetItem *item) {
    if (!item) return;
    const QString moduleName = item->data(Qt::UserRole).toString();
    if (moduleName.isEmpty()) return;
    project_->addLayoutModule(moduleName, moduleName);
    refreshLayoutList();
    refreshPreview();
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
        QListWidgetItem *item = new QListWidgetItem(entry.displayLabel);
        item->setData(Qt::UserRole, entry.moduleName);
        layoutList_->addItem(item);
    }
    layoutList_->blockSignals(false);
}

void MainWindow::refreshPreview() {
    previewList_->clear();
    if (project_->layout().isEmpty()) {
        previewList_->addItem(tr("[empty layout] Add modules from the Import tab."));
        return;
    }
    for (const auto &entry : project_->layout()) {
        const int idx = findModuleIndexByName(entry.moduleName);
        QString detail;
        if (idx >= 0) {
            const auto &module = project_->modules()[idx];
            detail = QStringLiteral("%1 • %2 controls").arg(entry.displayLabel).arg(module.controls.size());
        } else {
            detail = QStringLiteral("%1 • (unknown module)").arg(entry.displayLabel);
        }
        QListWidgetItem *item = new QListWidgetItem(detail);
        item->setData(Qt::UserRole, entry.moduleName);
        previewList_->addItem(item);
    }
}

} // namespace BBB
