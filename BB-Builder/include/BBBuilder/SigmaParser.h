#pragma once

#include <QString>
#include <QVector>
#include <optional>

#include <BBBuilder/Project.h>

namespace BBB {

class SigmaParser {
public:
    struct Result {
        QVector<ModuleDescriptor> modules;
        QString message;
        QString programBinaryPath;
        QString interfaceXmlPath;
    };

    // Attempts to parse a SigmaStudio export directory or file.
    std::optional<Result> parseFromPath(const QString &path);
};

} // namespace BBB
