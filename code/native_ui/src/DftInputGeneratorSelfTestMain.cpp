#include "DftInputGenerator.h"
#include "DftInputGeneratorDialog.h"
#include "DftInputParser.h"
#include "DftParameterRegistry.h"
#include "StructureFileLoader.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextStream>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>

namespace {

struct TestContext {
    QStringList passed;
};

int fail(const QString& message) {
    QTextStream(stderr) << "DFT self-test failed: " << message << '\n';
    return 1;
}

bool check(TestContext* ctx, bool condition, const QString& name, int* result) {
    if (condition) {
        ctx->passed << name;
        return true;
    }
    *result = fail(name);
    return false;
}

NativeAtom atom(int id, const QString& element, float x, float y, float z,
                std::array<bool, 3> movable = {true, true, true}) {
    NativeAtom a;
    a.atomId = id;
    a.element = element;
    a.cartesian = QVector3D(x, y, z);
    a.fractional = QVector3D(x / 10.0f, y / 10.0f, z / 24.0f);
    a.movable = movable;
    return a;
}

StructureData ganSevenLayerLikeSlab() {
    StructureData s;
    s.title = QStringLiteral("GaN_7layer_audit");
    s.sourcePath = QStringLiteral("memory://GaN_7layer_audit");
    s.cellVectors[0] = QVector3D(10.0f, 0.0f, 0.0f);
    s.cellVectors[1] = QVector3D(0.0f, 10.0f, 0.0f);
    s.cellVectors[2] = QVector3D(0.0f, 0.0f, 24.0f);
    s.atoms.push_back(atom(1, QStringLiteral("H"), 0.0f, 0.0f, 0.0f));
    s.atoms.push_back(atom(2, QStringLiteral("N"), 0.0f, 0.0f, 0.95f, {false, false, false}));
    s.atoms.push_back(atom(3, QStringLiteral("Ga"), 1.8f, 0.0f, 2.2f, {false, false, false}));
    s.atoms.push_back(atom(4, QStringLiteral("N"), 0.0f, 1.8f, 3.5f));
    s.atoms.push_back(atom(5, QStringLiteral("Ga"), 1.8f, 1.8f, 4.8f));
    s.atoms.push_back(atom(6, QStringLiteral("N"), 0.0f, 0.0f, 6.1f));
    s.atoms.push_back(atom(7, QStringLiteral("Ga"), 1.8f, 0.0f, 7.4f));
    s.atoms.push_back(atom(8, QStringLiteral("N"), 0.0f, 1.8f, 8.7f));
    s.atoms.push_back(atom(9, QStringLiteral("Ga"), 1.8f, 1.8f, 10.0f));
    s.atoms.push_back(atom(10, QStringLiteral("H"), 1.8f, 1.8f, 11.0f));
    return s;
}

StructureData gaAtomReferenceStructure() {
    StructureData s;
    s.title = QStringLiteral("Ga_atom");
    s.sourcePath = QStringLiteral("memory://Ga_atom");
    s.cellVectors[0] = QVector3D(20.0f, 0.0f, 0.0f);
    s.cellVectors[1] = QVector3D(0.0f, 20.0f, 0.0f);
    s.cellVectors[2] = QVector3D(0.0f, 0.0f, 20.0f);
    s.atoms.push_back(atom(1, QStringLiteral("Ga"), 10.0f, 10.0f, 10.0f));
    return s;
}

StructureData h2Structure() {
    StructureData s;
    s.title = QStringLiteral("H2");
    s.sourcePath = QStringLiteral("memory://H2");
    s.cellVectors[0] = QVector3D(12.0f, 0.0f, 0.0f);
    s.cellVectors[1] = QVector3D(0.0f, 12.0f, 0.0f);
    s.cellVectors[2] = QVector3D(0.0f, 0.0f, 12.0f);
    s.atoms.push_back(atom(1, QStringLiteral("H"), 6.0f, 6.0f, 5.63f));
    s.atoms.push_back(atom(2, QStringLiteral("H"), 6.0f, 6.0f, 6.37f));
    return s;
}

StructureData threeGaHStructure() {
    StructureData s;
    s.title = QStringLiteral("3GaH");
    s.sourcePath = QStringLiteral("memory://3GaH");
    s.cellVectors[0] = QVector3D(12.0f, 0.0f, 0.0f);
    s.cellVectors[1] = QVector3D(0.0f, 12.0f, 0.0f);
    s.cellVectors[2] = QVector3D(0.0f, 0.0f, 12.0f);
    s.atoms.push_back(atom(1, QStringLiteral("Ga"), 5.0f, 5.0f, 5.0f));
    s.atoms.push_back(atom(2, QStringLiteral("Ga"), 7.0f, 5.0f, 5.0f));
    s.atoms.push_back(atom(3, QStringLiteral("Ga"), 6.0f, 6.7f, 5.0f));
    s.atoms.push_back(atom(4, QStringLiteral("H"), 6.0f, 5.6f, 6.0f));
    return s;
}

StructureData unknownHydrogenStructure() {
    StructureData s;
    s.title = QStringLiteral("unknown_H");
    s.sourcePath = QStringLiteral("memory://unknown_H");
    s.cellVectors[0] = QVector3D(20.0f, 0.0f, 0.0f);
    s.cellVectors[1] = QVector3D(0.0f, 20.0f, 0.0f);
    s.cellVectors[2] = QVector3D(0.0f, 0.0f, 20.0f);
    s.atoms.push_back(atom(1, QStringLiteral("H"), 0.0f, 0.0f, 0.0f));
    s.atoms.push_back(atom(2, QStringLiteral("Ga"), 10.0f, 10.0f, 10.0f));
    return s;
}

QString sourceFile(const QString& relative) {
#ifdef ASEAPP_NATIVE_UI_SOURCE_DIR
    return QDir(QStringLiteral(ASEAPP_NATIVE_UI_SOURCE_DIR)).filePath(relative);
#else
    return QDir::current().filePath(QStringLiteral("code/native_ui/") + relative);
#endif
}

QString sevenLayerVaspPath() {
    const QString envPath = qEnvironmentVariable("ASEAPP_DFT_7LAYER_VASP");
    if (!envPath.trimmed().isEmpty()) return envPath;
    const QStringList candidates = {
        QStringLiteral("C:/Users/Takae/Documents/GitHub/Siesta_data/layers/7layer.vasp"),
        sourceFile(QStringLiteral("tests/data/7layer.vasp")),
    };
    for (const QString& path : candidates) {
        if (QFileInfo::exists(path)) return path;
    }
    return candidates.first();
}

std::optional<StructureData> loadSevenLayerVasp(QString* usedPath, QString* error) {
    const QString path = sevenLayerVaspPath();
    if (usedPath) *usedPath = path;
    StructureFileLoader loader;
    auto loaded = loader.load(path, error);
    if (!loaded.has_value()) return std::nullopt;
    loaded->sourcePath = path;
    return loaded;
}

std::optional<StructureData> loadSevenLayerVaspWithOptions(StructureTrailingFlagInterpretation interpretation,
                                                           QString* usedPath,
                                                           QString* error) {
    const QString path = sevenLayerVaspPath();
    if (usedPath) *usedPath = path;
    StructureImportOptions options;
    options.trailingFlagInterpretation = interpretation;
    StructureFileLoader loader;
    auto loaded = loader.load(path, error, options);
    if (!loaded.has_value()) return std::nullopt;
    loaded->sourcePath = path;
    return loaded;
}

QString readTextFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    QTextStream in(&file);
    return in.readAll();
}

int countOccurrences(const QString& text, const QString& needle) {
    int count = 0;
    int from = 0;
    while (true) {
        const int index = text.indexOf(needle, from, Qt::CaseInsensitive);
        if (index < 0) break;
        ++count;
        from = index + needle.size();
    }
    return count;
}

int elementCount(const StructureData& structure, const QString& element) {
    int count = 0;
    for (const auto& atom : structure.atoms) {
        if (atom.element.compare(element, Qt::CaseInsensitive) == 0) ++count;
    }
    return count;
}

int integerValueForKey(const QString& text, const QString& key) {
    const QRegularExpression re(QStringLiteral("\\b%1\\s*=*\\s*(\\d+)").arg(QRegularExpression::escape(key)), QRegularExpression::CaseInsensitiveOption);
    const auto match = re.match(text);
    return match.hasMatch() ? match.captured(1).toInt() : -1;
}

QStringList linesBetween(const QString& text, const QString& start, const QString& end) {
    QStringList out;
    bool capture = false;
    for (const QString& line : text.split('\n')) {
        const QString trimmed = line.trimmed();
        if (trimmed.compare(start, Qt::CaseInsensitive) == 0 || trimmed.startsWith(start + QLatin1Char(' '), Qt::CaseInsensitive)) {
            capture = true;
            continue;
        }
        if (capture && (trimmed.compare(end, Qt::CaseInsensitive) == 0 || trimmed.startsWith(end, Qt::CaseInsensitive))) break;
        if (capture && !trimmed.isEmpty()) out << trimmed;
    }
    return out;
}

int qeAtomicSpeciesCount(const QString& text) {
    return linesBetween(text, QStringLiteral("ATOMIC_SPECIES"), QStringLiteral("ATOMIC_POSITIONS")).size();
}

QVector<int> siestaAtomicSpeciesSequence(const QString& text) {
    QVector<int> species;
    const QStringList lines = linesBetween(text, QStringLiteral("%block AtomicCoordinatesAndAtomicSpecies"), QStringLiteral("%endblock AtomicCoordinatesAndAtomicSpecies"));
    for (const QString& line : lines) {
        const QStringList parts = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (parts.size() < 4) continue;
        species << parts.last().toInt();
    }
    return species;
}

QString qePositionFlags(const QString& text, int zeroBasedAtomIndex) {
    const QStringList lines = linesBetween(text, QStringLiteral("ATOMIC_POSITIONS"), QStringLiteral("K_POINTS"));
    if (zeroBasedAtomIndex < 0 || zeroBasedAtomIndex >= lines.size()) return QString();
    const QStringList parts = lines.at(zeroBasedAtomIndex).split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (parts.size() < 7) return QString();
    return parts.mid(parts.size() - 3).join(QLatin1Char(' '));
}

QStringList qeAtomicLabelSequence(const QString& text) {
    QStringList labels;
    const QStringList lines = linesBetween(text, QStringLiteral("ATOMIC_POSITIONS"), QStringLiteral("K_POINTS"));
    for (const QString& line : lines) {
        const QStringList parts = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (!parts.isEmpty()) labels << parts.first();
    }
    return labels;
}

QVector<int> siestaConstraintAtoms(const QString& text) {
    QVector<int> atoms;
    const QStringList lines = linesBetween(text, QStringLiteral("%block Geometry.Constraints"), QStringLiteral("%endblock Geometry.Constraints"));
    const QRegularExpression re(QStringLiteral("\\batom\\s+(\\d+)"), QRegularExpression::CaseInsensitiveOption);
    for (const QString& line : lines) {
        const auto match = re.match(line);
        if (match.hasMatch()) atoms << match.captured(1).toInt();
    }
    return atoms;
}

int psLmaxValue(const QString& text, const QString& label) {
    const QStringList blockRows = linesBetween(text, QStringLiteral("%block PS.lmax"), QStringLiteral("%endblock PS.lmax"));
    for (const QString& row : blockRows) {
        const QStringList parts = row.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (parts.size() >= 2 && parts.at(0).compare(label, Qt::CaseInsensitive) == 0) return parts.at(1).toInt();
    }
    const QRegularExpression scalarRe(QStringLiteral("\\bPS\\.lmax\\s+%1\\s+(\\d+)").arg(QRegularExpression::escape(label)), QRegularExpression::CaseInsensitiveOption);
    const auto match = scalarRe.match(text);
    return match.hasMatch() ? match.captured(1).toInt() : -1;
}

int extractedPsLmaxValue(const QString& path, const QString& label) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return -1;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    return doc.object()
        .value(QStringLiteral("siesta")).toObject()
        .value(QStringLiteral("PS.lmax")).toObject()
        .value(label).toInt(-1);
}

bool summaryAtomOrderMatches(const StructureData& structure, const QJsonObject& summary) {
    const QJsonArray order = summary.value(QStringLiteral("atom_order")).toArray();
    if (order.size() != static_cast<int>(structure.atoms.size())) return false;
    for (int i = 0; i < order.size(); ++i) {
        const QJsonObject item = order.at(i).toObject();
        if (item.value(QStringLiteral("atom_index")).toInt() != i + 1) return false;
        if (item.value(QStringLiteral("element")).toString().compare(structure.atoms.at(static_cast<std::size_t>(i)).element, Qt::CaseInsensitive) != 0) return false;
    }
    return summary.value(QStringLiteral("atom_order_preserved")).toBool(false);
}

int decimalPlacesForFirstNumberInBlock(const QString& text, const QString& start, const QString& end, int tokenIndex) {
    const QStringList lines = linesBetween(text, start, end);
    for (const QString& line : lines) {
        const QStringList parts = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (parts.size() <= tokenIndex) continue;
        const QString token = parts.at(tokenIndex);
        const int dot = token.indexOf('.');
        if (dot < 0) continue;
        return token.size() - dot - 1;
    }
    return -1;
}

bool sevenLayerHydrogenRolesAreBottom075(const QVector<DftHydrogenAssignment>& assignments) {
    if (assignments.size() != 4) return false;
    for (const auto& h : assignments) {
        if (h.selectedRole != DftHydrogenRole::BottomPseudoHNTerminated075) return false;
        if (h.siestaSpecies != QStringLiteral("H-0.750") || h.siestaSpeciesIndex != 1) return false;
        if (h.qeLabel != QStringLiteral("H") || h.qePseudoFile != QStringLiteral("H.pbe-MT.075.UPF")) return false;
        if (!h.fixedByRole) return false;
    }
    return true;
}

bool containsWarning(const QStringList& warnings, const QString& text) {
    for (const auto& warning : warnings) {
        if (warning.contains(text, Qt::CaseInsensitive)) return true;
    }
    return false;
}

const DftHydrogenAssignment* assignmentFor(const QVector<DftHydrogenAssignment>& assignments, int atomIndex) {
    for (const auto& a : assignments) {
        if (a.atomIndex == atomIndex) return &a;
    }
    return nullptr;
}

QVector<int> expectedSevenFixedAtoms() {
    return {1, 5, 9, 13, 42, 46, 50, 54, 58, 59, 60, 61};
}

QVector<int> bottomHydrogenAtoms() {
    return {58, 59, 60, 61};
}

QVector<int> sortedCopy(QVector<int> values) {
    std::sort(values.begin(), values.end());
    return values;
}

void setTargetName(DftSettings* settings, const QString& targetName) {
    if (settings == nullptr) return;
    settings->targetName = targetName;
    if (settings->code == DftCode::Siesta) {
        DftParameterRegistry::setParameterValue(settings, QStringLiteral("siesta.general.SystemName"), targetName, DftParameterSource::UserOverride);
        DftParameterRegistry::setParameterValue(settings, QStringLiteral("siesta.general.SystemLabel"), targetName, DftParameterSource::UserOverride);
    }
}

QVector<int> summaryFixedIndices(const QJsonObject& summary) {
    QVector<int> indices;
    const QJsonArray atoms = summary.value(QStringLiteral("fixed_atoms")).toArray();
    for (const auto& value : atoms) {
        const QJsonObject item = value.toObject();
        const QJsonArray flags = item.value(QStringLiteral("fixed")).toArray();
        bool hasFixedAxis = false;
        for (const auto& flag : flags) hasFixedAxis = hasFixedAxis || flag.toBool(false);
        if (hasFixedAxis) {
            indices << item.value(QStringLiteral("atom_index")).toInt();
        }
    }
    return sortedCopy(indices);
}

QString summaryFixedReason(const QJsonObject& summary, int atomIndex) {
    const QJsonArray atoms = summary.value(QStringLiteral("fixed_atoms")).toArray();
    for (const auto& value : atoms) {
        const QJsonObject item = value.toObject();
        if (item.value(QStringLiteral("atom_index")).toInt() == atomIndex) {
            return item.value(QStringLiteral("reason")).toString();
        }
    }
    return QString();
}

bool summaryHydrogenOverride(const QJsonObject& summary, int atomIndex, bool expected) {
    const QJsonArray roles = summary.value(QStringLiteral("hydrogen_roles")).toArray();
    for (const auto& value : roles) {
        const QJsonObject item = value.toObject();
        if (item.value(QStringLiteral("atom_index")).toInt() == atomIndex) {
            return item.value(QStringLiteral("user_overrode_inference")).toBool(false) == expected;
        }
    }
    return false;
}

QSet<QString> qeAtomicSpeciesLabels(const QString& text) {
    QSet<QString> labels;
    const QStringList lines = linesBetween(text, QStringLiteral("ATOMIC_SPECIES"), QStringLiteral("ATOMIC_POSITIONS"));
    for (const QString& line : lines) {
        const QStringList parts = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (!parts.isEmpty()) labels.insert(parts.first());
    }
    return labels;
}

double qeAtomicSpeciesMass(const QString& text, const QString& label) {
    const QStringList lines = linesBetween(text, QStringLiteral("ATOMIC_SPECIES"), QStringLiteral("ATOMIC_POSITIONS"));
    for (const QString& line : lines) {
        const QStringList parts = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (parts.size() >= 2 && parts.first().compare(label, Qt::CaseInsensitive) == 0) return parts.at(1).toDouble();
    }
    return std::numeric_limits<double>::quiet_NaN();
}

double qeSpeciesMass(const QVector<DftQeSpecies>& species, const QString& label) {
    for (const auto& sp : species) {
        if (sp.label.compare(label, Qt::CaseInsensitive) == 0) return sp.mass;
    }
    return std::numeric_limits<double>::quiet_NaN();
}

bool almostEqual(double actual, double expected, double eps = 1.0e-8) {
    return std::abs(actual - expected) <= eps;
}

bool allMovable(const StructureData& structure, int zeroBasedIndex) {
    if (zeroBasedIndex < 0 || zeroBasedIndex >= static_cast<int>(structure.atoms.size())) return false;
    const auto& m = structure.atoms.at(static_cast<std::size_t>(zeroBasedIndex)).movable;
    return m[0] && m[1] && m[2];
}

bool allFixed(const StructureData& structure, int zeroBasedIndex) {
    if (zeroBasedIndex < 0 || zeroBasedIndex >= static_cast<int>(structure.atoms.size())) return false;
    const auto& m = structure.atoms.at(static_cast<std::size_t>(zeroBasedIndex)).movable;
    return !m[0] && !m[1] && !m[2];
}

bool noJobScriptsUnder(const QString& outputRoot) {
    if (outputRoot.isEmpty()) return true;
    QDirIterator it(outputRoot, QStringList() << QStringLiteral("*.sh") << QStringLiteral("*.csh")
                                             << QStringLiteral("*.pbs") << QStringLiteral("*.slurm")
                                             << QStringLiteral("*.sbatch"),
                    QDir::Files, QDirIterator::Subdirectories);
    return !it.hasNext();
}

bool parameterWidgetHasTooltip(QTableWidget* table, const QString& key) {
    if (table == nullptr) return false;
    for (int row = 0; row < table->rowCount(); ++row) {
        const auto* keyItem = table->item(row, 1);
        if (keyItem == nullptr || keyItem->text() != key) continue;
        QWidget* value = table->cellWidget(row, 4);
        if (value != nullptr && !value->toolTip().trimmed().isEmpty()) return true;
        if (auto* item = table->item(row, 4); item != nullptr && !item->toolTip().trimmed().isEmpty()) return true;
    }
    return false;
}

DftRawParameter siestaRawScalar(const QString& key, const QString& value) {
    DftRawParameter raw;
    raw.code = DftCode::Siesta;
    raw.key = key;
    raw.value = value;
    raw.enabled = true;
    return raw;
}

DftRawParameter siestaRawBlock(const QString& key, const QString& value) {
    DftRawParameter raw = siestaRawScalar(key, value);
    raw.blockOrCard = true;
    return raw;
}

DftRawParameter qeRawNamelist(const QString& section, const QString& key, const QString& value) {
    DftRawParameter raw;
    raw.code = DftCode::QuantumEspresso;
    raw.namelistOrBlock = section;
    raw.key = key;
    raw.value = value;
    raw.enabled = true;
    return raw;
}

bool writeSample(const QString& outputRoot, const QString& caseName, const StructureData& structure,
                 const DftSettings& settings, const DftGeneratedInput& generated, QString* error) {
    if (outputRoot.isEmpty()) return true;
    QDir root(outputRoot);
    if (!root.exists() && !root.mkpath(QStringLiteral("."))) {
        if (error) *error = QStringLiteral("cannot create audit output directory: %1").arg(outputRoot);
        return false;
    }
    Q_UNUSED(structure);
    return DftInputGenerator::writeGeneratedFiles(root.filePath(caseName), settings, generated, error);
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    Q_UNUSED(app);
    TestContext ctx;
    int result = 0;
    const QString auditOutput = qEnvironmentVariable("ASEAPP_DFT_AUDIT_OUT");
    QString writeError;
    if (!auditOutput.isEmpty()) {
        QDir auditDir(auditOutput);
        if (auditDir.exists()) auditDir.removeRecursively();
        QDir().mkpath(auditOutput);
    }

    const StructureData slab = ganSevenLayerLikeSlab();
    QString sevenLayerPath;
    QString sevenLayerError;
    const auto sevenLayerLoaded = loadSevenLayerVasp(&sevenLayerPath, &sevenLayerError);
    if (!check(&ctx, sevenLayerLoaded.has_value(), QStringLiteral("actual 7layer.vasp loads from %1: %2").arg(sevenLayerPath, sevenLayerError), &result)) return result;
    const StructureData sevenLayer = *sevenLayerLoaded;
    if (!check(&ctx, static_cast<int>(sevenLayer.atoms.size()) == 61, QStringLiteral("7layer.vasp atom count is 61"), &result)) return result;
    if (!check(&ctx, elementCount(sevenLayer, QStringLiteral("Ga")) == 29 && elementCount(sevenLayer, QStringLiteral("N")) == 28 && elementCount(sevenLayer, QStringLiteral("H")) == 4, QStringLiteral("7layer.vasp composition is Ga29 N28 H4"), &result)) return result;
    if (!check(&ctx, sevenLayer.trailingFlagInterpretation == QStringLiteral("preserve_or_ignore_unknown") && sevenLayer.importedExtraColumns.contains(58) && allMovable(sevenLayer, 57), QStringLiteral("7layer default POSCAR numeric trailing flags are preserved as extra columns and not fixed"), &result)) return result;
    const auto sevenLayerNumericFixed = loadSevenLayerVaspWithOptions(StructureTrailingFlagInterpretation::NumericOneMeansFixed, nullptr, &sevenLayerError);
    const auto sevenLayerNumericMovable = loadSevenLayerVaspWithOptions(StructureTrailingFlagInterpretation::NumericOneMeansMovable, nullptr, &sevenLayerError);
    const auto sevenLayerIgnored = loadSevenLayerVaspWithOptions(StructureTrailingFlagInterpretation::IgnoreTrailingFlags, nullptr, &sevenLayerError);
    if (!check(&ctx, sevenLayerNumericFixed.has_value() && allFixed(*sevenLayerNumericFixed, 57) && allFixed(*sevenLayerNumericFixed, 41), QStringLiteral("POSCAR numeric 1 1 1 can be explicitly interpreted as fixed"), &result)) return result;
    if (!check(&ctx, sevenLayerNumericMovable.has_value() && allMovable(*sevenLayerNumericMovable, 57) && sevenLayerIgnored.has_value() && allMovable(*sevenLayerIgnored, 57), QStringLiteral("POSCAR numeric 1 1 1 can be interpreted as movable or ignored"), &result)) return result;

    DftSettings siestaNeutral = DftParameterRegistry::defaultSettings(DftCode::Siesta, QStringLiteral("4.1.5"), QStringLiteral("ideal"));
    siestaNeutral.generationMode = DftGenerationMode::Manual;
    siestaNeutral.includeXcFdf = true;
    siestaNeutral.xcFdfPath = QStringLiteral("xc.fdf");
    siestaNeutral.fixedAtomMode = DftFixedAtomMode::FixBottomPseudoHPlusBottomMolecularLayer;
    siestaNeutral.hydrogenAssignments = DftInputGenerator::inferHydrogenRoles(slab, siestaNeutral);
    DftGeneratedInput siestaNeutralOut = DftInputGenerator::generate(slab, siestaNeutral);
    if (!check(&ctx, siestaNeutralOut.ok, QStringLiteral("Manual SIESTA neutral without profile generates"), &result)) return result;
    if (!check(&ctx, DftInputGenerator::hasNoExplanatoryComments(siestaNeutralOut.primaryText, DftCode::Siesta), QStringLiteral("SIESTA neutral has no explanatory comments"), &result)) return result;
    if (!check(&ctx, integerValueForKey(siestaNeutralOut.primaryText, QStringLiteral("NumberOfAtoms")) == static_cast<int>(slab.atoms.size()), QStringLiteral("SIESTA neutral NumberOfAtoms matches structure"), &result)) return result;
    if (!check(&ctx, siestaAtomicSpeciesSequence(siestaNeutralOut.primaryText) == QVector<int>({1, 2, 3, 2, 3, 2, 3, 2, 3, 4}), QStringLiteral("SIESTA neutral atom order and species indices preserved"), &result)) return result;
    if (!check(&ctx, siestaNeutralOut.primaryText.contains(QStringLiteral("%include xc.fdf")) && !siestaNeutralOut.primaryText.contains(QStringLiteral("%block ChemicalSpeciesLabel")) && !siestaNeutralOut.primaryText.contains(QStringLiteral("%block PAO.Basis")) && !siestaNeutralOut.primaryText.contains(QStringLiteral("%block SyntheticAtoms")), QStringLiteral("SIESTA neutral include mode avoids inline species/basis duplication"), &result)) return result;
    if (!check(&ctx, siestaNeutralOut.primaryText.contains(QStringLiteral("%block Geometry.Constraints")) && siestaNeutralOut.primaryText.contains(QStringLiteral("atom 1")) && siestaNeutralOut.primaryText.contains(QStringLiteral("atom 2")) && siestaNeutralOut.primaryText.contains(QStringLiteral("atom 3")), QStringLiteral("SIESTA neutral fixed native/pseudo-H atoms in Geometry.Constraints"), &result)) return result;
    if (!check(&ctx, siestaNeutralOut.summaryObject.contains(QStringLiteral("hydrogen_roles")) && siestaNeutralOut.summaryObject.contains(QStringLiteral("fixed_atoms")) && siestaNeutralOut.summaryObject.contains(QStringLiteral("movable_flags")) && siestaNeutralOut.summaryObject.contains(QStringLiteral("atom_order")), QStringLiteral("SIESTA neutral summary records judgments"), &result)) return result;
    Q_UNUSED(writeError);

    const auto* bottomH = assignmentFor(siestaNeutral.hydrogenAssignments, 0);
    const auto* topH = assignmentFor(siestaNeutral.hydrogenAssignments, 9);
    if (!check(&ctx, bottomH != nullptr && bottomH->selectedRole == DftHydrogenRole::BottomPseudoHNTerminated075 && bottomH->nearestNonHElement == QStringLiteral("N") && bottomH->fixedByRole, QStringLiteral("HydrogenRole bottom H inferred as H-0.750 from nearest N and fixed"), &result)) return result;
    if (!check(&ctx, topH != nullptr && topH->selectedRole == DftHydrogenRole::SurfaceAdsorbedHydrogen, QStringLiteral("HydrogenRole top ordinary surface H is not pseudo-H"), &result)) return result;

    DftSettings siestaCharged = siestaNeutral;
    setTargetName(&siestaCharged, QStringLiteral("ideal_n025"));
    DftParameterRegistry::applyCalculationModeDefaults(&siestaCharged, QStringLiteral("charged_slab_electron_added"));
    siestaCharged.hydrogenAssignments = DftInputGenerator::inferHydrogenRoles(slab, siestaCharged);
    DftGeneratedInput siestaChargedOut = DftInputGenerator::generate(slab, siestaCharged);
    if (!check(&ctx, siestaChargedOut.ok, QStringLiteral("SIESTA charged slab generates"), &result)) return result;
    if (!check(&ctx, siestaChargedOut.primaryText.contains(QStringLiteral("NetCharge -0.25")) && siestaChargedOut.primaryText.contains(QStringLiteral("Spin polarized")) && siestaChargedOut.primaryText.contains(QStringLiteral("Spin.Fix F")) && !siestaChargedOut.primaryText.contains(QStringLiteral("Spin.Total")), QStringLiteral("SIESTA charged slab charge/spin fields correct"), &result)) return result;
    if (!check(&ctx, siestaChargedOut.primaryText.contains(QStringLiteral("atom 1")) && siestaChargedOut.primaryText.contains(QStringLiteral("atom 2")) && siestaCharged.hydrogenAssignments.size() == siestaNeutral.hydrogenAssignments.size(), QStringLiteral("SIESTA charged fixed atoms and H roles match neutral"), &result)) return result;

    const QString xcPath = sourceFile(QStringLiteral("assets/dft_profiles/siesta/xc.fdf"));
    const QString xcText = readTextFile(xcPath);
    if (!check(&ctx, !xcText.isEmpty(), QStringLiteral("bundled xc.fdf is readable"), &result)) return result;
    if (!check(&ctx, psLmaxValue(xcText, QStringLiteral("H-0.750")) == 2 && psLmaxValue(xcText, QStringLiteral("H-1.250")) == 2 && psLmaxValue(xcText, QStringLiteral("H")) == 2 && psLmaxValue(xcText, QStringLiteral("N")) == 2 && psLmaxValue(xcText, QStringLiteral("Ga")) == 2, QStringLiteral("bundled xc.fdf PS.lmax values are all 2"), &result)) return result;
    DftSettings siestaStandaloneBase = DftParameterRegistry::defaultSettings(DftCode::Siesta, QStringLiteral("4.1.5"), QStringLiteral("ideal_standalone"));
    auto importedXc = DftInputParser::parseSiestaFdfText(xcText, siestaStandaloneBase, DftParameterSource::ImportedFile);
    DftSettings siestaStandalone = importedXc.settings;
    setTargetName(&siestaStandalone, QStringLiteral("ideal_standalone"));
    siestaStandalone.generationMode = DftGenerationMode::ImportEdit;
    siestaStandalone.includeXcFdf = false;
    siestaStandalone.standaloneInline = true;
    siestaStandalone.fixedAtomMode = DftFixedAtomMode::FixBottomPseudoHPlusBottomMolecularLayer;
    siestaStandalone.hydrogenAssignments = DftInputGenerator::inferHydrogenRoles(slab, siestaStandalone);
    DftGeneratedInput siestaStandaloneOut = DftInputGenerator::generate(slab, siestaStandalone);
    if (!check(&ctx, siestaStandaloneOut.ok, QStringLiteral("SIESTA standalone inline xc generates"), &result)) return result;
    if (!check(&ctx, !siestaStandaloneOut.primaryText.contains(QStringLiteral("%include")) && siestaStandaloneOut.primaryText.contains(QStringLiteral("%block ChemicalSpeciesLabel")) && siestaStandaloneOut.primaryText.contains(QStringLiteral("%block PAO.Basis")) && siestaStandaloneOut.primaryText.contains(QStringLiteral("%block PS.lmax")) && psLmaxValue(siestaStandaloneOut.primaryText, QStringLiteral("H-0.750")) == 2 && psLmaxValue(siestaStandaloneOut.primaryText, QStringLiteral("H-1.250")) == 2 && psLmaxValue(siestaStandaloneOut.primaryText, QStringLiteral("H")) == 2 && psLmaxValue(siestaStandaloneOut.primaryText, QStringLiteral("N")) == 2 && psLmaxValue(siestaStandaloneOut.primaryText, QStringLiteral("Ga")) == 2 && siestaStandaloneOut.primaryText.contains(QStringLiteral("%block SyntheticAtoms")) && siestaStandaloneOut.primaryText.contains(QStringLiteral("MeshCutoff 410 Ry")), QStringLiteral("SIESTA standalone includes inline species/basis/PS.lmax=2/synthetic/mesh"), &result)) return result;
    if (!check(&ctx, countOccurrences(siestaStandaloneOut.primaryText, QStringLiteral("%block ChemicalSpeciesLabel")) == 1 && countOccurrences(siestaStandaloneOut.primaryText, QStringLiteral("%block SyntheticAtoms")) == 1, QStringLiteral("SIESTA standalone avoids duplicated generated species blocks"), &result)) return result;
    QTemporaryDir extractedDir;
    if (!check(&ctx, extractedDir.isValid() && DftInputGenerator::writeGeneratedFiles(extractedDir.path(), siestaStandalone, siestaStandaloneOut, &writeError), QStringLiteral("SIESTA standalone writes extracted parameters"), &result)) return result;
    if (!check(&ctx, extractedPsLmaxValue(extractedDir.filePath(QStringLiteral("ideal_standalone.extracted_parameters.json")), QStringLiteral("H-0.750")) == 2 && extractedPsLmaxValue(extractedDir.filePath(QStringLiteral("ideal_standalone.extracted_parameters.json")), QStringLiteral("H-1.250")) == 2 && extractedPsLmaxValue(extractedDir.filePath(QStringLiteral("ideal_standalone.extracted_parameters.json")), QStringLiteral("H")) == 2 && extractedPsLmaxValue(extractedDir.filePath(QStringLiteral("ideal_standalone.extracted_parameters.json")), QStringLiteral("N")) == 2 && extractedPsLmaxValue(extractedDir.filePath(QStringLiteral("ideal_standalone.extracted_parameters.json")), QStringLiteral("Ga")) == 2, QStringLiteral("extracted_parameters.json records PS.lmax all expected labels as 2"), &result)) return result;

    DftSettings sevenSiestaNeutral = DftParameterRegistry::defaultSettings(DftCode::Siesta, QStringLiteral("4.1.5"), QStringLiteral("7layer_ideal"));
    sevenSiestaNeutral.generationMode = DftGenerationMode::Manual;
    sevenSiestaNeutral.includeXcFdf = true;
    sevenSiestaNeutral.xcFdfPath = QStringLiteral("xc.fdf");
    sevenSiestaNeutral.sourceStructurePath = sevenLayerPath;
    sevenSiestaNeutral.fixedAtomMode = DftFixedAtomMode::FixBottomPseudoHPlusBottomMolecularLayer;
    sevenSiestaNeutral.trailingFlagInterpretation = DftTrailingFlagInterpretation::PreserveOrIgnoreUnknown;
    sevenSiestaNeutral.hydrogenAssignments = DftInputGenerator::inferHydrogenRoles(sevenLayer, sevenSiestaNeutral);
    DftGeneratedInput sevenSiestaNeutralOut = DftInputGenerator::generate(sevenLayer, sevenSiestaNeutral);
    if (!check(&ctx, sevenSiestaNeutralOut.ok, QStringLiteral("7layer SIESTA neutral generates"), &result)) return result;
    if (!check(&ctx, DftInputGenerator::hasNoExplanatoryComments(sevenSiestaNeutralOut.primaryText, DftCode::Siesta), QStringLiteral("7layer SIESTA neutral FDF has no explanatory comments"), &result)) return result;
    if (!check(&ctx, integerValueForKey(sevenSiestaNeutralOut.primaryText, QStringLiteral("NumberOfAtoms")) == 61, QStringLiteral("7layer SIESTA neutral NumberOfAtoms is 61"), &result)) return result;
    if (!check(&ctx, sevenLayerHydrogenRolesAreBottom075(sevenSiestaNeutral.hydrogenAssignments), QStringLiteral("7layer SIESTA neutral bottom H roles are H-0.750 species index 1 and fixed"), &result)) return result;
    QVector<int> sevenExpectedSiestaSpecies;
    for (const auto& atom : sevenLayer.atoms) {
        if (atom.element.compare(QStringLiteral("Ga"), Qt::CaseInsensitive) == 0) sevenExpectedSiestaSpecies << 3;
        else if (atom.element.compare(QStringLiteral("N"), Qt::CaseInsensitive) == 0) sevenExpectedSiestaSpecies << 2;
        else if (atom.element.compare(QStringLiteral("H"), Qt::CaseInsensitive) == 0) sevenExpectedSiestaSpecies << 1;
        else sevenExpectedSiestaSpecies << 0;
    }
    if (!check(&ctx, siestaAtomicSpeciesSequence(sevenSiestaNeutralOut.primaryText) == sevenExpectedSiestaSpecies, QStringLiteral("7layer SIESTA atom_order preserved with H=1 N=2 Ga=3 species indices"), &result)) return result;
    const QVector<int> sevenSiestaConstraints = siestaConstraintAtoms(sevenSiestaNeutralOut.primaryText);
    if (!check(&ctx, sortedCopy(sevenSiestaConstraints) == expectedSevenFixedAtoms() && summaryFixedIndices(sevenSiestaNeutralOut.summaryObject) == expectedSevenFixedAtoms() && summaryFixedReason(sevenSiestaNeutralOut.summaryObject, 58) == QStringLiteral("bottom_pseudo_h") && summaryFixedReason(sevenSiestaNeutralOut.summaryObject, 42).startsWith(QStringLiteral("bottom_molecular_layer")), QStringLiteral("7layer SIESTA Geometry.Constraints fixes Ga 1/5/9/13, N 42/46/50/54, H 58-61 with reasons"), &result)) return result;
    if (!check(&ctx, sevenSiestaNeutralOut.primaryText.contains(QStringLiteral("%include xc.fdf")) && !sevenSiestaNeutralOut.primaryText.contains(QStringLiteral("%block ChemicalSpeciesLabel")) && !sevenSiestaNeutralOut.primaryText.contains(QStringLiteral("%block PAO.Basis")) && !sevenSiestaNeutralOut.primaryText.contains(QStringLiteral("%block SyntheticAtoms")), QStringLiteral("7layer SIESTA neutral include mode has no duplicate species/basis/synthetic blocks"), &result)) return result;
    if (!check(&ctx, summaryAtomOrderMatches(sevenLayer, sevenSiestaNeutralOut.summaryObject), QStringLiteral("7layer SIESTA summary atom_order preserves all 61 atoms"), &result)) return result;
    if (!check(&ctx, sevenSiestaNeutralOut.summaryObject.value(QStringLiteral("fixed_atom_mode")).toString() == QStringLiteral("bottom_pseudo_h_plus_bottom_molecular_layer") && sevenSiestaNeutralOut.summaryObject.value(QStringLiteral("trailing_flag_interpretation")).toString() == QStringLiteral("preserve_or_ignore_unknown") && sevenSiestaNeutralOut.summaryObject.value(QStringLiteral("fixed_atom_count")).toInt() == 12, QStringLiteral("7layer SIESTA summary records fixed mode, trailing flag interpretation, and fixed count"), &result)) return result;
    if (!check(&ctx, sevenSiestaNeutralOut.summaryObject.value(QStringLiteral("precision")).toObject().value(QStringLiteral("coordinate_precision")).toInt() == 10 && sevenSiestaNeutralOut.summaryObject.value(QStringLiteral("precision")).toObject().value(QStringLiteral("cell_precision")).toInt() == 10 && decimalPlacesForFirstNumberInBlock(sevenSiestaNeutralOut.primaryText, QStringLiteral("%block AtomicCoordinatesAndAtomicSpecies"), QStringLiteral("%endblock AtomicCoordinatesAndAtomicSpecies"), 0) == 10 && decimalPlacesForFirstNumberInBlock(sevenSiestaNeutralOut.primaryText, QStringLiteral("%block LatticeVectors"), QStringLiteral("%endblock LatticeVectors"), 0) == 10, QStringLiteral("7layer SIESTA default coordinate/cell precision is 10 digits and reflected in FDF"), &result)) return result;
    DftSettings sevenSiestaPrecision = sevenSiestaNeutral;
    DftParameterRegistry::setParameterValue(&sevenSiestaPrecision, QStringLiteral("siesta.output.coordinate_precision"), QStringLiteral("12"), DftParameterSource::UserOverride);
    DftParameterRegistry::setParameterValue(&sevenSiestaPrecision, QStringLiteral("siesta.output.cell_precision"), QStringLiteral("12"), DftParameterSource::UserOverride);
    sevenSiestaPrecision.hydrogenAssignments = DftInputGenerator::inferHydrogenRoles(sevenLayer, sevenSiestaPrecision);
    const DftGeneratedInput sevenSiestaPrecisionOut = DftInputGenerator::generate(sevenLayer, sevenSiestaPrecision);
    if (!check(&ctx, sevenSiestaPrecisionOut.ok && decimalPlacesForFirstNumberInBlock(sevenSiestaPrecisionOut.primaryText, QStringLiteral("%block AtomicCoordinatesAndAtomicSpecies"), QStringLiteral("%endblock AtomicCoordinatesAndAtomicSpecies"), 0) == 12 && decimalPlacesForFirstNumberInBlock(sevenSiestaPrecisionOut.primaryText, QStringLiteral("%block LatticeVectors"), QStringLiteral("%endblock LatticeVectors"), 0) == 12, QStringLiteral("7layer SIESTA coordinate_precision/cell_precision override is reflected"), &result)) return result;
    if (!check(&ctx, writeSample(auditOutput, QStringLiteral("A_7layer_siesta_neutral"), sevenLayer, sevenSiestaNeutral, sevenSiestaNeutralOut, &writeError), QStringLiteral("write sample A 7layer SIESTA neutral"), &result)) return result;

    DftSettings sevenSiestaCharged = sevenSiestaNeutral;
    setTargetName(&sevenSiestaCharged, QStringLiteral("7layer_ideal_n025"));
    DftParameterRegistry::applyCalculationModeDefaults(&sevenSiestaCharged, QStringLiteral("charged_slab_electron_added"));
    sevenSiestaCharged.hydrogenAssignments = DftInputGenerator::inferHydrogenRoles(sevenLayer, sevenSiestaCharged);
    DftGeneratedInput sevenSiestaChargedOut = DftInputGenerator::generate(sevenLayer, sevenSiestaCharged);
    if (!check(&ctx, sevenSiestaChargedOut.ok && integerValueForKey(sevenSiestaChargedOut.primaryText, QStringLiteral("NumberOfAtoms")) == 61, QStringLiteral("7layer SIESTA charged NumberOfAtoms is 61"), &result)) return result;
    if (!check(&ctx, sevenLayerHydrogenRolesAreBottom075(sevenSiestaCharged.hydrogenAssignments) && siestaConstraintAtoms(sevenSiestaChargedOut.primaryText) == sevenSiestaConstraints, QStringLiteral("7layer SIESTA charged H roles and fixed atoms match neutral"), &result)) return result;
    if (!check(&ctx, sevenSiestaChargedOut.primaryText.contains(QStringLiteral("NetCharge -0.25")) && sevenSiestaChargedOut.primaryText.contains(QStringLiteral("Spin polarized")) && sevenSiestaChargedOut.primaryText.contains(QStringLiteral("Spin.Fix F")) && !sevenSiestaChargedOut.primaryText.contains(QStringLiteral("Spin.Total")), QStringLiteral("7layer SIESTA charged emits NetCharge/Spin and omits Spin.Total"), &result)) return result;
    if (!check(&ctx, DftInputGenerator::hasNoExplanatoryComments(sevenSiestaChargedOut.primaryText, DftCode::Siesta), QStringLiteral("7layer SIESTA charged FDF has no explanatory comments"), &result)) return result;
    if (!check(&ctx, writeSample(auditOutput, QStringLiteral("B_7layer_siesta_charged_n025"), sevenLayer, sevenSiestaCharged, sevenSiestaChargedOut, &writeError), QStringLiteral("write sample B 7layer SIESTA charged"), &result)) return result;

    DftSettings sevenSiestaStandalone = importedXc.settings;
    setTargetName(&sevenSiestaStandalone, QStringLiteral("7layer_ideal_standalone"));
    sevenSiestaStandalone.generationMode = DftGenerationMode::ImportEdit;
    sevenSiestaStandalone.includeXcFdf = false;
    sevenSiestaStandalone.standaloneInline = true;
    sevenSiestaStandalone.sourceStructurePath = sevenLayerPath;
    sevenSiestaStandalone.fixedAtomMode = DftFixedAtomMode::FixBottomPseudoHPlusBottomMolecularLayer;
    sevenSiestaStandalone.trailingFlagInterpretation = DftTrailingFlagInterpretation::PreserveOrIgnoreUnknown;
    sevenSiestaStandalone.hydrogenAssignments = DftInputGenerator::inferHydrogenRoles(sevenLayer, sevenSiestaStandalone);
    DftGeneratedInput sevenSiestaStandaloneOut = DftInputGenerator::generate(sevenLayer, sevenSiestaStandalone);
    if (!check(&ctx, sevenSiestaStandaloneOut.ok && integerValueForKey(sevenSiestaStandaloneOut.primaryText, QStringLiteral("NumberOfAtoms")) == 61 && psLmaxValue(sevenSiestaStandaloneOut.primaryText, QStringLiteral("H-0.750")) == 2 && psLmaxValue(sevenSiestaStandaloneOut.primaryText, QStringLiteral("H-1.250")) == 2 && psLmaxValue(sevenSiestaStandaloneOut.primaryText, QStringLiteral("H")) == 2 && psLmaxValue(sevenSiestaStandaloneOut.primaryText, QStringLiteral("N")) == 2 && psLmaxValue(sevenSiestaStandaloneOut.primaryText, QStringLiteral("Ga")) == 2, QStringLiteral("7layer SIESTA standalone PS.lmax values are all 2"), &result)) return result;
    if (!check(&ctx, writeSample(auditOutput, QStringLiteral("C_7layer_siesta_standalone"), sevenLayer, sevenSiestaStandalone, sevenSiestaStandaloneOut, &writeError), QStringLiteral("write sample C 7layer SIESTA standalone"), &result)) return result;

    DftSettings hRoleOverride = sevenSiestaNeutral;
    setTargetName(&hRoleOverride, QStringLiteral("hrole_override_test"));
    for (auto& h : hRoleOverride.hydrogenAssignments) {
        if (h.atomIndex == 57) {
            h.selectedRole = DftHydrogenRole::OrdinaryHydrogen;
            h.userOverrodeInference = true;
        }
    }
    const DftGeneratedInput hRoleOverrideOut = DftInputGenerator::generate(sevenLayer, hRoleOverride);
    const QVector<int> overrideSpecies = siestaAtomicSpeciesSequence(hRoleOverrideOut.primaryText);
    if (!check(&ctx, hRoleOverrideOut.ok && overrideSpecies.size() == 61 && overrideSpecies.at(57) == 4 && !summaryFixedIndices(hRoleOverrideOut.summaryObject).contains(58) && containsWarning(hRoleOverrideOut.warnings, QStringLiteral("HydrogenRole manual override")) && summaryHydrogenOverride(hRoleOverrideOut.summaryObject, 58, true), QStringLiteral("7layer H role manual override makes atom 58 ordinary H species 4 and removes pseudo-H fixed status"), &result)) return result;
    DftSettings hRoleRevert = hRoleOverride;
    for (auto& h : hRoleRevert.hydrogenAssignments) {
        if (h.atomIndex == 57) {
            h.selectedRole = DftHydrogenRole::BottomPseudoHNTerminated075;
            h.userOverrodeInference = false;
        }
    }
    const DftGeneratedInput hRoleRevertOut = DftInputGenerator::generate(sevenLayer, hRoleRevert);
    const QVector<int> revertSpecies = siestaAtomicSpeciesSequence(hRoleRevertOut.primaryText);
    if (!check(&ctx, hRoleRevertOut.ok && revertSpecies.size() == 61 && revertSpecies.at(57) == 1 && summaryFixedIndices(hRoleRevertOut.summaryObject).contains(58), QStringLiteral("7layer H role revert restores atom 58 to H-0.750 species 1 and fixed"), &result)) return result;
    if (!check(&ctx, writeSample(auditOutput, QStringLiteral("H_hrole_manual_override"), sevenLayer, hRoleOverride, hRoleOverrideOut, &writeError), QStringLiteral("write sample H H-role manual override"), &result)) return result;

    DftSettings sevenPseudoOnly = sevenSiestaNeutral;
    sevenPseudoOnly.fixedAtomMode = DftFixedAtomMode::FixBottomPseudoHOnly;
    sevenPseudoOnly.hydrogenAssignments = DftInputGenerator::inferHydrogenRoles(sevenLayer, sevenPseudoOnly);
    const DftGeneratedInput sevenPseudoOnlyOut = DftInputGenerator::generate(sevenLayer, sevenPseudoOnly);
    if (!check(&ctx, summaryFixedIndices(sevenPseudoOnlyOut.summaryObject) == bottomHydrogenAtoms(), QStringLiteral("Fixed atom mode bottom_pseudo_h_only fixes only atoms 58-61"), &result)) return result;
    StructureData manualStructure = sevenLayer;
    manualStructure.atoms.at(6).movable = {false, false, false};
    DftSettings manualOnly = sevenSiestaNeutral;
    manualOnly.fixedAtomMode = DftFixedAtomMode::ManualOnly;
    manualOnly.hydrogenAssignments = DftInputGenerator::inferHydrogenRoles(manualStructure, manualOnly);
    const DftGeneratedInput manualOnlyOut = DftInputGenerator::generate(manualStructure, manualOnly);
    if (!check(&ctx, summaryFixedIndices(manualOnlyOut.summaryObject) == QVector<int>({7}) && summaryFixedReason(manualOnlyOut.summaryObject, 7) == QStringLiteral("manual_user_fixed"), QStringLiteral("Fixed atom mode manual_only fixes only user-marked atom with reason"), &result)) return result;
    StructureData importedFlagStructure = sevenLayer;
    importedFlagStructure.atoms.at(1).movable = {false, false, false};
    DftSettings preserveFlags = sevenSiestaNeutral;
    preserveFlags.fixedAtomMode = DftFixedAtomMode::PreserveImportedFlags;
    preserveFlags.hydrogenAssignments = DftInputGenerator::inferHydrogenRoles(importedFlagStructure, preserveFlags);
    const DftGeneratedInput preserveFlagsOut = DftInputGenerator::generate(importedFlagStructure, preserveFlags);
    if (!check(&ctx, summaryFixedIndices(preserveFlagsOut.summaryObject) == QVector<int>({2}) && summaryFixedReason(preserveFlagsOut.summaryObject, 2) == QStringLiteral("imported_flag"), QStringLiteral("Fixed atom mode preserve_imported_flags fixes only imported/manual NativeAtom flags"), &result)) return result;

    if (!check(&ctx, almostEqual(DftParameterRegistry::defaultAtomicMass(QStringLiteral("H")), 1.00784) && almostEqual(DftParameterRegistry::defaultAtomicMass(QStringLiteral("N")), 14.0067) && almostEqual(DftParameterRegistry::defaultAtomicMass(QStringLiteral("Al")), 26.9815385) && almostEqual(DftParameterRegistry::defaultAtomicMass(QStringLiteral("Ga")), 69.723) && almostEqual(DftParameterRegistry::defaultAtomicMass(QStringLiteral("In")), 114.818), QStringLiteral("QE project default atomic mass table covers H/N/Al/Ga/In"), &result)) return result;

    DftSettings qeNeutral = DftParameterRegistry::defaultSettings(DftCode::QuantumEspresso, QStringLiteral("7.3.1"), QStringLiteral("ideal_qe"));
    qeNeutral.generationMode = DftGenerationMode::Manual;
    qeNeutral.executable = QStringLiteral("pw.x");
    qeNeutral.qeProjectStyleFixedFlags = true;
    qeNeutral.fixedAtomMode = DftFixedAtomMode::FixBottomPseudoHPlusBottomMolecularLayer;
    qeNeutral.hydrogenAssignments = DftInputGenerator::inferHydrogenRoles(slab, qeNeutral);
    DftGeneratedInput qeNeutralOut = DftInputGenerator::generate(slab, qeNeutral);
    if (!check(&ctx, qeNeutralOut.ok, QStringLiteral("QE neutral slab generates"), &result)) return result;
    if (!check(&ctx, DftInputGenerator::hasNoExplanatoryComments(qeNeutralOut.primaryText, DftCode::QuantumEspresso), QStringLiteral("QE neutral has no explanatory comments"), &result)) return result;
    if (!check(&ctx, integerValueForKey(qeNeutralOut.primaryText, QStringLiteral("nat")) == static_cast<int>(slab.atoms.size()) && integerValueForKey(qeNeutralOut.primaryText, QStringLiteral("ntyp")) == qeAtomicSpeciesCount(qeNeutralOut.primaryText), QStringLiteral("QE neutral nat/ntyp match structure/species"), &result)) return result;
    if (!check(&ctx, integerValueForKey(qeNeutralOut.primaryText, QStringLiteral("ntyp")) == 4 &&
        qeNeutralOut.primaryText.contains(QStringLiteral("H.pbe-MT.075.UPF")) &&
        qeNeutralOut.primaryText.contains(QStringLiteral("H2")) &&
        qeNeutralOut.primaryText.contains(QStringLiteral("H.pbe-mt_fhi.UPF")) &&
        almostEqual(qeAtomicSpeciesMass(qeNeutralOut.primaryText, QStringLiteral("Ga")), 69.723) &&
        almostEqual(qeAtomicSpeciesMass(qeNeutralOut.primaryText, QStringLiteral("N")), 14.0067) &&
        almostEqual(qeAtomicSpeciesMass(qeNeutralOut.primaryText, QStringLiteral("H")), 1.00784) &&
        almostEqual(qeAtomicSpeciesMass(qeNeutralOut.primaryText, QStringLiteral("H2")), 1.00784),
        QStringLiteral("QE neutral separates bottom pseudo-H/ordinary H species and uses project default real masses"), &result)) return result;
    if (!check(&ctx, qePositionFlags(qeNeutralOut.primaryText, 0) == QStringLiteral("0 0 0") && qePositionFlags(qeNeutralOut.primaryText, 9).isEmpty(), QStringLiteral("QE neutral fixed atoms get 0 0 0 and free atoms omit flags in project style"), &result)) return result;

    DftSettings qeCharged = qeNeutral;
    qeCharged.targetName = QStringLiteral("ideal_qe_n025");
    qeCharged.qeAssumeIsolated = true;
    DftParameterRegistry::applyCalculationModeDefaults(&qeCharged, QStringLiteral("charged_slab_electron_added"));
    DftParameterRegistry::setParameterValue(&qeCharged, QStringLiteral("qe.SYSTEM.assume_isolated"), QStringLiteral("2D"), DftParameterSource::UserOverride);
    DftParameterRegistry::setParameterValue(&qeCharged, QStringLiteral("qe.SYSTEM.starting_magnetization(1)"), QStringLiteral("0.1"), DftParameterSource::UserOverride);
    qeCharged.hydrogenAssignments = DftInputGenerator::inferHydrogenRoles(slab, qeCharged);
    DftGeneratedInput qeChargedOut = DftInputGenerator::generate(slab, qeCharged);
    if (!check(&ctx, qeChargedOut.ok, QStringLiteral("QE charged slab generates"), &result)) return result;
    if (!check(&ctx, qeChargedOut.primaryText.contains(QStringLiteral("tot_charge = -0.25")) && qeChargedOut.primaryText.contains(QStringLiteral("nspin = 2")) && qeChargedOut.primaryText.contains(QStringLiteral("starting_magnetization(1) = 0.1")) && qeChargedOut.primaryText.contains(QStringLiteral("assume_isolated = '2D'")) && qeChargedOut.primaryText.contains(QStringLiteral("degauss = 0.03")), QStringLiteral("QE charged slab charge/spin/isolated/degauss fields correct"), &result)) return result;
    DftSettings qeChargedNoIso = qeNeutral;
    qeChargedNoIso.targetName = QStringLiteral("ideal_qe_n025_noiso");
    qeChargedNoIso.qeAssumeIsolated = false;
    DftParameterRegistry::applyCalculationModeDefaults(&qeChargedNoIso, QStringLiteral("charged_slab_electron_added"));
    qeChargedNoIso.hydrogenAssignments = DftInputGenerator::inferHydrogenRoles(slab, qeChargedNoIso);
    DftGeneratedInput qeChargedNoIsoOut = DftInputGenerator::generate(slab, qeChargedNoIso);
    if (!check(&ctx, qeChargedNoIsoOut.ok && !qeChargedNoIsoOut.primaryText.contains(QStringLiteral("assume_isolated")), QStringLiteral("QE charged assume_isolated follows user toggle off"), &result)) return result;

    DftSettings sevenQeNeutral = DftParameterRegistry::defaultSettings(DftCode::QuantumEspresso, QStringLiteral("7.3.1"), QStringLiteral("7layer_ideal_qe"));
    sevenQeNeutral.generationMode = DftGenerationMode::Manual;
    sevenQeNeutral.executable = QStringLiteral("pw.x");
    sevenQeNeutral.qeProjectStyleFixedFlags = true;
    sevenQeNeutral.sourceStructurePath = sevenLayerPath;
    sevenQeNeutral.fixedAtomMode = DftFixedAtomMode::FixBottomPseudoHPlusBottomMolecularLayer;
    sevenQeNeutral.trailingFlagInterpretation = DftTrailingFlagInterpretation::PreserveOrIgnoreUnknown;
    DftParameterRegistry::setParameterValue(&sevenQeNeutral, QStringLiteral("qe.CONTROL.calculation"), QStringLiteral("relax"), DftParameterSource::UserOverride);
    DftParameterRegistry::setParameterValue(&sevenQeNeutral, QStringLiteral("qe.K_POINTS.automatic"), QStringLiteral("3 3 1 0 0 0"), DftParameterSource::UserOverride);
    sevenQeNeutral.hydrogenAssignments = DftInputGenerator::inferHydrogenRoles(sevenLayer, sevenQeNeutral);
    DftGeneratedInput sevenQeNeutralOut = DftInputGenerator::generate(sevenLayer, sevenQeNeutral);
    const QStringList sevenQeNeutralLabels = qeAtomicLabelSequence(sevenQeNeutralOut.primaryText);
    bool sevenQeOrderPreserved = sevenQeNeutralLabels.size() == static_cast<int>(sevenLayer.atoms.size());
    for (int i = 0; sevenQeOrderPreserved && i < sevenQeNeutralLabels.size(); ++i) {
        sevenQeOrderPreserved = sevenQeNeutralLabels.at(i).compare(sevenLayer.atoms.at(static_cast<std::size_t>(i)).element, Qt::CaseInsensitive) == 0;
    }
    if (!check(&ctx, sevenQeNeutralOut.ok, QStringLiteral("7layer QE neutral generates"), &result)) return result;
    if (!check(&ctx, DftInputGenerator::hasNoExplanatoryComments(sevenQeNeutralOut.primaryText, DftCode::QuantumEspresso), QStringLiteral("7layer QE neutral input has no explanatory comments"), &result)) return result;
    if (!check(&ctx, integerValueForKey(sevenQeNeutralOut.primaryText, QStringLiteral("nat")) == 61 && integerValueForKey(sevenQeNeutralOut.primaryText, QStringLiteral("ntyp")) == qeAtomicSpeciesCount(sevenQeNeutralOut.primaryText), QStringLiteral("7layer QE neutral nat is 61 and ntyp matches ATOMIC_SPECIES"), &result)) return result;
    if (!check(&ctx, integerValueForKey(sevenQeNeutralOut.primaryText, QStringLiteral("ntyp")) == 3 && qeAtomicSpeciesLabels(sevenQeNeutralOut.primaryText) == QSet<QString>({QStringLiteral("Ga"), QStringLiteral("N"), QStringLiteral("H")}), QStringLiteral("7layer QE neutral ntyp=3 with Ga/N/bottom pseudo-H only"), &result)) return result;
    if (!check(&ctx, sevenQeNeutralOut.primaryText.contains(QStringLiteral("H.pbe-MT.075.UPF")) && sevenQeNeutralLabels.size() == 61 && sevenQeNeutralLabels.at(57) == QStringLiteral("H") && sevenQeNeutralLabels.at(58) == QStringLiteral("H") && sevenQeNeutralLabels.at(59) == QStringLiteral("H") && sevenQeNeutralLabels.at(60) == QStringLiteral("H"), QStringLiteral("7layer QE neutral bottom pseudo-H label and pseudo file are correct"), &result)) return result;
    if (!check(&ctx, almostEqual(qeAtomicSpeciesMass(sevenQeNeutralOut.primaryText, QStringLiteral("Ga")), 69.723) && almostEqual(qeAtomicSpeciesMass(sevenQeNeutralOut.primaryText, QStringLiteral("N")), 14.0067) && almostEqual(qeAtomicSpeciesMass(sevenQeNeutralOut.primaryText, QStringLiteral("H")), 1.00784), QStringLiteral("7layer QE neutral ATOMIC_SPECIES uses Ga/N/H real atomic masses"), &result)) return result;
    bool sevenQeFixedFlagsOk = true;
    for (int atomIndex : expectedSevenFixedAtoms()) {
        sevenQeFixedFlagsOk = sevenQeFixedFlagsOk && qePositionFlags(sevenQeNeutralOut.primaryText, atomIndex - 1) == QStringLiteral("0 0 0");
    }
    if (!check(&ctx, sevenQeFixedFlagsOk && qePositionFlags(sevenQeNeutralOut.primaryText, 1).isEmpty() && summaryFixedIndices(sevenQeNeutralOut.summaryObject) == expectedSevenFixedAtoms(), QStringLiteral("7layer QE neutral fixed atoms have 0 0 0 and free atoms omit flags"), &result)) return result;
    if (!check(&ctx, sevenQeOrderPreserved && summaryAtomOrderMatches(sevenLayer, sevenQeNeutralOut.summaryObject), QStringLiteral("7layer QE neutral atom_order preserves all 61 atoms"), &result)) return result;
    if (!check(&ctx, sevenQeNeutralOut.summaryObject.value(QStringLiteral("precision")).toObject().value(QStringLiteral("coordinate_precision")).toInt() == 10 && sevenQeNeutralOut.summaryObject.value(QStringLiteral("precision")).toObject().value(QStringLiteral("cell_precision")).toInt() == 10 && decimalPlacesForFirstNumberInBlock(sevenQeNeutralOut.primaryText, QStringLiteral("ATOMIC_POSITIONS"), QStringLiteral("K_POINTS"), 1) == 10 && decimalPlacesForFirstNumberInBlock(sevenQeNeutralOut.primaryText, QStringLiteral("CELL_PARAMETERS"), QStringLiteral("ATOMIC_SPECIES"), 0) == 10, QStringLiteral("7layer QE default coordinate/cell precision is 10 digits and reflected in input"), &result)) return result;
    DftSettings sevenQePrecision = sevenQeNeutral;
    DftParameterRegistry::setParameterValue(&sevenQePrecision, QStringLiteral("qe.output.coordinate_precision"), QStringLiteral("12"), DftParameterSource::UserOverride);
    DftParameterRegistry::setParameterValue(&sevenQePrecision, QStringLiteral("qe.output.cell_precision"), QStringLiteral("12"), DftParameterSource::UserOverride);
    sevenQePrecision.hydrogenAssignments = DftInputGenerator::inferHydrogenRoles(sevenLayer, sevenQePrecision);
    const DftGeneratedInput sevenQePrecisionOut = DftInputGenerator::generate(sevenLayer, sevenQePrecision);
    if (!check(&ctx, sevenQePrecisionOut.ok && decimalPlacesForFirstNumberInBlock(sevenQePrecisionOut.primaryText, QStringLiteral("ATOMIC_POSITIONS"), QStringLiteral("K_POINTS"), 1) == 12 && decimalPlacesForFirstNumberInBlock(sevenQePrecisionOut.primaryText, QStringLiteral("CELL_PARAMETERS"), QStringLiteral("ATOMIC_SPECIES"), 0) == 12, QStringLiteral("7layer QE coordinate_precision/cell_precision override is reflected"), &result)) return result;
    if (!check(&ctx, writeSample(auditOutput, QStringLiteral("D_7layer_qe_neutral"), sevenLayer, sevenQeNeutral, sevenQeNeutralOut, &writeError), QStringLiteral("write sample D 7layer QE neutral"), &result)) return result;

    DftSettings sevenQeCharged = sevenQeNeutral;
    sevenQeCharged.targetName = QStringLiteral("7layer_ideal_qe_n025");
    sevenQeCharged.qeAssumeIsolated = false;
    DftParameterRegistry::applyCalculationModeDefaults(&sevenQeCharged, QStringLiteral("charged_slab_electron_added"));
    sevenQeCharged.hydrogenAssignments = DftInputGenerator::inferHydrogenRoles(sevenLayer, sevenQeCharged);
    DftGeneratedInput sevenQeChargedOut = DftInputGenerator::generate(sevenLayer, sevenQeCharged);
    if (!check(&ctx, sevenQeChargedOut.ok && integerValueForKey(sevenQeChargedOut.primaryText, QStringLiteral("nat")) == 61, QStringLiteral("7layer QE charged nat is 61"), &result)) return result;
    if (!check(&ctx, sevenQeChargedOut.primaryText.contains(QStringLiteral("tot_charge = -0.25")) && sevenQeChargedOut.primaryText.contains(QStringLiteral("nspin = 2")) && !sevenQeChargedOut.primaryText.contains(QStringLiteral("assume_isolated")) && !sevenQeChargedOut.summaryObject.value(QStringLiteral("assume_isolated")).toObject().value(QStringLiteral("enabled")).toBool(true), QStringLiteral("7layer QE charged emits charge/spin and omits assume_isolated when toggle off"), &result)) return result;
    bool sevenQeChargedFlagsOk = true;
    for (int atomIndex : expectedSevenFixedAtoms()) {
        sevenQeChargedFlagsOk = sevenQeChargedFlagsOk && qePositionFlags(sevenQeChargedOut.primaryText, atomIndex - 1) == QStringLiteral("0 0 0");
    }
    if (!check(&ctx, sevenQeChargedFlagsOk && DftInputGenerator::hasNoExplanatoryComments(sevenQeChargedOut.primaryText, DftCode::QuantumEspresso), QStringLiteral("7layer QE charged fixed flags and comment policy are correct"), &result)) return result;
    if (!check(&ctx, writeSample(auditOutput, QStringLiteral("E_7layer_qe_charged_n025_no2d"), sevenLayer, sevenQeCharged, sevenQeChargedOut, &writeError), QStringLiteral("write sample E 7layer QE charged no2D"), &result)) return result;

    DftSettings sevenQeChargedIso = sevenQeCharged;
    sevenQeChargedIso.targetName = QStringLiteral("7layer_ideal_qe_n025_isolated");
    sevenQeChargedIso.qeAssumeIsolated = true;
    DftParameterRegistry::applyCalculationModeDefaults(&sevenQeChargedIso, QStringLiteral("charged_slab_electron_added"));
    sevenQeChargedIso.hydrogenAssignments = DftInputGenerator::inferHydrogenRoles(sevenLayer, sevenQeChargedIso);
    const DftGeneratedInput sevenQeChargedIsoOut = DftInputGenerator::generate(sevenLayer, sevenQeChargedIso);
    if (!check(&ctx, sevenQeChargedIsoOut.ok && sevenQeChargedIsoOut.primaryText.contains(QStringLiteral("assume_isolated = '2D'")) && sevenQeChargedIsoOut.summaryObject.value(QStringLiteral("assume_isolated")).toObject().value(QStringLiteral("enabled")).toBool(false), QStringLiteral("7layer QE charged emits assume_isolated only when toggle on"), &result)) return result;
    sevenQeChargedIso.targetName = QStringLiteral("7layer_ideal_qe_n025_2d");
    const DftGeneratedInput sevenQeChargedIsoSampleOut = DftInputGenerator::generate(sevenLayer, sevenQeChargedIso);
    if (!check(&ctx, writeSample(auditOutput, QStringLiteral("F_7layer_qe_charged_n025_2d"), sevenLayer, sevenQeChargedIso, sevenQeChargedIsoSampleOut, &writeError), QStringLiteral("write sample F 7layer QE charged 2D"), &result)) return result;

    const StructureData gaRef = gaAtomReferenceStructure();
    DftSettings qeGa = DftParameterRegistry::defaultSettings(DftCode::QuantumEspresso, QStringLiteral("7.3.1"), QStringLiteral("Ga_atom"));
    DftParameterRegistry::applyCalculationModeDefaults(&qeGa, QStringLiteral("ga_atom_reference"));
    DftGeneratedInput qeGaOut = DftInputGenerator::generate(gaRef, qeGa);
    if (!check(&ctx, qeGaOut.ok, QStringLiteral("QE Ga atom reference generates"), &result)) return result;
    if (!check(&ctx, qeGaOut.primaryText.contains(QStringLiteral("calculation = 'scf'")) && qeGaOut.primaryText.contains(QStringLiteral("nat = 1")) && qeGaOut.primaryText.contains(QStringLiteral("ntyp = 1")) && qeGaOut.primaryText.contains(QStringLiteral("occupations = 'fixed'")) && qeGaOut.primaryText.contains(QStringLiteral("nspin = 2")) && qeGaOut.primaryText.contains(QStringLiteral("starting_magnetization(1) = 1.0")) && qeGaOut.primaryText.contains(QStringLiteral("tot_magnetization = 1.0")) && qeGaOut.primaryText.contains(QStringLiteral("1 1 1 0 0 0")) && !qeGaOut.primaryText.contains(QStringLiteral("smearing =")) && !qeGaOut.primaryText.contains(QStringLiteral("degauss =")), QStringLiteral("QE Ga atom reference fields and 20A cubic structure correct"), &result)) return result;
    if (!check(&ctx, almostEqual(qeAtomicSpeciesMass(qeGaOut.primaryText, QStringLiteral("Ga")), 69.723), QStringLiteral("QE Ga atom generated ATOMIC_SPECIES uses Ga mass 69.723 by project default"), &result)) return result;
    if (!check(&ctx, writeSample(auditOutput, QStringLiteral("G_qe_ga_atom"), gaRef, qeGa, qeGaOut, &writeError), QStringLiteral("write sample G QE Ga atom"), &result)) return result;

    const StructureData h2 = h2Structure();
    DftSettings h2Settings = DftParameterRegistry::defaultSettings(DftCode::Siesta, QStringLiteral("4.1.5"), QStringLiteral("H2"));
    const auto h2Assignments = DftInputGenerator::inferHydrogenRoles(h2, h2Settings);
    if (!check(&ctx, h2Assignments.size() == 2 && h2Assignments.first().selectedRole == DftHydrogenRole::MoleculeH2Hydrogen && h2Assignments.first().siestaSpecies == QStringLiteral("H"), QStringLiteral("H2 template does not use H-0.750 pseudo-H"), &result)) return result;
    DftSettings h2QeSettings = DftParameterRegistry::defaultSettings(DftCode::QuantumEspresso, QStringLiteral("7.3.1"), QStringLiteral("H2_qe"));
    DftParameterRegistry::applyCalculationModeDefaults(&h2QeSettings, QStringLiteral("h2_reference"));
    h2QeSettings.hydrogenAssignments = DftInputGenerator::inferHydrogenRoles(h2, h2QeSettings);
    const DftGeneratedInput h2QeOut = DftInputGenerator::generate(h2, h2QeSettings);
    if (!check(&ctx, h2QeOut.ok && integerValueForKey(h2QeOut.primaryText, QStringLiteral("ntyp")) == 1 && almostEqual(qeAtomicSpeciesMass(h2QeOut.primaryText, QStringLiteral("H")), 1.00784) && h2QeOut.primaryText.contains(QStringLiteral("H.pbe-mt_fhi.UPF")) && !qeAtomicSpeciesLabels(h2QeOut.primaryText).contains(QStringLiteral("H2")), QStringLiteral("QE H2 molecule reference uses ordinary H pseudo/mass without H2 label confusion"), &result)) return result;

    const StructureData threeGaH = threeGaHStructure();
    DftSettings threeGaHSettings = DftParameterRegistry::defaultSettings(DftCode::Siesta, QStringLiteral("4.1.5"), QStringLiteral("3GaH"));
    const auto threeGaHAssignments = DftInputGenerator::inferHydrogenRoles(threeGaH, threeGaHSettings);
    if (!check(&ctx, !threeGaHAssignments.isEmpty() && threeGaHAssignments.first().selectedRole != DftHydrogenRole::BottomPseudoHNTerminated075 && threeGaHAssignments.first().selectedRole != DftHydrogenRole::BottomPseudoHIIITerminated125, QStringLiteral("3Ga-H surface H is not classified as bottom pseudo-H"), &result)) return result;

    const StructureData unknownH = unknownHydrogenStructure();
    DftSettings unknownSettings = DftParameterRegistry::defaultSettings(DftCode::Siesta, QStringLiteral("4.1.5"), QStringLiteral("unknown_H"));
    unknownSettings.hydrogenAssignments = DftInputGenerator::inferHydrogenRoles(unknownH, unknownSettings);
    DftGeneratedInput unknownOut = DftInputGenerator::generate(unknownH, unknownSettings);
    unknownSettings.allowUnknownHydrogen = true;
    DftGeneratedInput unknownAllowedOut = DftInputGenerator::generate(unknownH, unknownSettings);
    if (!check(&ctx, !unknownOut.ok && unknownAllowedOut.ok && containsWarning(unknownAllowedOut.warnings, QStringLiteral("unknown_hydrogen")), QStringLiteral("unknown_hydrogen blocks export unless explicitly allowed"), &result)) return result;

    StructureData partial = slab;
    partial.atoms[3].movable = {true, false, true};
    DftSettings partialSiesta = siestaNeutral;
    partialSiesta.fixedAtomMode = DftFixedAtomMode::PreserveImportedFlags;
    partialSiesta.hydrogenAssignments = DftInputGenerator::inferHydrogenRoles(partial, partialSiesta);
    DftGeneratedInput partialSiestaOut = DftInputGenerator::generate(partial, partialSiesta);
    DftSettings partialQe = qeNeutral;
    partialQe.fixedAtomMode = DftFixedAtomMode::PreserveImportedFlags;
    partialQe.qeProjectStyleFixedFlags = false;
    partialQe.hydrogenAssignments = DftInputGenerator::inferHydrogenRoles(partial, partialQe);
    DftGeneratedInput partialQeOut = DftInputGenerator::generate(partial, partialQe);
    if (!check(&ctx, containsWarning(partialSiestaOut.warnings, QStringLiteral("partial constraint")) && partialSiestaOut.primaryText.contains(QStringLiteral("atom 4")), QStringLiteral("SIESTA partial movable becomes atom constraint with warning"), &result)) return result;
    if (!check(&ctx, qePositionFlags(partialQeOut.primaryText, 3) == QStringLiteral("1 0 1") && qePositionFlags(partialQeOut.primaryText, 4) == QStringLiteral("1 1 1"), QStringLiteral("QE explicit style emits partial and free flags"), &result)) return result;

    const QString legacyQe = QStringLiteral("&SYSTEM\n  nat = 2, ntyp = 2, ecutwfc = 80,\n/\nATOMIC_SPECIES\nH 1.0 H.pbe-MT.075.UPF\nH2 1.0 H.pbe-mt_fhi.UPF\nATOMIC_POSITIONS angstrom\nH 0 0 0 0 0 0\nH2 0 0 1\nK_POINTS automatic\n3 3 1 0 0 0\n");
    const auto parsedLegacyQe = DftInputParser::parseQeInputText(legacyQe, qeNeutral);
    if (!check(&ctx, parsedLegacyQe.ok && parsedLegacyQe.settings.qeSpecies.size() == 2 && parsedLegacyQe.settings.qeSpecies.at(0).role == dftHydrogenRoleKey(DftHydrogenRole::BottomPseudoHNTerminated075) && parsedLegacyQe.settings.qeSpecies.at(1).role == dftHydrogenRoleKey(DftHydrogenRole::SurfaceAdsorbedHydrogen), QStringLiteral("QE legacy import maps H/H2 species to correct H roles"), &result)) return result;
    if (!check(&ctx, almostEqual(parsedLegacyQe.settings.qeSpecies.at(0).mass, 1.0) && parsedLegacyQe.settings.qeSpecies.at(0).source == DftParameterSource::ImportedQeIn && almostEqual(parsedLegacyQe.settings.qeSpecies.at(1).mass, 1.0) && parsedLegacyQe.settings.qeSpecies.at(1).source == DftParameterSource::ImportedQeIn, QStringLiteral("QE import preserves mass=1.0 values as source imported_qe_in"), &result)) return result;
    const QString importedGaMassQe = QStringLiteral("&SYSTEM\n  nat = 1, ntyp = 1,\n/\nATOMIC_SPECIES\nGa 1.0 Ga.pbe-mt_fhi.UPF\nATOMIC_POSITIONS angstrom\nGa 0 0 0\nK_POINTS automatic\n1 1 1 0 0 0\n");
    const auto parsedGaMassQe = DftInputParser::parseQeInputText(importedGaMassQe, qeNeutral);
    if (!check(&ctx, parsedGaMassQe.ok && parsedGaMassQe.settings.qeSpecies.size() == 1 && parsedGaMassQe.settings.qeSpecies.first().label == QStringLiteral("Ga") && almostEqual(parsedGaMassQe.settings.qeSpecies.first().mass, 1.0) && parsedGaMassQe.settings.qeSpecies.first().source == DftParameterSource::ImportedQeIn, QStringLiteral("QE imported Ga ATOMIC_SPECIES mass=1.0 is preserved as imported_qe_in"), &result)) return result;
    DftSettings resetImportedQe = parsedGaMassQe.settings;
    DftParameterRegistry::resetQeSpeciesToProjectDefaults(&resetImportedQe);
    if (!check(&ctx, almostEqual(qeSpeciesMass(resetImportedQe.qeSpecies, QStringLiteral("Ga")), 69.723) && almostEqual(qeSpeciesMass(resetImportedQe.qeSpecies, QStringLiteral("N")), 14.0067) && almostEqual(qeSpeciesMass(resetImportedQe.qeSpecies, QStringLiteral("H")), 1.00784) && resetImportedQe.qeSpecies.first().source == DftParameterSource::ProjectProfile, QStringLiteral("QE reset to project default changes imported mass to real atomic masses"), &result)) return result;

    const auto parsedQe = DftInputParser::parseQeInputText(QStringLiteral("&SYSTEM\n  ecutwfc = 80, ! inline comment\n  nspin = 2,\n/\nK_POINTS automatic\n3 3 1 0 0 0\n"), qeNeutral);
    if (!check(&ctx, parsedQe.ok && DftParameterRegistry::parameterValue(parsedQe.settings, QStringLiteral("qe.K_POINTS.automatic")) == QStringLiteral("3 3 1 0 0 0") && parsedQe.settings.parameters.value(QStringLiteral("qe.SYSTEM.nspin")).source == DftParameterSource::ImportedQeIn, QStringLiteral("QE .in import strips comments and records imported_qe_in source"), &result)) return result;

    const auto parsedFdf = DftInputParser::parseSiestaFdfText(QStringLiteral("SystemName test\n%block kgrid.Monkhorst_Pack\n  3 0 0 0\n  0 3 0 0\n  0 0 1 0\n%endblock kgrid.Monkhorst_Pack\n"), siestaNeutral, DftParameterSource::ImportedFile);
    if (!check(&ctx, parsedFdf.ok && DftParameterRegistry::parameterValue(parsedFdf.settings, QStringLiteral("siesta.kpoints.kgrid")) == QStringLiteral("3 3 1"), QStringLiteral("xc.fdf/SIESTA FDF import parses kgrid block"), &result)) return result;
    const auto parsedLog = DftInputParser::parseSiestaLogText(QStringLiteral("NetCharge -0.25\nSpin polarized\n"), siestaNeutral);
    if (!check(&ctx, parsedLog.ok && parsedLog.settings.parameters.value(QStringLiteral("siesta.charge_spin.NetCharge")).source == DftParameterSource::ImportedFdfLog, QStringLiteral("fdf-log import records imported_fdf_log source"), &result)) return result;
    const auto parsedJson = DftInputParser::parseProfileJsonText(QStringLiteral("{\"code\":\"qe\",\"fixed_atom_mode\":\"bottom_pseudo_h_only\",\"trailing_flag_interpretation\":\"numeric_one_means_fixed\",\"parameters\":{\"qe.SYSTEM.ecutwfc\":\"70\",\"qe.output.coordinate_precision\":\"16\"}}"), qeNeutral);
    if (!check(&ctx, parsedJson.ok && DftParameterRegistry::parameterValue(parsedJson.settings, QStringLiteral("qe.SYSTEM.ecutwfc")) == QStringLiteral("70") && DftParameterRegistry::parameterValue(parsedJson.settings, QStringLiteral("qe.output.coordinate_precision")) == QStringLiteral("16") && parsedJson.settings.fixedAtomMode == DftFixedAtomMode::FixBottomPseudoHOnly && parsedJson.settings.trailingFlagInterpretation == DftTrailingFlagInterpretation::NumericOneMeansFixed, QStringLiteral("JSON profile import works for parameters/fixed mode/trailing flag interpretation"), &result)) return result;

    DftSettings profileSiesta = DftParameterRegistry::defaultSettings(DftCode::Siesta, QStringLiteral("4.1.5"), QStringLiteral("ideal_profile"));
    QStringList profileMessages;
    const bool profileLoaded = DftParameterRegistry::applyBuiltInProfile(QStringLiteral("Kangawa_GaN_surface"), &profileSiesta, &profileMessages);
    profileSiesta.hydrogenAssignments = DftInputGenerator::inferHydrogenRoles(slab, profileSiesta);
    DftGeneratedInput profileOut = DftInputGenerator::generate(slab, profileSiesta);
    if (!check(&ctx, profileLoaded && profileOut.ok && DftParameterRegistry::builtInProfiles(DftCode::Siesta, QStringLiteral("4.1.5")).contains(QStringLiteral("Kangawa_GaN_surface")) && QFileInfo::exists(sourceFile(QStringLiteral("assets/dft_profiles/siesta/Kangawa_GaN_surface.profile.json"))), QStringLiteral("Profile mode loads built-in Kangawa profile and asset exists"), &result)) return result;

    DftSettings rawSiesta = siestaNeutral;
    setTargetName(&rawSiesta, QStringLiteral("raw_siesta"));
    rawSiesta.rawParameters << siestaRawScalar(QStringLiteral("PS.lmax"), QStringLiteral("H 1"));
    rawSiesta.rawParameters << siestaRawScalar(QStringLiteral("MeshCutoff"), QStringLiteral("999 Ry"));
    DftGeneratedInput rawSiestaOut = DftInputGenerator::generate(slab, rawSiesta);
    DftSettings rawQe = qeNeutral;
    rawQe.targetName = QStringLiteral("raw_qe");
    rawQe.rawParameters << qeRawNamelist(QStringLiteral("SYSTEM"), QStringLiteral("input_dft"), QStringLiteral("PBE"));
    rawQe.rawParameters << qeRawNamelist(QStringLiteral("SYSTEM"), QStringLiteral("ecutwfc"), QStringLiteral("90"));
    DftGeneratedInput rawQeOut = DftInputGenerator::generate(slab, rawQe);
    if (!check(&ctx, rawSiestaOut.ok && rawSiestaOut.primaryText.contains(QStringLiteral("PS.lmax H 1")) && containsWarning(rawSiestaOut.warnings, QStringLiteral("duplicates generated SIESTA key")), QStringLiteral("SIESTA Raw Additional scalar outputs and duplicate warning works"), &result)) return result;
    if (!check(&ctx, rawQeOut.ok && rawQeOut.primaryText.contains(QStringLiteral("input_dft = 'PBE'")) && containsWarning(rawQeOut.warnings, QStringLiteral("duplicates generated QE key")), QStringLiteral("QE Raw Additional namelist key outputs and duplicate warning works"), &result)) return result;

    const auto siestaSpecs = DftParameterRegistry::specsForCode(DftCode::Siesta, QStringLiteral("4.1.5"));
    const auto qeSpecs = DftParameterRegistry::specsForCode(DftCode::QuantumEspresso, QStringLiteral("7.3.1"));
    auto hasTooltip = [](const QVector<DftParameterSpec>& specs, const QString& key) {
        for (const auto& spec : specs) {
            if (spec.key == key && !spec.tooltipLong.trimmed().isEmpty()) return true;
        }
        return false;
    };
    if (!check(&ctx, hasTooltip(siestaSpecs, QStringLiteral("NetCharge")) && hasTooltip(siestaSpecs, QStringLiteral("Spin")) && hasTooltip(siestaSpecs, QStringLiteral("coordinate_precision")) && hasTooltip(qeSpecs, QStringLiteral("assume_isolated")) && hasTooltip(qeSpecs, QStringLiteral("ecutwfc")) && hasTooltip(qeSpecs, QStringLiteral("coordinate_precision")), QStringLiteral("Tooltips exist for key SIESTA/QE parameters"), &result)) return result;

    DftInputGeneratorDialog dialog(sevenLayer);
    auto* fixedModeCombo = dialog.findChild<QComboBox*>(QStringLiteral("fixedAtomModeCombo"));
    auto* trailingCombo = dialog.findChild<QComboBox*>(QStringLiteral("trailingFlagInterpretationCombo"));
    auto* hTable = dialog.findChild<QTableWidget*>(QStringLiteral("hydrogenRoleTable"));
    auto* pTable = dialog.findChild<QTableWidget*>(QStringLiteral("parameterTable"));
    auto* speciesTable = dialog.findChild<QTableWidget*>(QStringLiteral("speciesTable"));
    auto* assumeBox = dialog.findChild<QCheckBox*>(QStringLiteral("assumeIsolatedCheck"));
    auto* codeCombo = dialog.findChild<QComboBox*>(QStringLiteral("codeCombo"));
    auto* resetSpeciesButton = dialog.findChild<QAbstractButton*>(QStringLiteral("resetQeSpeciesProjectDefaultButton"));
    if (!check(&ctx, fixedModeCombo != nullptr && trailingCombo != nullptr && hTable != nullptr && pTable != nullptr && speciesTable != nullptr && assumeBox != nullptr && codeCombo != nullptr && resetSpeciesButton != nullptr, QStringLiteral("DFT dialog exposes required fixed/trailing/H-role/species/parameter widgets"), &result)) return result;
    if (!check(&ctx, !fixedModeCombo->toolTip().trimmed().isEmpty() && !trailingCombo->toolTip().trimmed().isEmpty() && !assumeBox->toolTip().trimmed().isEmpty() && hTable->rowCount() == 4, QStringLiteral("DFT dialog tooltips and 7layer H role rows are visible in widget model"), &result)) return result;
    auto* h58Combo = dialog.findChild<QComboBox*>(QStringLiteral("hydrogenRoleCombo_58"));
    if (!check(&ctx, h58Combo != nullptr && !h58Combo->toolTip().trimmed().isEmpty(), QStringLiteral("DFT dialog H-role manual override combo has tooltip"), &result)) return result;
    if (!check(&ctx, parameterWidgetHasTooltip(pTable, QStringLiteral("siesta.charge_spin.NetCharge")) && parameterWidgetHasTooltip(pTable, QStringLiteral("siesta.charge_spin.Spin")) && parameterWidgetHasTooltip(pTable, QStringLiteral("siesta.species.MeshCutoff")) && parameterWidgetHasTooltip(pTable, QStringLiteral("siesta.output.coordinate_precision")) && parameterWidgetHasTooltip(pTable, QStringLiteral("siesta.output.cell_precision")), QStringLiteral("DFT dialog SIESTA parameter widgets expose tooltips"), &result)) return result;
    codeCombo->setCurrentIndex(1);
    QCoreApplication::processEvents();
    if (!check(&ctx, parameterWidgetHasTooltip(pTable, QStringLiteral("qe.SYSTEM.ecutwfc")) && parameterWidgetHasTooltip(pTable, QStringLiteral("qe.SYSTEM.assume_isolated")) && parameterWidgetHasTooltip(pTable, QStringLiteral("qe.output.coordinate_precision")) && parameterWidgetHasTooltip(pTable, QStringLiteral("qe.output.cell_precision")), QStringLiteral("DFT dialog QE parameter widgets expose tooltips"), &result)) return result;
    if (speciesTable->rowCount() > 0 && speciesTable->item(0, 2) != nullptr) speciesTable->item(0, 2)->setText(QStringLiteral("1.0"));
    resetSpeciesButton->click();
    QCoreApplication::processEvents();
    if (!check(&ctx, !resetSpeciesButton->toolTip().trimmed().isEmpty() && speciesTable->rowCount() > 0 && speciesTable->columnCount() == 6 && speciesTable->item(0, 0) != nullptr && speciesTable->item(0, 0)->text() == QStringLiteral("Ga") && speciesTable->item(0, 2) != nullptr && almostEqual(speciesTable->item(0, 2)->text().toDouble(), 69.723) && speciesTable->item(0, 5) != nullptr && speciesTable->item(0, 5)->text() == QStringLiteral("project_profile"), QStringLiteral("DFT dialog QE species mass remains editable and reset-to-project-default restores Ga real mass/source"), &result)) return result;

    QTemporaryDir dir;
    if (!check(&ctx, dir.isValid(), QStringLiteral("temporary directory available"), &result)) return result;
    if (!check(&ctx, DftInputGenerator::writeGeneratedFiles(dir.path(), siestaNeutral, siestaNeutralOut, &writeError), QStringLiteral("writeGeneratedFiles writes primary and summaries"), &result)) return result;
    if (!check(&ctx, QFileInfo::exists(dir.filePath(QStringLiteral("ideal.fdf"))) && QFileInfo::exists(dir.filePath(QStringLiteral("ideal.generation_summary.json"))) && QFileInfo::exists(dir.filePath(QStringLiteral("ideal.generation_summary.md"))) && QFileInfo::exists(dir.filePath(QStringLiteral("ideal.parameters.json"))) && QFileInfo::exists(dir.filePath(QStringLiteral("ideal.extracted_parameters.json"))) && !QFileInfo::exists(dir.filePath(QStringLiteral("ideal.sh"))) && !QFileInfo::exists(dir.filePath(QStringLiteral("ideal.csh"))) && !QFileInfo::exists(dir.filePath(QStringLiteral("ideal.pbs"))), QStringLiteral("writeGeneratedFiles limits outputs to input/summary/parameters/extracted parameters, no job scripts"), &result)) return result;
    if (!check(&ctx, noJobScriptsUnder(auditOutput), QStringLiteral("final audit sample tree contains no job scripts"), &result)) return result;

    QTextStream out(stdout);
    out << "DFT input generator self-test passed: " << ctx.passed.size() << " assertions\n";
    for (const auto& name : ctx.passed) out << "PASS: " << name << '\n';
    if (!auditOutput.isEmpty()) out << "Audit samples: " << QDir(auditOutput).absolutePath() << '\n';
    return 0;
}
