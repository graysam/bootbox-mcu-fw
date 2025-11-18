#include <BBBuilder/BundleWriter.h>

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QXmlStreamWriter>
#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

QString formatNumber(double value) {
    QString text = QString::number(value, 'f', 6);
    while (text.contains('.') && text.endsWith('0')) text.chop(1);
    if (text.endsWith('.')) text.chop(1);
    if (text.isEmpty()) text = QStringLiteral("0");
    return text;
}

void sanitizeControl(BBB::ControlDescriptor &ctrl) {
    if (ctrl.type.isEmpty()) ctrl.type = QStringLiteral("slider");
    if (ctrl.step <= 0.0) ctrl.step = 0.1;
    if (ctrl.max <= ctrl.min) ctrl.max = ctrl.min + ctrl.step;
    if (std::isnan(ctrl.defaultValue)) ctrl.defaultValue = ctrl.min;
    ctrl.defaultValue = std::clamp(ctrl.defaultValue, ctrl.min, ctrl.max);
    if (ctrl.byteWidth == 0) ctrl.byteWidth = 4;
    if (ctrl.format.isEmpty()) ctrl.format = QStringLiteral("fixed5.23");
}

QByteArray renderInterfaceXml(const BBB::Project &project, QString *errorMessage) {
    using namespace BBB;
    QVector<ControlDescriptor> ordered;
    QSet<QString> seen;

    auto appendControls = [&](const ModuleDescriptor &module) {
        for (const auto &control : module.controls) {
            if (control.id.isEmpty() || seen.contains(control.id)) continue;
            ordered.append(control);
            seen.insert(control.id);
        }
    };

    // 1) If canvas is present, order strictly by canvas widgets with module+control bindings.
    const QJsonArray canvas = project.canvas();
    if (!canvas.isEmpty()) {
        for (const auto &v : canvas) {
            const auto o = v.toObject();
            const QString mod = o.value(QStringLiteral("module")).toString();
            const QString cid = o.value(QStringLiteral("control")).toString();
            if (mod.isEmpty() || cid.isEmpty()) continue;
            if (seen.contains(cid)) continue;
            if (const auto *ctrl = project.findControl(mod, cid)) {
                ordered.append(*ctrl);
                seen.insert(cid);
            }
        }
    }

    // 2) Append remaining controls based on layout order, then any leftovers.
    QSet<QString> layoutModules;
    for (const auto &entry : project.layout()) {
        if (layoutModules.contains(entry.moduleName)) continue;
        if (const auto *module = project.findModule(entry.moduleName)) {
            layoutModules.insert(entry.moduleName);
            appendControls(*module);
        }
    }
    for (const auto &module : project.modules()) {
        if (layoutModules.contains(module.name)) continue;
        appendControls(module);
    }

    if (ordered.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Project has no controls to export.");
        return {};
    }

    QString xml;
    QXmlStreamWriter writer(&xml);
    writer.setAutoFormatting(true);
    writer.writeStartDocument(QStringLiteral("1.0"), true);
    writer.writeStartElement(QStringLiteral("interface"));
    writer.writeAttribute(QStringLiteral("version"), QStringLiteral("1"));

    for (auto control : ordered) {
        sanitizeControl(control);
        writer.writeEmptyElement(QStringLiteral("control"));
        writer.writeAttribute(QStringLiteral("id"), control.id);
        if (!control.label.isEmpty()) {
            writer.writeAttribute(QStringLiteral("label"), control.label);
        }
        writer.writeAttribute(QStringLiteral("type"), control.type.toLower());
        writer.writeAttribute(QStringLiteral("min"), formatNumber(control.min));
        writer.writeAttribute(QStringLiteral("max"), formatNumber(control.max));
        writer.writeAttribute(QStringLiteral("step"), formatNumber(control.step));
        writer.writeAttribute(QStringLiteral("default"), formatNumber(control.defaultValue));
        if (!control.unit.isEmpty()) {
            writer.writeAttribute(QStringLiteral("unit"), control.unit);
        }
        writer.writeAttribute(QStringLiteral("address"),
                              control.address == 0 ? QStringLiteral("0")
                                                   : QStringLiteral("0x%1").arg(control.address, 0, 16));
        if (control.byteWidth != 4) {
            writer.writeAttribute(QStringLiteral("bytes"), QString::number(control.byteWidth));
        }
        if (control.format.compare(QStringLiteral("fixed5.23"), Qt::CaseInsensitive) != 0) {
            writer.writeAttribute(QStringLiteral("format"), control.format.toLower());
        }
    }

    writer.writeEndElement();
    writer.writeEndDocument();
    return xml.toUtf8();
}

struct TarEntry {
    QByteArray name;
    QByteArray data;
};

bool writeOctal(char *buffer, int size, quint64 value) {
    QByteArray oct = QByteArray::number(value, 8);
    if (oct.size() + 2 > size) return false;
    memset(buffer, '0', size);
    memcpy(buffer + size - oct.size() - 2, oct.constData(), oct.size());
    buffer[size - 2] = '\0';
    buffer[size - 1] = ' ';
    return true;
}

bool writeTarStream(QIODevice &device, const QVector<TarEntry> &entries, QString *errorMessage) {
    for (const auto &entry : entries) {
        if (entry.name.isEmpty() || entry.name.size() > 100) {
            if (errorMessage) *errorMessage = QStringLiteral("Tar entry name invalid: %1").arg(QString::fromUtf8(entry.name));
            return false;
        }
        QByteArray header(512, '\0');
        memcpy(header.data(), entry.name.constData(), entry.name.size());
        if (!writeOctal(header.data() + 100, 8, 0644)) return false;
        if (!writeOctal(header.data() + 108, 8, 0)) return false;
        if (!writeOctal(header.data() + 116, 8, 0)) return false;
        if (!writeOctal(header.data() + 124, 12, static_cast<quint64>(entry.data.size()))) return false;
        if (!writeOctal(header.data() + 136, 12, static_cast<quint64>(QDateTime::currentSecsSinceEpoch()))) return false;
        memset(header.data() + 148, ' ', 8);
        header[156] = '0';
        memcpy(header.data() + 257, "ustar", 5);
        header[262] = '\0';
        header[263] = '0';
        header[264] = '0';

        quint32 checksum = 0;
        for (int i = 0; i < 512; ++i) {
            checksum += static_cast<unsigned char>(header[i]);
        }
        if (!writeOctal(header.data() + 148, 8, checksum)) return false;
        header[155] = '\0';

        if (device.write(header) != header.size()) {
            if (errorMessage) *errorMessage = QStringLiteral("Failed to write tar header.");
            return false;
        }
        if (!entry.data.isEmpty() && device.write(entry.data) != entry.data.size()) {
            if (errorMessage) *errorMessage = QStringLiteral("Failed to write tar data for %1").arg(QString::fromUtf8(entry.name));
            return false;
        }
        qint64 pad = 512 - (entry.data.size() % 512);
        if (pad != 512) {
            QByteArray padding(pad, '\0');
            if (device.write(padding) != padding.size()) {
                if (errorMessage) *errorMessage = QStringLiteral("Failed to write tar padding.");
                return false;
            }
        }
    }
    QByteArray trailer(1024, '\0');
    if (device.write(trailer) != trailer.size()) {
        if (errorMessage) *errorMessage = QStringLiteral("Failed to finalize tar archive.");
        return false;
    }
    return true;
}

} // namespace

namespace BBB {

bool BundleWriter::writeBundle(const Project &project, const QString &outputPath, Options options) {
    if (project.programBinaryPath().isEmpty()) {
        lastError_ = QStringLiteral("Program binary not set.");
        return false;
    }
    if (outputPath.isEmpty()) {
        lastError_ = QStringLiteral("Output path not specified.");
        return false;
    }
    if (project.layout().isEmpty() && project.modules().isEmpty()) {
        lastError_ = QStringLiteral("Add at least one module/control before exporting.");
        return false;
    }

    QFileInfo binInfo(project.programBinaryPath());
    if (!binInfo.exists() || !binInfo.isFile()) {
        lastError_ = QStringLiteral("Program binary missing: %1").arg(project.programBinaryPath());
        return false;
    }
    QFile programFile(binInfo.absoluteFilePath());
    if (!programFile.open(QIODevice::ReadOnly)) {
        lastError_ = QStringLiteral("Unable to read %1").arg(binInfo.absoluteFilePath());
        return false;
    }
    QByteArray programData = programFile.readAll();
    programFile.close();

    QString ifaceError;
    QByteArray interfaceData = renderInterfaceXml(project, &ifaceError);
    if (interfaceData.isEmpty()) {
        lastError_ = ifaceError.isEmpty() ? QStringLiteral("Interface generation failed.") : ifaceError;
        return false;
    }

    QVector<TarEntry> entries;
    entries.append({QByteArrayLiteral("program.bin"), programData});
    entries.append({QByteArrayLiteral("interface.xml"), interfaceData});

    if (options.dryRun) {
        return true;
    }

    QSaveFile out(outputPath);
    if (!out.open(QIODevice::WriteOnly)) {
        lastError_ = QStringLiteral("Unable to open %1 for writing.").arg(outputPath);
        return false;
    }
    if (!writeTarStream(out, entries, &lastError_)) {
        out.cancelWriting();
        return false;
    }
    if (!out.commit()) {
        lastError_ = QStringLiteral("Failed to finalize bundle: %1").arg(out.errorString());
        return false;
    }
    lastError_.clear();
    return true;
}

QString BundleWriter::lastError() const { return lastError_; }

QByteArray BundleWriter::buildInterfaceXml(const Project &project, QString *errorMessage) {
    return renderInterfaceXml(project, errorMessage);
}

} // namespace BBB
