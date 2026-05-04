#include "SurfaceOperations.h"

#include "ElementStyle.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <QObject>
#include <limits>

namespace {
QVector3D toCartesian(const std::array<QVector3D, 3>& cell, const QVector3D& frac) {
    return cell[0] * frac.x() + cell[1] * frac.y() + cell[2] * frac.z();
}

QVector3D solveFractional(const std::array<QVector3D, 3>& cell, const QVector3D& cart) {
    const QVector3D a = cell[0];
    const QVector3D b = cell[1];
    const QVector3D c = cell[2];
    const float det = QVector3D::dotProduct(a, QVector3D::crossProduct(b, c));
    if (std::abs(det) < 1.0e-8f) {
        return cart;
    }
    return {
        QVector3D::dotProduct(cart, QVector3D::crossProduct(b, c)) / det,
        QVector3D::dotProduct(cart, QVector3D::crossProduct(c, a)) / det,
        QVector3D::dotProduct(cart, QVector3D::crossProduct(a, b)) / det
    };
}

QVector3D safeNormalized(const QVector3D& vector, const QVector3D& fallback = QVector3D(0.0f, 0.0f, 1.0f)) {
    if (vector.lengthSquared() <= 1.0e-8f) {
        return fallback.normalized();
    }
    return vector.normalized();
}

QVector3D cellAxisUnit(const std::array<QVector3D, 3>& cell, int axisIndex) {
    const int idx = std::clamp(axisIndex, 0, 2);
    const QVector3D fallback = idx == 0
        ? QVector3D(1.0f, 0.0f, 0.0f)
        : (idx == 1 ? QVector3D(0.0f, 1.0f, 0.0f) : QVector3D(0.0f, 0.0f, 1.0f));
    return safeNormalized(cell[static_cast<std::size_t>(idx)], fallback);
}

void updateFractionalCoordinates(StructureData& data) {
    for (auto& atom : data.atoms) {
        atom.fractional = solveFractional(data.cellVectors, atom.cartesian);
    }
}

void fitOrVacuumAxis(StructureData& data, int axisIndex, double vacuumAngstrom, int placementMode, double customCenterFraction) {
    if (data.atoms.empty()) {
        return;
    }
    const int idx = std::clamp(axisIndex, 0, 2);
    const QVector3D axis = cellAxisUnit(data.cellVectors, idx);

    float minProj = std::numeric_limits<float>::max();
    float maxProj = std::numeric_limits<float>::lowest();
    for (const auto& atom : data.atoms) {
        const float projection = QVector3D::dotProduct(atom.cartesian, axis);
        minProj = std::min(minProj, projection);
        maxProj = std::max(maxProj, projection);
    }

    const float slabSpan = std::max(0.1f, maxProj - minProj);
    const float vacuum = static_cast<float>(std::max(0.0, vacuumAngstrom));
    const float newLength = std::max(0.1f, slabSpan + vacuum);
    const float freeSpace = std::max(0.0f, newLength - slabSpan);

    float targetMin = 0.0f;
    switch (placementMode) {
    case 1:  // centered
        targetMin = freeSpace * 0.5f;
        break;
    case 2:  // negative side
        targetMin = freeSpace;
        break;
    case 3: {  // custom slab-center fraction in the new cell
        const float fraction = static_cast<float>(std::clamp(customCenterFraction, 0.0, 1.0));
        targetMin = fraction * newLength - slabSpan * 0.5f;
        targetMin = std::clamp(targetMin, 0.0f, freeSpace);
        break;
    }
    case 0:  // positive side
    default:
        targetMin = 0.0f;
        break;
    }

    const QVector3D shift = axis * (targetMin - minProj);
    for (auto& atom : data.atoms) {
        atom.cartesian += shift;
    }
    data.cellVectors[static_cast<std::size_t>(idx)] = axis * newLength;
    updateFractionalCoordinates(data);
}

std::array<QVector3D, 3> orthonormalFrameFromDirection(const QVector3D& direction) {
    QVector3D normal = safeNormalized(direction);
    QVector3D helper = std::abs(QVector3D::dotProduct(normal, QVector3D(1.0f, 0.0f, 0.0f))) < 0.92f
        ? QVector3D(1.0f, 0.0f, 0.0f)
        : QVector3D(0.0f, 1.0f, 0.0f);
    QVector3D u = QVector3D::crossProduct(helper, normal).normalized();
    if (u.lengthSquared() <= 1.0e-8f) {
        helper = QVector3D(0.0f, 0.0f, 1.0f);
        u = QVector3D::crossProduct(helper, normal).normalized();
    }
    QVector3D v = QVector3D::crossProduct(normal, u).normalized();
    return {u, v, normal};
}

struct SurfaceLayerMasks {
    std::vector<int> topIndices;
    std::vector<int> bottomIndices;
    QVector3D normal;
};

const NativeAtom* findAtomById(const StructureData& source, int atomId) {
    for (const auto& atom : source.atoms) {
        if (atom.atomId == atomId) {
            return &atom;
        }
    }
    return nullptr;
}

QString normalizedMode(QString mode) {
    return mode.trimmed().toLower();
}

int requiredSelectionCount(const QString& mode) {
    const auto normalized = normalizedMode(mode);
    if (normalized == "pair_midpoint" || normalized == "pair_fraction") {
        return 2;
    }
    if (normalized == "triple_centroid" || normalized == "triple_weighted") {
        return 3;
    }
    if (normalized == "multi_centroid" || normalized == "multi_weighted") {
        return 1;
    }
    if (normalized == "multi_plane_normal") {
        return 3;
    }
    return 1;
}

int selectionCountForRule(const SurfacePlacementRule& rule) {
    return rule.selectionCount > 0 ? rule.selectionCount : requiredSelectionCount(rule.mode);
}

QVector3D placementAnchor(
    const std::vector<const NativeAtom*>& references,
    const SurfacePlacementRule& rule,
    QString* errorMessage)
{
    const auto mode = normalizedMode(rule.mode);
    if (mode == "single_above" || mode == "single_below") {
        return references.front()->cartesian;
    }
    if (mode == "pair_midpoint") {
        return (references[0]->cartesian + references[1]->cartesian) * 0.5f;
    }
    if (mode == "pair_fraction") {
        const double fraction = std::clamp(rule.fraction, 0.0, 1.0);
        return references[0]->cartesian * static_cast<float>(1.0 - fraction)
            + references[1]->cartesian * static_cast<float>(fraction);
    }
    if (mode == "triple_centroid") {
        return (references[0]->cartesian + references[1]->cartesian + references[2]->cartesian) / 3.0f;
    }
    if (mode == "triple_weighted") {
        const double w0 = rule.weights[0];
        const double w1 = rule.weights[1];
        const double w2 = rule.weights[2];
        const double sum = w0 + w1 + w2;
        if (std::abs(sum) < 1.0e-12) {
            if (errorMessage != nullptr) {
                *errorMessage = QObject::tr("Preset weights must not all be zero.");
            }
            return {};
        }
        const float inv = static_cast<float>(1.0 / sum);
        return references[0]->cartesian * static_cast<float>(w0) * inv
            + references[1]->cartesian * static_cast<float>(w1) * inv
            + references[2]->cartesian * static_cast<float>(w2) * inv;
    }
    if (mode == "multi_centroid" || mode == "multi_plane_normal") {
        QVector3D sum;
        for (const auto* atom : references) {
            sum += atom->cartesian;
        }
        return sum / static_cast<float>(references.size());
    }
    if (mode == "multi_weighted") {
        QVector3D sum;
        double weightSum = 0.0;
        for (std::size_t i = 0; i < references.size(); ++i) {
            const double weight = i < rule.weights.size() ? rule.weights[i] : 1.0;
            sum += references[i]->cartesian * static_cast<float>(weight);
            weightSum += weight;
        }
        if (std::abs(weightSum) < 1.0e-12) {
            if (errorMessage != nullptr) {
                *errorMessage = QObject::tr("Preset weights must not all be zero.");
            }
            return {};
        }
        return sum / static_cast<float>(weightSum);
    }
    if (errorMessage != nullptr) {
        *errorMessage = QObject::tr("Unknown placement mode: %1").arg(rule.mode);
    }
    return {};
}

QVector3D surfaceNormalImpl(const StructureData& source) {
    const QVector3D normal = QVector3D::crossProduct(source.cellVectors[0], source.cellVectors[1]);
    if (normal.lengthSquared() <= 1.0e-8f) {
        return QVector3D(0.0f, 0.0f, 1.0f);
    }
    return normal.normalized();
}

QVector3D tiltedPlacementNormal(
    const StructureData& source,
    const std::vector<const NativeAtom*>& references,
    const QVector3D& baseNormal,
    double tiltDegrees)
{
    const double clampedDegrees = std::clamp(tiltDegrees, -89.0, 89.0);
    if (std::abs(clampedDegrees) <= 1.0e-8) {
        return safeNormalized(baseNormal);
    }

    const QVector3D normal = safeNormalized(baseNormal);
    QVector3D tangent;
    if (references.size() >= 2) {
        tangent = references[1]->cartesian - references[0]->cartesian;
    }
    if (tangent.lengthSquared() <= 1.0e-8f) {
        tangent = source.cellVectors[0];
    }
    tangent -= QVector3D::dotProduct(tangent, normal) * normal;
    if (tangent.lengthSquared() <= 1.0e-8f) {
        tangent = source.cellVectors[1] - QVector3D::dotProduct(source.cellVectors[1], normal) * normal;
    }
    if (tangent.lengthSquared() <= 1.0e-8f) {
        return normal;
    }
    tangent.normalize();

    constexpr double pi = 3.14159265358979323846;
    const double radians = clampedDegrees * pi / 180.0;
    const QVector3D tilted = normal * static_cast<float>(std::cos(radians))
        + tangent * static_cast<float>(std::sin(radians));
    return safeNormalized(tilted, normal);
}

SurfaceLayerMasks surfaceLayerMasks(const StructureData& source, double layerThicknessAngstrom) {
    SurfaceLayerMasks masks;
    if (source.atoms.empty()) {
        return masks;
    }
    const QVector3D normal = safeNormalized(QVector3D::crossProduct(source.cellVectors[0], source.cellVectors[1]));
    const std::vector<float> projections = [&]() {
        std::vector<float> values;
        values.reserve(source.atoms.size());
        for (const auto& atom : source.atoms) {
            values.push_back(QVector3D::dotProduct(atom.cartesian, normal));
        }
        return values;
    }();
    const auto [minIt, maxIt] = std::minmax_element(projections.begin(), projections.end());
    const float minProj = *minIt;
    const float maxProj = *maxIt;
    const float thickness = static_cast<float>(std::max(0.1, layerThicknessAngstrom));
    for (std::size_t i = 0; i < projections.size(); ++i) {
        if (projections[i] >= maxProj - thickness) {
            masks.topIndices.push_back(static_cast<int>(i));
        }
        if (projections[i] <= minProj + thickness) {
            masks.bottomIndices.push_back(static_cast<int>(i));
        }
    }
    masks.normal = normal;
    return masks;
}
}  // namespace

StructureData makeDefaultStructure() {
    StructureData data;
    data.title = QStringLiteral("Untitled");
    data.cellVectors = {
        QVector3D(10.0f, 0.0f, 0.0f),
        QVector3D(0.0f, 10.0f, 0.0f),
        QVector3D(0.0f, 0.0f, 10.0f)
    };
    data.dirty = false;
    return data;
}

StructureData makeSupercellStructure(const StructureData& source, int aMult, int bMult, int cMult) {
    const int a = std::max(1, aMult);
    const int b = std::max(1, bMult);
    const int c = std::max(1, cMult);
    StructureData out = source;
    out.cellVectors = {
        source.cellVectors[0] * static_cast<float>(a),
        source.cellVectors[1] * static_cast<float>(b),
        source.cellVectors[2] * static_cast<float>(c)
    };
    out.atoms.clear();
    out.atoms.reserve(source.atoms.size() * static_cast<std::size_t>(a * b * c));

    int nextId = 1;
    for (int ia = 0; ia < a; ++ia) {
        for (int ib = 0; ib < b; ++ib) {
            for (int ic = 0; ic < c; ++ic) {
                const QVector3D shiftFrac(static_cast<float>(ia), static_cast<float>(ib), static_cast<float>(ic));
                for (const auto& atom : source.atoms) {
                    NativeAtom copy = atom;
                    copy.atomId = nextId++;
                    copy.fractional = QVector3D(
                        (atom.fractional.x() + shiftFrac.x()) / static_cast<float>(a),
                        (atom.fractional.y() + shiftFrac.y()) / static_cast<float>(b),
                        (atom.fractional.z() + shiftFrac.z()) / static_cast<float>(c));
                    copy.cartesian = toCartesian(out.cellVectors, copy.fractional);
                    copy.tag = QString("%1-%2").arg(copy.element).arg(copy.atomId, 4, 10, QChar('0'));
                    out.atoms.push_back(copy);
                }
            }
        }
    }
    out.dirty = true;
    return out;
}

StructureData makeVacuumReboxedStructure(
    const StructureData& source,
    const QVector3D& direction,
    double thicknessAngstrom,
    int placementMode)
{
    StructureData out = source;
    if (source.atoms.empty()) {
        return out;
    }

    const auto frame = orthonormalFrameFromDirection(direction);
    const QVector3D u = frame[0];
    const QVector3D v = frame[1];
    const QVector3D n = frame[2];
    const float padding = 1.0f;

    float minU = std::numeric_limits<float>::max();
    float minV = std::numeric_limits<float>::max();
    float minN = std::numeric_limits<float>::max();
    float maxU = std::numeric_limits<float>::lowest();
    float maxV = std::numeric_limits<float>::lowest();
    float maxN = std::numeric_limits<float>::lowest();

    for (const auto& atom : source.atoms) {
        const float pu = QVector3D::dotProduct(atom.cartesian, u);
        const float pv = QVector3D::dotProduct(atom.cartesian, v);
        const float pn = QVector3D::dotProduct(atom.cartesian, n);
        minU = std::min(minU, pu);
        minV = std::min(minV, pv);
        minN = std::min(minN, pn);
        maxU = std::max(maxU, pu);
        maxV = std::max(maxV, pv);
        maxN = std::max(maxN, pn);
    }

    const float spanU = std::max(6.0f, maxU - minU + padding * 2.0f);
    const float spanV = std::max(6.0f, maxV - minV + padding * 2.0f);
    const float spanN = std::max(6.0f, maxN - minN + padding * 2.0f + static_cast<float>(std::max(0.0, thicknessAngstrom)));
    out.cellVectors = {u * spanU, v * spanV, n * spanN};

    const float vacuumOffset = placementMode == 1
        ? static_cast<float>(std::max(0.0, thicknessAngstrom) * 0.5)
        : (placementMode == 2 ? static_cast<float>(std::max(0.0, thicknessAngstrom)) : 0.0f);
    const QVector3D origin = u * (minU - padding) + v * (minV - padding) + n * (minN - padding - vacuumOffset);
    for (auto& atom : out.atoms) {
        atom.cartesian -= origin;
        atom.fractional = solveFractional(out.cellVectors, atom.cartesian);
    }
    out.dirty = true;
    return out;
}

StructureData adjustVacuumAndSlab(
    const StructureData& source,
    const VacuumAdjustmentOptions& options)
{
    StructureData out = source;
    if (out.atoms.empty()) {
        return out;
    }

    if (options.moveOnly) {
        // Keep cell vectors unchanged; only apply the translation below.
    } else if (options.fitAllAxes) {
        for (int axis = 0; axis < 3; ++axis) {
            fitOrVacuumAxis(out, axis, 0.0, 0, 0.5);
        }
    } else {
        fitOrVacuumAxis(
            out,
            options.axisIndex,
            options.fitTight ? 0.0 : options.vacuumAngstrom,
            options.fitTight ? 0 : options.placementMode,
            options.customCenterFraction);
    }

    QVector3D translation;
    for (int axis = 0; axis < 3; ++axis) {
        translation += cellAxisUnit(out.cellVectors, axis) * static_cast<float>(options.translationAngstrom[static_cast<std::size_t>(axis)]);
    }
    if (translation.lengthSquared() > 1.0e-12f) {
        for (auto& atom : out.atoms) {
            atom.cartesian += translation;
        }
        updateFractionalCoordinates(out);
    }

    out.dirty = true;
    return out;
}

StructureData applyCellAxisTilt(
    const StructureData& source,
    const CellAxisTiltOptions& options,
    QString* errorMessage)
{
    StructureData out = source;
    const int targetIndex = std::clamp(options.targetAxisIndex, 0, 2);
    const int directionIndex = std::clamp(options.directionAxisIndex, 0, 2);
    if (targetIndex == directionIndex) {
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("Target axis and tilt direction must be different.");
        }
        return out;
    }

    const QVector3D target = source.cellVectors[static_cast<std::size_t>(targetIndex)];
    const float targetLength = target.length();
    if (targetLength <= 1.0e-6f) {
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("Target cell axis is too short to tilt.");
        }
        return out;
    }

    const QVector3D targetUnit = target / targetLength;
    QVector3D tangent = source.cellVectors[static_cast<std::size_t>(directionIndex)];
    tangent -= QVector3D::dotProduct(tangent, targetUnit) * targetUnit;
    if (tangent.lengthSquared() <= 1.0e-8f) {
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("Tilt direction is parallel to the target axis.");
        }
        return out;
    }
    tangent.normalize();

    constexpr double pi = 3.14159265358979323846;
    const double clampedDegrees = std::clamp(options.angleDegrees, -80.0, 80.0);
    const double radians = clampedDegrees * pi / 180.0;
    out.cellVectors[static_cast<std::size_t>(targetIndex)] =
        targetUnit * static_cast<float>(std::cos(radians) * targetLength)
        + tangent * static_cast<float>(std::sin(radians) * targetLength);

    for (auto& atom : out.atoms) {
        atom.cartesian = toCartesian(out.cellVectors, atom.fractional);
    }
    out.dirty = true;
    return out;
}

StructureData shiftSurfaceTopLayer(
    const StructureData& source,
    double shiftAlongA,
    double shiftAlongB,
    double layerThicknessAngstrom)
{
    StructureData out = source;
    if (source.atoms.empty()) {
        return out;
    }

    const auto frame = orthonormalFrameFromDirection(QVector3D::crossProduct(source.cellVectors[0], source.cellVectors[1]));
    const QVector3D u = frame[0];
    const QVector3D v = frame[1];
    const auto masks = surfaceLayerMasks(source, layerThicknessAngstrom);
    const QVector3D displacement = static_cast<float>(shiftAlongA) * u + static_cast<float>(shiftAlongB) * v;

    for (int idx : masks.topIndices) {
        out.atoms[static_cast<std::size_t>(idx)].cartesian += displacement;
        out.atoms[static_cast<std::size_t>(idx)].fractional = solveFractional(out.cellVectors, out.atoms[static_cast<std::size_t>(idx)].cartesian);
    }
    out.dirty = true;
    return out;
}

StructureData addHydrogenTermination(
    const StructureData& source,
    double bondLengthAngstrom,
    bool top,
    bool bottom,
    double layerThicknessAngstrom)
{
    StructureData out = source;
    if (source.atoms.empty()) {
        return out;
    }

    const auto masks = surfaceLayerMasks(source, layerThicknessAngstrom);
    const QVector3D normal = masks.normal;
    const double bond = std::max(0.5, bondLengthAngstrom);
    int nextId = 1;
    for (const auto& atom : out.atoms) {
        nextId = std::max(nextId, atom.atomId + 1);
    }

    auto appendHydrogen = [&](const QVector3D& position) {
        NativeAtom atom;
        atom.atomId = nextId++;
        atom.element = QStringLiteral("H");
        atom.tag = QString("H-%1").arg(atom.atomId, 4, 10, QChar('0'));
        atom.cartesian = position;
        atom.fractional = solveFractional(out.cellVectors, atom.cartesian);
        atom.color = vestaElementColor(atom.element);
        atom.radius = vestaElementRadius(atom.element);
        out.atoms.push_back(atom);
    };

    if (top) {
        for (int idx : masks.topIndices) {
            appendHydrogen(source.atoms[static_cast<std::size_t>(idx)].cartesian + normal * static_cast<float>(bond));
        }
    }
    if (bottom) {
        for (int idx : masks.bottomIndices) {
            appendHydrogen(source.atoms[static_cast<std::size_t>(idx)].cartesian - normal * static_cast<float>(bond));
        }
    }

    out.dirty = true;
    return out;
}

QVector3D surfaceNormalForStructure(const StructureData& source) {
    return surfaceNormalImpl(source);
}

StructureData addPlacementAtom(
    const StructureData& source,
    const std::vector<int>& selectedAtomIds,
    const SurfacePlacementRule& rule,
    QString* errorMessage)
{
    StructureData out = source;
    if (source.atoms.empty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("No atoms are available for custom placement.");
        }
        return out;
    }

    const auto mode = normalizedMode(rule.mode);
    const int required = selectionCountForRule(rule);
    if (static_cast<int>(selectedAtomIds.size()) < required) {
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("Preset \"%1\" requires %2 selected atom(s).")
                .arg(rule.name.isEmpty() ? rule.mode : rule.name)
                .arg(required);
        }
        return out;
    }

    std::vector<const NativeAtom*> references;
    const bool usesAllSelected = mode == "multi_centroid" || mode == "multi_weighted" || mode == "multi_plane_normal";
    const bool needsTiltReference = std::abs(rule.tiltDegrees) > 1.0e-8 && static_cast<int>(selectedAtomIds.size()) > required;
    const int referenceCount = usesAllSelected
        ? static_cast<int>(selectedAtomIds.size())
        : std::min(static_cast<int>(selectedAtomIds.size()), required + (needsTiltReference ? 1 : 0));
    references.reserve(static_cast<std::size_t>(referenceCount));
    for (int i = 0; i < referenceCount; ++i) {
        const NativeAtom* atom = findAtomById(source, selectedAtomIds[static_cast<std::size_t>(i)]);
        if (atom == nullptr) {
            if (errorMessage != nullptr) {
                *errorMessage = QObject::tr("Atom ID %1 was not found in the current structure.")
                    .arg(selectedAtomIds[static_cast<std::size_t>(i)]);
            }
            return out;
        }
        references.push_back(atom);
    }

    double signedHeight = rule.height;
    if (mode == "single_above") {
        signedHeight = std::abs(rule.height);
    } else if (mode == "single_below") {
        signedHeight = -std::abs(rule.height);
    }

    QVector3D placementNormal = surfaceNormalImpl(source);
    if (mode == "multi_plane_normal" && references.size() >= 3) {
        const QVector3D v1 = references[1]->cartesian - references[0]->cartesian;
        const QVector3D v2 = references[2]->cartesian - references[0]->cartesian;
        const QVector3D candidate = QVector3D::crossProduct(v1, v2);
        if (candidate.lengthSquared() > 1.0e-8f) {
            placementNormal = candidate.normalized();
            if (QVector3D::dotProduct(placementNormal, surfaceNormalImpl(source)) < 0.0f) {
                placementNormal *= -1.0f;
            }
        }
    }
    placementNormal = tiltedPlacementNormal(source, references, placementNormal, rule.tiltDegrees);
    int nextId = 1;
    for (const auto& atom : out.atoms) {
        nextId = std::max(nextId, atom.atomId + 1);
    }

    const QString element = rule.element.trimmed().isEmpty() ? QStringLiteral("H") : rule.element.trimmed();
    auto appendAtom = [&](const QVector3D& position) {
        NativeAtom atom;
        atom.atomId = nextId++;
        atom.element = element;
        atom.tag = QString("%1-%2").arg(atom.element).arg(atom.atomId, 4, 10, QChar('0'));
        atom.cartesian = position;
        atom.fractional = solveFractional(out.cellVectors, atom.cartesian);
        atom.color = vestaElementColor(atom.element);
        atom.radius = vestaElementRadius(atom.element);
        out.atoms.push_back(atom);
    };

    if (mode == "single_above" || mode == "single_below") {
        std::vector<const NativeAtom*> targets;
        targets.reserve(selectedAtomIds.size());
        for (int atomId : selectedAtomIds) {
            const NativeAtom* atom = findAtomById(source, atomId);
            if (atom == nullptr) {
                if (errorMessage != nullptr) {
                    *errorMessage = QObject::tr("Atom ID %1 was not found in the current structure.")
                        .arg(atomId);
                }
                return source;
            }
            targets.push_back(atom);
        }
        for (const auto* target : targets) {
            appendAtom(target->cartesian + placementNormal * static_cast<float>(signedHeight));
        }
        out.dirty = true;
        return out;
    }

    QString placementError;
    const QVector3D anchor = placementAnchor(references, rule, &placementError);
    if (!placementError.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = placementError;
        }
        return out;
    }

    appendAtom(anchor + placementNormal * static_cast<float>(signedHeight));
    out.dirty = true;
    return out;
}
