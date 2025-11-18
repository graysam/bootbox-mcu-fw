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

    // Build interface.xml as bytes without writing a bundle.
    QByteArray buildInterfaceXml(const Project &project, QString *errorMessage = nullptr);

private:
    QString lastError_;
};

} // namespace BBB
