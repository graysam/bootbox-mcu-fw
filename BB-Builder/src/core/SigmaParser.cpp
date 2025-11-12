#include <BBBuilder/SigmaParser.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QStringConverter>
#include <QTextStream>
#include <QDomDocument>

namespace BBB {

namespace {

QString locateFile(const QFileInfo &info, const QStringList &patterns) {
    if (info.isFile()) {
        for (const auto &pattern : patterns) {
            if (QDir::match(pattern, info.fileName())) {
                return info.absoluteFilePath();
            }
        }
    }
    if (info.isDir()) {
        const auto files = QDir(info.absoluteFilePath()).entryInfoList(patterns, QDir::Files, QDir::Name);
        if (!files.isEmpty()) return files.first().absoluteFilePath();
    }
    return {};
}

struct ParamBlock {
    QString cellName;
    QString paramName;
    quint32 address = 0;
    double value = 0.0;
};

void applyHeuristics(ControlDescriptor &control);

bool finishBlock(const ParamBlock &block, QMap<QString, ModuleDescriptor> &modules) {
    if (block.cellName.isEmpty() || block.paramName.isEmpty()) {
        return false;
    }
    auto &module = modules[block.cellName];
    if (module.name.isEmpty()) {
        module.name = block.cellName.trimmed();
    }
    ControlDescriptor control;
    control.id = QStringLiteral("%1_%2").arg(block.cellName.trimmed(), block.paramName.trimmed()).replace(' ', '_');
    control.label = block.paramName.trimmed();
    control.type = QStringLiteral("slider");
    control.min = -60.0;
    control.max = 12.0;
    control.step = 0.1;
    control.defaultValue = block.value;
    control.address = block.address;
    control.format = QStringLiteral("fixed5.23");
    applyHeuristics(control);
    module.controls.push_back(control);
    return true;
}

void applyHeuristics(ControlDescriptor &control) {
    const QString label = control.label.toLower();
    if (label.contains(QStringLiteral("gain"))) {
        control.unit = QStringLiteral("dB");
        control.min = -80.0;
        control.max = 12.0;
        control.step = 0.5;
    } else if (label.contains(QStringLiteral("freq"))) {
        control.unit = QStringLiteral("Hz");
        control.min = 10.0;
        control.max = 20000.0;
        control.step = 1.0;
    } else if (label.contains(QStringLiteral("delay"))) {
        control.unit = QStringLiteral("ms");
        control.min = 0.0;
        control.max = 500.0;
        control.step = 0.1;
    } else if (label.contains(QStringLiteral("phase")) ||
               label.contains(QStringLiteral("invert")) ||
               label.contains(QStringLiteral("mute")) ||
               label.contains(QStringLiteral("active"))) {
        control.type = QStringLiteral("toggle");
        control.min = 0.0;
        control.max = 1.0;
        control.step = 1.0;
    }
}

bool integrateXmlMeta(const QString &xmlFile, QMap<QString, ModuleDescriptor> &modules) {
    if (xmlFile.isEmpty()) return false;
    QFile file(xmlFile);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    const QString content = stream.readAll();
    QDomDocument doc;
    if (!doc.setContent(content)) return false;
#else
    QDomDocument doc;
    if (!doc.setContent(&file)) return false;
#endif
    auto moduleNodes = doc.elementsByTagName(QStringLiteral("Module"));
    for (int i = 0; i < moduleNodes.count(); ++i) {
        auto moduleElement = moduleNodes.at(i).toElement();
        const QString cellName = moduleElement.firstChildElement(QStringLiteral("CellName")).text().trimmed();
        if (cellName.isEmpty()) continue;
        auto &module = modules[cellName];
        if (module.name.isEmpty()) module.name = cellName;
        module.description = moduleElement.firstChildElement(QStringLiteral("Description")).text().trimmed();
        auto algoElements = moduleElement.elementsByTagName(QStringLiteral("Algorithm"));
        QString description;
        for (int j = 0; j < algoElements.count(); ++j) {
            auto algoEl = algoElements.at(j).toElement();
            if (description.isEmpty()) {
                description = algoEl.firstChildElement(QStringLiteral("Description")).text();
            }
            auto params = algoEl.elementsByTagName(QStringLiteral("ModuleParameter"));
            for (int k = 0; k < params.count(); ++k) {
                auto param = params.at(k).toElement();
                const QString paramName = param.firstChildElement(QStringLiteral("Name")).text().trimmed();
                const QString addrStr = param.firstChildElement(QStringLiteral("Address")).text().trimmed();
                bool ok = false;
                const quint32 addr = addrStr.toUInt(&ok);
                for (auto &ctrl : module.controls) {
                    if (ctrl.label == paramName && ok) {
                        ctrl.address = addr;
                        ctrl.defaultValue = param.firstChildElement(QStringLiteral("Value")).text().toDouble();
                        ctrl.byteWidth = static_cast<quint8>(param.firstChildElement(QStringLiteral("Size")).text().toUInt());
                        const QString type = param.firstChildElement(QStringLiteral("Type")).text().trimmed();
                        if (type.contains(QStringLiteral("Fixed"), Qt::CaseInsensitive)) {
                            ctrl.format = QStringLiteral("fixed5.23");
                        } else if (type.contains(QStringLiteral("Int"), Qt::CaseInsensitive)) {
                            ctrl.format = QStringLiteral("u32");
                        } else {
                            ctrl.format = QStringLiteral("raw");
                        }
                        applyHeuristics(ctrl);
                    }
                }
            }
        }
    }
    return true;
}

} // namespace

std::optional<SigmaParser::Result> SigmaParser::parseFromPath(const QString &path) {
    QFileInfo info(path);
    if (!info.exists()) {
        return std::nullopt;
    }
    const QString paramsFile = locateFile(info, {QStringLiteral("*.params")});
    if (paramsFile.isEmpty()) {
        return std::nullopt;
    }
    const QString xmlFile = locateFile(info, {QStringLiteral("*.xml")});
    const QString programFile = locateFile(info, {QStringLiteral("*.bin"),
                                                  QStringLiteral("*.dat"),
                                                  QStringLiteral("*.hex")});
    QFile file(paramsFile);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return std::nullopt;
    }
    QTextStream stream(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    stream.setCodec("UTF-8");
#else
    stream.setEncoding(QStringConverter::Utf8);
#endif

    QMap<QString, ModuleDescriptor> moduleMap;
    ParamBlock current;

    auto flush = [&](){
        if (finishBlock(current, moduleMap)) {
            current = ParamBlock{};
        }
    };

    QRegularExpression valueRegex(QStringLiteral("=\\s*(.+)$"));

    while (!stream.atEnd()) {
        const QString rawLine = stream.readLine();
        const QString line = rawLine.trimmed();
        if (line.isEmpty()) {
            flush();
            continue;
        }
        if (line.startsWith(QStringLiteral("Cell Name"))) {
            flush();
            const auto match = valueRegex.match(line);
            current.cellName = match.hasMatch() ? match.captured(1).trimmed() : QString();
        } else if (line.startsWith(QStringLiteral("Parameter Name"))) {
            const auto match = valueRegex.match(line);
            current.paramName = match.hasMatch() ? match.captured(1).trimmed() : QString();
        } else if (line.startsWith(QStringLiteral("Parameter Address"))) {
            const auto match = valueRegex.match(line);
            bool ok = false;
            const quint32 addr = match.hasMatch() ? match.captured(1).trimmed().toUInt(&ok) : 0;
            current.address = ok ? addr : 0;
        } else if (line.startsWith(QStringLiteral("Parameter Value"))) {
            const auto match = valueRegex.match(line);
            bool ok = false;
            const double value = match.hasMatch() ? match.captured(1).trimmed().toDouble(&ok) : 0.0;
            current.value = ok ? value : 0.0;
        }
    }
    flush();

    integrateXmlMeta(xmlFile, moduleMap);

    Result result;
    result.modules = moduleMap.values().toVector();
    result.message = QStringLiteral("Parsed %1 modules from %2")
                         .arg(result.modules.size())
                         .arg(QFileInfo(paramsFile).fileName());
    result.programBinaryPath = programFile;
    result.interfaceXmlPath = xmlFile;
    return result;
}

} // namespace BBB
