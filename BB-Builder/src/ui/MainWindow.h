#pragma once

#include <QMainWindow>
#include <memory>
#include <functional>

#include <BBBuilder/Project.h>
#include <BBBuilder/SigmaParser.h>
#include <BBBuilder/BundleWriter.h>

QT_BEGIN_NAMESPACE
class QTabWidget;
class QListWidget;
class QListWidgetItem;
class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class QLineEdit;
class QTextEdit;
class QToolButton;
QT_END_NAMESPACE

namespace BBB {

class ModuleCard;
class InspectorPanel;
class PreviewPane;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void createNewProject();
    void openProject();
    void saveProject();
    void importSigmaStudio();
    void exportBundle();
    void addSelectedModulesToLayout();
    void removeSelectedLayoutItems();
    void handleLayoutSelectionChanged();
    void applyProjectMetadata();
    void deleteControl(const QString &moduleName, const QString &controlId);
    void duplicateModulePanel(const QString &moduleId);
    void removeModulePanel(const QString &moduleId);
    void handleAlgorithmSelectionChanged();
    void handleControlDropPayload(const QString &moduleId, const QString &controlId, const QByteArray &payload);
    void applyModuleUpdate(const QString &moduleName, const QString &displayLabel, const QString &description);
    void applyControlUpdate(const QString &moduleName, const QString &previousId, const ControlDescriptor &control);
    void undoLastChange();

private:
    void setupUi();
    void refreshStatus();
    void refreshLayoutList();
    void refreshAlgorithmLists();
    void refreshPreview();
    void rebuildLayoutFromWidget();
    int findModuleIndexByName(const QString &name) const;
    const LayoutModule* findLayoutEntry(const QString &moduleName) const;
    void refreshProjectMetaFields();
    void refreshUnassignedList();
    void handleModuleDropPayload(const QString &moduleId, const QByteArray &payload);
    void startPaletteDrag(const QString &controlType);
    void createControlFromPalette(const QString &moduleId, const QString &controlType);
    void addControlFromModule(const QString &targetModule, const QString &sourceModule, const QString &controlId);
    void applyPaletteToControl(const QString &moduleId, const QString &controlId, const QString &controlType);
    void remapControlFromModule(const QString &targetModule, const QString &controlId, const QString &sourceModule, const QString &sourceControl);
    QString generateControlId(const ModuleDescriptor &module, const QString &base) const;
    QJsonObject buildPreviewSchema() const;
    void renderLivePreview();

    std::unique_ptr<Project> project_;
    SigmaParser parser_;
    BundleWriter bundleWriter_;

    QTabWidget *tabWidget_ = nullptr;
    QListWidget *algorithmList_ = nullptr;
    QListWidget *parameterList_ = nullptr;
    QListWidget *layoutList_ = nullptr;
    QTreeWidget *unassignedTree_ = nullptr;
    QLabel *previewInfo_ = nullptr;
    QScrollArea *previewScroll_ = nullptr;
    QWidget *previewCanvas_ = nullptr;
    QVBoxLayout *previewCardsLayout_ = nullptr;
    PreviewPane *livePreview_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QPushButton *addToLayoutButton_ = nullptr;
    QPushButton *removeFromLayoutButton_ = nullptr;
    InspectorPanel *inspector_ = nullptr;
    QLineEdit *projectNameEdit_ = nullptr;
    QLineEdit *projectAuthorEdit_ = nullptr;
    QTextEdit *projectDescriptionEdit_ = nullptr;
    QPushButton *projectMetaApplyButton_ = nullptr;
    QWidget *controlPalette_ = nullptr;
    QVector<QToolButton*> paletteButtons_;

    struct UndoCommand {
        std::function<void()> undo;
    };
    QVector<UndoCommand> undoStack_;
    QString currentProjectPath_;
};

} // namespace BBB
