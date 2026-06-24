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

#include <algorithm>

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

QString jsonScalarText(const QJsonValue& value) {
    if (value.isUndefined() || value.isNull()) return QString();
    if (value.isBool()) return value.toBool() ? QStringLiteral("T") : QStringLiteral("F");
    return value.toVariant().toString().trimmed();
}

QString jsonProfileValue(const QJsonValue& value) {
    if (!value.isObject()) return jsonScalarText(value);
    const QJsonObject object = value.toObject();
    if (object.contains(QStringLiteral("emit")) && !object.value(QStringLiteral("emit")).toBool(true)) {
        return QString();
    }
    if (object.contains(QStringLiteral("value"))) return jsonScalarText(object.value(QStringLiteral("value")));
    return jsonScalarText(value);
}

QString removeUnitSuffix(QString value, const QString& unit) {
    value = value.trimmed();
    const QRegularExpression suffix(QStringLiteral("\\s+%1\\s*$").arg(QRegularExpression::escape(unit)),
                                    QRegularExpression::CaseInsensitiveOption);
    return value.remove(suffix).trimmed();
}

QString normalizedFdfLabel(QString label) {
    label = label.trimmed().toLower();
    label.remove(QLatin1Char('.'));
    label.remove(QLatin1Char('_'));
    label.remove(QLatin1Char('-'));
    label.remove(QLatin1Char(' '));
    return label;
}

QString siestaParameterIdForFdfKey(const DftSettings& settings, const QString& key, bool blockOnly = false) {
    const QString wanted = normalizedFdfLabel(key);
    if (wanted.isEmpty()) return QString();
    for (auto it = settings.parameters.constBegin(); it != settings.parameters.constEnd(); ++it) {
        if (it->spec.code != DftCode::Siesta) continue;
        if (blockOnly && it->spec.outputFormat != QStringLiteral("block")) continue;
        if (normalizedFdfLabel(it->spec.key) == wanted) return it.key();
    }
    return QString();
}

QString normalizedSiestaProfileValue(const QString& key, QString value) {
    if (key.compare(QStringLiteral("NetCharge"), Qt::CaseInsensitive) == 0 && value.trimmed() == QStringLiteral("0")) return QStringLiteral("0.0");
    if (key.compare(QStringLiteral("MD.MaxForceTol"), Qt::CaseInsensitive) == 0) return removeUnitSuffix(value, QStringLiteral("eV/Ang"));
    if (key.compare(QStringLiteral("MD.MaxDispl"), Qt::CaseInsensitive) == 0) return removeUnitSuffix(value, QStringLiteral("Ang"));
    if (key.compare(QStringLiteral("MD.MaxStressTol"), Qt::CaseInsensitive) == 0) return removeUnitSuffix(value, QStringLiteral("GPa"));
    if (key.compare(QStringLiteral("MeshCutoff"), Qt::CaseInsensitive) == 0) return removeUnitSuffix(value, QStringLiteral("Ry"));
    return value.trimmed();
}

QString siestaElementFromProfileLabel(const QString& label) {
    if (label.startsWith(QStringLiteral("H"), Qt::CaseInsensitive)) return QStringLiteral("H");
    const QRegularExpression re(QStringLiteral("^([A-Z][a-z]?)"));
    const auto match = re.match(label.trimmed());
    return match.hasMatch() ? match.captured(1) : label.trimmed();
}

void applyJsonProfileParameters(DftSettings* settings, const QJsonObject& object,
                                const QString& prefix, DftParameterSource source) {
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        const QString value = normalizedSiestaProfileValue(it.key(), jsonProfileValue(it.value()));
        DftParameterRegistry::setParameterValue(settings, prefix + it.key(), value, source);
    }
}

void applyKnownSiesta(DftSettings* s, const QString& key, const QString& value, DftParameterSource source, QStringList* diff) {
    static const QMap<QString, QString> ids = {
        {"SystemName", "siesta.general.SystemName"}, {"SystemLabel", "siesta.general.SystemLabel"},
        {"NetCharge", "siesta.charge_spin.NetCharge"}, {"Spin", "siesta.charge_spin.Spin"},
        {"Spin.Fix", "siesta.charge_spin.Spin.Fix"}, {"Spin.Total", "siesta.charge_spin.Spin.Total"},
        {"SolutionMethod", "siesta.scf.SolutionMethod"}, {"MaxSCFIterations", "siesta.scf.MaxSCFIterations"},
        {"SCF.MustConverge", "siesta.scf.SCF.MustConverge"}, {"DM.Tolerance", "siesta.scf.DM.Tolerance"},
        {"DM.MixingWeight", "siesta.scf.DM.MixingWeight"}, {"DM.NumberPulay", "siesta.scf.DM.NumberPulay"},
        {"ElectronicTemperature", "siesta.scf.ElectronicTemperature"}, {"OccupationFunction", "siesta.scf.OccupationFunction"},
        {"MD.TypeOfRun", "siesta.relaxation.MD.TypeOfRun"}, {"MD.VariableCell", "siesta.relaxation.MD.VariableCell"},
        {"MD.Steps", "siesta.relaxation.MD.Steps"}, {"MD.MaxForceTol", "siesta.relaxation.MD.MaxForceTol"},
        {"MD.MaxDispl", "siesta.relaxation.MD.MaxDispl"}, {"MD.MaxStressTol", "siesta.relaxation.MD.MaxStressTol"},
        {"GeometryMustConverge", "siesta.relaxation.GeometryMustConverge"}, {"MeshCutoff", "siesta.species.MeshCutoff"},
        {"xc.functional", "siesta.species.xc.functional"}, {"xc.authors", "siesta.species.xc.authors"},
    };
    QString id = ids.value(key);
    if (id.isEmpty()) id = siestaParameterIdForFdfKey(*s, key, false);
    if (!id.isEmpty()) {
        const QString before = DftParameterRegistry::parameterValue(*s, id);
        DftParameterRegistry::setParameterValue(s, id, normalizedSiestaProfileValue(key, value), source);
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
                    const QString id = siestaParameterIdForFdfKey(r.settings, currentBlock, true);
                    if (!id.isEmpty()) {
                        const QString before = DftParameterRegistry::parameterValue(r.settings, id);
                        const QString value = blockValue.trimmed();
                        DftParameterRegistry::setParameterValue(&r.settings, id, value, source);
                        auto paramIt = r.settings.parameters.find(id);
                        if (paramIt != r.settings.parameters.end()) paramIt->enabled = !value.isEmpty();
                        if (before != value) r.diffLines << QStringLiteral("%1 block imported").arg(currentBlock);
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
    const bool profileSchema = root.value(QStringLiteral("schema_version")).toString().startsWith(QStringLiteral("aseapp.dft_profile"));
    const QString code = root.value("code").toString(dftCodeKey(baseSettings.code)).toLower();
    const DftCode parsedCode = code == "qe" || code.contains("espresso") ? DftCode::QuantumEspresso : DftCode::Siesta;
    const QString parsedVersion = root.value("version").toString(root.value("package_version").toString(r.settings.version));
    const QString parsedTarget = root.value("target").toString(root.value("target_name").toString(r.settings.targetName));
    if (profileSchema) {
        r.settings = DftParameterRegistry::defaultSettings(parsedCode, parsedVersion, parsedTarget);
        r.settings.sourceStructurePath = baseSettings.sourceStructurePath;
        r.settings.allowUnknownHydrogen = baseSettings.allowUnknownHydrogen;
        r.settings.qeProjectStyleFixedFlags = baseSettings.qeProjectStyleFixedFlags;
        r.settings.qeAssumeIsolated = baseSettings.qeAssumeIsolated;
    } else {
        r.settings.code = parsedCode;
        r.settings.version = parsedVersion;
    }
    r.settings.targetName = root.value("target").toString(root.value("target_name").toString(r.settings.targetName));
    r.settings.profileName = root.value("display_name").toString(
        root.value("profile").toString(
            root.value("profile_name").toString(
                root.value("selected_profile").toString(
                    root.value("profile_id").toString(QStringLiteral("Imported JSON"))))));
    r.settings.moduleName = root.value(QStringLiteral("module_name")).toString(r.settings.moduleName);
    r.settings.schema = root.value(QStringLiteral("syntax_profile")).toString(root.value(QStringLiteral("schema")).toString(r.settings.schema));
    r.settings.executable = root.value(QStringLiteral("executable")).toString(r.settings.executable);
    const QString profilePseudoDir = root.value(QStringLiteral("pseudopotential_directory")).toString(
        root.value(QStringLiteral("siesta_psf_directory")).toString(
            root.value(QStringLiteral("psf_directory")).toString(
                root.value(QStringLiteral("pseudo_dir")).toString(r.settings.pseudoDir))));
    if (!profilePseudoDir.trimmed().isEmpty()) r.settings.pseudoDir = profilePseudoDir.trimmed();
    const QString calculationMode = root.value(QStringLiteral("calculation_template")).toString(root.value(QStringLiteral("calculation_mode")).toString());
    if (!calculationMode.isEmpty()) {
        DftParameterRegistry::applyCalculationModeDefaults(&r.settings, calculationMode);
        r.diffLines << QStringLiteral("calculation_template imported: %1").arg(calculationMode);
    }
    const QJsonValue includeValue = root.value(QStringLiteral("include_xc_fdf"));
    if (includeValue.isObject()) {
        const QJsonObject include = includeValue.toObject();
        r.settings.includeXcFdf = include.value(QStringLiteral("default")).toBool(r.settings.includeXcFdf);
        r.settings.xcFdfPath = include.value(QStringLiteral("xc_include_file")).toString(r.settings.xcFdfPath);
        r.diffLines << QStringLiteral("include_xc_fdf imported");
    } else if (includeValue.isBool()) {
        r.settings.includeXcFdf = includeValue.toBool();
        r.diffLines << QStringLiteral("include_xc_fdf imported");
    }
    r.settings.xcFdfPath = root.value(QStringLiteral("xc_fdf_path")).toString(r.settings.xcFdfPath);
    const QJsonValue standaloneValue = root.value(QStringLiteral("standalone_inline"));
    if (standaloneValue.isObject()) {
        r.settings.standaloneInline = standaloneValue.toObject().value(QStringLiteral("default")).toBool(r.settings.standaloneInline);
    } else if (standaloneValue.isBool()) {
        r.settings.standaloneInline = standaloneValue.toBool();
    }
    if (root.contains(QStringLiteral("fixed_atom_mode"))) {
        r.settings.fixedAtomMode = dftFixedAtomModeFromKey(root.value(QStringLiteral("fixed_atom_mode")).toString());
        r.diffLines << QStringLiteral("fixed_atom_mode imported");
    }
    if (root.contains(QStringLiteral("trailing_flag_interpretation"))) {
        r.settings.trailingFlagInterpretation = dftTrailingFlagInterpretationFromKey(root.value(QStringLiteral("trailing_flag_interpretation")).toString());
        r.diffLines << QStringLiteral("trailing_flag_interpretation imported");
    }
    const QJsonObject fixedPolicy = root.value(QStringLiteral("fixed_atom_policy")).toObject();
    if (!fixedPolicy.isEmpty()) {
        const QString mode = fixedPolicy.value(QStringLiteral("default_mode")).toString();
        if (!mode.isEmpty()) r.settings.fixedAtomMode = dftFixedAtomModeFromKey(mode);
        const QString trailing = fixedPolicy.value(QStringLiteral("trailing_flag_interpretation_default")).toString();
        if (!trailing.isEmpty()) r.settings.trailingFlagInterpretation = dftTrailingFlagInterpretationFromKey(trailing);
        r.diffLines << QStringLiteral("fixed_atom_policy imported");
    }
    const QJsonObject structureHandling = root.value(QStringLiteral("structure_handling")).toObject();
    if (!structureHandling.isEmpty()) {
        const int coordinatePrecision = structureHandling.value(QStringLiteral("coordinate_precision_default")).toInt(-1);
        if (coordinatePrecision > 0) {
            DftParameterRegistry::setParameterValue(&r.settings, QStringLiteral("siesta.output.coordinate_precision"), QString::number(coordinatePrecision), DftParameterSource::ProjectProfile);
        }
        const int cellPrecision = structureHandling.value(QStringLiteral("cell_precision_default")).toInt(-1);
        if (cellPrecision > 0) {
            DftParameterRegistry::setParameterValue(&r.settings, QStringLiteral("siesta.output.cell_precision"), QString::number(cellPrecision), DftParameterSource::ProjectProfile);
        }
        r.diffLines << QStringLiteral("structure_handling imported");
    }
    if (r.settings.code == DftCode::Siesta) {
        QVector<DftSiestaSpecies> species;
        auto appendSiestaSpecies = [&species](const QJsonObject& obj, int fallbackIndex) {
            DftSiestaSpecies sp;
            sp.index = obj.value(QStringLiteral("index")).toInt(fallbackIndex);
            sp.atomicNumber = obj.value(QStringLiteral("atomic_number")).toInt();
            sp.label = obj.value(QStringLiteral("label")).toString();
            sp.element = obj.value(QStringLiteral("element")).toString(siestaElementFromProfileLabel(sp.label));
            sp.role = obj.value(QStringLiteral("role")).toString();
            sp.pseudopotential = obj.value(QStringLiteral("pseudopotential"))
                                     .toString(obj.value(QStringLiteral("psf_file"))
                                                   .toString(obj.value(QStringLiteral("pseudo_file")).toString()));
            if (sp.index > 0 && !sp.label.isEmpty()) species << sp;
        };
        const QJsonValue speciesMappingValue = root.value(QStringLiteral("species_mapping"));
        if (speciesMappingValue.isObject()) {
            const QJsonObject speciesMapping = speciesMappingValue.toObject();
            QStringList keys = speciesMapping.keys();
            std::sort(keys.begin(), keys.end(), [](const QString& a, const QString& b) { return a.toInt() < b.toInt(); });
            for (const QString& key : keys) appendSiestaSpecies(speciesMapping.value(key).toObject(), key.toInt());
        } else if (speciesMappingValue.isArray()) {
            for (const auto& value : speciesMappingValue.toArray()) appendSiestaSpecies(value.toObject(), 0);
        }
        for (const auto& value : root.value(QStringLiteral("siesta_species")).toArray()) appendSiestaSpecies(value.toObject(), 0);
        if (!species.isEmpty()) {
            std::sort(species.begin(), species.end(), [](const DftSiestaSpecies& a, const DftSiestaSpecies& b) { return a.index < b.index; });
            r.settings.siestaSpecies = species;
            r.diffLines << QStringLiteral("species_mapping imported");
        }
    } else {
        QJsonArray qeSpeciesArray = root.value(QStringLiteral("qe_species")).toArray();
        if (qeSpeciesArray.isEmpty()) qeSpeciesArray = root.value(QStringLiteral("pseudopotential_mapping")).toArray();
        if (!qeSpeciesArray.isEmpty()) {
            QVector<DftQeSpecies> species;
            for (const auto& value : qeSpeciesArray) {
                const QJsonObject obj = value.toObject();
                DftQeSpecies sp;
                sp.label = obj.value(QStringLiteral("label")).toString();
                sp.element = obj.value(QStringLiteral("element")).toString(elementFromQeSpeciesLabel(sp.label));
                sp.mass = obj.value(QStringLiteral("mass")).toDouble(1.0);
                sp.pseudoFile = obj.value(QStringLiteral("pseudo_file")).toString();
                sp.role = obj.value(QStringLiteral("role")).toString();
                sp.source = dftParameterSourceFromKey(obj.value(QStringLiteral("source")).toString(QStringLiteral("project_profile")));
                if (!sp.label.isEmpty()) species << sp;
            }
            if (!species.isEmpty()) {
                r.settings.qeSpecies = species;
                r.diffLines << QStringLiteral("pseudopotential_mapping imported");
            }
        }
    }
    if (r.settings.code == DftCode::Siesta) {
        const QJsonObject chargeSpin = root.value(QStringLiteral("charge_spin")).toObject();
        applyJsonProfileParameters(&r.settings, chargeSpin, QStringLiteral("siesta.charge_spin."), DftParameterSource::ProjectProfile);
        const QJsonObject relaxDefaults = root.value(QStringLiteral("relax_defaults")).toObject();
        applyJsonProfileParameters(&r.settings, relaxDefaults, QStringLiteral("siesta.relaxation."), DftParameterSource::ProjectProfile);
        const QJsonObject scfDefaults = root.value(QStringLiteral("scf_defaults")).toObject();
        applyJsonProfileParameters(&r.settings, scfDefaults, QStringLiteral("siesta.scf."), DftParameterSource::ProjectProfile);
        const QJsonObject kpoints = root.value(QStringLiteral("kpoints")).toObject();
        const QJsonArray kgridRows = kpoints.value(QStringLiteral("kgrid.MonkhorstPack")).toObject().value(QStringLiteral("value")).toArray();
        if (kgridRows.size() >= 3) {
            QStringList diagonal;
            for (int i = 0; i < 3; ++i) {
                const QJsonArray row = kgridRows.at(i).toArray();
                diagonal << jsonScalarText(row.size() > i ? row.at(i) : QJsonValue());
            }
            DftParameterRegistry::setParameterValue(&r.settings, QStringLiteral("siesta.kpoints.kgrid"), diagonal.join(QLatin1Char(' ')), DftParameterSource::ProjectProfile);
            r.diffLines << QStringLiteral("kgrid.MonkhorstPack imported");
        }
    }
    r.settings.generationMode = profileSchema ? DftGenerationMode::Profile : DftGenerationMode::ImportEdit;
    const QJsonObject params = root.value("parameters").toObject();
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        QString id = it.key();
        if (!id.startsWith("siesta.") && !id.startsWith("qe.")) {
            id = dftCodeKey(r.settings.code) + QStringLiteral(".") + id;
        }
        DftParameterSource source = profileSchema ? DftParameterSource::ProjectProfile : DftParameterSource::ImportedFile;
        bool enabled = true;
        if (it.value().isObject()) {
            const QJsonObject obj = it.value().toObject();
            enabled = obj.value(QStringLiteral("enabled")).toBool(true);
            const QString sourceKey = obj.value(QStringLiteral("source")).toString();
            if (!sourceKey.isEmpty()) source = dftParameterSourceFromKey(sourceKey);
        }
        DftParameterRegistry::setParameterValue(&r.settings, id, jsonProfileValue(it.value()), source);
        if (auto paramIt = r.settings.parameters.find(id); paramIt != r.settings.parameters.end()) {
            paramIt->enabled = enabled;
        }
        r.diffLines << QStringLiteral("%1 imported").arg(id);
    }
    const QJsonArray raw = root.value("raw_parameters").toArray();
    for (const auto& value : raw) {
        const QJsonObject obj = value.toObject();
        DftRawParameter p;
        const QString codeKey = obj.value(QStringLiteral("code")).toString(dftCodeKey(r.settings.code)).toLower();
        p.code = codeKey == QStringLiteral("qe") || codeKey.contains(QStringLiteral("espresso")) ? DftCode::QuantumEspresso : DftCode::Siesta;
        p.namelistOrBlock = obj.value(QStringLiteral("namelist_or_block")).toString(
            obj.value(QStringLiteral("namelist_or_card")).toString(obj.value(QStringLiteral("block")).toString()));
        p.key = obj.value("key").toString();
        p.value = obj.value("value").toString();
        p.unit = obj.value("unit").toString();
        p.outputPosition = obj.value(QStringLiteral("output_position")).toString();
        p.enabled = obj.value("enabled").toBool(true);
        p.blockOrCard = obj.contains(QStringLiteral("block_or_card"))
            ? obj.value(QStringLiteral("block_or_card")).toBool(false)
            : (obj.value("block_or_scalar").toString() == "block" || obj.value("block").toBool(false));
        r.settings.rawParameters << p;
    }
    return r;
}

