#include <BBBuilder/BundleWriter.h>

namespace BBB {

bool BundleWriter::writeBundle(const Project &project, const QString &outputPath, Options options) {
    Q_UNUSED(options);
    if (project.programBinaryPath().isEmpty()) {
        lastError_ = QStringLiteral("Program binary not set");
        return false;
    }
    if (outputPath.isEmpty()) {
        lastError_ = QStringLiteral("Output path not specified");
        return false;
    }
    lastError_.clear();
    // TODO: implement archive creation (.bbx zip/tar)
    return true;
}

QString BundleWriter::lastError() const { return lastError_; }

} // namespace BBB
