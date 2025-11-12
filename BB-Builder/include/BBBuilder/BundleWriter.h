#pragma once

#include <QString>

#include <BBBuilder/Project.h>

namespace BBB {

class BundleWriter {
public:
    struct Options {
        bool dryRun = false;
    };

    bool writeBundle(const Project &project, const QString &outputPath, Options options);
    QString lastError() const;

private:
    QString lastError_;
};

} // namespace BBB
