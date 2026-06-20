#include "DftInputParser.h"

#include "DftParameterRegistry.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringConverter>
#include <QTextStream>

namespace {

QString stripFdfComment(const QString& line) {
    const int hash = line.indexOf('#');
    return hash >= 0 ? line.left(hash) : line;
}

QString stripQeComment(QString line) {
    bool inSingle = false;
    bool inDouble = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar ch = line.at(i);
        if (ch == '\'' && !inDouble) inSingle = !inSingle;
        if (ch == '"' && !inSingle) inDouble = !inDouble;
        if (ch == '!' && !inSingle && !inDouble) return line.left(i);
    }
    return line;
}

QString normalizeValue(QString value) {
    value = value.trimmed();
    while (value.endsWith(',')) value.chop(1);
    value = value.trimmed();
    if ((value.startsWith('\'') && value.endsWith('\'')) || (value.startsWith('"') && value.endsWith('"'))) {
        value = value.mid(1, value.size() - 2);
    }
    return value.trimmed();
}

void applyKnownSiesta(DftSettings* s, const QString& key, const QString& value, DftParameterSource source, QStringList* diff) {
    static const QMap<QString, QString> ids = {
        {"SystemName", "siesta.general.SystemName"}, {"SystemLabel", "siesta.general.SystemLabel"},
        {"NetCharge", "siesta.charge_spin.NetCharge"}, {"Spin", "siesta.charge_spin.Spin"},
        {"Spin.Fix", "siesta.charge_spin.Spin.Fix"}, {"Spin.Total", "siesta.charge_spin.Spin.Total"},
        {"MaxSCFIterations", "siesta.scf.MaxSCFIterations"}, {"DM.Tolerance", "siesta.scf.DM.Tolerance"},
        {"MD.TypeOfRun", "siesta.relaxation.MD.TypeOfRun"}, {"MD.VariableCell", "siesta.relaxation.MD.VariableCell"},
        {"MD.Steps", "siesta.relaxation.MD.Steps"}, {"MD.MaxForceTol", "siesta.relaxation.MD.MaxForceTol"},
        {"MD.MaxDispl", "siesta.relaxation.MD.MaxDispl"}, {"MD.MaxStressTol", "siesta.relaxation.MD.MaxStressTol"},
        {"GeometryMustConverge", "siesta.relaxation.GeometryMustConverge"}, {"MeshCutoff", "siesta.species.MeshCutoff"},
        {"xc.functional", "siesta.species.xc.functional"}, {"xc.authors", "siesta.species.xc.authors"},
    };
    const QString id = ids.value(key);
    if (!id.isEmpty()) {
        const QString before = DftParameterRegistry::parameterValue(*s, id);
        DftParameterRegistry::setParameterValue(s, id, value.section(QRegularExpression("\\s+"), 0, 0), source);
        if (diff && before != value) *diff << QStringLiteral("%1: %2 -> %3").arg(key, before, value);
    } else if (!key.trimmed().isEmpty()) {
        DftRawParameter raw;
        raw.code = DftCode::Siesta;
        raw.key = key;
        raw.value = value;
        raw.enabled = true;
        s->rawParameters << raw;
    }
}

void applyKnownQe(DftSettings* s, const QString& section, const QString& key, const QString& value, QStringList* diff) {
    const QString id = QStringLiteral("qe.%1.%2").arg(section, key);
    const QString before = DftParameterRegistry::parameterValue(*s, id);
    if (s->parameters.contains(id)) {
        DftParameterRegistry::setParameterValue(s, id, normalizeValue(value), DftParameterSource::ImportedQeIn);
        if (diff && before != normalizeValue(value)) *diff << QStringLiteral("%1.%2: %3 -> %4").arg(section, key, before, normalizeValue(value));
    } else {
        DftRawParameter raw;
        raw.code = DftCode::QuantumEspresso;
        raw.namelistOrBlock = section;
        raw.key = key;
        raw.value = normalizeValue(value);
        raw.enabled = true;
        s->rawParameters << raw;
    }
}

DftHydrogenRole qeHydrogenRoleFromSpecies(const QString& label, const QString& pseudoFile) {
    const QString normalizedLabel = label.trimmed().toLower();
    const QString normalizedPseudo = pseudoFile.trimmed().toLower();
    if (normalizedLabel == QStringLiteral("h2")) return DftHydrogenRole::SurfaceAdsorbedHydrogen;
    if (normalizedLabel == QStringLiteral("hp125") || normalizedPseudo.contains(QStringLiteral("125"))) {
        return DftHydrogenRole::BottomPseudoHIIITerminated125;
    }
    if (normalizedPseudo.contains(QStringLiteral("075")) || normalizedPseudo.contains(QStringLiteral("0.75"))) {
        return DftHydrogenRole::BottomPseudoHNTerminated075;
    }
    if (normalizedLabel == QStringLiteral("h")) return DftHydrogenRole::OrdinaryHydrogen;
    return DftHydrogenRole::UnknownHydrogen;
}

QString elementFromQeSpeciesLabel(const QString& label) {
    if (label.compare(QStringLiteral("H2"), Qt::CaseInsensitive) == 0 ||
        label.compare(QStringLiteral("Hp125"), Qt::CaseInsensitive) == 0 ||
        label.compare(QStringLiteral("H"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("H");
    }
    QString element;
    for (const QChar ch : label) {
        if (!ch.isLetter()) break;
        element.append(ch);
        if (element.size() == 2) break;
    }
    return element.isEmpty() ? label : element;
}

void appendImportedQeSpecies(DftSettings* s, const QString& line) {
    const QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (parts.size() < 3) return;
    DftQeSpecies sp;
    sp.label = parts.at(0);
    sp.mass = parts.at(1).toDouble();
    sp.pseudoFile = parts.at(2);
    sp.element = elementFromQeSpeciesLabel(sp.label);
    sp.source = DftParameterSource::ImportedQeIn;
    if (sp.element.compare(QStringLiteral("H"), Qt::CaseInsensitive) == 0) {
        sp.role = dftHydrogenRoleKey(qeHydrogenRoleFromSpecies(sp.label, sp.pseudoFile));
    } else if (sp.element.compare(QStringLiteral("Ga"), Qt::CaseInsensitive) == 0) {
        sp.role = QStringLiteral("cation");
    } else if (sp.element.compare(QStringLiteral("N"), Qt::CaseInsensitive) == 0) {
        sp.role = QStringLiteral("anion");
    } else {
        sp.role = sp.element;
    }
    s->qeSpecies << sp;
}

} // namespace
DftImportResult DftInputParser::parseFile(const QString& path, const DftSettings& baseSettings) {
    DftImportResult result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.messages << QStringLiteral("読み込めません: %1").arg(path);
        return result;
    }
    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);
    const QString text = in.readAll();
    const QFileInfo info(path);
    const QString suffix = info.suffix().toLower();
    const QString name = info.fileName().toLower();
    if (suffix == "json") return parseProfileJsonText(text, baseSettings);
    if (suffix == "in") return parseQeInputText(text, baseSettings);
    if (suffix == "fdf") return parseSiestaFdfText(text, baseSettings, DftParameterSource::ImportedFile);
    if (name.startsWith("fdf-") && suffix == "log") return parseSiestaLogText(text, baseSettings);
    if (suffix == "yaml" || suffix == "yml") {
        auto r = parseSiestaFdfText(text, baseSettings, DftParameterSource::ImportedFile);
        r.sourceKind = QStringLiteral("yaml_limited");
        r.messages << QStringLiteral("YAMLは限定key:value parserとして読み込みました。複雑なYAMLはJSONへ変換してください。");
        return r;
    }
    result.messages << QStringLiteral("未対応のimport形式: %1").arg(path);
    return result;
}

DftImportResult DftInputParser::parseSiestaFdfText(const QString& text, const DftSettings& baseSettings, DftParameterSource source) {
    DftImportResult r;
    r.ok = true;
    r.sourceKind = source == DftParameterSource::ImportedFdfLog ? QStringLiteral("fdf_log") : QStringLiteral("siesta_fdf");
    r.settings = baseSettings;
    r.settings.code = DftCode::Siesta;
    const QStringList lines = text.split('\n');
    QString currentBlock;
    QString blockValue;
    for (const QString& rawLine : lines) {
        const QString line = stripFdfComment(rawLine).trimmed();
        if (line.isEmpty()) continue;
        if (line.startsWith("%block", Qt::CaseInsensitive)) {
            currentBlock = line.section(QRegularExpression("\\s+"), 1, 1).trimmed();
            blockValue.clear();
            continue;
        }
        if (line.startsWith("%endblock", Qt::CaseInsensitive)) {
            if (!currentBlock.isEmpty()) {
                QString normalizedBlock = currentBlock;
                normalizedBlock.remove('_');
                if (normalizedBlock.compare("kgrid.MonkhorstPack", Qt::CaseInsensitive) == 0) {
                    QStringList k;
                    const auto rows = blockValue.split('\n', Qt::SkipEmptyParts);
                    for (const QString& row : rows) k << row.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts).value(k.size());
                    if (k.size() >= 3) DftParameterRegistry::setParameterValue(&r.settings, "siesta.kpoints.kgrid", k.mid(0, 3).join(' '), source);
                } else {
                    DftRawParameter raw;
                    raw.code = DftCode::Siesta;
                    raw.key = currentBlock;
                    raw.value = blockValue.trimmed();
                    raw.blockOrCard = true;
                    raw.enabled = true;
                    r.settings.rawParameters << raw;
                }
            }
            currentBlock.clear();
            blockValue.clear();
            continue;
        }
        if (!currentBlock.isEmpty()) {
            blockValue += line + '\n';
            continue;
        }
        if (line.startsWith("%include", Qt::CaseInsensitive)) {
            r.settings.includeXcFdf = true;
            r.settings.xcFdfPath = line.section(QRegularExpression("\\s+"), 1).trimmed();
            r.diffLines << QStringLiteral("include xc.fdf -> %1").arg(r.settings.xcFdfPath);
            continue;
        }
        const QString key = line.section(QRegularExpression("\\s+"), 0, 0).trimmed();
        const QString value = line.section(QRegularExpression("\\s+"), 1).trimmed();
        applyKnownSiesta(&r.settings, key, value, source, &r.diffLines);
    }
    return r;
}

DftImportResult DftInputParser::parseSiestaLogText(const QString& text, const DftSettings& baseSettings) {
    auto r = parseSiestaFdfText(text, baseSettings, DftParameterSource::ImportedFdfLog);
    r.sourceKind = QStringLiteral("fdf_log");
    for (auto it = r.settings.parameters.begin(); it != r.settings.parameters.end(); ++it) {
        if (it->source == DftParameterSource::ImportedFile) it->source = DftParameterSource::ImportedFdfLog;
    }
    r.messages << QStringLiteral("fdf-logから既知FDFラベルとblockを抽出しました。default/originally表記はrawとして保持します。");
    return r;
}
DftImportResult DftInputParser::parseQeInputText(const QString& text, const DftSettings& baseSettings) {
    DftImportResult r;
    r.ok = true;
    r.sourceKind = QStringLiteral("qe_in");
    r.settings = baseSettings;
    r.settings.code = DftCode::QuantumEspresso;
    QString section;
    QString activeCard;
    const QStringList lines = text.split('\n');
    for (QString line : lines) {
        line = stripQeComment(line).trimmed();
        if (line.isEmpty()) continue;
        if (line.startsWith('&')) {
            section = line.mid(1).trimmed().toUpper();
            activeCard.clear();
            continue;
        }
        if (line == "/") {
            section.clear();
            continue;
        }
        if (section == "__KPOINTS_NEXT__") {
            DftParameterRegistry::setParameterValue(&r.settings, "qe.K_POINTS.automatic", line, DftParameterSource::ImportedQeIn);
            section.clear();
            continue;
        }
        if (!section.isEmpty()) {
            QString buffer = line;
            const QStringList pieces = buffer.split(',', Qt::SkipEmptyParts);
            for (QString piece : pieces) {
                piece = piece.trimmed();
                const int eq = piece.indexOf('=');
                if (eq <= 0) continue;
                const QString key = piece.left(eq).trimmed();
                const QString value = piece.mid(eq + 1).trimmed();
                applyKnownQe(&r.settings, section, key, value, &r.diffLines);
            }
            continue;
        }
        const QString upper = line.toUpper();
        if (upper.startsWith("K_POINTS")) {
            section = QStringLiteral("__KPOINTS_NEXT__");
            activeCard.clear();
            continue;
        }
        if (upper.startsWith("CELL_PARAMETERS") || upper.startsWith("ATOMIC_SPECIES") || upper.startsWith("ATOMIC_POSITIONS")) {
            DftRawParameter raw;
            raw.code = DftCode::QuantumEspresso;
            raw.namelistOrBlock = line.section(QRegularExpression("\\s+"), 0, 0);
            raw.key = QStringLiteral("card");
            raw.value = line;
            raw.blockOrCard = true;
            raw.enabled = false;
            r.settings.rawParameters << raw;
            activeCard = raw.namelistOrBlock.toUpper();
            if (activeCard == QStringLiteral("ATOMIC_SPECIES")) r.settings.qeSpecies.clear();
            continue;
        }
        if (activeCard == QStringLiteral("ATOMIC_SPECIES")) {
            appendImportedQeSpecies(&r.settings, line);
            r.diffLines << QStringLiteral("ATOMIC_SPECIES imported: %1").arg(line.section(QRegularExpression("\\s+"), 0, 0));
            continue;
        }
    }
    return r;
}

DftImportResult DftInputParser::parseProfileJsonText(const QString& text, const DftSettings& baseSettings) {
    DftImportResult r;
    r.settings = baseSettings;
    const auto doc = QJsonDocument::fromJson(text.toUtf8());
    if (!doc.isObject()) {
        r.messages << QStringLiteral("JSON profileとして読めません。");
        return r;
    }
    r.ok = true;
    r.sourceKind = QStringLiteral("json_profile");
    const QJsonObject root = doc.object();
    const QString code = root.value("code").toString(dftCodeKey(baseSettings.code)).toLower();
    r.settings.code = code == "qe" || code.contains("espresso") ? DftCode::QuantumEspresso : DftCode::Siesta;
    r.settings.version = root.value("version").toString(root.value("package_version").toString(r.settings.version));
    r.settings.targetName = root.value("target").toString(root.value("target_name").toString(r.settings.targetName));
    r.settings.profileName = root.value("profile").toString(root.value("profile_name").toString(root.value("selected_profile").toString(QStringLiteral("Imported JSON"))));
    if (root.contains(QStringLiteral("fixed_atom_mode"))) {
        r.settings.fixedAtomMode = dftFixedAtomModeFromKey(root.value(QStringLiteral("fixed_atom_mode")).toString());
        r.diffLines << QStringLiteral("fixed_atom_mode imported");
    }
    if (root.contains(QStringLiteral("trailing_flag_interpretation"))) {
        r.settings.trailingFlagInterpretation = dftTrailingFlagInterpretationFromKey(root.value(QStringLiteral("trailing_flag_interpretation")).toString());
        r.diffLines << QStringLiteral("trailing_flag_interpretation imported");
    }
    r.settings.generationMode = DftGenerationMode::ImportEdit;
    const QJsonObject params = root.value("parameters").toObject();
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        QString id = it.key();
        if (!id.startsWith("siesta.") && !id.startsWith("qe.")) {
            id = dftCodeKey(r.settings.code) + QStringLiteral(".") + id;
        }
        DftParameterRegistry::setParameterValue(&r.settings, id, it.value().toVariant().toString(), DftParameterSource::ImportedFile);
        r.diffLines << QStringLiteral("%1 imported").arg(id);
    }
    const QJsonArray raw = root.value("raw_parameters").toArray();
    for (const auto& value : raw) {
        const QJsonObject obj = value.toObject();
        DftRawParameter p;
        p.code = r.settings.code;
        p.namelistOrBlock = obj.value("namelist_or_card").toString(obj.value("block").toString());
        p.key = obj.value("key").toString();
        p.value = obj.value("value").toString();
        p.unit = obj.value("unit").toString();
        p.enabled = obj.value("enabled").toBool(true);
        p.blockOrCard = obj.value("block_or_scalar").toString() == "block" || obj.value("block").toBool(false);
        r.settings.rawParameters << p;
    }
    return r;
}

