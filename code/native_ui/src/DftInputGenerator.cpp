#include "DftInputGenerator.h"

#include "DftParameterRegistry.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QStringConverter>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {

constexpr int kDefaultOutputPrecision = 10;

int outputPrecision(const DftSettings& settings, const QString& key) {
    const QString id = dftCodeKey(settings.code) + QStringLiteral(".output.") + key;
    bool ok = false;
    const int value = DftParameterRegistry::parameterValue(settings, id, QString::number(kDefaultOutputPrecision)).toInt(&ok);
    const QSet<int> allowed = {6, 8, 10, 12, 16};
    return ok && allowed.contains(value) ? value : kDefaultOutputPrecision;
}

QString f(double value, int precision = kDefaultOutputPrecision) {
    precision = std::clamp(precision, 0, 16);
    const double zeroThreshold = 0.5 * std::pow(10.0, -precision);
    if (std::abs(value) < zeroThreshold) value = 0.0;
    return QString::number(value, 'f', precision)
        .replace(QRegularExpression("0+$"), "")
        .replace(QRegularExpression("\\.$"), "");
}

QString fixedF(double value, int precision) {
    precision = std::clamp(precision, 0, 16);
    const double zeroThreshold = 0.5 * std::pow(10.0, -precision);
    if (std::abs(value) < zeroThreshold) value = 0.0;
    return QString::number(value, 'f', precision);
}

QString normalizedFdfLabel(QString label) {
    label = label.trimmed().toLower();
    label.remove(QLatin1Char('.'));
    label.remove(QLatin1Char('_'));
    label.remove(QLatin1Char('-'));
    label.remove(QLatin1Char(' '));
    return label;
}

bool siestaCatalogParameter(const DftParameterSpec& spec) {
    return spec.validationRule == QStringLiteral("siesta_fdf_catalog");
}

QSet<QString> normalizedSet(const QSet<QString>& values) {
    QSet<QString> out;
    for (const auto& value : values) {
        const QString normalized = normalizedFdfLabel(value);
        if (!normalized.isEmpty()) out.insert(normalized);
    }
    return out;
}

QString vecLine(const QVector3D& v, int precision) {
    return QStringLiteral("  %1 %2 %3").arg(fixedF(v.x(), precision), fixedF(v.y(), precision), fixedF(v.z(), precision));
}

bool isH(const NativeAtom& atom) { return atom.element.compare(QStringLiteral("H"), Qt::CaseInsensitive) == 0; }
bool allMovable(const NativeAtom& atom) { return atom.movable[0] && atom.movable[1] && atom.movable[2]; }
bool allFixed(const NativeAtom& atom) { return !atom.movable[0] && !atom.movable[1] && !atom.movable[2]; }
bool partiallyFixed(const NativeAtom& atom) { return !allMovable(atom) && !allFixed(atom); }

bool roleFixed(DftHydrogenRole role) {
    return role == DftHydrogenRole::BottomPseudoHNTerminated075 ||
           role == DftHydrogenRole::BottomPseudoHIIITerminated125;
}

int siestaSpeciesIndexForRole(DftHydrogenRole role) {
    if (role == DftHydrogenRole::BottomPseudoHNTerminated075) return 1;
    if (role == DftHydrogenRole::BottomPseudoHIIITerminated125) return 5;
    return 4;
}

QString siestaSpeciesForRole(DftHydrogenRole role) {
    if (role == DftHydrogenRole::BottomPseudoHNTerminated075) return QStringLiteral("H-0.750");
    if (role == DftHydrogenRole::BottomPseudoHIIITerminated125) return QStringLiteral("H-1.250");
    return QStringLiteral("H");
}

QString qeLabelForRole(DftHydrogenRole role) {
    if (role == DftHydrogenRole::SurfaceAdsorbedHydrogen ||
        role == DftHydrogenRole::OrdinaryHydrogen) {
        return QStringLiteral("H2");
    }
    if (role == DftHydrogenRole::BottomPseudoHIIITerminated125) return QStringLiteral("Hp125");
    return QStringLiteral("H");
}

QString qePseudoForRole(DftHydrogenRole role) {
    if (role == DftHydrogenRole::BottomPseudoHNTerminated075) return QStringLiteral("H.pbe-MT.075.UPF");
    if (role == DftHydrogenRole::BottomPseudoHIIITerminated125) return QStringLiteral("H.pbe-MT.125.UPF");
    return QStringLiteral("H.pbe-mt_fhi.UPF");
}

QString companionFileName(const QString& value, const QString& fallback = QString()) {
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) return fallback;
    const QString fileName = QFileInfo(trimmed).fileName();
    return fileName.isEmpty() ? trimmed : fileName;
}

QString normalizedCompanionReference(const QString& value) {
    QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) return QString();
    trimmed.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return QDir::cleanPath(trimmed).replace(QLatin1Char('\\'), QLatin1Char('/'));
}

QString joinRelativeCompanionReference(const QString& directory, const QString& fileName) {
    const QString file = normalizedCompanionReference(fileName);
    if (file.isEmpty()) return QString();
    if (QFileInfo(file).isAbsolute() || file.contains(QLatin1Char('/'))) return file;
    const QString dir = normalizedCompanionReference(directory);
    if (dir.isEmpty() || dir == QStringLiteral(".")) return file;
    return QDir::cleanPath(dir + QLatin1Char('/') + file).replace(QLatin1Char('\\'), QLatin1Char('/'));
}

QString siestaDefaultPsfForLabel(const QString& label) {
    if (label.compare(QStringLiteral("Ga"), Qt::CaseInsensitive) == 0) return QStringLiteral("Ga.psf");
    if (label.compare(QStringLiteral("N"), Qt::CaseInsensitive) == 0) return QStringLiteral("N.psf");
    if (label.compare(QStringLiteral("H"), Qt::CaseInsensitive) == 0) return QStringLiteral("H.psf");
    if (label.compare(QStringLiteral("H-0.750"), Qt::CaseInsensitive) == 0) return QStringLiteral("H-0.750.psf");
    if (label.compare(QStringLiteral("H-1.250"), Qt::CaseInsensitive) == 0) return QStringLiteral("H-1.250.psf");
    return QString();
}

QString siestaPsfForSpecies(const DftSiestaSpecies& species) {
    return companionFileName(species.pseudopotential, siestaDefaultPsfForLabel(species.label));
}

QString siestaPsfReferenceForSpecies(const DftSettings& settings, const DftSiestaSpecies& species) {
    return joinRelativeCompanionReference(settings.pseudoDir, siestaPsfForSpecies(species));
}

QString siestaXcFdfIncludeReference(const DftSettings& settings) {
    return companionFileName(settings.xcFdfPath, QStringLiteral("xc.fdf"));
}

QStringList orderedCompanionFileList(QStringList files) {
    QStringList normalized;
    for (const QString& file : std::as_const(files)) {
        const QString name = normalizedCompanionReference(file);
        if (name.isEmpty()) continue;
        bool exists = false;
        for (const QString& current : std::as_const(normalized)) {
            if (current.compare(name, Qt::CaseInsensitive) == 0) {
                exists = true;
                break;
            }
        }
        if (!exists) normalized << name;
    }

    const QStringList preferred = {
        QStringLiteral("xc.fdf"),
        QStringLiteral("Ga.psf"),
        QStringLiteral("N.psf"),
        QStringLiteral("H.psf"),
        QStringLiteral("H-0.750.psf"),
        QStringLiteral("H-1.250.psf"),
    };
    QStringList ordered;
    for (const QString& wanted : preferred) {
        for (const QString& file : std::as_const(normalized)) {
            if (QFileInfo(file).fileName().compare(wanted, Qt::CaseInsensitive) == 0 && !ordered.contains(file)) ordered << file;
        }
    }
    std::sort(normalized.begin(), normalized.end(), [](const QString& a, const QString& b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    for (const QString& file : std::as_const(normalized)) {
        bool exists = false;
        for (const QString& current : std::as_const(ordered)) {
            if (current.compare(file, Qt::CaseInsensitive) == 0) {
                exists = true;
                break;
            }
        }
        if (!exists) ordered << file;
    }
    return ordered;
}

QString requiredCompanionFilePath(const QDir& outputDir, const QString& reference) {
    const QString normalized = normalizedCompanionReference(reference);
    if (normalized.isEmpty()) return QString();
    if (QFileInfo(normalized).isAbsolute()) return normalized;
    return outputDir.filePath(normalized);
}

QJsonArray stringListJsonArray(const QStringList& values) {
    QJsonArray arr;
    for (const QString& value : values) arr << value;
    return arr;
}

QStringList siestaCompanionFilesFromSettings(const DftSettings& settings, const DftGeneratedInput& generated) {
    QStringList files = generated.requiredCompanionFiles;
    if (settings.code == DftCode::Siesta && settings.includeXcFdf) files << siestaXcFdfIncludeReference(settings);
    return orderedCompanionFileList(files);
}

QString sourceTreeAssetPath(const QString& relativePath) {
#ifdef ASEAPP_NATIVE_UI_SOURCE_DIR
    return QDir(QStringLiteral(ASEAPP_NATIVE_UI_SOURCE_DIR)).filePath(relativePath);
#else
    Q_UNUSED(relativePath);
    return QString();
#endif
}

QStringList xcFdfSourceCandidates(const DftSettings& settings, const QDir& outputDir) {
    const QString includeName = siestaXcFdfIncludeReference(settings);
    const QString raw = settings.xcFdfPath.trimmed();
    QStringList candidates;
    if (!raw.isEmpty()) {
        candidates << raw;
        if (QFileInfo(raw).isRelative()) {
            candidates << outputDir.filePath(raw);
            candidates << outputDir.filePath(companionFileName(raw));
        }
    }
    const QStringList kangawaProfileDirs = {
        QStringLiteral("Kangawa_GaN_neutral_slab"),
        QStringLiteral("Kangawa_GaN_n_type_slab"),
        QStringLiteral("Kangawa_GaN_p_type_slab"),
    };
    if (!settings.version.trimmed().isEmpty()) {
        for (const QString& profileDir : kangawaProfileDirs) {
            candidates << QStringLiteral(":/dft_profiles/siesta/%1/%2/%3").arg(settings.version.trimmed(), profileDir, includeName);
            candidates << sourceTreeAssetPath(QStringLiteral("assets/dft_profiles/siesta/%1/%2/%3").arg(settings.version.trimmed(), profileDir, includeName));
            candidates << QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("assets/dft_profiles/siesta/%1/%2/%3").arg(settings.version.trimmed(), profileDir, includeName));
        }
    }
    for (const QString& profileDir : kangawaProfileDirs) {
        candidates << QStringLiteral(":/dft_profiles/siesta/4.1.5/%1/%2").arg(profileDir, includeName);
        candidates << sourceTreeAssetPath(QStringLiteral("assets/dft_profiles/siesta/4.1.5/%1/%2").arg(profileDir, includeName));
        candidates << QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("assets/dft_profiles/siesta/4.1.5/%1/%2").arg(profileDir, includeName));
    }
    candidates << QStringLiteral(":/dft_profiles/siesta/%1").arg(includeName);
    candidates << sourceTreeAssetPath(QStringLiteral("assets/dft_profiles/siesta/%1").arg(includeName));
    candidates << QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("assets/dft_profiles/siesta/%1").arg(includeName));

    QStringList unique;
    for (const QString& candidate : std::as_const(candidates)) {
        if (candidate.trimmed().isEmpty()) continue;
        if (!unique.contains(candidate)) unique << candidate;
    }
    return unique;
}

QString resolveXcFdfSourcePath(const DftSettings& settings, const QDir& outputDir) {
    for (const QString& candidate : xcFdfSourceCandidates(settings, outputDir)) {
        QFileInfo info(candidate);
        if (candidate.startsWith(QStringLiteral(":/"))) {
            if (QFile::exists(candidate)) return candidate;
        } else if (info.exists() && info.isFile()) {
            return info.absoluteFilePath();
        }
    }
    return QString();
}

bool copyFileReplacing(const QString& sourcePath, const QString& destinationPath, QString* errorMessage) {
    if (sourcePath.trimmed().isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("コピー元が未解決です: %1").arg(destinationPath);
        return false;
    }

    const QFileInfo sourceInfo(sourcePath);
    const QFileInfo destInfo(destinationPath);
    if (!sourcePath.startsWith(QStringLiteral(":/")) &&
        sourceInfo.exists() &&
        destInfo.exists() &&
        sourceInfo.canonicalFilePath() == destInfo.canonicalFilePath()) {
        return true;
    }

    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        if (errorMessage) *errorMessage = QStringLiteral("コピー元を読めません: %1").arg(sourcePath);
        return false;
    }
    QFile destination(destinationPath);
    if (!destination.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) *errorMessage = QStringLiteral("コピー先へ書き込めません: %1").arg(destinationPath);
        return false;
    }
    if (destination.write(source.readAll()) < 0) {
        if (errorMessage) *errorMessage = QStringLiteral("コピーに失敗しました: %1 -> %2").arg(sourcePath, destinationPath);
        return false;
    }
    return true;
}

const DftHydrogenAssignment* assignmentForAtom(const QVector<DftHydrogenAssignment>& assignments, int atomIndex) {
    for (const auto& a : assignments) {
        if (a.atomIndex == atomIndex) return &a;
    }
    return nullptr;
}

bool isCationElement(const QString& element) {
    return element.compare(QStringLiteral("Ga"), Qt::CaseInsensitive) == 0 ||
           element.compare(QStringLiteral("Al"), Qt::CaseInsensitive) == 0;
}

bool isAnionElement(const QString& element) {
    return element.compare(QStringLiteral("N"), Qt::CaseInsensitive) == 0;
}

QJsonArray sortedIndicesJson(const QSet<int>& zeroBasedIndices) {
    QVector<int> values;
    values.reserve(zeroBasedIndices.size());
    for (int index : zeroBasedIndices) values << (index + 1);
    std::sort(values.begin(), values.end());
    QJsonArray arr;
    for (int value : values) arr << value;
    return arr;
}

QString fixedModeSource(DftFixedAtomMode mode) {
    return QStringLiteral("fixed_mode:%1").arg(dftFixedAtomModeKey(mode));
}

struct BottomLayerDetection {
    QSet<int> bottomH;
    QSet<int> bottomN;
    QSet<int> bottomGa;
    QString level = QStringLiteral("not_applicable");
    double score = 0.0;
    QJsonObject settings;
    QJsonObject toJson() const {
        QJsonObject obj;
        obj.insert(QStringLiteral("level"), level);
        obj.insert(QStringLiteral("score"), score);
        obj.insert(QStringLiteral("bottom_h_indices"), sortedIndicesJson(bottomH));
        obj.insert(QStringLiteral("bottom_n_indices"), sortedIndicesJson(bottomN));
        obj.insert(QStringLiteral("bottom_ga_indices"), sortedIndicesJson(bottomGa));
        return obj;
    }
};

BottomLayerDetection detectBottomMolecularLayer(const StructureData& structure, const DftSettings& settings) {
    BottomLayerDetection detection;
    detection.settings.insert(QStringLiteral("method"), QStringLiteral("geometry_z_cluster_nearest_neighbor"));
    detection.settings.insert(QStringLiteral("bottom_side"), QStringLiteral("min_z"));
    detection.settings.insert(QStringLiteral("direction"), QStringLiteral("cartesian_z"));
    detection.settings.insert(QStringLiteral("material"), QStringLiteral("GaN_or_AlN"));
    detection.settings.insert(QStringLiteral("bottom_h_role"), dftHydrogenRoleKey(DftHydrogenRole::BottomPseudoHNTerminated075));
    detection.settings.insert(QStringLiteral("n_layer_tolerance_ang"), 0.35);
    detection.settings.insert(QStringLiteral("ga_layer_tolerance_ang"), 0.35);
    detection.settings.insert(QStringLiteral("nearest_neighbor_fallback"), true);
    if (structure.atoms.empty()) return detection;

    for (const auto& h : settings.hydrogenAssignments) {
        if (h.atomIndex < 0 || h.atomIndex >= static_cast<int>(structure.atoms.size())) continue;
        if (h.selectedRole == DftHydrogenRole::BottomPseudoHNTerminated075 ||
            h.selectedRole == DftHydrogenRole::BottomPseudoHIIITerminated125) {
            detection.bottomH.insert(h.atomIndex);
        }
    }
    if (detection.bottomH.isEmpty()) {
        detection.level = QStringLiteral("none");
        return detection;
    }

    QVector<int> seedN;
    for (int hIndex : detection.bottomH) {
        const auto* h = assignmentForAtom(settings.hydrogenAssignments, hIndex);
        if (h != nullptr && h->nearestNonHAtomIndex >= 0 &&
            h->nearestNonHAtomIndex < static_cast<int>(structure.atoms.size()) &&
            isAnionElement(structure.atoms[static_cast<std::size_t>(h->nearestNonHAtomIndex)].element)) {
            seedN << h->nearestNonHAtomIndex;
            continue;
        }
        double best = std::numeric_limits<double>::max();
        int bestIndex = -1;
        const QVector3D hPos = structure.atoms[static_cast<std::size_t>(hIndex)].cartesian;
        for (int i = 0; i < static_cast<int>(structure.atoms.size()); ++i) {
            const auto& atom = structure.atoms[static_cast<std::size_t>(i)];
            if (!isAnionElement(atom.element)) continue;
            const double distance = (atom.cartesian - hPos).length();
            if (distance < best) {
                best = distance;
                bestIndex = i;
            }
        }
        if (bestIndex >= 0) seedN << bestIndex;
    }

    double nSeedZ = 0.0;
    if (!seedN.isEmpty()) {
        for (int index : seedN) nSeedZ += structure.atoms[static_cast<std::size_t>(index)].cartesian.z();
        nSeedZ /= static_cast<double>(seedN.size());
    } else {
        double minN = std::numeric_limits<double>::max();
        for (int i = 0; i < static_cast<int>(structure.atoms.size()); ++i) {
            const auto& atom = structure.atoms[static_cast<std::size_t>(i)];
            if (!isAnionElement(atom.element)) continue;
            minN = std::min(minN, static_cast<double>(atom.cartesian.z()));
        }
        if (minN != std::numeric_limits<double>::max()) nSeedZ = minN;
    }

    if (nSeedZ != 0.0 || !seedN.isEmpty()) {
        for (int i = 0; i < static_cast<int>(structure.atoms.size()); ++i) {
            const auto& atom = structure.atoms[static_cast<std::size_t>(i)];
            if (isAnionElement(atom.element) && std::abs(static_cast<double>(atom.cartesian.z()) - nSeedZ) <= 0.35) {
                detection.bottomN.insert(i);
            }
        }
    }

    QVector<int> cationIndices;
    for (int i = 0; i < static_cast<int>(structure.atoms.size()); ++i) {
        if (isCationElement(structure.atoms[static_cast<std::size_t>(i)].element)) cationIndices << i;
    }
    if (!cationIndices.isEmpty() && !detection.bottomN.isEmpty()) {
        double bestZ = 0.0;
        double bestDz = std::numeric_limits<double>::max();
        for (int i : cationIndices) {
            const double z = structure.atoms[static_cast<std::size_t>(i)].cartesian.z();
            const double dz = z - nSeedZ;
            if (dz > -0.05 && dz < bestDz) {
                bestDz = dz;
                bestZ = z;
            }
        }
        if (bestDz == std::numeric_limits<double>::max()) {
            for (int i : cationIndices) {
                const double z = structure.atoms[static_cast<std::size_t>(i)].cartesian.z();
                const double dz = std::abs(z - nSeedZ);
                if (dz < bestDz) {
                    bestDz = dz;
                    bestZ = z;
                }
            }
        }
        if (bestDz != std::numeric_limits<double>::max()) {
            for (int i : cationIndices) {
                const double z = structure.atoms[static_cast<std::size_t>(i)].cartesian.z();
                if (std::abs(z - bestZ) <= 0.35) detection.bottomGa.insert(i);
            }
        }
    }

    const bool high = !detection.bottomH.isEmpty() &&
        detection.bottomN.size() >= detection.bottomH.size() &&
        detection.bottomGa.size() >= detection.bottomH.size();
    if (high) {
        detection.level = QStringLiteral("high");
        detection.score = 1.0;
    } else if (!detection.bottomN.isEmpty() || !detection.bottomGa.isEmpty()) {
        detection.level = QStringLiteral("medium");
        detection.score = 0.6;
    } else {
        detection.level = QStringLiteral("low");
        detection.score = 0.2;
    }
    return detection;
}

struct FixedAtomDecision {
    std::array<bool, 3> fixed{false, false, false};
    QString reason = QStringLiteral("none");
    QString source = QStringLiteral("none");
    QString hydrogenRole;
};

struct FixedAtomPlan {
    QVector<FixedAtomDecision> atoms;
    BottomLayerDetection bottomLayer;
};

bool decisionHasFixedAxis(const FixedAtomDecision& decision) {
    return decision.fixed[0] || decision.fixed[1] || decision.fixed[2];
}

void applyFullFixed(FixedAtomPlan* plan, int atomIndex, const QString& reason,
                    const QString& source, const QString& hydrogenRole = QString()) {
    if (plan == nullptr || atomIndex < 0 || atomIndex >= plan->atoms.size()) return;
    auto& decision = plan->atoms[atomIndex];
    decision.fixed = {true, true, true};
    decision.reason = reason;
    decision.source = source;
    if (!hydrogenRole.isEmpty()) decision.hydrogenRole = hydrogenRole;
}

FixedAtomPlan buildFixedAtomPlan(const StructureData& structure, const DftSettings& settings) {
    FixedAtomPlan plan;
    plan.atoms.resize(static_cast<int>(structure.atoms.size()));
    plan.bottomLayer = detectBottomMolecularLayer(structure, settings);

    const QString importedReason = settings.fixedAtomMode == DftFixedAtomMode::ManualOnly
        ? QStringLiteral("manual_user_fixed")
        : QStringLiteral("imported_flag");
    const QString importedSource = settings.fixedAtomMode == DftFixedAtomMode::ManualOnly
        ? QStringLiteral("manual")
        : QStringLiteral("structure_import");
    for (int i = 0; i < static_cast<int>(structure.atoms.size()); ++i) {
        const auto& atom = structure.atoms[static_cast<std::size_t>(i)];
        std::array<bool, 3> fixed = {!atom.movable[0], !atom.movable[1], !atom.movable[2]};
        if (fixed[0] || fixed[1] || fixed[2]) {
            auto& decision = plan.atoms[i];
            decision.fixed = fixed;
            decision.reason = importedReason;
            decision.source = importedSource;
        }
    }

    if (settings.fixedAtomMode == DftFixedAtomMode::FixBottomPseudoHOnly ||
        settings.fixedAtomMode == DftFixedAtomMode::FixBottomPseudoHPlusBottomMolecularLayer) {
        const QString source = fixedModeSource(settings.fixedAtomMode);
        for (const auto& h : settings.hydrogenAssignments) {
            if (h.atomIndex < 0 || h.atomIndex >= static_cast<int>(structure.atoms.size())) continue;
            if (!roleFixed(h.selectedRole)) continue;
            applyFullFixed(&plan, h.atomIndex, QStringLiteral("bottom_pseudo_h"),
                           QStringLiteral("hydrogen_role"), dftHydrogenRoleKey(h.selectedRole));
            plan.atoms[h.atomIndex].source = QStringLiteral("hydrogen_role");
        }
        if (settings.fixedAtomMode == DftFixedAtomMode::FixBottomPseudoHPlusBottomMolecularLayer) {
            for (int index : plan.bottomLayer.bottomN) {
                applyFullFixed(&plan, index, QStringLiteral("bottom_molecular_layer_n"), source);
            }
            for (int index : plan.bottomLayer.bottomGa) {
                applyFullFixed(&plan, index, QStringLiteral("bottom_molecular_layer_ga"), source);
            }
        }
    }

    if (settings.fixedAtomMode != DftFixedAtomMode::ManualOnly &&
        settings.calculationMode == QStringLiteral("h2_reference")) {
        for (const auto& h : settings.hydrogenAssignments) {
            if (h.atomIndex == 0) {
                applyFullFixed(&plan, h.atomIndex, QStringLiteral("molecule_reference_anchor"),
                               QStringLiteral("calculation_mode:h2_reference"),
                               dftHydrogenRoleKey(h.selectedRole));
                break;
            }
        }
    }

    return plan;
}

DftHydrogenAssignment makeAssignment(const StructureData& structure, int atomIndex, const DftSettings& settings) {
    const auto& atom = structure.atoms[static_cast<std::size_t>(atomIndex)];
    DftHydrogenAssignment a;
    a.atomIndex = atomIndex;
    a.cartesian = atom.cartesian;
    a.fractional = atom.fractional;
    a.inferenceSource = QStringLiteral("geometry");
    a.inferredRole = DftHydrogenRole::UnknownHydrogen;
    a.selectedRole = DftHydrogenRole::UnknownHydrogen;
    a.confidence = QStringLiteral("unknown");

    int hCount = 0;
    int nonHCount = 0;
    double zMin = std::numeric_limits<double>::max();
    double zMax = -std::numeric_limits<double>::max();
    for (const auto& other : structure.atoms) {
        zMin = std::min(zMin, static_cast<double>(other.cartesian.z()));
        zMax = std::max(zMax, static_cast<double>(other.cartesian.z()));
        if (isH(other)) ++hCount; else ++nonHCount;
    }
    if (hCount == 2 && nonHCount == 0) {
        a.inferredRole = DftHydrogenRole::MoleculeH2Hydrogen;
        a.selectedRole = a.inferredRole;
        a.confidence = QStringLiteral("high");
        a.inferenceSource = QStringLiteral("h2_molecule");
    } else {
        double best = std::numeric_limits<double>::max();
        int bestIndex = -1;
        QString bestElement;
        for (int i = 0; i < static_cast<int>(structure.atoms.size()); ++i) {
            if (i == atomIndex || isH(structure.atoms[static_cast<std::size_t>(i)])) continue;
            const double d = (structure.atoms[static_cast<std::size_t>(i)].cartesian - atom.cartesian).length();
            if (d < best) {
                best = d;
                bestIndex = i;
                bestElement = structure.atoms[static_cast<std::size_t>(i)].element;
            }
        }
        a.nearestNonHAtomIndex = bestIndex;
        a.nearestNonHElement = bestElement;
        a.nearestNonHDistanceAng = best == std::numeric_limits<double>::max() ? 0.0 : best;
        const double z = atom.cartesian.z();
        const double zSpan = zMax - zMin;
        const bool slabLikeBottom = nonHCount >= 2 && zSpan > 2.5;
        const bool nearBottom = slabLikeBottom && (z - zMin) <= 1.5;
        const bool nearTop = (zMax - z) <= 2.0;
        if (nearBottom && bestElement.compare("N", Qt::CaseInsensitive) == 0) {
            a.inferredRole = DftHydrogenRole::BottomPseudoHNTerminated075;
            a.confidence = best <= 1.4 ? QStringLiteral("high") : QStringLiteral("medium");
        } else if (nearBottom && (bestElement.compare("Ga", Qt::CaseInsensitive) == 0 || bestElement.compare("Al", Qt::CaseInsensitive) == 0)) {
            a.inferredRole = DftHydrogenRole::BottomPseudoHIIITerminated125;
            a.confidence = best <= 1.4 ? QStringLiteral("high") : QStringLiteral("medium");
        } else if (nearTop && best <= 1.4) {
            a.inferredRole = DftHydrogenRole::SurfaceAdsorbedHydrogen;
            a.confidence = QStringLiteral("medium");
        } else {
            a.inferredRole = DftHydrogenRole::UnknownHydrogen;
            a.confidence = QStringLiteral("unknown");
            a.warning = QStringLiteral("HydrogenRole未確定");
        }
        a.selectedRole = a.inferredRole;
    }
    if (settings.calculationMode == QStringLiteral("h2_reference")) {
        a.inferredRole = DftHydrogenRole::MoleculeH2Hydrogen;
        a.selectedRole = a.inferredRole;
        a.confidence = QStringLiteral("high");
    }
    a.siestaSpecies = siestaSpeciesForRole(a.selectedRole);
    a.siestaSpeciesIndex = siestaSpeciesIndexForRole(a.selectedRole);
    a.qeLabel = qeLabelForRole(a.selectedRole);
    a.qePseudoFile = qePseudoForRole(a.selectedRole);
    a.fixedByRole = roleFixed(a.selectedRole) || (settings.calculationMode == QStringLiteral("h2_reference") && atomIndex == 0);
    return a;
}

QString param(const DftSettings& settings, const QString& id, const QString& fallback = QString()) {
    return DftParameterRegistry::parameterValue(settings, id, fallback);
}

void addUnique(QStringList* list, const QString& value) {
    if (list == nullptr) return;
    const QString trimmed = value.trimmed();
    if (!trimmed.isEmpty() && !list->contains(trimmed, Qt::CaseInsensitive)) *list << trimmed;
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

bool hasScalarKey(const QString& text, const QString& key) {
    const QRegularExpression re(QStringLiteral("(^|\\n)\\s*%1\\s+").arg(QRegularExpression::escape(key)),
                                QRegularExpression::CaseInsensitiveOption);
    return re.match(text).hasMatch();
}

bool hasBlock(const QString& text, const QString& block) {
    const QRegularExpression re(QStringLiteral("(^|\\n)\\s*%block\\s+%1\\b").arg(QRegularExpression::escape(block)),
                                QRegularExpression::CaseInsensitiveOption);
    return re.match(text).hasMatch();
}

bool hasIncludeXc(const QString& text) {
    const QRegularExpression re(QStringLiteral("(^|\\n)\\s*%include\\s+\\S*xc\\.fdf\\s*($|\\n)"),
                                QRegularExpression::CaseInsensitiveOption);
    return re.match(text).hasMatch();
}

QVector<int> constraintAtoms(const QString& text) {
    QVector<int> atoms;
    const QStringList lines = linesBetween(text, QStringLiteral("%block Geometry.Constraints"), QStringLiteral("%endblock Geometry.Constraints"));
    for (const QString& line : lines) {
        const QStringList parts = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (parts.isEmpty() || parts.first().compare(QStringLiteral("atom"), Qt::CaseInsensitive) != 0) continue;
        for (int i = 1; i < parts.size(); ++i) {
            bool ok = false;
            const int atom = parts.at(i).toInt(&ok);
            if (ok) atoms << atom;
        }
    }
    std::sort(atoms.begin(), atoms.end());
    return atoms;
}

QVector<int> expectedSevenLayerFixedAtoms() {
    return {1, 5, 9, 13, 42, 46, 50, 54, 58, 59, 60, 61};
}

bool containsAll(QVector<int> values, const QVector<int>& expected) {
    std::sort(values.begin(), values.end());
    for (int v : expected) {
        if (!values.contains(v)) return false;
    }
    return true;
}

bool isNeutralSiesta(const DftSettings& settings) {
    return settings.code == DftCode::Siesta &&
           param(settings, QStringLiteral("siesta.charge_spin.NetCharge"), QStringLiteral("0.0")) == QStringLiteral("0.0") &&
           param(settings, QStringLiteral("siesta.charge_spin.Spin"), QStringLiteral("none")).compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0;
}

double scalarDouble(const DftSettings& settings, const QString& id, double fallback, bool* okOut = nullptr) {
    bool ok = false;
    const double value = param(settings, id).toDouble(&ok);
    if (okOut) *okOut = ok;
    return ok ? value : fallback;
}

double topVacuumEstimate(const StructureData& structure) {
    if (structure.atoms.empty()) return 0.0;
    double zMin = structure.atoms.front().cartesian.z();
    double zMax = zMin;
    for (const auto& atom : structure.atoms) {
        zMin = std::min(zMin, static_cast<double>(atom.cartesian.z()));
        zMax = std::max(zMax, static_cast<double>(atom.cartesian.z()));
    }
    const double cLength = structure.cellVectors[2].length();
    return std::max(0.0, cLength - (zMax - zMin));
}

bool settingsHasUsefulUnknownSource(const DftSettings& settings) {
    const QStringList usefulIds = {
        QStringLiteral("siesta.scf.MaxSCFIterations"),
        QStringLiteral("siesta.scf.DM.Tolerance"),
        QStringLiteral("siesta.scf.DM.MixingWeight"),
        QStringLiteral("siesta.scf.DM.NumberPulay"),
        QStringLiteral("siesta.relaxation.MD.VariableCell"),
        QStringLiteral("siesta.kpoints.kgrid"),
    };
    for (const QString& id : usefulIds) {
        const auto it = settings.parameters.constFind(id);
        if (it != settings.parameters.constEnd() && it.value().enabled && it.value().source == DftParameterSource::Unknown) return true;
    }
    return false;
}

QJsonArray vectorToJson(const QVector3D& v) {
    QJsonArray arr;
    arr << v.x() << v.y() << v.z();
    return arr;
}

QJsonObject psLmaxJson(const DftSettings& settings) {
    QJsonObject values;
    for (const auto& raw : settings.rawParameters) {
        if (!raw.enabled || raw.code != DftCode::Siesta || raw.key.compare(QStringLiteral("PS.lmax"), Qt::CaseInsensitive) != 0) continue;
        const QStringList rows = raw.blockOrCard ? raw.value.split('\n', Qt::SkipEmptyParts) : QStringList{raw.value};
        for (QString row : rows) {
            row = row.trimmed();
            if (row.isEmpty()) continue;
            const QStringList parts = row.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
            if (parts.size() < 2) continue;
            bool ok = false;
            const int value = parts.at(1).toInt(&ok);
            if (ok) values.insert(parts.at(0), value);
        }
    }
    return values;
}

QJsonObject extractedParametersJson(const DftSettings& settings) {
    QJsonObject root;
    root.insert(QStringLiteral("code"), dftCodeKey(settings.code));
    QJsonObject siesta;
    siesta.insert(QStringLiteral("PS.lmax"), psLmaxJson(settings));
    if (settings.code == DftCode::Siesta) root.insert(QStringLiteral("siesta"), siesta);
    return root;
}

QJsonObject parametersSnapshotJson(const DftSettings& settings) {
    QJsonObject root;
    root.insert(QStringLiteral("code"), dftCodeKey(settings.code));
    root.insert(QStringLiteral("version"), settings.version);
    root.insert(QStringLiteral("executable_or_schema"), settings.code == DftCode::QuantumEspresso ? settings.executable : settings.schema);
    root.insert(QStringLiteral("target_name"), DftInputGenerator::sanitizeTargetName(settings.targetName));
    root.insert(QStringLiteral("selected_profile"), settings.profileName);
    root.insert(QStringLiteral("generation_mode"), dftGenerationModeKey(settings.generationMode));
    root.insert(QStringLiteral("calculation_mode"), settings.calculationMode);
    root.insert(QStringLiteral("fixed_atom_mode"), dftFixedAtomModeKey(settings.fixedAtomMode));
    root.insert(QStringLiteral("trailing_flag_interpretation"), dftTrailingFlagInterpretationKey(settings.trailingFlagInterpretation));
    root.insert(QStringLiteral("include_xc_fdf"), settings.includeXcFdf);
    root.insert(QStringLiteral("standalone_inline"), settings.standaloneInline);
    root.insert(QStringLiteral("xc_fdf_path"), settings.xcFdfPath);
    root.insert(QStringLiteral("pseudo_dir"), settings.pseudoDir);
    root.insert(QStringLiteral("qe_project_style_fixed_flags"), settings.qeProjectStyleFixedFlags);
    root.insert(QStringLiteral("qe_assume_isolated"), settings.qeAssumeIsolated);
    root.insert(QStringLiteral("allow_unknown_hydrogen"), settings.allowUnknownHydrogen);

    QJsonObject parameters;
    for (auto it = settings.parameters.constBegin(); it != settings.parameters.constEnd(); ++it) {
        const DftParameterEntry& entry = it.value();
        QJsonObject obj;
        obj.insert(QStringLiteral("value"), entry.value);
        obj.insert(QStringLiteral("imported_value"), entry.importedValue);
        obj.insert(QStringLiteral("enabled"), entry.enabled);
        obj.insert(QStringLiteral("source"), dftParameterSourceKey(entry.source));
        obj.insert(QStringLiteral("section"), entry.spec.section);
        obj.insert(QStringLiteral("key"), entry.spec.key);
        obj.insert(QStringLiteral("label"), entry.spec.label);
        obj.insert(QStringLiteral("type"), entry.spec.type);
        obj.insert(QStringLiteral("unit"), entry.spec.unit);
        obj.insert(QStringLiteral("required"), entry.spec.required);
        obj.insert(QStringLiteral("advanced"), entry.spec.advanced);
        parameters.insert(it.key(), obj);
    }
    root.insert(QStringLiteral("parameters"), parameters);

    QJsonArray rawParameters;
    for (const auto& raw : settings.rawParameters) {
        QJsonObject obj;
        obj.insert(QStringLiteral("code"), dftCodeKey(raw.code));
        obj.insert(QStringLiteral("namelist_or_block"), raw.namelistOrBlock);
        obj.insert(QStringLiteral("key"), raw.key);
        obj.insert(QStringLiteral("value"), raw.value);
        obj.insert(QStringLiteral("unit"), raw.unit);
        obj.insert(QStringLiteral("output_position"), raw.outputPosition);
        obj.insert(QStringLiteral("block_or_card"), raw.blockOrCard);
        obj.insert(QStringLiteral("enabled"), raw.enabled);
        rawParameters << obj;
    }
    root.insert(QStringLiteral("raw_parameters"), rawParameters);

    QJsonArray siestaSpecies;
    for (const auto& sp : settings.siestaSpecies) {
        QJsonObject obj;
        obj.insert(QStringLiteral("index"), sp.index);
        obj.insert(QStringLiteral("atomic_number"), sp.atomicNumber);
        obj.insert(QStringLiteral("label"), sp.label);
        obj.insert(QStringLiteral("element"), sp.element);
        obj.insert(QStringLiteral("role"), sp.role);
        obj.insert(QStringLiteral("pseudopotential"), siestaPsfForSpecies(sp));
        obj.insert(QStringLiteral("required_path"), siestaPsfReferenceForSpecies(settings, sp));
        siestaSpecies << obj;
    }
    if (!siestaSpecies.isEmpty()) root.insert(QStringLiteral("siesta_species"), siestaSpecies);

    QJsonArray qeSpecies;
    for (const auto& sp : settings.qeSpecies) {
        QJsonObject obj;
        obj.insert(QStringLiteral("label"), sp.label);
        obj.insert(QStringLiteral("element"), sp.element);
        obj.insert(QStringLiteral("mass"), sp.mass);
        obj.insert(QStringLiteral("pseudo_file"), sp.pseudoFile);
        obj.insert(QStringLiteral("role"), sp.role);
        obj.insert(QStringLiteral("source"), dftParameterSourceKey(sp.source));
        qeSpecies << obj;
    }
    if (!qeSpecies.isEmpty()) root.insert(QStringLiteral("qe_species"), qeSpecies);

    return root;
}

QJsonObject summaryBase(const StructureData& structure, const DftSettings& settings, const DftGeneratedInput& generated) {
    QJsonObject root;
    root.insert(QStringLiteral("code"), dftCodeKey(settings.code));
    root.insert(QStringLiteral("version"), settings.version);
    root.insert(QStringLiteral("executable_or_schema"), settings.code == DftCode::QuantumEspresso ? settings.executable : settings.schema);
    root.insert(QStringLiteral("target_name"), settings.targetName);
    root.insert(QStringLiteral("source_structure"), structure.sourcePath);
    root.insert(QStringLiteral("generated_file"), DftInputGenerator::sanitizeTargetName(settings.targetName) + generated.fileExtension);
    root.insert(QStringLiteral("selected_profile"), settings.profileName);
    root.insert(QStringLiteral("generation_mode"), dftGenerationModeKey(settings.generationMode));
    root.insert(QStringLiteral("structure_hash"), DftInputGenerator::structureHash(structure));
    root.insert(QStringLiteral("atom_order_preserved"), true);
    root.insert(QStringLiteral("fixed_atom_mode"), dftFixedAtomModeKey(settings.fixedAtomMode));
    root.insert(QStringLiteral("trailing_flag_interpretation"), dftTrailingFlagInterpretationKey(settings.trailingFlagInterpretation));
    if (!structure.trailingFlagInterpretation.trimmed().isEmpty()) {
        root.insert(QStringLiteral("structure_import_trailing_flag_interpretation"), structure.trailingFlagInterpretation);
    }
    QJsonObject precision;
    precision.insert(QStringLiteral("coordinate_precision"), outputPrecision(settings, QStringLiteral("coordinate_precision")));
    precision.insert(QStringLiteral("cell_precision"), outputPrecision(settings, QStringLiteral("cell_precision")));
    root.insert(QStringLiteral("precision"), precision);
    const FixedAtomPlan fixedPlan = buildFixedAtomPlan(structure, settings);
    root.insert(QStringLiteral("bottom_layer_detection_settings"), fixedPlan.bottomLayer.settings);
    root.insert(QStringLiteral("bottom_layer_detection_confidence"), fixedPlan.bottomLayer.toJson());
    QJsonArray warnings;
    for (const auto& w : generated.warnings) warnings << w;
    root.insert(QStringLiteral("warnings"), warnings);
    QJsonArray errors;
    for (const auto& e : generated.errors) errors << e;
    root.insert(QStringLiteral("errors"), errors);
    const QStringList companionFiles = siestaCompanionFilesFromSettings(settings, generated);
    root.insert(QStringLiteral("required_companion_files"), stringListJsonArray(companionFiles));
    if (settings.code == DftCode::Siesta) {
        root.insert(QStringLiteral("include_xc_fdf"), settings.includeXcFdf);
        root.insert(QStringLiteral("xc_fdf_include"), settings.includeXcFdf ? siestaXcFdfIncludeReference(settings) : QString());
        root.insert(QStringLiteral("xc_fdf_source_path"), QString());
        root.insert(QStringLiteral("xc_fdf_output_path"), QString());
        root.insert(QStringLiteral("xc_fdf_copied"), false);
        root.insert(QStringLiteral("missing_companion_files"), stringListJsonArray(companionFiles));
        root.insert(QStringLiteral("run_ready"), false);
        root.insert(QStringLiteral("run_ready_status"), companionFiles.isEmpty()
                        ? QStringLiteral("no_companion_files_required")
                        : QStringLiteral("not_export_checked"));
    }
    QJsonArray fixed;
    QJsonArray fixedIndices;
    QJsonArray movable;
    QJsonArray atomOrder;
    for (int i = 0; i < static_cast<int>(structure.atoms.size()); ++i) {
        const auto& atom = structure.atoms[static_cast<std::size_t>(i)];
        QJsonArray flags;
        flags << atom.movable[0] << atom.movable[1] << atom.movable[2];
        QJsonObject m;
        m.insert(QStringLiteral("atom_index"), i + 1);
        m.insert(QStringLiteral("movable"), flags);
        movable << m;
        const auto* h = assignmentForAtom(settings.hydrogenAssignments, i);
        const FixedAtomDecision decision = i < fixedPlan.atoms.size() ? fixedPlan.atoms.at(i) : FixedAtomDecision{};
        if (decisionHasFixedAxis(decision)) {
            QJsonArray fixedFlags;
            fixedFlags << decision.fixed[0] << decision.fixed[1] << decision.fixed[2];
            QJsonObject fixedAtom;
            fixedAtom.insert(QStringLiteral("atom_index"), i + 1);
            fixedAtom.insert(QStringLiteral("element"), atom.element);
            fixedAtom.insert(QStringLiteral("fixed"), fixedFlags);
            fixedAtom.insert(QStringLiteral("reason"), decision.reason);
            fixedAtom.insert(QStringLiteral("source"), decision.source);
            if (h != nullptr) fixedAtom.insert(QStringLiteral("hydrogen_role"), dftHydrogenRoleKey(h->selectedRole));
            fixed << fixedAtom;
            fixedIndices << (i + 1);
        }
        QJsonObject order;
        order.insert(QStringLiteral("atom_index"), i + 1);
        order.insert(QStringLiteral("element"), atom.element);
        order.insert(QStringLiteral("cartesian"), vectorToJson(atom.cartesian));
        order.insert(QStringLiteral("input_order_preserved"), true);
        atomOrder << order;
    }
    root.insert(QStringLiteral("fixed_atoms"), fixed);
    root.insert(QStringLiteral("fixed_atom_count"), fixed.size());
    root.insert(QStringLiteral("fixed_atom_indices"), fixedIndices);
    root.insert(QStringLiteral("movable_flags"), movable);
    root.insert(QStringLiteral("atom_order"), atomOrder);
    QJsonObject ps;
    for (auto it = settings.parameters.constBegin(); it != settings.parameters.constEnd(); ++it) {
        ps.insert(it.key(), it.value().value);
    }
    root.insert(QStringLiteral("parameters"), ps);
    QJsonObject sources;
    for (auto it = settings.parameters.constBegin(); it != settings.parameters.constEnd(); ++it) {
        sources.insert(it.key(), dftParameterSourceKey(it.value().source));
    }
    root.insert(QStringLiteral("parameter_sources"), sources);
    return root;
}
QString stripStandaloneFdfComment(const QString& line) {
    const int hash = line.indexOf(QLatin1Char('#'));
    return (hash >= 0 ? line.left(hash) : line).trimmed();
}

QString standaloneChemicalSpeciesBlock(const DftSettings& settings) {
    QString text;
    QTextStream out(&text, QIODevice::WriteOnly);
    out << "%block ChemicalSpeciesLabel\n";
    for (const auto& sp : settings.siestaSpecies) {
        out << "  " << sp.index << " " << sp.atomicNumber << " " << sp.label << "\n";
    }
    out << "%endblock ChemicalSpeciesLabel\n";
    return text;
}

QString standaloneScalarOverrideLine(const DftSettings& settings, const QString& key) {
    const QString lower = key.trimmed().toLower();
    if (lower == QStringLiteral("xc.functional")) {
        return QStringLiteral("xc.functional %1").arg(param(settings, "siesta.species.xc.functional", "GGA").trimmed());
    }
    if (lower == QStringLiteral("xc.authors")) {
        return QStringLiteral("xc.authors %1").arg(param(settings, "siesta.species.xc.authors", "PBEJsJrLO").trimmed());
    }
    if (lower == QStringLiteral("meshcutoff")) {
        QString mesh = param(settings, "siesta.species.MeshCutoff", "410").trimmed();
        if (!mesh.contains(QStringLiteral("Ry"), Qt::CaseInsensitive)) mesh += QStringLiteral(" Ry");
        return QStringLiteral("MeshCutoff %1").arg(mesh);
    }
    return QString();
}

QString writeStandaloneSiestaBlocks(const DftSettings& settings, QStringList* errors, QSet<QString>* emittedKeys) {
    QString text;
    QTextStream out(&text, QIODevice::WriteOnly);
    auto mark = [emittedKeys](const QString& key) {
        if (emittedKeys != nullptr && !key.trimmed().isEmpty()) emittedKeys->insert(key.trimmed().toLower());
    };

    const QString sourcePath = resolveXcFdfSourcePath(settings, QDir(QDir::currentPath()));
    if (sourcePath.isEmpty()) {
        out << standaloneScalarOverrideLine(settings, QStringLiteral("xc.functional")) << "\n";
        out << standaloneScalarOverrideLine(settings, QStringLiteral("xc.authors")) << "\n";
        out << standaloneScalarOverrideLine(settings, QStringLiteral("MeshCutoff")) << "\n";
        out << standaloneChemicalSpeciesBlock(settings);
        mark(QStringLiteral("xc.functional"));
        mark(QStringLiteral("xc.authors"));
        mark(QStringLiteral("meshcutoff"));
        mark(QStringLiteral("chemicalspecieslabel"));
        if (errors != nullptr) {
            *errors << QStringLiteral("standalone SIESTA mode requires bundled/profile xc.fdf for PAO.Basis, PS.lmax, and SyntheticAtoms.");
        }
        return text;
    }

    QFile file(sourcePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errors != nullptr) *errors << QStringLiteral("standalone SIESTA mode could not read xc.fdf: %1").arg(sourcePath);
        return text;
    }
    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);

    const QRegularExpression blockStart(QStringLiteral("^%\\s*block\\s+(.+)$"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression blockEnd(QStringLiteral("^%\\s*endblock\\s+(.+)$"), QRegularExpression::CaseInsensitiveOption);
    bool skippingOriginalBlock = false;
    QString activeKeptBlock;
    QSet<QString> replacedScalars;
    const QStringList lines = in.readAll().split(QLatin1Char('\n'));
    for (const QString& rawLine : lines) {
        const QString line = stripStandaloneFdfComment(rawLine);
        if (line.isEmpty()) continue;

        const auto startMatch = blockStart.match(line);
        if (startMatch.hasMatch()) {
            const QString blockName = startMatch.captured(1).trimmed();
            const QString blockKey = blockName.toLower();
            if (blockKey == QStringLiteral("chemicalspecieslabel")) {
                out << standaloneChemicalSpeciesBlock(settings);
                mark(blockKey);
                skippingOriginalBlock = true;
                activeKeptBlock.clear();
                continue;
            }
            out << "%block " << blockName << "\n";
            mark(blockKey);
            activeKeptBlock = blockKey;
            continue;
        }

        const auto endMatch = blockEnd.match(line);
        if (endMatch.hasMatch()) {
            if (skippingOriginalBlock) {
                skippingOriginalBlock = false;
                continue;
            }
            const QString blockName = endMatch.captured(1).trimmed();
            out << "%endblock " << blockName << "\n";
            activeKeptBlock.clear();
            continue;
        }

        if (skippingOriginalBlock) continue;
        if (!activeKeptBlock.isEmpty()) {
            out << line << "\n";
            continue;
        }

        const QString key = line.section(QRegularExpression("\\s+"), 0, 0).trimmed();
        const QString keyLower = key.toLower();
        const QString overrideLine = standaloneScalarOverrideLine(settings, key);
        if (!overrideLine.isEmpty()) {
            if (!replacedScalars.contains(keyLower)) out << overrideLine << "\n";
            replacedScalars.insert(keyLower);
            mark(keyLower);
            continue;
        }
        out << line << "\n";
        mark(keyLower);
    }

    if (errors != nullptr && emittedKeys != nullptr) {
        if (!emittedKeys->contains(QStringLiteral("pao.basis"))) *errors << QStringLiteral("standalone SIESTA mode requires PAO.Basis from xc.fdf/profile.");
        if (!emittedKeys->contains(QStringLiteral("ps.lmax"))) *errors << QStringLiteral("standalone SIESTA mode requires PS.lmax from xc.fdf/profile.");
        if (!emittedKeys->contains(QStringLiteral("syntheticatoms"))) *errors << QStringLiteral("standalone SIESTA mode requires SyntheticAtoms from xc.fdf/profile.");
    }
    return text;
}

bool sameText(const QString& a, const QString& b) {
    return a.compare(b, Qt::CaseInsensitive) == 0;
}

const DftSiestaSpecies* siestaSpeciesForAtom(const NativeAtom& atom, const DftHydrogenAssignment* h, const DftSettings& settings) {
    const bool hydrogen = isH(atom);
    const DftHydrogenRole role = h != nullptr ? h->selectedRole : DftHydrogenRole::OrdinaryHydrogen;
    const QString roleKey = dftHydrogenRoleKey(role);
    const QString fallbackLabel = hydrogen ? siestaSpeciesForRole(role) : atom.element;
    if (hydrogen) {
        for (const auto& sp : settings.siestaSpecies) {
            if (sameText(sp.element, QStringLiteral("H")) && sameText(sp.role, roleKey)) return &sp;
        }
        for (const auto& sp : settings.siestaSpecies) {
            if (sameText(sp.element, QStringLiteral("H")) && sameText(sp.label, fallbackLabel)) return &sp;
        }
        for (const auto& sp : settings.siestaSpecies) {
            if (sameText(sp.element, QStringLiteral("H")) && sameText(sp.role, QStringLiteral("ordinary_hydrogen"))) return &sp;
        }
        for (const auto& sp : settings.siestaSpecies) {
            if (sameText(sp.element, QStringLiteral("H")) && sameText(sp.label, QStringLiteral("H"))) return &sp;
        }
    } else {
        for (const auto& sp : settings.siestaSpecies) {
            if (sameText(sp.element, atom.element)) return &sp;
        }
        for (const auto& sp : settings.siestaSpecies) {
            if (sameText(sp.label, atom.element)) return &sp;
        }
    }
    return nullptr;
}

int siestaSpeciesIndexForAtom(const NativeAtom& atom, const DftHydrogenAssignment* h, const DftSettings& settings) {
    if (const auto* sp = siestaSpeciesForAtom(atom, h, settings)) return sp->index;
    if (atom.element.compare("Ga", Qt::CaseInsensitive) == 0) return 3;
    if (atom.element.compare("N", Qt::CaseInsensitive) == 0) return 2;
    if (atom.element.compare("H", Qt::CaseInsensitive) == 0) return h != nullptr ? h->siestaSpeciesIndex : 4;
    return 0;
}

QString siestaText(const StructureData& structure, const DftSettings& settings, DftGeneratedInput* generated) {
    QString text;
    QTextStream out(&text, QIODevice::WriteOnly);
    const QString target = settings.targetName;
    const int coordinatePrecision = outputPrecision(settings, QStringLiteral("coordinate_precision"));
    const int cellPrecision = outputPrecision(settings, QStringLiteral("cell_precision"));
    for (const auto& sp : settings.siestaSpecies) {
        const QString psf = siestaPsfReferenceForSpecies(settings, sp);
        if (!psf.isEmpty()) addUnique(&generated->requiredCompanionFiles, psf);
    }
    out << "SystemName " << param(settings, "siesta.general.SystemName", target) << "\n";
    out << "SystemLabel " << param(settings, "siesta.general.SystemLabel", target) << "\n";
    out << "NumberOfAtoms " << structure.atoms.size() << "\n";
    QSet<QString> standaloneEmittedKeys;
    if (!settings.includeXcFdf && settings.standaloneInline) out << writeStandaloneSiestaBlocks(settings, &generated->errors, &standaloneEmittedKeys);
    out << "LatticeConstant 1.0 Ang\n";
    out << "%block LatticeVectors\n";
    for (const auto& v : structure.cellVectors) out << vecLine(v, cellPrecision) << "\n";
    out << "%endblock LatticeVectors\n";
    out << "AtomicCoordinatesFormat Ang\n";
    out << "%block AtomicCoordinatesAndAtomicSpecies\n";
    for (int i = 0; i < static_cast<int>(structure.atoms.size()); ++i) {
        const auto& atom = structure.atoms[static_cast<std::size_t>(i)];
        const auto* h = assignmentForAtom(settings.hydrogenAssignments, i);
        const int sp = siestaSpeciesIndexForAtom(atom, h, settings);
        if (sp <= 0) generated->errors << QStringLiteral("SIESTA species未割当: atom %1 %2").arg(i + 1).arg(atom.element);
        out << "  " << fixedF(atom.cartesian.x(), coordinatePrecision) << " "
            << fixedF(atom.cartesian.y(), coordinatePrecision) << " "
            << fixedF(atom.cartesian.z(), coordinatePrecision) << " " << sp << "\n";
    }
    out << "%endblock AtomicCoordinatesAndAtomicSpecies\n";
    out << "NetCharge " << param(settings, "siesta.charge_spin.NetCharge", "0.0") << "\n";
    out << "Spin " << param(settings, "siesta.charge_spin.Spin", "none") << "\n";
    const QString spinFix = param(settings, "siesta.charge_spin.Spin.Fix");
    const QString spinTotal = param(settings, "siesta.charge_spin.Spin.Total");
    if (!spinFix.isEmpty()) out << "Spin.Fix " << spinFix << "\n";
    if (!spinTotal.isEmpty() && spinFix.compare("T", Qt::CaseInsensitive) == 0) out << "Spin.Total " << spinTotal << "\n";
    if (settings.calculationMode == "ga_atom_reference") {
        out << "%block DM.InitSpin\n";
        out << "  1 1.0\n";
        out << "%endblock DM.InitSpin\n";
    }
    const QStringList scfKeys = {"SolutionMethod", "MaxSCFIterations", "SCF.MustConverge", "DM.Tolerance",
                                 "DM.MixingWeight", "DM.NumberPulay", "ElectronicTemperature", "OccupationFunction"};
    for (const auto& key : scfKeys) {
        const QString id = QStringLiteral("siesta.scf.") + key;
        const QString value = param(settings, id);
        if (value.trimmed().isEmpty()) continue;
        out << key << " " << value.trimmed() << "\n";
    }
    const QStringList mdKeys = {"MD.TypeOfRun", "MD.VariableCell", "MD.Steps", "MD.MaxForceTol", "MD.MaxDispl", "MD.MaxStressTol", "GeometryMustConverge"};
    for (const auto& key : mdKeys) {
        const QString id = QStringLiteral("siesta.relaxation.") + key;
        const QString value = param(settings, id);
        if (value.isEmpty()) continue;
        out << key << " " << value;
        if (key == "MD.MaxForceTol") out << " eV/Ang";
        if (key == "MD.MaxDispl") out << " Ang";
        if (key == "MD.MaxStressTol") out << " GPa";
        out << "\n";
    }
    const FixedAtomPlan fixedPlan = buildFixedAtomPlan(structure, settings);
    QVector<int> fixedAtoms;
    for (int i = 0; i < static_cast<int>(structure.atoms.size()); ++i) {
        const FixedAtomDecision decision = i < fixedPlan.atoms.size() ? fixedPlan.atoms.at(i) : FixedAtomDecision{};
        if (decisionHasFixedAxis(decision)) {
            fixedAtoms << (i + 1);
            if (!(decision.fixed[0] && decision.fixed[1] && decision.fixed[2])) {
                generated->warnings << QStringLiteral("SIESTA partial constraint rounded to atom constraint: atom %1").arg(i + 1);
            }
        }
    }
    if (!fixedAtoms.isEmpty()) {
        out << "%block Geometry.Constraints\n";
        QStringList atomIds;
        for (int id : fixedAtoms) atomIds << QString::number(id);
        out << "  atom " << atomIds.join(QLatin1Char(' ')) << "\n";
        out << "%endblock Geometry.Constraints\n";
    } else if (settings.calculationMode.contains("slab")) {
        generated->warnings << QStringLiteral("slab固定原子ゼロ");
    }
    const QStringList kg = param(settings, "siesta.kpoints.kgrid", "3 3 1").split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    const QString k1 = kg.value(0, "3");
    const QString k2 = kg.value(1, "3");
    const QString k3 = kg.value(2, "1");
    out << "%block kgrid.MonkhorstPack\n";
    out << "  " << k1 << " 0 0 0.0\n";
    out << "  0 " << k2 << " 0 0.0\n";
    out << "  0 0 " << k3 << " 0.0\n";
    out << "%endblock kgrid.MonkhorstPack\n";
    QSet<QString> emittedRaw;
    const QSet<QString> generatedStandaloneBlocks = {
        QStringLiteral("chemicalspecieslabel"),
        QStringLiteral("syntheticatoms"),
    };
    const QSet<QString> generatedScalarKeys = {
        QStringLiteral("systemname"), QStringLiteral("systemlabel"), QStringLiteral("numberofatoms"),
        QStringLiteral("latticeconstant"), QStringLiteral("atomiccoordinatesformat"),
        QStringLiteral("netcharge"), QStringLiteral("spin"), QStringLiteral("spin.fix"), QStringLiteral("spin.total"),
        QStringLiteral("solutionmethod"), QStringLiteral("maxscfiterations"), QStringLiteral("scf.mustconverge"),
        QStringLiteral("dm.tolerance"), QStringLiteral("dm.mixingweight"), QStringLiteral("dm.numberpulay"),
        QStringLiteral("electronictemperature"), QStringLiteral("occupationfunction"),
        QStringLiteral("md.typeofrun"), QStringLiteral("md.variablecell"), QStringLiteral("md.steps"),
        QStringLiteral("md.maxforcetol"), QStringLiteral("md.maxdispl"), QStringLiteral("md.maxstresstol"),
        QStringLiteral("geometrymustconverge"), QStringLiteral("meshcutoff"),
        QStringLiteral("xc.functional"), QStringLiteral("xc.authors"),
    };
    const QSet<QString> includeXcBlocks = {
        QStringLiteral("chemicalspecieslabel"),
        QStringLiteral("pao.basis"),
        QStringLiteral("ps.lmax"),
        QStringLiteral("syntheticatoms"),
    };
    const QSet<QString> includeXcScalarKeys = {
        QStringLiteral("xc.functional"), QStringLiteral("xc.authors"),
        QStringLiteral("pao.oldstylepolorbs"), QStringLiteral("meshsubdivisions"),
        QStringLiteral("numberofspecies"), QStringLiteral("pao.basissize"),
        QStringLiteral("pao.energyshift"), QStringLiteral("meshcutoff"),
        QStringLiteral("pao.splitnorm"), QStringLiteral("pao.splitnormh"),
        QStringLiteral("pao.softdefault"), QStringLiteral("pao.newsplitcode"),
        QStringLiteral("pao.basistype"), QStringLiteral("ps.lmax"),
    };
    const QSet<QString> generatedBlockKeys = {
        QStringLiteral("latticevectors"),
        QStringLiteral("atomiccoordinatesandatomicspecies"),
        QStringLiteral("geometryconstraints"),
        QStringLiteral("kgridmonkhorstpack"),
    };
    const QSet<QString> generatedScalarKeyCanon = normalizedSet(generatedScalarKeys);
    const QSet<QString> includeXcBlockCanon = normalizedSet(includeXcBlocks);
    const QSet<QString> includeXcScalarCanon = normalizedSet(includeXcScalarKeys);
    const QSet<QString> standaloneCanon = normalizedSet(standaloneEmittedKeys);
    QSet<QString> emittedCatalog;
    for (auto it = settings.parameters.constBegin(); it != settings.parameters.constEnd(); ++it) {
        const auto& entry = it.value();
        if (entry.spec.code != DftCode::Siesta || !siestaCatalogParameter(entry.spec) || !entry.enabled) continue;
        const QString key = entry.spec.key.trimmed();
        const QString value = entry.value.trimmed();
        if (key.isEmpty() || value.isEmpty()) continue;
        const bool block = entry.spec.outputFormat == QStringLiteral("block");
        const QString canon = normalizedFdfLabel(key);
        const QString signature = QStringLiteral("%1:%2").arg(block ? QStringLiteral("block") : QStringLiteral("scalar"), canon);
        if (emittedCatalog.contains(signature)) {
            generated->warnings << QStringLiteral("SIESTA catalog parameter duplicate and was skipped: %1").arg(key);
            continue;
        }
        emittedCatalog.insert(signature);
        if (block && generatedBlockKeys.contains(canon)) {
            generated->warnings << QStringLiteral("SIESTA catalog parameter duplicates generated block and was skipped: %1").arg(key);
            continue;
        }
        if (!block && generatedScalarKeyCanon.contains(canon)) {
            generated->warnings << QStringLiteral("SIESTA catalog parameter duplicates generated key and was skipped: %1").arg(key);
            continue;
        }
        if (settings.includeXcFdf &&
            ((block && includeXcBlockCanon.contains(canon)) || (!block && includeXcScalarCanon.contains(canon)))) {
            generated->warnings << QStringLiteral("SIESTA catalog parameter duplicates xc.fdf include key/block and was skipped: %1").arg(key);
            continue;
        }
        if (settings.standaloneInline && standaloneCanon.contains(canon)) {
            generated->warnings << QStringLiteral("SIESTA catalog parameter duplicates standalone inlined key/block and was skipped: %1").arg(key);
            continue;
        }
        if (block) {
            out << "%block " << key << "\n" << value << "\n%endblock " << key << "\n";
        } else {
            out << key << " " << value;
            if (!entry.spec.unit.isEmpty()) out << " " << entry.spec.unit;
            out << "\n";
        }
    }
    for (const auto& raw : settings.rawParameters) {
        if (!raw.enabled || raw.code != DftCode::Siesta) continue;
        const QString rawKey = raw.key.trimmed().toLower();
        const QString signature = QStringLiteral("%1:%2").arg(raw.blockOrCard ? QStringLiteral("block") : QStringLiteral("scalar"), rawKey);
        if (emittedRaw.contains(signature)) {
            generated->warnings << QStringLiteral("Raw Additional Parameter duplicate: %1").arg(raw.key);
        }
        emittedRaw.insert(signature);
        if (settings.includeXcFdf &&
            ((raw.blockOrCard && includeXcBlocks.contains(rawKey)) ||
             (!raw.blockOrCard && includeXcScalarKeys.contains(rawKey)))) {
            generated->warnings << QStringLiteral("Raw Additional Parameter duplicates xc.fdf include key/block and was skipped: %1").arg(raw.key);
            continue;
        }
        if (settings.standaloneInline && standaloneEmittedKeys.contains(rawKey)) {
            generated->warnings << QStringLiteral("Raw Additional Parameter duplicates standalone inlined SIESTA key/block and was skipped: %1").arg(raw.key);
            continue;
        }
        if (raw.blockOrCard && settings.standaloneInline && generatedStandaloneBlocks.contains(rawKey)) {
            generated->warnings << QStringLiteral("Raw Additional Parameter duplicates generated SIESTA block and was skipped: %1").arg(raw.key);
            continue;
        }
        if (!raw.blockOrCard && generatedScalarKeys.contains(rawKey)) {
            generated->warnings << QStringLiteral("Raw Additional Parameter duplicates generated SIESTA key: %1").arg(raw.key);
        }
        if (raw.blockOrCard) {
            out << "%block " << raw.key << "\n" << raw.value.trimmed() << "\n%endblock " << raw.key << "\n";
        } else {
            out << raw.key << " " << raw.value;
            if (!raw.unit.isEmpty()) out << " " << raw.unit;
            out << "\n";
        }
    }
    if (settings.includeXcFdf) {
        if (settings.xcFdfPath.trimmed().isEmpty()) generated->errors << QStringLiteral("include modeでxc.fdf path未指定");
        const QString includeRef = siestaXcFdfIncludeReference(settings);
        out << "%include " << includeRef << "\n";
        addUnique(&generated->requiredCompanionFiles, includeRef);
    }
    return text;
}

void validateSiestaGenerated(const StructureData& structure, const DftSettings& settings, DftGeneratedInput* generated) {
    if (generated == nullptr || settings.code != DftCode::Siesta) return;
    const QString& text = generated->primaryText;
    const bool slab = settings.calculationMode.contains(QStringLiteral("slab"), Qt::CaseInsensitive);
    const bool includeMode = settings.includeXcFdf;
    const bool standaloneMode = !settings.includeXcFdf && settings.standaloneInline;
    const QVector<int> constraints = constraintAtoms(text);

    if (includeMode && !hasIncludeXc(text)) {
        generated->errors << QStringLiteral("include modeですが%include xc.fdfが出力されていません");
    }
    if (includeMode && settings.xcFdfPath.trimmed().isEmpty()) {
        generated->errors << QStringLiteral("include modeですがxc.fdf pathが空です");
    }
    if (standaloneMode) {
        if (!hasBlock(text, QStringLiteral("ChemicalSpeciesLabel"))) generated->errors << QStringLiteral("standalone modeですがChemicalSpeciesLabelがありません");
        if (!hasBlock(text, QStringLiteral("PAO.Basis"))) generated->errors << QStringLiteral("standalone modeですがPAO.Basisがありません");
        if (!hasBlock(text, QStringLiteral("SyntheticAtoms"))) generated->errors << QStringLiteral("standalone modeですがSyntheticAtomsがありません");
    }
    if (!includeMode && !standaloneMode) {
        generated->errors << QStringLiteral("SIESTA basis sourceが未指定です（%include xc.fdfまたはstandalone inline blocksが必要）");
    }
    if (isNeutralSiesta(settings)) {
        if (hasScalarKey(text, QStringLiteral("Spin.Fix"))) generated->errors << QStringLiteral("neutral presetでSpin.Fixが出力されています");
        if (hasScalarKey(text, QStringLiteral("Spin.Total"))) generated->errors << QStringLiteral("neutral presetでSpin.Totalが出力されています");
        if (hasBlock(text, QStringLiteral("DM.InitSpin"))) generated->errors << QStringLiteral("neutral presetでDM.InitSpinが出力されています");
    }
    const bool slabNeedsConstraints = slab && static_cast<int>(structure.atoms.size()) >= 10;
    if (slabNeedsConstraints && constraints.isEmpty()) {
        generated->errors << QStringLiteral("slabですがGeometry.Constraintsが空です");
    }
    for (const auto& h : settings.hydrogenAssignments) {
        if (h.selectedRole == DftHydrogenRole::BottomPseudoHNTerminated075) {
            if (h.siestaSpecies.compare(QStringLiteral("H-0.750"), Qt::CaseInsensitive) != 0 || h.siestaSpeciesIndex != 1) {
                generated->errors << QStringLiteral("bottom pseudo-H atom %1 がH-0.750 species index 1ではありません").arg(h.atomIndex + 1);
            }
            if (!constraints.contains(h.atomIndex + 1)) {
                generated->errors << QStringLiteral("bottom pseudo-H atom %1 がGeometry.Constraintsで固定されていません").arg(h.atomIndex + 1);
            }
        }
        if (h.inferredRole == DftHydrogenRole::BottomPseudoHNTerminated075 &&
            h.selectedRole == DftHydrogenRole::OrdinaryHydrogen &&
            static_cast<int>(structure.atoms.size()) >= 10) {
            generated->errors << QStringLiteral("bottom H atom %1 がordinary Hとして出力されています（H-0.750であるべき候補）").arg(h.atomIndex + 1);
        }
    }
    if (static_cast<int>(structure.atoms.size()) == 61) {
        const QVector<int> expected = expectedSevenLayerFixedAtoms();
        if (!containsAll(constraints, expected)) {
            generated->errors << QStringLiteral("7layer slabの固定原子が期待値と一致しません。期待: %1")
                                     .arg([&expected]() {
                                         QStringList parts;
                                         for (int v : expected) parts << QString::number(v);
                                         return parts.join(QLatin1Char(' '));
                                     }());
        }
    }
    const QRegularExpression atomCountRe(QStringLiteral("(^|\\n)\\s*NumberOfAtoms\\s+(\\d+)\\b"), QRegularExpression::CaseInsensitiveOption);
    const auto atomCountMatch = atomCountRe.match(text);
    if (!atomCountMatch.hasMatch() || atomCountMatch.captured(2).toInt() != static_cast<int>(structure.atoms.size())) {
        generated->errors << QStringLiteral("NumberOfAtomsが構造の原子数と一致しません");
    }
    bool maxScfOk = false;
    const double maxScf = scalarDouble(settings, QStringLiteral("siesta.scf.MaxSCFIterations"), 0.0, &maxScfOk);
    if (!maxScfOk || maxScf < 1000.0) generated->warnings << QStringLiteral("MaxSCFIterationsが1000未満または未設定です");
    if (param(settings, QStringLiteral("siesta.scf.DM.Tolerance")).trimmed().isEmpty()) generated->warnings << QStringLiteral("DM.Toleranceが未設定です");
    if (!hasScalarKey(text, QStringLiteral("MeshCutoff")) && !includeMode) generated->warnings << QStringLiteral("MeshCutoffがmain/includeのどちらにも確認できません");
    const QString label = param(settings, QStringLiteral("siesta.general.SystemLabel"), settings.targetName).trimmed();
    if (label.compare(QStringLiteral("GaN"), Qt::CaseInsensitive) == 0) generated->warnings << QStringLiteral("SystemLabelがGaNのままでtarget-specificではありません");
    if (settingsHasUsefulUnknownSource(settings)) generated->warnings << QStringLiteral("重要パラメータにsource=unknownがあります");
}

QString qeValue(const QString& section, const QString& key, QString value) {
    Q_UNUSED(section);
    value = value.trimmed();
    if (value.isEmpty()) return value;
    const QString lower = value.toLower();
    if (lower == ".true." || lower == ".false." || lower == "true" || lower == "false") {
        return (lower == "true") ? QStringLiteral(".true.") : (lower == "false" ? QStringLiteral(".false.") : value);
    }
    bool numeric = false;
    value.toDouble(&numeric);
    if (numeric || value.contains(QRegularExpression("^[+-]?[0-9.]+[dD][+-]?[0-9]+$"))) return value;
    if (key == "nspin" || key == "ibrav" || key == "nstep" || key == "mixing_ndim" || key == "electron_maxstep") return value;
    QString stripped = value;
    if ((stripped.startsWith('\'') && stripped.endsWith('\'')) || (stripped.startsWith('"') && stripped.endsWith('"'))) return stripped;
    return QStringLiteral("'%1'").arg(stripped);
}

QString pseudoForElement(const QString& element) {
    if (element.compare("Ga", Qt::CaseInsensitive) == 0) return QStringLiteral("Ga.pbe-mt_fhi.UPF");
    if (element.compare("N", Qt::CaseInsensitive) == 0) return QStringLiteral("N.pbe-mt_fhi.UPF");
    if (element.compare("H", Qt::CaseInsensitive) == 0) return QStringLiteral("H.pbe-mt_fhi.UPF");
    return element + QStringLiteral(".UPF");
}

DftQeSpecies fallbackQeSpeciesForAtom(const NativeAtom& atom, const DftHydrogenAssignment* h) {
    DftQeSpecies sp;
    if (atom.element.compare("H", Qt::CaseInsensitive) == 0) {
        const DftHydrogenRole role = h != nullptr ? h->selectedRole : DftHydrogenRole::OrdinaryHydrogen;
        sp.label = qeLabelForRole(role);
        sp.element = "H";
        sp.mass = DftParameterRegistry::defaultAtomicMass(sp.element);
        sp.pseudoFile = qePseudoForRole(role);
        sp.role = dftHydrogenRoleKey(role);
        sp.source = DftParameterSource::ProjectProfile;
        return sp;
    }
    sp.label = atom.element;
    sp.element = atom.element;
    sp.mass = DftParameterRegistry::defaultAtomicMass(sp.element);
    sp.pseudoFile = pseudoForElement(atom.element);
    sp.role = atom.element;
    sp.source = DftParameterSource::ProjectProfile;
    return sp;
}

DftQeSpecies qeSpeciesForAtom(const NativeAtom& atom, const DftHydrogenAssignment* h, const DftSettings& settings) {
    const bool hydrogen = isH(atom);
    const DftHydrogenRole role = h != nullptr ? h->selectedRole : DftHydrogenRole::OrdinaryHydrogen;
    const QString roleKey = dftHydrogenRoleKey(role);
    const QString fallbackLabel = hydrogen ? qeLabelForRole(role) : atom.element;
    if (hydrogen) {
        for (const auto& sp : settings.qeSpecies) {
            if (sameText(sp.element, QStringLiteral("H")) && sameText(sp.role, roleKey)) return sp;
        }
        const bool roleHasProjectAlias = role == DftHydrogenRole::BottomPseudoHNTerminated075 ||
                                         role == DftHydrogenRole::BottomPseudoHIIITerminated125 ||
                                         role == DftHydrogenRole::SurfaceAdsorbedHydrogen;
        if (roleHasProjectAlias) {
            for (const auto& sp : settings.qeSpecies) {
                if (sameText(sp.element, QStringLiteral("H")) && sameText(sp.label, fallbackLabel)) return sp;
            }
        }
        if (role == DftHydrogenRole::UnknownHydrogen) {
            for (const auto& sp : settings.qeSpecies) {
                if (sameText(sp.element, QStringLiteral("H")) && sameText(sp.role, QStringLiteral("ordinary_hydrogen"))) return sp;
            }
        }
    } else {
        for (const auto& sp : settings.qeSpecies) {
            if (sameText(sp.element, atom.element)) return sp;
        }
        for (const auto& sp : settings.qeSpecies) {
            if (sameText(sp.label, atom.element)) return sp;
        }
    }
    return fallbackQeSpeciesForAtom(atom, h);
}

QString qeText(const StructureData& structure, const DftSettings& settings, DftGeneratedInput* generated) {
    const int coordinatePrecision = outputPrecision(settings, QStringLiteral("coordinate_precision"));
    const int cellPrecision = outputPrecision(settings, QStringLiteral("cell_precision"));
    const FixedAtomPlan fixedPlan = buildFixedAtomPlan(structure, settings);
    QMap<QString, DftQeSpecies> speciesByLabel;
    QStringList atomLabels;
    for (int i = 0; i < static_cast<int>(structure.atoms.size()); ++i) {
        const auto& atom = structure.atoms[static_cast<std::size_t>(i)];
        const auto* h = assignmentForAtom(settings.hydrogenAssignments, i);
        const auto sp = qeSpeciesForAtom(atom, h, settings);
        if (speciesByLabel.contains(sp.label) && speciesByLabel.value(sp.label).pseudoFile != sp.pseudoFile) {
            generated->errors << QStringLiteral("QEで同一labelが異なるpseudo_fileを指しています: %1").arg(sp.label);
        }
        speciesByLabel.insert(sp.label, sp);
        atomLabels << sp.label;
    }
    QString text;
    QTextStream out(&text, QIODevice::WriteOnly);
    const QStringList sections = {"CONTROL", "SYSTEM", "ELECTRONS", "IONS", "CELL"};
    QSet<QString> emittedRawNamelistKeys;
    for (const auto& section : sections) {
        bool hasSection = section != "CELL" || param(settings, "qe.CONTROL.calculation", "relax") == "vc-relax";
        QString sectionText;
        QTextStream so(&sectionText, QIODevice::WriteOnly);
        if (section == "SYSTEM") {
            so << "  nat = " << structure.atoms.size() << ",\n";
            so << "  ntyp = " << speciesByLabel.size() << ",\n";
        }
        const QString prefix = QStringLiteral("qe.") + section + QStringLiteral(".");
        for (auto it = settings.parameters.constBegin(); it != settings.parameters.constEnd(); ++it) {
            if (!it.key().startsWith(prefix) || !it.value().enabled || it.value().value.trimmed().isEmpty()) continue;
            const QString key = it.value().spec.key.isEmpty() ? it.key().section('.', -1) : it.value().spec.key;
            if (section == "SYSTEM" && (key == "nat" || key == "ntyp")) continue;
            so << "  " << key << " = " << qeValue(section, key, it.value().value) << ",\n";
            hasSection = true;
        }
        for (const auto& raw : settings.rawParameters) {
            if (!raw.enabled || raw.code != DftCode::QuantumEspresso || raw.namelistOrBlock.compare(section, Qt::CaseInsensitive) != 0) continue;
            const QString signature = section.toUpper() + QStringLiteral(".") + raw.key.trimmed().toLower();
            if (emittedRawNamelistKeys.contains(signature)) {
                generated->warnings << QStringLiteral("Raw Additional Parameter duplicate: %1.%2").arg(section, raw.key);
            }
            emittedRawNamelistKeys.insert(signature);
            const QString id = QStringLiteral("qe.%1.%2").arg(section, raw.key);
            if (settings.parameters.contains(id)) {
                generated->warnings << QStringLiteral("Raw Additional Parameter duplicates generated QE key: %1.%2").arg(section, raw.key);
            }
            so << "  " << raw.key << " = " << qeValue(section, raw.key, raw.value) << ",\n";
            hasSection = true;
        }
        if (hasSection && (section != "CELL" || !sectionText.trimmed().isEmpty())) {
            out << "&" << section << "\n" << sectionText << "/\n";
        }
    }
    out << "CELL_PARAMETERS angstrom\n";
    for (const auto& v : structure.cellVectors) out << vecLine(v, cellPrecision).trimmed() << "\n";
    out << "ATOMIC_SPECIES\n";
    for (auto it = speciesByLabel.constBegin(); it != speciesByLabel.constEnd(); ++it) {
        out << it.value().label << " " << f(it.value().mass) << " " << it.value().pseudoFile << "\n";
    }
    out << "ATOMIC_POSITIONS angstrom\n";
    for (int i = 0; i < static_cast<int>(structure.atoms.size()); ++i) {
        const auto& atom = structure.atoms[static_cast<std::size_t>(i)];
        const FixedAtomDecision decision = i < fixedPlan.atoms.size() ? fixedPlan.atoms.at(i) : FixedAtomDecision{};
        out << atomLabels.value(i) << " " << fixedF(atom.cartesian.x(), coordinatePrecision) << " "
            << fixedF(atom.cartesian.y(), coordinatePrecision) << " "
            << fixedF(atom.cartesian.z(), coordinatePrecision);
        if (decision.fixed[0] && decision.fixed[1] && decision.fixed[2]) {
            out << " 0 0 0";
        } else if (decisionHasFixedAxis(decision)) {
            out << " " << (decision.fixed[0] ? 0 : 1) << " " << (decision.fixed[1] ? 0 : 1) << " " << (decision.fixed[2] ? 0 : 1);
            generated->warnings << QStringLiteral("QE partial fixed flags emitted explicitly: atom %1").arg(i + 1);
        } else if (!settings.qeProjectStyleFixedFlags) {
            out << " 1 1 1";
        }
        out << "\n";
    }
    out << "K_POINTS automatic\n";
    out << param(settings, "qe.K_POINTS.automatic", "3 3 1 0 0 0") << "\n";
    for (const auto& raw : settings.rawParameters) {
        if (!raw.enabled || raw.code != DftCode::QuantumEspresso) continue;
        if (sections.contains(raw.namelistOrBlock, Qt::CaseInsensitive)) continue;
        const QString signature = raw.namelistOrBlock.trimmed().toUpper() + QStringLiteral(".") + raw.key.trimmed().toLower();
        if (emittedRawNamelistKeys.contains(signature)) {
            generated->warnings << QStringLiteral("Raw Additional Parameter duplicate: %1 %2").arg(raw.namelistOrBlock, raw.key);
        }
        emittedRawNamelistKeys.insert(signature);
        out << raw.namelistOrBlock << " " << raw.key << " " << raw.value << "\n";
    }
    return text;
}

QJsonArray hydrogenJson(const DftSettings& settings) {
    QJsonArray arr;
    for (const auto& h : settings.hydrogenAssignments) {
        QJsonObject obj;
        obj.insert("atom_index", h.atomIndex + 1);
        obj.insert("element", "H");
        obj.insert("cartesian", vectorToJson(h.cartesian));
        obj.insert("fractional", vectorToJson(h.fractional));
        obj.insert("nearest_non_h_atom", h.nearestNonHAtomIndex >= 0 ? h.nearestNonHAtomIndex + 1 : 0);
        obj.insert("nearest_non_h_element", h.nearestNonHElement);
        obj.insert("nearest_non_h_distance_ang", h.nearestNonHDistanceAng);
        obj.insert("role", dftHydrogenRoleKey(h.selectedRole));
        obj.insert("inference_source", h.inferenceSource);
        obj.insert("confidence", h.confidence);
        obj.insert("siesta_species", h.siestaSpecies);
        obj.insert("siesta_species_index", h.siestaSpeciesIndex);
        obj.insert("qe_label", h.qeLabel);
        obj.insert("qe_pseudo_file", h.qePseudoFile);
        obj.insert("fixed", h.fixedByRole);
        obj.insert("user_overrode_inference", h.userOverrodeInference);
        arr << obj;
    }
    return arr;
}

QJsonArray scientificChecklistJson(const QVector<DftScientificChecklistItem>& items) {
    QJsonArray arr;
    for (const auto& item : items) {
        QJsonObject obj;
        obj.insert(QStringLiteral("group"), item.group);
        obj.insert(QStringLiteral("item"), item.item);
        obj.insert(QStringLiteral("status"), item.status);
        obj.insert(QStringLiteral("detail"), item.detail);
        arr << obj;
    }
    return arr;
}

QString markdownSummary(const StructureData& structure, const DftSettings& settings, const DftGeneratedInput& generated) {
    QString md;
    QTextStream out(&md, QIODevice::WriteOnly);
    out << "# DFT Input Generation Summary\n\n";
    out << "- Code: " << dftCodeToString(settings.code) << "\n";
    out << "- Version: " << settings.version << "\n";
    out << "- Target: " << settings.targetName << "\n";
    out << "- Mode: " << dftGenerationModeKey(settings.generationMode) << "\n";
    out << "- Atom count: " << structure.atoms.size() << "\n";
    out << "- Atom order preserved: yes\n";
    if (!generated.requiredCompanionFiles.isEmpty()) {
        out << "- Required companion files: " << orderedCompanionFileList(generated.requiredCompanionFiles).join(QStringLiteral(", ")) << "\n";
    }
    if (!generated.errors.isEmpty()) {
        out << "\n## Errors\n";
        for (const auto& e : generated.errors) out << "- " << e << "\n";
    }
    if (!generated.warnings.isEmpty()) {
        out << "\n## Warnings\n";
        for (const auto& w : generated.warnings) out << "- " << w << "\n";
    }
    const QJsonArray checklist = generated.summaryObject.value(QStringLiteral("scientific_checklist")).toArray();
    if (!checklist.isEmpty()) {
        out << "\n## Scientific Checklist\n";
        for (const auto& value : checklist) {
            const QJsonObject obj = value.toObject();
            out << "- " << obj.value(QStringLiteral("status")).toString() << ": "
                << obj.value(QStringLiteral("group")).toString() << " / "
                << obj.value(QStringLiteral("item")).toString();
            const QString detail = obj.value(QStringLiteral("detail")).toString().trimmed();
            if (!detail.isEmpty()) out << " - " << detail;
            out << "\n";
        }
    }
    return md;
}

} // namespace
QString DftInputGenerator::sanitizeTargetName(const QString& input) {
    QString value = input.trimmed();
    value.replace(QRegularExpression("[^A-Za-z0-9_-]"), QStringLiteral("_"));
    value.replace(QRegularExpression("_+"), QStringLiteral("_"));
    value = value.trimmed();
    while (value.startsWith('_')) value.remove(0, 1);
    while (value.endsWith('_')) value.chop(1);
    return value.isEmpty() ? QStringLiteral("target") : value;
}

QVector<DftHydrogenAssignment> DftInputGenerator::inferHydrogenRoles(const StructureData& structure, const DftSettings& settings) {
    QVector<DftHydrogenAssignment> out;
    for (int i = 0; i < static_cast<int>(structure.atoms.size()); ++i) {
        if (!isH(structure.atoms[static_cast<std::size_t>(i)])) continue;
        out << makeAssignment(structure, i, settings);
    }
    return out;
}

DftGeneratedInput DftInputGenerator::generate(const StructureData& structure, DftSettings settings) {
    DftGeneratedInput generated;
    generated.fileExtension = settings.code == DftCode::Siesta ? QStringLiteral(".fdf") : QStringLiteral(".in");
    settings.targetName = sanitizeTargetName(settings.targetName);
    if (structure.atoms.empty()) generated.errors << QStringLiteral("structureなし");
    if (settings.targetName.isEmpty()) generated.errors << QStringLiteral("target名不正");
    if (settings.hydrogenAssignments.isEmpty()) settings.hydrogenAssignments = inferHydrogenRoles(structure, settings);
    for (auto& h : settings.hydrogenAssignments) {
        const bool validAtomIndex = h.atomIndex >= 0 && h.atomIndex < static_cast<int>(structure.atoms.size());
        const NativeAtom* atom = validAtomIndex ? &structure.atoms[static_cast<std::size_t>(h.atomIndex)] : nullptr;
        if (atom != nullptr) {
            if (const auto* siestaSp = siestaSpeciesForAtom(*atom, &h, settings)) {
                h.siestaSpecies = siestaSp->label;
                h.siestaSpeciesIndex = siestaSp->index;
            } else {
                h.siestaSpecies = siestaSpeciesForRole(h.selectedRole);
                h.siestaSpeciesIndex = siestaSpeciesIndexForRole(h.selectedRole);
            }
            const auto qeSp = qeSpeciesForAtom(*atom, &h, settings);
            h.qeLabel = qeSp.label;
            h.qePseudoFile = qeSp.pseudoFile;
        } else {
            h.siestaSpecies = siestaSpeciesForRole(h.selectedRole);
            h.siestaSpeciesIndex = siestaSpeciesIndexForRole(h.selectedRole);
            h.qeLabel = qeLabelForRole(h.selectedRole);
            h.qePseudoFile = qePseudoForRole(h.selectedRole);
        }
        h.fixedByRole = roleFixed(h.selectedRole) || (settings.calculationMode == "h2_reference" && h.atomIndex == 0);
        if (h.selectedRole == DftHydrogenRole::UnknownHydrogen && !settings.allowUnknownHydrogen) {
            generated.errors << QStringLiteral("HydrogenRole未確定Hがあります: atom %1").arg(h.atomIndex + 1);
        } else if (h.selectedRole == DftHydrogenRole::UnknownHydrogen) {
            generated.warnings << QStringLiteral("unknown_hydrogenを明示許可して出力します: atom %1").arg(h.atomIndex + 1);
        }
        if ((h.selectedRole == DftHydrogenRole::MoleculeH2Hydrogen || h.selectedRole == DftHydrogenRole::SurfaceAdsorbedHydrogen) &&
            (h.siestaSpecies == "H-0.750" || h.siestaSpecies == "H-1.250")) {
            generated.errors << QStringLiteral("H2/通常Hにpseudo-H speciesが使われています: atom %1").arg(h.atomIndex + 1);
        }
        if (h.userOverrodeInference) {
            generated.warnings << QStringLiteral("HydrogenRole manual override: atom %1 %2 -> %3")
                                      .arg(h.atomIndex + 1)
                                      .arg(dftHydrogenRoleKey(h.inferredRole), dftHydrogenRoleKey(h.selectedRole));
        }
        if (h.confidence == "low") generated.warnings << QStringLiteral("HydrogenRole inference confidence low: atom %1").arg(h.atomIndex + 1);
    }
    if (settings.code == DftCode::Siesta) {
        generated.primaryText = siestaText(structure, settings, &generated);
        validateSiestaGenerated(structure, settings, &generated);
    } else {
        generated.primaryText = qeText(structure, settings, &generated);
    }
    QStringList commentLines;
    if (!hasNoExplanatoryComments(generated.primaryText, settings.code, &commentLines)) {
        generated.errors << QStringLiteral("generated file contains comments: %1").arg(commentLines.join(QStringLiteral(", ")));
    }
    QJsonObject root = summaryBase(structure, settings, generated);
    root.insert(QStringLiteral("hydrogen_roles"), hydrogenJson(settings));
    QJsonArray species;
    if (settings.code == DftCode::Siesta) {
        for (const auto& sp : settings.siestaSpecies) {
            QJsonObject obj;
            obj.insert("index", sp.index);
            obj.insert("atomic_number", sp.atomicNumber);
            obj.insert("label", sp.label);
            obj.insert("element", sp.element);
            obj.insert("role", sp.role);
            obj.insert("pseudopotential", siestaPsfForSpecies(sp));
            obj.insert("required_path", siestaPsfReferenceForSpecies(settings, sp));
            species << obj;
        }
        root.insert("species_mapping", species);
    } else {
        QSet<QString> labels;
        for (int i = 0; i < static_cast<int>(structure.atoms.size()); ++i) {
            const auto sp = qeSpeciesForAtom(structure.atoms[static_cast<std::size_t>(i)], assignmentForAtom(settings.hydrogenAssignments, i), settings);
            if (labels.contains(sp.label)) continue;
            labels.insert(sp.label);
            QJsonObject obj;
            obj.insert("label", sp.label);
            obj.insert("element", sp.element);
            obj.insert("mass", sp.mass);
            obj.insert("pseudo_file", sp.pseudoFile);
            obj.insert("role", sp.role);
            obj.insert("source", dftParameterSourceKey(sp.source));
            species << obj;
        }
        root.insert("pseudopotential_mapping", species);
    }
    QJsonObject chargeSpin;
    if (settings.code == DftCode::Siesta) {
        chargeSpin.insert("NetCharge", param(settings, "siesta.charge_spin.NetCharge", "0.0"));
        chargeSpin.insert("Spin", param(settings, "siesta.charge_spin.Spin", "none"));
    } else {
        chargeSpin.insert("tot_charge", param(settings, "qe.SYSTEM.tot_charge"));
        chargeSpin.insert("nspin", param(settings, "qe.SYSTEM.nspin"));
        QJsonObject isolated;
        const QString assumeValue = param(settings, "qe.SYSTEM.assume_isolated");
        isolated.insert(QStringLiteral("enabled"), !assumeValue.trimmed().isEmpty());
        isolated.insert(QStringLiteral("value"), assumeValue);
        const auto assumeIt = settings.parameters.constFind(QStringLiteral("qe.SYSTEM.assume_isolated"));
        isolated.insert(QStringLiteral("source"), assumeIt == settings.parameters.constEnd()
                            ? QStringLiteral("unknown")
                            : dftParameterSourceKey(assumeIt.value().source));
        root.insert(QStringLiteral("assume_isolated"), isolated);
    }
    root.insert("charge_spin", chargeSpin);
    root.insert("kpoints", settings.code == DftCode::Siesta ? param(settings, "siesta.kpoints.kgrid") : param(settings, "qe.K_POINTS.automatic"));
    root.insert(QStringLiteral("scientific_checklist"), scientificChecklistJson(DftInputGenerator::scientificChecklist(structure, settings, generated)));
    generated.summaryObject = root;
    generated.jsonSummary = QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
    generated.markdownSummary = markdownSummary(structure, settings, generated);
    generated.ok = generated.errors.isEmpty();
    return generated;
}

bool DftInputGenerator::writeGeneratedFiles(const QString& outputDirectory, const DftSettings& settings,
                                            const DftGeneratedInput& generated, QString* errorMessage) {
    QDir dir(outputDirectory);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        if (errorMessage) *errorMessage = QStringLiteral("Output directoryを作成できません: %1").arg(outputDirectory);
        return false;
    }
    const QString target = sanitizeTargetName(settings.targetName);
    const QString primaryPath = dir.filePath(target + generated.fileExtension);
    auto writeText = [&](const QString& path, const QString& text) -> bool {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            if (errorMessage) *errorMessage = QStringLiteral("書き込みできません: %1").arg(path);
            return false;
        }
        QTextStream out(&file);
        out.setEncoding(QStringConverter::Utf8);
        out << text;
        return true;
    };

    return writeText(primaryPath, generated.primaryText);
}

QVector<DftScientificChecklistItem> DftInputGenerator::scientificChecklist(const StructureData& structure,
                                                                           const DftSettings& settings,
                                                                           const DftGeneratedInput& generated) {
    QVector<DftScientificChecklistItem> items;
    auto add = [&items](const QString& group, const QString& itemName, const QString& status, const QString& detail) {
        DftScientificChecklistItem item;
        item.group = group;
        item.item = itemName;
        item.status = status;
        item.detail = detail;
        items << item;
    };
    const QString text = generated.primaryText;
    if (settings.code != DftCode::Siesta) {
        add(QStringLiteral("General"), QStringLiteral("SIESTA scientific checklist"), QStringLiteral("WARNING"), QStringLiteral("SIESTA以外の入力です"));
        return items;
    }

    const bool includeOk = settings.includeXcFdf && hasIncludeXc(text);
    const bool standaloneFull = !settings.includeXcFdf &&
        hasBlock(text, QStringLiteral("ChemicalSpeciesLabel")) &&
        hasBlock(text, QStringLiteral("PAO.Basis")) &&
        hasBlock(text, QStringLiteral("SyntheticAtoms")) &&
        (hasBlock(text, QStringLiteral("PS.lmax")) || hasScalarKey(text, QStringLiteral("PS.lmax"))) &&
        hasScalarKey(text, QStringLiteral("MeshCutoff"));
    add(QStringLiteral("Kempisty setup"), QStringLiteral("xc.fdf include or standalone full basis"),
        (includeOk || standaloneFull) ? QStringLiteral("PASS") : QStringLiteral("ERROR"),
        includeOk ? QStringLiteral("%include xc.fdfあり") : (standaloneFull ? QStringLiteral("standalone full basisあり") : QStringLiteral("include/standalone basisが不足")));
    const QString xcAuthors = param(settings, QStringLiteral("siesta.species.xc.authors"));
    add(QStringLiteral("Kempisty setup"), QStringLiteral("PBEJsJrLO available"),
        (includeOk || xcAuthors.compare(QStringLiteral("PBEJsJrLO"), Qt::CaseInsensitive) == 0 || text.contains(QStringLiteral("PBEJsJrLO"), Qt::CaseInsensitive))
            ? QStringLiteral("PASS") : QStringLiteral("WARNING"),
        includeOk ? QStringLiteral("xc.fdf参照で確認") : QStringLiteral("xc.authors=%1").arg(xcAuthors.isEmpty() ? QStringLiteral("unset") : xcAuthors));
    const QString mesh = param(settings, QStringLiteral("siesta.species.MeshCutoff"));
    add(QStringLiteral("Kempisty setup"), QStringLiteral("MeshCutoff 410 Ry available"),
        (mesh.trimmed() == QStringLiteral("410") || text.contains(QStringLiteral("MeshCutoff 410"), Qt::CaseInsensitive))
            ? QStringLiteral("PASS") : (includeOk ? QStringLiteral("PASS") : QStringLiteral("WARNING")),
        includeOk ? QStringLiteral("xc.fdf include / registry=%1 Ry").arg(mesh.isEmpty() ? QStringLiteral("unset") : mesh) : QStringLiteral("main FDF/registry=%1").arg(mesh.isEmpty() ? QStringLiteral("unset") : mesh));
    add(QStringLiteral("Kempisty setup"), QStringLiteral("PAO.Basis available"),
        (includeOk || hasBlock(text, QStringLiteral("PAO.Basis"))) ? QStringLiteral("PASS") : QStringLiteral("ERROR"),
        includeOk ? QStringLiteral("xc.fdf参照") : (hasBlock(text, QStringLiteral("PAO.Basis")) ? QStringLiteral("inline") : QStringLiteral("未確認")));
    add(QStringLiteral("Kempisty setup"), QStringLiteral("PS.lmax H/N/Ga/H-0.750/H-1.250=2"),
        (includeOk || hasBlock(text, QStringLiteral("PS.lmax")) || hasScalarKey(text, QStringLiteral("PS.lmax"))) ? QStringLiteral("PASS") : QStringLiteral("WARNING"),
        includeOk ? QStringLiteral("xc.fdf参照") : QStringLiteral("main FDFで確認"));
    add(QStringLiteral("Kempisty setup"), QStringLiteral("SyntheticAtoms H-0.750/H-1.250 available"),
        (includeOk || hasBlock(text, QStringLiteral("SyntheticAtoms"))) ? QStringLiteral("PASS") : QStringLiteral("ERROR"),
        includeOk ? QStringLiteral("xc.fdf参照") : (hasBlock(text, QStringLiteral("SyntheticAtoms")) ? QStringLiteral("inline") : QStringLiteral("未確認")));

    const double vacuum = topVacuumEstimate(structure);
    add(QStringLiteral("Kangawa/Miyaguchi slab setup"), QStringLiteral("20 Å top vacuum"),
        vacuum >= 19.5 ? QStringLiteral("PASS") : QStringLiteral("WARNING"),
        QStringLiteral("estimated vacuum=%1 Å").arg(vacuum, 0, 'f', 3));
    const QVector<int> constraints = constraintAtoms(text);
    bool bottomHOk = true;
    bool bottomHFixed = true;
    int bottomHCount = 0;
    for (const auto& h : settings.hydrogenAssignments) {
        if (h.selectedRole == DftHydrogenRole::BottomPseudoHNTerminated075) {
            ++bottomHCount;
            bottomHOk = bottomHOk && (h.siestaSpecies.compare(QStringLiteral("H-0.750"), Qt::CaseInsensitive) == 0) && h.siestaSpeciesIndex == 1;
            bottomHFixed = bottomHFixed && constraints.contains(h.atomIndex + 1);
        }
    }
    if (bottomHCount == 0) {
        bottomHOk = false;
        bottomHFixed = false;
    }
    if (static_cast<int>(structure.atoms.size()) == 61) {
        bottomHOk = true;
        bottomHFixed = true;
        for (int index = 57; index < 61; ++index) {
            const auto* h = assignmentForAtom(settings.hydrogenAssignments, index);
            bottomHOk = bottomHOk && h != nullptr &&
                h->selectedRole == DftHydrogenRole::BottomPseudoHNTerminated075 &&
                h->siestaSpecies.compare(QStringLiteral("H-0.750"), Qt::CaseInsensitive) == 0 &&
                h->siestaSpeciesIndex == 1;
            bottomHFixed = bottomHFixed && constraints.contains(index + 1);
        }
    }
    add(QStringLiteral("Kangawa/Miyaguchi slab setup"), QStringLiteral("bottom pseudo-H H-0.750"),
        bottomHOk ? QStringLiteral("PASS") : QStringLiteral("ERROR"),
        QStringLiteral("bottom H candidates=%1").arg(bottomHCount));
    add(QStringLiteral("Kangawa/Miyaguchi slab setup"), QStringLiteral("bottom pseudo-H fixed"),
        bottomHFixed ? QStringLiteral("PASS") : QStringLiteral("ERROR"),
        QStringLiteral("constraints=%1 atoms").arg(constraints.size()));
    const QVector<int> expected = expectedSevenLayerFixedAtoms();
    const bool bottomLayerFixed = static_cast<int>(structure.atoms.size()) == 61
        ? containsAll(constraints, expected)
        : !constraints.isEmpty();
    add(QStringLiteral("Kangawa/Miyaguchi slab setup"), QStringLiteral("bottom Ga-N molecular layer fixed"),
        bottomLayerFixed ? QStringLiteral("PASS") : QStringLiteral("ERROR"),
        static_cast<int>(structure.atoms.size()) == 61 ? QStringLiteral("expected 1 5 9 13 42 46 50 54 58-61") : QStringLiteral("constraints=%1 atoms").arg(constraints.size()));
    const QString kgrid = param(settings, QStringLiteral("siesta.kpoints.kgrid"));
    add(QStringLiteral("Kangawa/Miyaguchi slab setup"), QStringLiteral("kgrid 3 3 1"),
        (kgrid == QStringLiteral("3 3 1") ||
         (text.contains(QStringLiteral("  3 0 0 0.0")) &&
          text.contains(QStringLiteral("  0 3 0 0.0")) &&
          text.contains(QStringLiteral("  0 0 1 0.0"))))
            ? QStringLiteral("PASS") : QStringLiteral("ERROR"),
        QStringLiteral("kgrid=%1").arg(kgrid.isEmpty() ? QStringLiteral("unset") : kgrid));
    const QString variableCell = param(settings, QStringLiteral("siesta.relaxation.MD.VariableCell"));
    add(QStringLiteral("Kangawa/Miyaguchi slab setup"), QStringLiteral("MD.VariableCell F"),
        variableCell.compare(QStringLiteral("F"), Qt::CaseInsensitive) == 0 || text.contains(QStringLiteral("MD.VariableCell F"), Qt::CaseInsensitive)
            ? QStringLiteral("PASS") : QStringLiteral("ERROR"),
        QStringLiteral("MD.VariableCell=%1").arg(variableCell.isEmpty() ? QStringLiteral("unset") : variableCell));

    if (isNeutralSiesta(settings)) {
        add(QStringLiteral("Neutral calculation"), QStringLiteral("NetCharge 0.0"),
            text.contains(QStringLiteral("NetCharge 0.0"), Qt::CaseInsensitive) ? QStringLiteral("PASS") : QStringLiteral("ERROR"),
            param(settings, QStringLiteral("siesta.charge_spin.NetCharge"), QStringLiteral("unset")));
        add(QStringLiteral("Neutral calculation"), QStringLiteral("Spin none"),
            text.contains(QStringLiteral("Spin none"), Qt::CaseInsensitive) ? QStringLiteral("PASS") : QStringLiteral("ERROR"),
            param(settings, QStringLiteral("siesta.charge_spin.Spin"), QStringLiteral("unset")));
        add(QStringLiteral("Neutral calculation"), QStringLiteral("no Spin.Fix/Spin.Total/DM.InitSpin"),
            (!hasScalarKey(text, QStringLiteral("Spin.Fix")) && !hasScalarKey(text, QStringLiteral("Spin.Total")) && !hasBlock(text, QStringLiteral("DM.InitSpin")))
                ? QStringLiteral("PASS") : QStringLiteral("ERROR"),
            QStringLiteral("neutralでは未出力が必要"));
    } else {
        add(QStringLiteral("Neutral calculation"), QStringLiteral("neutral-only checks"),
            QStringLiteral("WARNING"), QStringLiteral("non-neutral modeのためneutral専用チェックは参考表示"));
    }
    return items;
}

QString DftInputGenerator::scientificChecklistText(const QVector<DftScientificChecklistItem>& items) {
    QString text;
    QTextStream out(&text, QIODevice::WriteOnly);
    for (const auto& item : items) {
        out << "[" << item.status << "] " << item.group << " / " << item.item;
        if (!item.detail.trimmed().isEmpty()) out << " - " << item.detail.trimmed();
        out << "\n";
    }
    return text.trimmed();
}

bool DftInputGenerator::hasNoExplanatoryComments(const QString& text, DftCode code, QStringList* offendingLines) {
    const auto lines = text.split('\n');
    for (int i = 0; i < lines.size(); ++i) {
        const QString trimmed = lines.at(i).trimmed();
        bool bad = false;
        if (code == DftCode::Siesta) bad = trimmed.startsWith('#');
        else bad = trimmed.contains('!');
        if (bad) {
            if (offendingLines) *offendingLines << QString::number(i + 1);
            else return false;
        }
    }
    return offendingLines == nullptr || offendingLines->isEmpty();
}

QString DftInputGenerator::structureHash(const StructureData& structure) {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const auto& v : structure.cellVectors) hash.addData(QStringLiteral("%1,%2,%3;").arg(v.x()).arg(v.y()).arg(v.z()).toUtf8());
    for (const auto& atom : structure.atoms) {
        hash.addData(QStringLiteral("%1:%2,%3,%4:%5%6%7;")
                         .arg(atom.element)
                         .arg(atom.cartesian.x()).arg(atom.cartesian.y()).arg(atom.cartesian.z())
                         .arg(atom.movable[0]).arg(atom.movable[1]).arg(atom.movable[2]).toUtf8());
    }
    return QString::fromLatin1(hash.result().toHex());
}
