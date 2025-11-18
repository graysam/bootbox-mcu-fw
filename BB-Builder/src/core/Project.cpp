#include <BBBuilder/Project.h>

#include <algorithm>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QObject>
#include <QtGlobal>

namespace BBB {

Project::Project() = default;

void Project::clear() {
    meta_ = ProjectMeta{};
    modules_.clear();
    algorithms_.clear();
    layoutModules_.clear();
    programBinaryPath_.clear();
    dirty_ = false;
}

ProjectMeta &Project::meta() { return meta_; }
const ProjectMeta &Project::meta() const { return meta_; }

QVector<ModuleDescriptor> &Project::modules() { return modules_; }
const QVector<ModuleDescriptor> &Project::modules() const { return modules_; }

QVector<AlgorithmDescriptor> &Project::algorithms() { return algorithms_; }
const QVector<AlgorithmDescriptor> &Project::algorithms() const { return algorithms_; }

QVector<LayoutModule> &Project::layout() { return layoutModules_; }
const QVector<LayoutModule> &Project::layout() const { return layoutModules_; }

QString Project::programBinaryPath() const { return programBinaryPath_; }

void Project::setProgramBinaryPath(const QString &path) {
    if (programBinaryPath_ == path) return;
    programBinaryPath_ = path;
    dirty_ = true;
}

void Project::addLayoutModule(const QString &moduleName, const QString &label) {
    if (moduleName.isEmpty()) return;
    LayoutModule entry;
    entry.moduleName = moduleName;
    entry.displayLabel = label.isEmpty() ? moduleName : label;
    layoutModules_.push_back(entry);
    dirty_ = true;
}

void Project::removeLayoutModule(int index) {
    if (index < 0 || index >= layoutModules_.size()) return;
    layoutModules_.removeAt(index);
    dirty_ = true;
}

void Project::moveLayoutModule(int from, int to) {
    if (from < 0 || from >= layoutModules_.size() || to < 0 || to >= layoutModules_.size() || from == to) return;
    layoutModules_.move(from, to);
    dirty_ = true;
}

ModuleDescriptor* Project::findModule(const QString &moduleName) {
    for (auto &module : modules_) {
        if (module.name == moduleName) return &module;
    }
    return nullptr;
}

const ModuleDescriptor* Project::findModule(const QString &moduleName) const {
    for (const auto &module : modules_) {
        if (module.name == moduleName) return &module;
    }
    return nullptr;
}

ControlDescriptor* Project::findControl(const QString &moduleName, const QString &controlId) {
    if (auto *module = findModule(moduleName)) {
        for (auto &control : module->controls) {
            if (control.id == controlId) return &control;
        }
    }
    return nullptr;
}

const ControlDescriptor* Project::findControl(const QString &moduleName, const QString &controlId) const {
    if (const auto *module = findModule(moduleName)) {
        for (const auto &control : module->controls) {
            if (control.id == controlId) return &control;
        }
    }
    return nullptr;
}

bool Project::updateLayoutLabel(const QString &moduleName, const QString &displayLabel) {
    bool updated = false;
    for (auto &entry : layoutModules_) {
        if (entry.moduleName == moduleName) {
            entry.displayLabel = displayLabel;
            updated = true;
        }
    }
    if (updated) dirty_ = true;
    return updated;
}

bool Project::updateModuleDescription(const QString &moduleName, const QString &description) {
    if (auto *module = findModule(moduleName)) {
        module->description = description;
        module->dirty = true;
        dirty_ = true;
        return true;
    }
    return false;
}

bool Project::updateControl(const QString &moduleName, const QString &previousId, const ControlDescriptor &descriptor) {
    if (auto *module = findModule(moduleName)) {
        for (auto &control : module->controls) {
            if (control.id == previousId) {
                control = descriptor;
                module->dirty = true;
                dirty_ = true;
                return true;
            }
        }
    }
    return false;
}

bool Project::isDirty() const { return dirty_; }
void Project::setDirty(bool value) { dirty_ = value; }

void Project::setAlgorithms(const QVector<AlgorithmDescriptor> &algorithms) {
    algorithms_ = algorithms;
    dirty_ = true;
}

EditorSettings &Project::editor() { return editor_; }
const EditorSettings &Project::editor() const { return editor_; }

bool Project::removeControl(const QString &moduleName, const QString &controlId) {
    if (auto *module = findModule(moduleName)) {
        const int before = module->controls.size();
        module->controls.erase(std::remove_if(module->controls.begin(),
                                              module->controls.end(),
                                              [&](const ControlDescriptor &ctrl){ return ctrl.id == controlId; }),
                               module->controls.end());
        if (module->controls.size() != before) {
            module->dirty = true;
            dirty_ = true;
            return true;
        }
    }
    return false;
}

bool Project::saveToFile(const QString &path, QString *errorMessage) const {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = QObject::tr("Unable to write %1").arg(path);
        }
        return false;
    }
    const QJsonDocument doc(toJson());
    file.write(doc.toJson(QJsonDocument::Indented));
    return true;
}

bool Project::loadFromFile(const QString &path, QString *errorMessage) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = QObject::tr("Unable to open %1").arg(path);
        }
        return false;
    }
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorMessage) {
            *errorMessage = QObject::tr("Invalid project format (%1)").arg(parseError.errorString());
        }
        return false;
    }
    if (!fromJson(doc.object(), errorMessage)) {
        return false;
    }
    dirty_ = false;
    return true;
}

QJsonObject Project::toJson() const {
    QJsonObject root;
    QJsonObject metaObj;
    metaObj.insert(QStringLiteral("name"), meta_.name);
    metaObj.insert(QStringLiteral("author"), meta_.author);
    metaObj.insert(QStringLiteral("description"), meta_.description);
    root.insert(QStringLiteral("meta"), metaObj);
    root.insert(QStringLiteral("programBinaryPath"), programBinaryPath_);

    QJsonArray modulesArray;
    for (const auto &module : modules_) {
        QJsonObject moduleObj;
        moduleObj.insert(QStringLiteral("name"), module.name);
        moduleObj.insert(QStringLiteral("description"), module.description);
        QJsonArray controlsArray;
        for (const auto &control : module.controls) {
            QJsonObject controlObj;
            controlObj.insert(QStringLiteral("id"), control.id);
            controlObj.insert(QStringLiteral("label"), control.label);
            controlObj.insert(QStringLiteral("type"), control.type);
            controlObj.insert(QStringLiteral("unit"), control.unit);
            controlObj.insert(QStringLiteral("description"), control.description);
            controlObj.insert(QStringLiteral("min"), control.min);
            controlObj.insert(QStringLiteral("max"), control.max);
            controlObj.insert(QStringLiteral("step"), control.step);
            controlObj.insert(QStringLiteral("defaultValue"), control.defaultValue);
            controlObj.insert(QStringLiteral("address"), static_cast<qint64>(control.address));
            controlObj.insert(QStringLiteral("byteWidth"), static_cast<int>(control.byteWidth));
            controlObj.insert(QStringLiteral("format"), control.format);
            controlObj.insert(QStringLiteral("readOnly"), control.readOnly);
            controlsArray.append(controlObj);
        }
        moduleObj.insert(QStringLiteral("controls"), controlsArray);
        modulesArray.append(moduleObj);
    }
    root.insert(QStringLiteral("modules"), modulesArray);

    QJsonArray algorithmsArray;
    for (const auto &alg : algorithms_) {
        QJsonObject algObj;
        algObj.insert(QStringLiteral("cellName"), alg.cellName);
        algObj.insert(QStringLiteral("friendlyName"), alg.friendlyName);
        algObj.insert(QStringLiteral("symbol"), alg.symbol);
        algObj.insert(QStringLiteral("moduleName"), alg.moduleName);
        algObj.insert(QStringLiteral("instanceIndex"), alg.instanceIndex);
        QJsonArray ids;
        for (const auto &id : alg.controlIds) {
            ids.append(id);
        }
        algObj.insert(QStringLiteral("controls"), ids);
        algorithmsArray.append(algObj);
    }
    root.insert(QStringLiteral("algorithms"), algorithmsArray);

    QJsonArray layoutArray;
    for (const auto &entry : layoutModules_) {
        QJsonObject layoutObj;
        layoutObj.insert(QStringLiteral("module"), entry.moduleName);
        layoutObj.insert(QStringLiteral("label"), entry.displayLabel);
        layoutArray.append(layoutObj);
    }
    root.insert(QStringLiteral("layout"), layoutArray);
    if (!canvasWidgets_.isEmpty()) {
        root.insert(QStringLiteral("canvas"), canvasWidgets_);
    }
    QJsonObject editorObj;
    editorObj.insert(QStringLiteral("algPanelHeight"), editor_.algPanelHeight);
    editorObj.insert(QStringLiteral("snap"), editor_.snap);
    editorObj.insert(QStringLiteral("grid"), editor_.grid);
    editorObj.insert(QStringLiteral("zoom"), editor_.zoom);
    editorObj.insert(QStringLiteral("panX"), editor_.panX);
    editorObj.insert(QStringLiteral("panY"), editor_.panY);
    root.insert(QStringLiteral("editor"), editorObj);
    return root;
}

bool Project::fromJson(const QJsonObject &object, QString *errorMessage) {
    if (!object.contains(QStringLiteral("modules")) || !object.value(QStringLiteral("modules")).isArray()) {
        if (errorMessage) {
            *errorMessage = QObject::tr("Project file missing module data.");
        }
        return false;
    }

    meta_.name = object.value(QStringLiteral("meta")).toObject().value(QStringLiteral("name")).toString();
    meta_.author = object.value(QStringLiteral("meta")).toObject().value(QStringLiteral("author")).toString();
    meta_.description = object.value(QStringLiteral("meta")).toObject().value(QStringLiteral("description")).toString();
    programBinaryPath_ = object.value(QStringLiteral("programBinaryPath")).toString();

    modules_.clear();
    algorithms_.clear();
    layoutModules_.clear();

    const auto modulesArray = object.value(QStringLiteral("modules")).toArray();
    for (const auto &moduleValue : modulesArray) {
        if (!moduleValue.isObject()) continue;
        ModuleDescriptor module;
        const auto moduleObj = moduleValue.toObject();
        module.name = moduleObj.value(QStringLiteral("name")).toString();
        module.description = moduleObj.value(QStringLiteral("description")).toString();
        const auto controlsArray = moduleObj.value(QStringLiteral("controls")).toArray();
        for (const auto &controlValue : controlsArray) {
            if (!controlValue.isObject()) continue;
            const auto controlObj = controlValue.toObject();
            ControlDescriptor control;
            control.id = controlObj.value(QStringLiteral("id")).toString();
            control.label = controlObj.value(QStringLiteral("label")).toString(control.id);
            control.type = controlObj.value(QStringLiteral("type")).toString();
            control.unit = controlObj.value(QStringLiteral("unit")).toString();
            control.description = controlObj.value(QStringLiteral("description")).toString();
            control.min = controlObj.value(QStringLiteral("min")).toDouble(0.0);
            control.max = controlObj.value(QStringLiteral("max")).toDouble(1.0);
            control.step = controlObj.value(QStringLiteral("step")).toDouble(0.1);
            control.defaultValue = controlObj.value(QStringLiteral("defaultValue")).toDouble(0.0);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            control.address = static_cast<quint32>(controlObj.value(QStringLiteral("address")).toInteger(0));
#else
            control.address = static_cast<quint32>(controlObj.value(QStringLiteral("address")).toDouble(0.0));
#endif
            control.byteWidth = static_cast<quint8>(controlObj.value(QStringLiteral("byteWidth")).toInt(4));
            control.format = controlObj.value(QStringLiteral("format")).toString();
            control.readOnly = controlObj.value(QStringLiteral("readOnly")).toBool(false);
            module.controls.append(control);
        }
        modules_.append(module);
    }

    const auto algoArray = object.value(QStringLiteral("algorithms")).toArray();
    for (const auto &algoValue : algoArray) {
        if (!algoValue.isObject()) continue;
        AlgorithmDescriptor alg;
        const auto algObj = algoValue.toObject();
        alg.cellName = algObj.value(QStringLiteral("cellName")).toString();
        alg.friendlyName = algObj.value(QStringLiteral("friendlyName")).toString();
        alg.symbol = algObj.value(QStringLiteral("symbol")).toString();
        alg.moduleName = algObj.value(QStringLiteral("moduleName")).toString();
        alg.instanceIndex = algObj.value(QStringLiteral("instanceIndex")).toInt();
        const auto ctrlIds = algObj.value(QStringLiteral("controls")).toArray();
        for (const auto &ctrlValue : ctrlIds) {
            alg.controlIds.append(ctrlValue.toString());
        }
        algorithms_.append(alg);
    }

    const auto layoutArray = object.value(QStringLiteral("layout")).toArray();
    for (const auto &layoutValue : layoutArray) {
        if (!layoutValue.isObject()) continue;
        const auto layoutObj = layoutValue.toObject();
        LayoutModule entry;
        entry.moduleName = layoutObj.value(QStringLiteral("module")).toString();
        entry.displayLabel = layoutObj.value(QStringLiteral("label")).toString(entry.moduleName);
        layoutModules_.append(entry);
    }
    // Optional canvas widgets
    const auto canvasArray = object.value(QStringLiteral("canvas")).toArray();
    canvasWidgets_ = QJsonArray();
    for (const auto &it : canvasArray) canvasWidgets_.append(it);

    // Editor settings
    const auto editorObj = object.value(QStringLiteral("editor")).toObject();
    if (!editorObj.isEmpty()) {
        editor_.algPanelHeight = editorObj.value(QStringLiteral("algPanelHeight")).toDouble(220.0);
        editor_.snap = editorObj.value(QStringLiteral("snap")).toBool(true);
        editor_.grid = editorObj.value(QStringLiteral("grid")).toDouble(12.0);
        editor_.zoom = editorObj.value(QStringLiteral("zoom")).toDouble(1.0);
        editor_.panX = editorObj.value(QStringLiteral("panX")).toDouble(0.0);
        editor_.panY = editorObj.value(QStringLiteral("panY")).toDouble(0.0);
    }
    return true;
}

} // namespace BBB
