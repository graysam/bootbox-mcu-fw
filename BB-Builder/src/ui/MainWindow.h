#pragma once

#include <QMainWindow>
#include <memory>

#include <BBBuilder/Project.h>
#include <BBBuilder/SigmaParser.h>
#include <BBBuilder/BundleWriter.h>

QT_BEGIN_NAMESPACE
class QTabWidget;
class QTextEdit;
class QListWidget;
class QListWidgetItem;
class QLabel;
class QPushButton;
QT_END_NAMESPACE

namespace BBB {

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
    void handleModuleDoubleClicked(QListWidgetItem *item);

private:
    void setupUi();
    void refreshStatus();
    void refreshLayoutList();
    void refreshPreview();
    void rebuildLayoutFromWidget();
    int findModuleIndexByName(const QString &name) const;

    std::unique_ptr<Project> project_;
    SigmaParser parser_;
    BundleWriter bundleWriter_;

    QTabWidget *tabWidget_ = nullptr;
    QListWidget *moduleList_ = nullptr;
    QListWidget *layoutList_ = nullptr;
    QLabel *previewInfo_ = nullptr;
    QListWidget *previewList_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QPushButton *addToLayoutButton_ = nullptr;
    QPushButton *removeFromLayoutButton_ = nullptr;
};

} // namespace BBB
