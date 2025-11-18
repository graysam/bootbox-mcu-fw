#include <BBBuilder/SigmaParser.h>

#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QPair>
#include <QRegularExpression>
#include <QSet>
#include <QStringConverter>
#include <QStringList>
#include <QTextStream>

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
    QString type;
    QString symbol;
    QString fullName;
    int algIndex = 0;
};

void applyHeuristics(ControlDescriptor &control);
QString canonicalKey(const QString &value) {
    QString normalized = value.trimmed().toLower();
    return normalized;
}
QString normalizeModuleName(const QString &input) {
    QString out = input;
    out.replace(' ', '_');
    out.replace('-', '_');
    out.replace('/', '_');
    return out.toUpper();
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

bool integrateXmlMeta(const QString &xmlFile, QMap<QString, ModuleDescriptor> &modules, QStringList &order, QVector<AlgorithmDescriptor> &algorithms) {
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
    auto moduleNodes = doc.elementsByTagName(QStringLiteral("Algorithm"));
    QMap<QString, int> instanceCounters;
    for (int i = 0; i < moduleNodes.count(); ++i) {
        auto moduleElement = moduleNodes.at(i).toElement();
        const QString cellName = moduleElement.attribute(QStringLiteral("cell")).trimmed();
        if (cellName.isEmpty()) continue;
        const QString key = canonicalKey(cellName);
        if (!modules.contains(key)) {
            ModuleDescriptor desc;
            desc.name = cellName;
            modules.insert(key, desc);
        }
        if (!order.contains(key)) order.append(key);

        AlgorithmDescriptor alg;
        alg.cellName = cellName;
        alg.moduleName = cellName;
        alg.friendlyName = moduleElement.attribute(QStringLiteral("friendlyname")).trimmed();
        alg.symbol = moduleElement.attribute(QStringLiteral("name"));
        alg.instanceIndex = instanceCounters[cellName]++;
        algorithms.append(alg);
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
    // Prefer NetList for algorithm metadata, fall back to any XML
    QString xmlFile = locateFile(info, {QStringLiteral("*_NetList.xml"), QStringLiteral("*NetList.xml")});
    if (xmlFile.isEmpty()) {
        xmlFile = locateFile(info, {QStringLiteral("*.xml")});
    }
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
    QStringList moduleOrder;

    const QString headerFile = locateFile(info, {QStringLiteral("*_PARAM.h")});
    if (headerFile.isEmpty()) {
        return std::nullopt;
    }

    QFile header(headerFile);
    if (!header.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return std::nullopt;
    }
    QTextStream headerStream(&header);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    headerStream.setCodec("UTF-8");
#else
    headerStream.setEncoding(QStringConverter::Utf8);
#endif

    QRegularExpression moduleStart(R"(\/\*\s*Module\s+(.+?)\s*-\s*(.+?)\s*\*\/)");
    QRegularExpression moduleCount(R"(#define\s+MOD_(\w+)_COUNT\s+(\d+))");
    QRegularExpression paramAddr(R"(#define\s+MOD_(\w+)_ALG(\d+)_([A-Za-z0-9_]+)_ADDR\s+(\d+))");
    QRegularExpression paramValue(R"(#define\s+MOD_(\w+)_ALG(\d+)_([A-Za-z0-9_]+)_VALUE\s+.+\(([-0-9eE\.\+]+)\))");
    QRegularExpression paramType(R"(#define\s+MOD_(\w+)_ALG(\d+)_([A-Za-z0-9_]+)_TYPE\s+([A-Z0-9_]+))");

    QString currentCellName;
    QString currentDescription;
    QString currentMacro;
    QMap<QString, QString> macroToCell;
    QMap<QString, QPair<QString, int>> pendingRefs;

    auto moduleKey = [&](const QString &cell) {
        return canonicalKey(cell);
    };

    while (!headerStream.atEnd()) {
        const QString line = headerStream.readLine();
        QRegularExpressionMatch match;
        if (line.contains(moduleStart, &match)) {
            currentCellName = match.captured(1).trimmed();
            currentDescription = match.captured(2).trimmed();
        } else if (line.contains(moduleCount, &match)) {
            currentMacro = match.captured(1);
            macroToCell.insert(currentMacro, currentCellName);
            const QString key = moduleKey(currentCellName);
            auto &module = moduleMap[key];
            if (module.name.isEmpty()) module.name = currentCellName;
            module.description = currentDescription;
            if (!moduleOrder.contains(key)) moduleOrder.append(key);
        } else if (line.contains(paramAddr, &match)) {
            const QString macro = match.captured(1);
            const int alg = match.captured(2).toInt();
            const QString symbol = match.captured(3);
            const quint32 addr = match.captured(4).toUInt();
            const QString cell = macroToCell.value(macro);
            if (cell.isEmpty()) continue;
            const QString key = moduleKey(cell);
            auto &module = moduleMap[key];
            ControlDescriptor control;
            control.id = QStringLiteral("%1_alg%2_%3").arg(module.name).arg(alg).arg(symbol);
            control.label = symbol;
            control.algorithmIndex = alg;
            control.address = addr;
            control.format = QStringLiteral("fixed5.23");
            applyHeuristics(control);
            module.controls.push_back(control);
            pendingRefs.insert(QStringLiteral("%1:%2:%3").arg(macro, QString::number(alg), symbol),
                               qMakePair(key, module.controls.size() - 1));
        } else if (line.contains(paramValue, &match)) {
            const QString macro = match.captured(1);
            const int alg = match.captured(2).toInt();
            const QString symbol = match.captured(3);
            const double value = match.captured(4).toDouble();
            const auto ref = pendingRefs.value(QStringLiteral("%1:%2:%3").arg(macro, QString::number(alg), symbol));
            if (!ref.first.isEmpty() && ref.second >= 0) {
                moduleMap[ref.first].controls[ref.second].defaultValue = value;
            }
        } else if (line.contains(paramType, &match)) {
            const QString macro = match.captured(1);
            const int alg = match.captured(2).toInt();
            const QString symbol = match.captured(3);
            QString type = match.captured(4).toLower();
            type.remove(QStringLiteral("sigmastudiotype_"));
            const auto ref = pendingRefs.value(QStringLiteral("%1:%2:%3").arg(macro, QString::number(alg), symbol));
            if (!ref.first.isEmpty() && ref.second >= 0) {
                moduleMap[ref.first].controls[ref.second].format = type;
            }
        }
    }

    ParamBlock current;
    auto flush = [&](){
        if (current.cellName.isEmpty()) return;
        const QString key = moduleKey(current.cellName);
        auto moduleIt = moduleMap.find(key);
        if (moduleIt == moduleMap.end()) {
            current = ParamBlock{};
            return;
        }
        for (auto &control : moduleIt->controls) {
            if (control.address == current.address) {
                if (!current.paramName.isEmpty()) control.label = current.paramName;
                break;
            }
        }
        current = ParamBlock{};
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
            current.address = match.hasMatch() ? match.captured(1).trimmed().toUInt(&ok) : 0;
        }
    }
    flush();

    QVector<AlgorithmDescriptor> algorithms;
    integrateXmlMeta(xmlFile, moduleMap, moduleOrder, algorithms);

    Result result;
    QSet<QString> orderedSet;
    for (const auto &name : moduleOrder) {
        if (moduleMap.contains(name)) {
            result.modules.append(moduleMap.value(name));
            orderedSet.insert(name);
        }
    }
    for (auto it = moduleMap.cbegin(); it != moduleMap.cend(); ++it) {
        if (!orderedSet.contains(it.key())) {
            result.modules.append(it.value());
        }
    }
    result.algorithms = algorithms;
    for (auto &alg : result.algorithms) {
        const QString key = moduleKey(alg.cellName);
        if (!moduleMap.contains(key)) continue;
        const auto &module = moduleMap.value(key);
        QVector<QString> ids;
        for (const auto &ctrl : module.controls) {
            if (ctrl.algorithmIndex == alg.instanceIndex) {
                ids.append(ctrl.id);
            }
        }
        alg.controlIds = ids;
        if (alg.moduleName.isEmpty()) alg.moduleName = module.name;
    }
    result.message = QStringLiteral("Parsed %1 modules from %2")
                         .arg(result.modules.size())
                         .arg(QFileInfo(paramsFile).fileName());
    result.programBinaryPath = programFile;
    result.interfaceXmlPath = xmlFile;
    return result;
}

} // namespace BBB
