#include <BBBuilder/Project.h>

namespace BBB {

Project::Project() = default;

void Project::clear() {
    meta_ = ProjectMeta{};
    modules_.clear();
    layoutModules_.clear();
    programBinaryPath_.clear();
    dirty_ = false;
}

ProjectMeta &Project::meta() { return meta_; }
const ProjectMeta &Project::meta() const { return meta_; }

QVector<ModuleDescriptor> &Project::modules() { return modules_; }
const QVector<ModuleDescriptor> &Project::modules() const { return modules_; }

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

bool Project::isDirty() const { return dirty_; }
void Project::setDirty(bool value) { dirty_ = value; }

} // namespace BBB
