#pragma once

#include <QString>
#include <QStringList>

namespace BBB {

struct ControlTemplate {
    QString humanName;
    QString defaultType;         // slider, toggle, etc.
    QStringList fallbackTypes;   // optional alternative widgets
    double minValue = 0.0;
    double maxValue = 1.0;
    double step = 0.1;
    QString unit;
};

struct ModuleTemplate {
    QString id;
    QString label;
    QString description;
    QStringList tags;
};

class ModuleCatalog {
public:
    static ModuleCatalog &instance();

    QStringList knownModuleIds() const;
    ModuleTemplate moduleForId(const QString &id) const;
    ControlTemplate controlTemplateFor(const QString &blockId, const QString &parameterName) const;

private:
    ModuleCatalog();
};

} // namespace BBB
