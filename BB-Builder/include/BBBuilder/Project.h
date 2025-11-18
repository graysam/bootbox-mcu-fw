#pragma once

#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <QVector>

namespace BBB {

struct ControlDescriptor {
    QString id;
    QString label;
    QString type;      // slider, knob, toggle, etc.
    QString unit;
    QString description;
    double min = 0.0;
    double max = 1.0;
    double step = 0.1;
    double defaultValue = 0.0;
    quint32 address = 0;
    quint8 byteWidth = 4;
    QString format;    // fixed5.23, u16, etc.
    bool readOnly = false;
    int algorithmIndex = 0;
};

struct ModuleDescriptor {
    QString name;
    QString description;
    QVector<ControlDescriptor> controls;
    bool dirty = false;
};

struct AlgorithmDescriptor {
    QString cellName;
    QString friendlyName;
    QString symbol;
    QString moduleName;
    int instanceIndex = 0;
    QVector<QString> controlIds;
};

struct LayoutModule {
    QString moduleName;
    QString displayLabel;
};

struct ProjectMeta {
    QString name;
    QString author;
    QString description;
};

struct EditorSettings {
    double algPanelHeight = 220.0;
    bool snap = true;
    double grid = 12.0;
    double zoom = 1.0;
    double panX = 0.0;
    double panY = 0.0;
};

class Project {
public:
    Project();

    void clear();

    ProjectMeta &meta();
    const ProjectMeta &meta() const;

    QVector<ModuleDescriptor> &modules();
    const QVector<ModuleDescriptor> &modules() const;

    QVector<AlgorithmDescriptor> &algorithms();
    const QVector<AlgorithmDescriptor> &algorithms() const;

    QVector<LayoutModule> &layout();
    const QVector<LayoutModule> &layout() const;

    QString programBinaryPath() const;
    void setProgramBinaryPath(const QString &path);

    bool saveToFile(const QString &path, QString *errorMessage = nullptr) const;
    bool loadFromFile(const QString &path, QString *errorMessage = nullptr);

    void addLayoutModule(const QString &moduleName, const QString &label = QString());
    void removeLayoutModule(int index);
    void moveLayoutModule(int from, int to);

    ModuleDescriptor* findModule(const QString &moduleName);
    const ModuleDescriptor* findModule(const QString &moduleName) const;

    ControlDescriptor* findControl(const QString &moduleName, const QString &controlId);
    const ControlDescriptor* findControl(const QString &moduleName, const QString &controlId) const;

    bool updateLayoutLabel(const QString &moduleName, const QString &displayLabel);
    bool updateModuleDescription(const QString &moduleName, const QString &description);
    bool updateControl(const QString &moduleName, const QString &previousId, const ControlDescriptor &descriptor);
    bool removeControl(const QString &moduleName, const QString &controlId);

    bool isDirty() const;
    void setDirty(bool value);
    void setAlgorithms(const QVector<AlgorithmDescriptor> &algorithms);

    // Optional UI layout payload (canvas). Stored as JSON array of widgets.
    QJsonArray canvas() const { return canvasWidgets_; }
    void setCanvas(const QJsonArray &widgets) { canvasWidgets_ = widgets; dirty_ = true; }

    EditorSettings &editor();
    const EditorSettings &editor() const;

private:
    ProjectMeta meta_;
    QVector<ModuleDescriptor> modules_;
    QVector<AlgorithmDescriptor> algorithms_;
    QVector<LayoutModule> layoutModules_;
    QString programBinaryPath_;
    bool dirty_ = false;
    QJsonArray canvasWidgets_;
    EditorSettings editor_;

    QJsonObject toJson() const;
    bool fromJson(const QJsonObject &object, QString *errorMessage);
};

} // namespace BBB
