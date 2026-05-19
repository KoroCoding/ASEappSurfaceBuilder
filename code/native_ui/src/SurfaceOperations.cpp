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

long long determinant3x3(const SupercellTransformMatrix& matrix) {
    return static_cast<long long>(matrix[0][0]) * (matrix[1][1] * matrix[2][2] - matrix[1][2] * matrix[2][1])
        - static_cast<long long>(matrix[0][1]) * (matrix[1][0] * matrix[2][2] - matrix[1][2] * matrix[2][0])
        + static_cast<long long>(matrix[0][2]) * (matrix[1][0] * matrix[2][1] - matrix[1][1] * matrix[2][0]);
}

bool invert3x3(const double matrix[3][3], double inverse[3][3]) {
    const double det =
        matrix[0][0] * (matrix[1][1] * matrix[2][2] - matrix[1][2] * matrix[2][1])
        - matrix[0][1] * (matrix[1][0] * matrix[2][2] - matrix[1][2] * matrix[2][0])
        + matrix[0][2] * (matrix[1][0] * matrix[2][1] - matrix[1][1] * matrix[2][0]);
    if (std::abs(det) < 1.0e-12) {
        return false;
    }
    const double invDet = 1.0 / det;
    inverse[0][0] = (matrix[1][1] * matrix[2][2] - matrix[1][2] * matrix[2][1]) * invDet;
    inverse[0][1] = (matrix[0][2] * matrix[2][1] - matrix[0][1] * matrix[2][2]) * invDet;
    inverse[0][2] = (matrix[0][1] * matrix[1][2] - matrix[0][2] * matrix[1][1]) * invDet;
    inverse[1][0] = (matrix[1][2] * matrix[2][0] - matrix[1][0] * matrix[2][2]) * invDet;
    inverse[1][1] = (matrix[0][0] * matrix[2][2] - matrix[0][2] * matrix[2][0]) * invDet;
    inverse[1][2] = (matrix[0][2] * matrix[1][0] - matrix[0][0] * matrix[1][2]) * invDet;
    inverse[2][0] = (matrix[1][0] * matrix[2][1] - matrix[1][1] * matrix[2][0]) * invDet;
    inverse[2][1] = (matrix[0][1] * matrix[2][0] - matrix[0][0] * matrix[2][1]) * invDet;
    inverse[2][2] = (matrix[0][0] * matrix[1][1] - matrix[0][1] * matrix[1][0]) * invDet;
    return true;
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
    if (normalized == "selection_centroid") {
        return 1;
    }
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
    if (mode == "selection_centroid"
        || mode == "pair_midpoint"
        || mode == "pair_fraction"
        || mode == "triple_centroid"
        || mode == "triple_weighted"
        || mode == "multi_centroid"
        || mode == "multi_weighted"
        || mode == "multi_plane_normal") {
        QVector3D sum;
        for (const auto* atom : references) {
            sum += atom->cartesian;
        }
        return sum / static_cast<float>(references.size());
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

SupercellTransformMatrix identitySupercellTransformMatrix() {
    return {{{{1, 0, 0}}, {{0, 1, 0}}, {{0, 0, 1}}}};
}

SupercellTransformMatrix diagonalSupercellTransformMatrix(int aMult, int bMult, int cMult) {
    return {{{{std::max(1, aMult), 0, 0}}, {{0, std::max(1, bMult), 0}}, {{0, 0, std::max(1, cMult)}}}};
}

long long supercellTransformDeterminant(const SupercellTransformMatrix& matrix) {
    return determinant3x3(matrix);
}

bool isIdentitySupercellTransform(const SupercellTransformMatrix& matrix) {
    return matrix == identitySupercellTransformMatrix();
}

bool isDiagonalSupercellTransform(const SupercellTransformMatrix& matrix, std::array<int, 3>* factors) {
    if (matrix[0][1] != 0 || matrix[0][2] != 0
        || matrix[1][0] != 0 || matrix[1][2] != 0
        || matrix[2][0] != 0 || matrix[2][1] != 0
        || matrix[0][0] < 1 || matrix[1][1] < 1 || matrix[2][2] < 1) {
        return false;
    }
    if (factors != nullptr) {
        *factors = {matrix[0][0], matrix[1][1], matrix[2][2]};
    }
    return true;
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

StructureData makeSupercellStructure(
    const StructureData& source,
    const SupercellTransformMatrix& matrix,
    QString* errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    constexpr long long kMaxVolumeFactor = 8000;
    constexpr double kEpsilon = 1.0e-9;
    const long long determinant = supercellTransformDeterminant(matrix);
    if (determinant <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("Supercell transform matrix must be right-handed and have a positive non-zero determinant.");
        }
        return source;
    }
    if (determinant > kMaxVolumeFactor) {
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("Supercell transform volume factor %1 is too large. Use %2 or less.").arg(determinant).arg(kMaxVolumeFactor);
        }
        return source;
    }

    double oldFractionalToNewFractional[3][3]{};
    double oldToNewInput[3][3]{};
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            oldToNewInput[row][column] = static_cast<double>(matrix[column][row]);
        }
    }
    if (!invert3x3(oldToNewInput, oldFractionalToNewFractional)) {
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("Supercell transform matrix is singular.");
        }
        return source;
    }

    StructureData out = source;
    out.cellVectors = {
        source.cellVectors[0] * static_cast<float>(matrix[0][0])
            + source.cellVectors[1] * static_cast<float>(matrix[0][1])
            + source.cellVectors[2] * static_cast<float>(matrix[0][2]),
        source.cellVectors[0] * static_cast<float>(matrix[1][0])
            + source.cellVectors[1] * static_cast<float>(matrix[1][1])
            + source.cellVectors[2] * static_cast<float>(matrix[1][2]),
        source.cellVectors[0] * static_cast<float>(matrix[2][0])
            + source.cellVectors[1] * static_cast<float>(matrix[2][1])
            + source.cellVectors[2] * static_cast<float>(matrix[2][2])
    };

    std::array<int, 3> minCorner{0, 0, 0};
    std::array<int, 3> maxCorner{0, 0, 0};
    for (int mask = 0; mask < 8; ++mask) {
        std::array<int, 3> corner{0, 0, 0};
        for (int newAxis = 0; newAxis < 3; ++newAxis) {
            if ((mask & (1 << newAxis)) == 0) {
                continue;
            }
            for (int oldAxis = 0; oldAxis < 3; ++oldAxis) {
                corner[oldAxis] += matrix[newAxis][oldAxis];
            }
        }
        for (int oldAxis = 0; oldAxis < 3; ++oldAxis) {
            minCorner[oldAxis] = std::min(minCorner[oldAxis], corner[oldAxis]);
            maxCorner[oldAxis] = std::max(maxCorner[oldAxis], corner[oldAxis]);
        }
    }

    const std::size_t expectedAtomCount = source.atoms.size() * static_cast<std::size_t>(determinant);
    out.atoms.clear();
    out.atoms.reserve(expectedAtomCount);

    int nextId = 1;
    for (const auto& atom : source.atoms) {
        const QVector3D baseFractional = solveFractional(source.cellVectors, atom.cartesian);
        const std::array<double, 3> base{
            static_cast<double>(baseFractional.x()),
            static_cast<double>(baseFractional.y()),
            static_cast<double>(baseFractional.z())
        };
        const int iaMin = static_cast<int>(std::floor(static_cast<double>(minCorner[0]) - base[0] - kEpsilon));
        const int iaMax = static_cast<int>(std::ceil(static_cast<double>(maxCorner[0]) - base[0] + kEpsilon));
        const int ibMin = static_cast<int>(std::floor(static_cast<double>(minCorner[1]) - base[1] - kEpsilon));
        const int ibMax = static_cast<int>(std::ceil(static_cast<double>(maxCorner[1]) - base[1] + kEpsilon));
        const int icMin = static_cast<int>(std::floor(static_cast<double>(minCorner[2]) - base[2] - kEpsilon));
        const int icMax = static_cast<int>(std::ceil(static_cast<double>(maxCorner[2]) - base[2] + kEpsilon));
        for (int ia = iaMin; ia <= iaMax; ++ia) {
            for (int ib = ibMin; ib <= ibMax; ++ib) {
                for (int ic = icMin; ic <= icMax; ++ic) {
                    const std::array<double, 3> oldFractional{
                        base[0] + static_cast<double>(ia),
                        base[1] + static_cast<double>(ib),
                        base[2] + static_cast<double>(ic)
                    };
                    std::array<double, 3> newFractional{};
                    bool inside = true;
                    for (int row = 0; row < 3; ++row) {
                        double value = 0.0;
                        for (int column = 0; column < 3; ++column) {
                            value += oldFractionalToNewFractional[row][column] * oldFractional[column];
                        }
                        if (value < -kEpsilon || value >= 1.0 - kEpsilon) {
                            inside = false;
                            break;
                        }
                        if (value < 0.0) {
                            value = 0.0;
                        }
                        newFractional[row] = value;
                    }
                    if (!inside) {
                        continue;
                    }
                    NativeAtom copy = atom;
                    copy.atomId = nextId++;
                    copy.fractional = QVector3D(
                        static_cast<float>(newFractional[0]),
                        static_cast<float>(newFractional[1]),
                        static_cast<float>(newFractional[2]));
                    copy.cartesian = toCartesian(out.cellVectors, copy.fractional);
                    copy.tag = QString("%1-%2").arg(copy.element).arg(copy.atomId, 4, 10, QChar('0'));
                    out.atoms.push_back(copy);
                }
            }
        }
    }

    if (out.atoms.size() != expectedAtomCount) {
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("Supercell transform produced %1 atoms, but %2 were expected from determinant %3.")
                .arg(out.atoms.size())
                .arg(expectedAtomCount)
                .arg(determinant);
        }
        return source;
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
    const bool usesAllSelected = mode == "selection_centroid"
        || mode == "pair_midpoint"
        || mode == "pair_fraction"
        || mode == "triple_centroid"
        || mode == "triple_weighted"
        || mode == "multi_centroid"
        || mode == "multi_weighted"
        || mode == "multi_plane_normal";
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
