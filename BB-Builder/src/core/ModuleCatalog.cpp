#include <BBBuilder/ModuleCatalog.h>

#include <QMap>

namespace BBB {

namespace {
struct CatalogData {
    QMap<QString, ModuleTemplate> modules;
};

CatalogData buildCatalog() {
    CatalogData data;
    ModuleTemplate crossover;
    crossover.id = QStringLiteral("xover_panel");
    crossover.label = QStringLiteral("Crossover Panel");
    crossover.description = QStringLiteral("Stereo + sub crossover controls");
    crossover.tags = {QStringLiteral("filter"), QStringLiteral("crossover")};
    data.modules.insert(crossover.id, crossover);
    return data;
}

const CatalogData &catalog() {
    static CatalogData data = buildCatalog();
    return data;
}
}

ModuleCatalog &ModuleCatalog::instance() {
    static ModuleCatalog catalog;
    return catalog;
}

ModuleCatalog::ModuleCatalog() = default;

QStringList ModuleCatalog::knownModuleIds() const {
    return catalog().modules.keys();
}

ModuleTemplate ModuleCatalog::moduleForId(const QString &id) const {
    return catalog().modules.value(id);
}

ControlTemplate ModuleCatalog::controlTemplateFor(const QString &blockId, const QString &parameterName) const {
    Q_UNUSED(blockId);
    Q_UNUSED(parameterName);
    ControlTemplate tpl;
    tpl.humanName = parameterName;
    return tpl;
}

} // namespace BBB
