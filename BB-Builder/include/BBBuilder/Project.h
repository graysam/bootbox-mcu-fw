#pragma once

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
};

struct ModuleDescriptor {
    QString name;
    QString description;
    QVector<ControlDescriptor> controls;
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

class Project {
public:
    Project();

    void clear();

    ProjectMeta &meta();
    const ProjectMeta &meta() const;

    QVector<ModuleDescriptor> &modules();
    const QVector<ModuleDescriptor> &modules() const;

    QVector<LayoutModule> &layout();
    const QVector<LayoutModule> &layout() const;

    QString programBinaryPath() const;
    void setProgramBinaryPath(const QString &path);

    void addLayoutModule(const QString &moduleName, const QString &label = QString());
    void removeLayoutModule(int index);
    void moveLayoutModule(int from, int to);

    bool isDirty() const;
    void setDirty(bool value);

private:
    ProjectMeta meta_;
    QVector<ModuleDescriptor> modules_;
    QVector<LayoutModule> layoutModules_;
    QString programBinaryPath_;
    bool dirty_ = false;
};

} // namespace BBB
