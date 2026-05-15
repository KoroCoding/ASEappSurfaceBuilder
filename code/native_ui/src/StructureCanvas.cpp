#include "StructureCanvas.h"
#include "ElementStyle.h"

#include <QEvent>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <QHash>
#include <QQuaternion>
#include <QRect>
#include <QWheelEvent>
#include <QtMath>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>

namespace {
constexpr double kAtomRadiusSceneFactor = 0.50;
constexpr double kAtomRadiusPixelFloorFactor = 6.8;
constexpr double kAtomRadiusPixelFloorMin = 5.0;
constexpr double kAtomRadiusPixelFloorMax = 13.0;
// Some symmetry-expanded CIFs contain almost duplicated sites when an input
// coordinate is close to a special position. Those sub-angstrom artifacts must
// not shrink all atoms or become visible bonds.
constexpr double kMinimumPhysicalAtomDistance = 0.50;
constexpr double kBondWidthPixels = 2.8;
constexpr double kPreviewBondWidthPixels = 2.4;

struct SpatialCellKey {
    int x = 0;
    int y = 0;
    int z = 0;

    bool operator==(const SpatialCellKey& other) const noexcept {
        return x == other.x && y == other.y && z == other.z;
    }
};

struct AtomImageKey {
    int atom = -1;
    int imageA = 0;
    int imageB = 0;
    int imageC = 0;

    bool operator==(const AtomImageKey& other) const noexcept {
        return atom == other.atom
            && imageA == other.imageA
            && imageB == other.imageB
            && imageC == other.imageC;
    }
};

size_t qHash(const SpatialCellKey& key, size_t seed = 0) noexcept {
    std::uint64_t hash = static_cast<std::uint64_t>(seed) ^ 1469598103934665603ull;
    const auto mix = [&hash](int value) {
        hash ^= static_cast<std::uint32_t>(value);
        hash *= 1099511628211ull;
    };
    mix(key.x);
    mix(key.y);
    mix(key.z);
    return static_cast<size_t>(hash);
}

size_t qHash(const AtomImageKey& key, size_t seed = 0) noexcept {
    std::uint64_t hash = static_cast<std::uint64_t>(seed) ^ 1469598103934665603ull;
    const auto mix = [&hash](int value) {
        hash ^= static_cast<std::uint32_t>(value);
        hash *= 1099511628211ull;
    };
    mix(key.atom);
    mix(key.imageA);
    mix(key.imageB);
    mix(key.imageC);
    return static_cast<size_t>(hash);
}

QVector3D cellPoint(const std::array<QVector3D, 3>& vectors, int mask) {
    QVector3D point;
    if (mask & 1) point += vectors[0];
    if (mask & 2) point += vectors[1];
    if (mask & 4) point += vectors[2];
    return point;
}

QVector3D cellTranslation(const std::array<QVector3D, 3>& vectors, int imageA, int imageB, int imageC) {
    return vectors[0] * static_cast<float>(imageA)
        + vectors[1] * static_cast<float>(imageB)
        + vectors[2] * static_cast<float>(imageC);
}

bool hasNonDegenerateCell(const std::array<QVector3D, 3>& vectors) {
    const double volume = std::abs(static_cast<double>(QVector3D::dotProduct(
        vectors[0],
        QVector3D::crossProduct(vectors[1], vectors[2]))));
    return volume > 1.0e-8;
}

bool fractionalInsideUnitCell(const QVector3D& fractional) {
    constexpr double kCellBoundaryTolerance = 1.0e-6;
    return fractional.x() >= -kCellBoundaryTolerance && fractional.x() <= 1.0 + kCellBoundaryTolerance
        && fractional.y() >= -kCellBoundaryTolerance && fractional.y() <= 1.0 + kCellBoundaryTolerance
        && fractional.z() >= -kCellBoundaryTolerance && fractional.z() <= 1.0 + kCellBoundaryTolerance;
}

bool shouldDisplayAtom(
    const NativeAtom& atom,
    const StructureCanvas::DisplayOptions& options,
    const std::array<QVector3D, 3>& cell)
{
    if (options.showOutsideCell || !hasNonDegenerateCell(cell)) {
        return true;
    }
    return fractionalInsideUnitCell(atom.fractional);
}

double visualAtomRadius(const NativeAtom& atom, double atomScale, double densityScale) {
    return atom.radius * kAtomRadiusSceneFactor * atomScale * densityScale;
}

double atomRadiusDensityScale(double sceneScale, double atomScale, double nearestDistance, double maxAtomRadius) {
    if (nearestDistance <= 1.0e-8 || maxAtomRadius <= 1.0e-8) {
        return 1.0;
    }

    const double projectedNearest = nearestDistance * sceneScale;
    if (projectedNearest >= 18.0) {
        return 1.0;
    }

    const double naturalMaxRadius = maxAtomRadius * sceneScale * kAtomRadiusSceneFactor * atomScale;
    if (naturalMaxRadius <= 1.0e-8) {
        return 1.0;
    }

    const double allowedMaxRadius = projectedNearest * 0.22;
    return std::clamp(allowedMaxRadius / naturalMaxRadius, 0.16, 1.0);
}

double screenAtomRadius(const NativeAtom& atom, double scale, double perspective, double atomScale, double densityScale) {
    const double styledRadius = std::max(0.05, atom.radius);
    const double projectedRadius = styledRadius * scale * perspective * kAtomRadiusSceneFactor * atomScale * densityScale;
    // Very large CIF cells can make the physical projection smaller than the
    // fixed-width bonds. Keep a screen-space floor so atoms remain legible while
    // preserving the world-space radius used to trim bond endpoints.
    const double readablePixelFloor = std::clamp(
        styledRadius * kAtomRadiusPixelFloorFactor * atomScale,
        kAtomRadiusPixelFloorMin,
        kAtomRadiusPixelFloorMax);
    return std::max(projectedRadius, readablePixelFloor);
}

double hitAtomRadius(const NativeAtom& atom, double scale, double perspective, double atomScale, double densityScale) {
    return std::max(8.0, screenAtomRadius(atom, scale, perspective, atomScale, densityScale) + 2.0);
}

bool distanceInRange(double distance, const BondDistanceRange& range) {
    if (distance < kMinimumPhysicalAtomDistance) {
        return false;
    }
    return distance >= range.minDistance - 1.0e-9 && distance <= range.maxDistance + 1.0e-9;
}

double maximumBondCutoffWithCustomRanges(const QHash<QString, BondDistanceRange>& customRanges) {
    double maxCutoff = vestaMaximumBondCutoff();
    for (auto it = customRanges.cbegin(); it != customRanges.cend(); ++it) {
        if (it->maxDistance > 0.0 && it->maxDistance >= it->minDistance) {
            maxCutoff = std::max(maxCutoff, it->maxDistance);
        }
    }
    return maxCutoff > 0.0 ? maxCutoff : 4.5;
}

bool effectiveBondRange(
    const QHash<QString, BondDistanceRange>& customRanges,
    const QString& elementA,
    const QString& elementB,
    BondDistanceRange* range)
{
    if (range == nullptr) {
        return false;
    }
    const auto custom = customRanges.constFind(vestaBondKey(elementA, elementB));
    if (custom != customRanges.cend()) {
        *range = custom.value();
        return range->maxDistance > 0.0 && range->maxDistance >= range->minDistance;
    }
    return vestaBondDistanceRange(elementA, elementB, range);
}

SpatialCellKey spatialCellIndex(const QVector3D& point, double cellSize) {
    return SpatialCellKey{
        static_cast<int>(std::floor(point.x() / cellSize)),
        static_cast<int>(std::floor(point.y() / cellSize)),
        static_cast<int>(std::floor(point.z() / cellSize))};
}

double typicalNearestNeighborDistance(const std::vector<NativeAtom>& atoms, double cellSize) {
    if (atoms.size() < 2) {
        return 0.0;
    }

    QHash<SpatialCellKey, std::vector<int>> grid;
    grid.reserve(atoms.size());
    std::vector<double> nearestDistanceByAtom(atoms.size(), std::numeric_limits<double>::infinity());
    const auto recordNearestDistance = [&](int atomA, int atomB, double distance) {
        if (distance < kMinimumPhysicalAtomDistance) {
            return;
        }
        auto& nearestA = nearestDistanceByAtom[static_cast<std::size_t>(atomA)];
        auto& nearestB = nearestDistanceByAtom[static_cast<std::size_t>(atomB)];
        nearestA = std::min(nearestA, distance);
        nearestB = std::min(nearestB, distance);
    };

    for (int i = 0; i < static_cast<int>(atoms.size()); ++i) {
        const auto base = spatialCellIndex(atoms[static_cast<std::size_t>(i)].cartesian, cellSize);
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dz = -1; dz <= 1; ++dz) {
                    const SpatialCellKey neighbor{base.x + dx, base.y + dy, base.z + dz};
                    const auto it = grid.constFind(neighbor);
                    if (it == grid.cend()) {
                        continue;
                    }
                    for (int j : it.value()) {
                        const QVector3D diff = atoms[static_cast<std::size_t>(j)].cartesian
                            - atoms[static_cast<std::size_t>(i)].cartesian;
                        recordNearestDistance(i, j, diff.length());
                    }
                }
            }
        }
        grid[base].push_back(i);
    }

    std::vector<double> finiteNearestDistances;
    finiteNearestDistances.reserve(nearestDistanceByAtom.size());
    for (double distance : nearestDistanceByAtom) {
        if (std::isfinite(distance)) {
            finiteNearestDistances.push_back(distance);
        }
    }
    if (finiteNearestDistances.empty()) {
        return 0.0;
    }

    std::sort(finiteNearestDistances.begin(), finiteNearestDistances.end());
    // Use a robust typical nearest-neighbor distance, not the single closest
    // pair. While inserting or dragging an atom between lattice sites, one
    // temporary near-overlap should not shrink every atom in the scene.
    return finiteNearestDistances[finiteNearestDistances.size() / 2];
}
}

StructureCanvas::StructureCanvas(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setAutoFillBackground(false);
    setBasisFromView(QVector3D(0.0f, 0.0f, 1.0f), QVector3D(0.0f, 1.0f, 0.0f));
}

void StructureCanvas::setDisplayOptions(const DisplayOptions& options) {
    const bool rebuildBonds = m_displayOptions.showBonds != options.showBonds
        || m_displayOptions.showOutsideCell != options.showOutsideCell
        || m_displayOptions.customBondRanges != options.customBondRanges;
    m_displayOptions = options;
    if (rebuildBonds) {
        rebuildSceneCache();
    }
    update();
}

StructureCanvas::DisplayOptions StructureCanvas::displayOptions() const {
    return m_displayOptions;
}

int StructureCanvas::bondCount() const {
    return static_cast<int>(m_cachedBonds.size());
}

void StructureCanvas::setStructure(const StructureData& structure) {
    m_structure = structure;
    m_focusAtomId = -1;
    m_selectedAtomIds.clear();
    updateInteractionCursor();
    rebuildSceneCache();
    update();
}

void StructureCanvas::setSelectedAtomIds(const std::vector<int>& atomIds) {
    m_selectedAtomIds = atomIds;
    updateInteractionCursor();
    update();
}

void StructureCanvas::setPreviewAtoms(const std::vector<NativeAtom>& atoms) {
    m_previewAtoms = atoms;
    update();
}

void StructureCanvas::resetView() {
    setBasisFromView(QVector3D(0.0f, 0.0f, 1.0f), QVector3D(0.0f, 1.0f, 0.0f));
    m_zoom = 1.0;
    m_panOffset = {};
    m_focusAtomId = -1;
    update();
}

void StructureCanvas::setJapanese(bool japanese) {
    if (m_japanese == japanese) {
        return;
    }
    m_japanese = japanese;
    update();
}

void StructureCanvas::setInteractionMode(InteractionMode mode) {
    if (m_interactionMode == mode) {
        return;
    }
    m_interactionMode = mode;
    m_draggingSelection = false;
    m_ctrlSelectingAtoms = false;
    updateInteractionCursor();
    update();
}

StructureCanvas::InteractionMode StructureCanvas::interactionMode() const {
    return m_interactionMode;
}

QVector3D StructureCanvas::viewForward() const {
    return m_viewForward;
}

void StructureCanvas::updateInteractionCursor() {
    if (m_structure.atoms.empty()) {
        setCursor(Qt::PointingHandCursor);
    } else if (m_interactionMode == InteractionMode::MoveModel ||
               (m_interactionMode == InteractionMode::MoveAtoms && !m_selectedAtomIds.empty())) {
        setCursor(Qt::SizeAllCursor);
    } else {
        setCursor(Qt::ArrowCursor);
    }
}

void StructureCanvas::fitToStructure() {
    m_panOffset = {};
    m_zoom = 1.0;
    update();
}

void StructureCanvas::focusAtom(int atomId) {
    m_focusAtomId = atomId;
    update();
}

void StructureCanvas::rotateBy(double yawDelta, double pitchDelta) {
    const QPoint delta(static_cast<int>(std::lround(yawDelta)), static_cast<int>(std::lround(pitchDelta)));
    rotateBasisFromDrag(delta);
    update();
}

void StructureCanvas::panBy(double dx, double dy) {
    m_panOffset += QPointF(dx, dy);
    update();
}

void StructureCanvas::zoomBy(double factor) {
    if (factor <= 0.0) {
        return;
    }
    m_zoom = std::clamp(m_zoom * factor, 0.35, 4.0);
    update();
}

void StructureCanvas::zoomAt(double factor, const QPointF& position) {
    if (factor <= 0.0 || !std::isfinite(factor)) {
        return;
    }

    const QRectF viewport = rect().adjusted(18, 18, -18, -18);
    const QPointF focus = viewport.contains(position) ? position : viewport.center();
    const QPointF focusFromViewCenter = focus - viewport.center();
    const double oldZoom = m_zoom;
    m_zoom = std::clamp(m_zoom * factor, 0.35, 4.0);
    const double appliedFactor = oldZoom <= 1.0e-9 ? 1.0 : (m_zoom / oldZoom);
    m_panOffset += (focusFromViewCenter - m_panOffset) * (1.0 - appliedFactor);
    update();
}

void StructureCanvas::setViewDirection(const QVector3D& direction, bool resetPan) {
    setViewDirection(direction, QVector3D(0.0f, 1.0f, 0.0f), resetPan);
}

void StructureCanvas::setViewDirection(const QVector3D& direction, const QVector3D& upHint, bool resetPan) {
    if (direction.lengthSquared() <= 1.0e-8f) {
        return;
    }
    setBasisFromView(direction, upHint);
    if (resetPan) {
        m_panOffset = {};
    }
    update();
}

void StructureCanvas::setAxisAlignedView(const QVector3D& horizontalAxis, const QVector3D& upHint, bool resetPan) {
    if (horizontalAxis.lengthSquared() <= 1.0e-8f) {
        return;
    }

    QVector3D right = horizontalAxis.normalized();
    QVector3D up = upHint.lengthSquared() > 1.0e-8f ? upHint.normalized() : QVector3D(0.0f, 1.0f, 0.0f);
    up -= QVector3D::dotProduct(up, right) * right;
    if (up.lengthSquared() <= 1.0e-8f) {
        up = std::abs(right.z()) < 0.9f ? QVector3D(0.0f, 0.0f, 1.0f) : QVector3D(0.0f, 1.0f, 0.0f);
        up -= QVector3D::dotProduct(up, right) * right;
    }
    if (up.lengthSquared() <= 1.0e-8f) {
        return;
    }
    up.normalize();

    QVector3D forward = QVector3D::crossProduct(right, up);
    if (forward.lengthSquared() <= 1.0e-8f) {
        return;
    }
    forward.normalize();
    up = QVector3D::crossProduct(forward, right);
    if (up.lengthSquared() <= 1.0e-8f) {
        return;
    }
    up.normalize();

    m_viewRight = right;
    m_viewUp = up;
    m_viewForward = forward;
    if (resetPan) {
        m_panOffset = {};
    }
    update();
}

QSize StructureCanvas::minimumSizeHint() const {
    return {720, 520};
}

QColor StructureCanvas::backgroundColor() const {
    return QColor("#FFFFFF");
}

QVector3D StructureCanvas::sceneCenter() const {
    return m_cachedCenter;
}

double StructureCanvas::sceneScale(const QRectF& viewport, const QVector3D& center) const {
    Q_UNUSED(center);
    return std::min(viewport.width(), viewport.height()) / (std::max(1.0, m_cachedRadius) * 2.75) * m_zoom;
}

QVector3D StructureCanvas::rotatePoint(const QVector3D& point) const {
    return {
        QVector3D::dotProduct(point, m_viewRight),
        QVector3D::dotProduct(point, m_viewUp),
        QVector3D::dotProduct(point, m_viewForward)
    };
}

QPointF StructureCanvas::projectPoint(const QVector3D& point, const QRectF& rect, double scale) const {
    const auto rotated = rotatePoint(point);
    const double perspective = depthPerspective(rotated.z());
    const double px = rect.center().x() + m_panOffset.x() + rotated.x() * scale * perspective;
    const double py = rect.center().y() + m_panOffset.y() - rotated.y() * scale * perspective;
    return {px, py};
}

double StructureCanvas::depthPerspective(double z) const {
    if (m_displayOptions.perspective) {
        return 1.0 / std::max(0.35, 2.6 - z * 0.08);
    }
    return 1.0;
}

void StructureCanvas::setBasisFromView(const QVector3D& forward, const QVector3D& upHint) {
    QVector3D f = forward.normalized();
    QVector3D up = upHint.lengthSquared() > 1.0e-8f ? upHint.normalized() : QVector3D(0.0f, 1.0f, 0.0f);
    if (std::abs(QVector3D::dotProduct(f, up)) > 0.95f) {
        up = std::abs(f.y()) < 0.9f ? QVector3D(0.0f, 1.0f, 0.0f) : QVector3D(1.0f, 0.0f, 0.0f);
    }
    QVector3D right = QVector3D::crossProduct(up, f);
    if (right.lengthSquared() <= 1.0e-8f) {
        right = QVector3D(1.0f, 0.0f, 0.0f);
    }
    right.normalize();
    QVector3D correctedUp = QVector3D::crossProduct(f, right);
    correctedUp.normalize();
    m_viewRight = right;
    m_viewUp = correctedUp;
    m_viewForward = f;
}

void StructureCanvas::rotateBasisFromDrag(const QPoint& delta) {
    const double distance = std::hypot(static_cast<double>(delta.x()), static_cast<double>(delta.y()));
    if (distance <= 0.0) {
        return;
    }
    QVector3D screenAxis(static_cast<float>(-delta.y()), static_cast<float>(delta.x()), 0.0f);
    if (screenAxis.lengthSquared() <= 1.0e-8f) {
        return;
    }
    screenAxis.normalize();
    QVector3D worldAxis =
        m_viewRight * screenAxis.x() +
        m_viewUp * screenAxis.y() +
        m_viewForward * screenAxis.z();
    if (worldAxis.lengthSquared() <= 1.0e-8f) {
        return;
    }
    worldAxis.normalize();
    QQuaternion rotation = QQuaternion::fromAxisAndAngle(worldAxis, static_cast<float>(distance * 0.55));
    m_viewRight = rotation.rotatedVector(m_viewRight).normalized();
    m_viewUp = rotation.rotatedVector(m_viewUp);
    m_viewForward = rotation.rotatedVector(m_viewForward);
    m_viewForward.normalize();
    m_viewRight = QVector3D::crossProduct(m_viewUp, m_viewForward);
    if (m_viewRight.lengthSquared() <= 1.0e-8f) {
        m_viewRight = QVector3D(1.0f, 0.0f, 0.0f);
    } else {
        m_viewRight.normalize();
    }
    m_viewUp = QVector3D::crossProduct(m_viewForward, m_viewRight);
    if (m_viewUp.lengthSquared() <= 1.0e-8f) {
        m_viewUp = QVector3D(0.0f, 1.0f, 0.0f);
    } else {
        m_viewUp.normalize();
    }
}

void StructureCanvas::rebuildSceneCache() {
    m_cachedBonds.clear();
    m_cachedAtomImages.clear();
    m_cachedCenter = {};
    m_cachedRadius = 1.0;
    m_cachedNearestAtomDistance = 0.0;
    m_cachedMaxAtomRadius = 1.0;

    m_cachedBonds = buildBondPairs();

    QHash<AtomImageKey, bool> atomImages;
    atomImages.reserve(static_cast<int>(m_structure.atoms.size() + m_cachedBonds.size()));
    const auto addAtomImage = [&](int atomIndex, int imageA, int imageB, int imageC, const QVector3D& shift) {
        if (atomIndex < 0 || atomIndex >= static_cast<int>(m_structure.atoms.size())) {
            return;
        }
        const AtomImageKey key{atomIndex, imageA, imageB, imageC};
        if (atomImages.constFind(key) != atomImages.cend()) {
            return;
        }
        atomImages.insert(key, true);
        m_cachedAtomImages.push_back({atomIndex, imageA, imageB, imageC, shift});
    };

    for (int i = 0; i < static_cast<int>(m_structure.atoms.size()); ++i) {
        if (shouldDisplayAtom(m_structure.atoms[static_cast<std::size_t>(i)], m_displayOptions, m_structure.cellVectors)) {
            addAtomImage(i, 0, 0, 0, {});
        }
    }
    for (const auto& bond : m_cachedBonds) {
        addAtomImage(bond.atomA, 0, 0, 0, {});
        addAtomImage(bond.atomB, bond.imageA, bond.imageB, bond.imageC, bond.shiftB);
    }

    std::vector<NativeAtom> visibleAtoms;
    visibleAtoms.reserve(m_cachedAtomImages.size());
    for (const auto& image : m_cachedAtomImages) {
        if (image.atom < 0 || image.atom >= static_cast<int>(m_structure.atoms.size())) {
            continue;
        }
        NativeAtom atom = m_structure.atoms[static_cast<std::size_t>(image.atom)];
        atom.cartesian += image.shift;
        visibleAtoms.push_back(atom);
    }

    if (visibleAtoms.empty()) {
        m_cachedCenter = (m_structure.cellVectors[0] + m_structure.cellVectors[1] + m_structure.cellVectors[2]) * 0.5f;
        for (int mask = 0; mask < 8; ++mask) {
            m_cachedRadius = std::max(
                m_cachedRadius,
                static_cast<double>((cellPoint(m_structure.cellVectors, mask) - m_cachedCenter).length()));
        }
        return;
    }
    m_cachedMaxAtomRadius = 0.05;
    for (const auto& atom : visibleAtoms) {
        m_cachedCenter += atom.cartesian;
        m_cachedMaxAtomRadius = std::max(m_cachedMaxAtomRadius, std::max(0.05, atom.radius));
    }
    m_cachedCenter /= static_cast<float>(visibleAtoms.size());
    for (const auto& atom : visibleAtoms) {
        m_cachedRadius = std::max(m_cachedRadius, static_cast<double>((atom.cartesian - m_cachedCenter).length()));
    }
    // VESTA fits the visible model, not the entire crystallographic cell box.
    // Keeping all cell corners in the fit radius zoomed large unit cells out
    // too far and made atoms look much smaller than VESTA's ball-and-stick view.
    m_cachedRadius += m_cachedMaxAtomRadius * kAtomRadiusSceneFactor;
    const double neighborCellSize = std::max(
        0.5,
        maximumBondCutoffWithCustomRanges(m_displayOptions.customBondRanges));
    m_cachedNearestAtomDistance = typicalNearestNeighborDistance(visibleAtoms, neighborCellSize);
}

std::vector<StructureCanvas::BondSegment> StructureCanvas::buildBondPairs() const {
    std::vector<BondSegment> bonds;
    if (!m_displayOptions.showBonds) {
        return bonds;
    }
    if (m_structure.atoms.empty()) {
        return bonds;
    }
    const auto& atoms = m_structure.atoms;
    const bool usePeriodicImages = m_displayOptions.showOutsideCell && hasNonDegenerateCell(m_structure.cellVectors);
    const int minImage = usePeriodicImages ? -1 : 0;
    const int maxImage = usePeriodicImages ? 1 : 0;
    const double cellSize = std::max(0.5, maximumBondCutoffWithCustomRanges(m_displayOptions.customBondRanges));

    struct BondCandidate {
        int atom = -1;
        int imageA = 0;
        int imageB = 0;
        int imageC = 0;
        QVector3D shift;
        QVector3D cartesian;
    };

    std::vector<BondCandidate> candidates;
    candidates.reserve(atoms.size() * static_cast<std::size_t>(usePeriodicImages ? 27 : 1));
    QHash<SpatialCellKey, std::vector<int>> grid;
    grid.reserve(static_cast<int>(atoms.size() * static_cast<std::size_t>(usePeriodicImages ? 27 : 1)));

    for (int j = 0; j < static_cast<int>(atoms.size()); ++j) {
        const auto& atom = atoms[static_cast<std::size_t>(j)];
        if (!shouldDisplayAtom(atom, m_displayOptions, m_structure.cellVectors)) {
            continue;
        }
        for (int imageA = minImage; imageA <= maxImage; ++imageA) {
            for (int imageB = minImage; imageB <= maxImage; ++imageB) {
                for (int imageC = minImage; imageC <= maxImage; ++imageC) {
                    const QVector3D shift = cellTranslation(m_structure.cellVectors, imageA, imageB, imageC);
                    const QVector3D cartesian = atom.cartesian + shift;
                    const int candidateIndex = static_cast<int>(candidates.size());
                    candidates.push_back({j, imageA, imageB, imageC, shift, cartesian});
                    grid[spatialCellIndex(cartesian, cellSize)].push_back(candidateIndex);
                }
            }
        }
    }

    for (int i = 0; i < static_cast<int>(atoms.size()); ++i) {
        const auto& a = atoms[static_cast<std::size_t>(i)];
        if (!shouldDisplayAtom(a, m_displayOptions, m_structure.cellVectors)) {
            continue;
        }
        const auto base = spatialCellIndex(a.cartesian, cellSize);
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dz = -1; dz <= 1; ++dz) {
                    const SpatialCellKey neighbor{base.x + dx, base.y + dy, base.z + dz};
                    const auto it = grid.constFind(neighbor);
                    if (it == grid.cend()) {
                        continue;
                    }
                    for (int candidateIndex : it.value()) {
                        const auto& candidate = candidates[static_cast<std::size_t>(candidateIndex)];
                        const bool primaryImage = candidate.imageA == 0 && candidate.imageB == 0 && candidate.imageC == 0;
                        if (primaryImage && candidate.atom <= i) {
                            continue;
                        }
                        const auto& b = atoms[static_cast<std::size_t>(candidate.atom)];
                        BondDistanceRange range;
                        if (!effectiveBondRange(m_displayOptions.customBondRanges, a.element, b.element, &range)) {
                            continue;
                        }
                        const QVector3D diff = candidate.cartesian - a.cartesian;
                        const double dist = diff.length();
                        if (distanceInRange(dist, range)) {
                            bonds.push_back({
                                i,
                                candidate.atom,
                                candidate.imageA,
                                candidate.imageB,
                                candidate.imageC,
                                candidate.shift,
                                dist});
                        }
                    }
                }
            }
        }
    }
    return bonds;
}

int StructureCanvas::pickAtomAt(const QPoint& pos) const {
    const auto candidates = pickAtomsAt(pos);
    return candidates.empty() ? -1 : candidates.front();
}

std::vector<int> StructureCanvas::pickAtomsAt(const QPoint& pos) const {
    if (m_structure.atoms.empty()) {
        return {};
    }
    const QRectF viewport = rect().adjusted(18, 18, -18, -18);
    const QVector3D center = sceneCenter();
    const double scale = sceneScale(viewport, center);
    const double densityScale = atomRadiusDensityScale(
        scale,
        m_displayOptions.atomScale,
        m_cachedNearestAtomDistance,
        m_cachedMaxAtomRadius);

    struct Candidate {
        int atomId = -1;
        double depth = 0.0;
        double distance = 0.0;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(m_cachedAtomImages.size());
    for (const auto& image : m_cachedAtomImages) {
        if (image.atom < 0 || image.atom >= static_cast<int>(m_structure.atoms.size())) {
            continue;
        }
        const auto& atom = m_structure.atoms[static_cast<std::size_t>(image.atom)];
        const QVector3D cartesian = atom.cartesian + image.shift;
        const auto rotated = rotatePoint(cartesian - center);
        const double perspective = depthPerspective(rotated.z());
        const QPointF point = projectPoint(cartesian - center, viewport, scale);
        const double radius = hitAtomRadius(atom, scale, perspective, m_displayOptions.atomScale, densityScale);
        const double distance = QLineF(point, pos).length();
        if (distance > radius + 6.0) {
            continue;
        }
        candidates.push_back({atom.atomId, rotated.z(), distance});
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        // The canvas renders atoms as opaque 2-D sphere billboards. For that
        // representation the whole atom must be ordered by its center depth;
        // using the front surface depth makes large rear atoms jump in front
        // of smaller front atoms while the view is rotated.
        if (std::abs(a.depth - b.depth) > 1.0e-6) {
            return a.depth > b.depth;
        }
        if (std::abs(a.distance - b.distance) > 1.0e-6) {
            return a.distance < b.distance;
        }
        return a.atomId < b.atomId;
    });

    std::vector<int> atomIds;
    atomIds.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        if (std::find(atomIds.begin(), atomIds.end(), candidate.atomId) != atomIds.end()) {
            continue;
        }
        atomIds.push_back(candidate.atomId);
    }
    return atomIds;
}

int StructureCanvas::pickNextCtrlAtomAt(const QPoint& pos) const {
    const auto candidates = pickAtomsAt(pos);
    for (int atomId : candidates) {
        if (!isAtomSelected(atomId)) {
            return atomId;
        }
    }
    return candidates.empty() ? -1 : candidates.front();
}

void StructureCanvas::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.fillRect(rect(), backgroundColor());

    const QRectF viewport = rect().adjusted(18, 18, -18, -18);
    const QVector3D center = sceneCenter();
    const double scale = sceneScale(viewport, center);
    const double densityScale = atomRadiusDensityScale(
        scale,
        m_displayOptions.atomScale,
        m_cachedNearestAtomDistance,
        m_cachedMaxAtomRadius);

    if (m_displayOptions.showCell) {
        painter.setPen(QPen(QColor("#202020"), 1.1));
        const int edgePairs[][2] = {{0,1},{0,2},{0,4},{1,3},{1,5},{2,3},{2,6},{3,7},{4,5},{4,6},{5,7},{6,7}};
        for (const auto& pair : edgePairs) {
            const auto a = cellPoint(m_structure.cellVectors, pair[0]) - center;
            const auto b = cellPoint(m_structure.cellVectors, pair[1]) - center;
            painter.drawLine(projectPoint(a, viewport, scale), projectPoint(b, viewport, scale));
        }
    }

    if (m_structure.atoms.empty()) {
        painter.setPen(QColor("#404040"));
        painter.drawText(
            viewport,
            Qt::AlignCenter,
            m_japanese
                ? QStringLiteral("構造ファイルを開くか、ここへドロップしてください。\n対応: ASE project, CIF, XYZ, POSCAR/CONTCAR, PDB, XSF")
                : QStringLiteral("Open or drop a structure file to start.\nSupported: ASE project, CIF, XYZ, POSCAR/CONTCAR, PDB, XSF."));
        painter.drawText(
            rect().adjusted(18, 0, -18, -14),
            Qt::AlignBottom | Qt::AlignLeft,
            m_japanese
                ? QStringLiteral("クリック: 開く   左ドラッグ: 回転   右/中ドラッグ: パン   ホイール: ズーム")
                : QStringLiteral("Click: open file   Left drag: rotate   Right/Middle drag: pan   Wheel: zoom"));
        return;
    }

    struct PaintedAtom {
        QPointF pos;
        double depth;
        double radius;
        QColor color;
        bool focus;
        bool selected;
        bool preview;
        int atomId;
        int selectionOrder;
        QString label;
    };

    struct PaintedBond {
        QPointF a;
        QPointF b;
        double depth;
        QColor colorA;
        QColor colorB;
        bool preview;
    };

    struct PaintItem {
        double depth;
        bool atom;
        int index;
    };

    std::vector<PaintedBond> paintBonds;
    if (m_displayOptions.showBonds) {
        paintBonds.reserve(m_cachedBonds.size());
        for (const auto& bond : m_cachedBonds) {
            const auto& a = m_structure.atoms[static_cast<std::size_t>(bond.atomA)];
            const auto& b = m_structure.atoms[static_cast<std::size_t>(bond.atomB)];
            const QVector3D aCart = a.cartesian - center;
            const QVector3D bCart = b.cartesian + bond.shiftB - center;
            const QVector3D diff = bCart - aCart;
            const double length = static_cast<double>(diff.length());
            if (length <= 1.0e-6) {
                continue;
            }
            const QVector3D direction = diff / static_cast<float>(length);
            const double trimA = visualAtomRadius(a, m_displayOptions.atomScale, densityScale);
            const double trimB = visualAtomRadius(b, m_displayOptions.atomScale, densityScale);
            if (length <= trimA + trimB) {
                continue;
            }
            const QVector3D aEdge = aCart + direction * static_cast<float>(trimA);
            const QVector3D bEdge = bCart - direction * static_cast<float>(trimB);
            const auto aRot = rotatePoint(aEdge);
            const auto bRot = rotatePoint(bEdge);
            paintBonds.push_back({
                projectPoint(aEdge, viewport, scale),
                projectPoint(bEdge, viewport, scale),
                (aRot.z() + bRot.z()) * 0.5 - 0.35,
                a.color,
                b.color,
                false
            });
        }
        for (int i = 0; i < static_cast<int>(m_previewAtoms.size()); ++i) {
            for (int j = i + 1; j < static_cast<int>(m_previewAtoms.size()); ++j) {
                const auto& a = m_previewAtoms[static_cast<std::size_t>(i)];
                const auto& b = m_previewAtoms[static_cast<std::size_t>(j)];
                if (!shouldDisplayAtom(a, m_displayOptions, m_structure.cellVectors)
                    || !shouldDisplayAtom(b, m_displayOptions, m_structure.cellVectors)) {
                    continue;
                }
                BondDistanceRange range;
                if (!effectiveBondRange(m_displayOptions.customBondRanges, a.element, b.element, &range)) {
                    range = BondDistanceRange{0.0, (vestaElementRadius(a.element) + vestaElementRadius(b.element)) * 0.85};
                }
                const QVector3D diff = b.cartesian - a.cartesian;
                const double distance = diff.length();
                range.maxDistance += 0.15;
                if (distance <= 1.0e-6 || !distanceInRange(distance, range)) {
                    continue;
                }
                const QVector3D direction = diff / static_cast<float>(distance);
                const double trimA = visualAtomRadius(a, m_displayOptions.atomScale, densityScale);
                const double trimB = visualAtomRadius(b, m_displayOptions.atomScale, densityScale);
                if (distance <= trimA + trimB) {
                    continue;
                }
                const QVector3D aEdge = a.cartesian - center + direction * static_cast<float>(trimA);
                const QVector3D bEdge = b.cartesian - center - direction * static_cast<float>(trimB);
                const auto aRot = rotatePoint(aEdge);
                const auto bRot = rotatePoint(bEdge);
                paintBonds.push_back({
                    projectPoint(aEdge, viewport, scale),
                    projectPoint(bEdge, viewport, scale),
                    (aRot.z() + bRot.z()) * 0.5 - 0.35,
                    QColor("#2D7FF9"),
                    QColor("#2D7FF9"),
                    true
                });
            }
        }
    }

    std::vector<PaintedAtom> paintAtoms;
    paintAtoms.reserve(m_cachedAtomImages.size() + m_previewAtoms.size());
    for (const auto& image : m_cachedAtomImages) {
        if (image.atom < 0 || image.atom >= static_cast<int>(m_structure.atoms.size())) {
            continue;
        }
        const auto& atom = m_structure.atoms[static_cast<std::size_t>(image.atom)];
        const QVector3D cartesian = atom.cartesian + image.shift;
        const auto rotated = rotatePoint(cartesian - center);
        const auto selectedIt = std::find(m_selectedAtomIds.begin(), m_selectedAtomIds.end(), atom.atomId);
        const bool primaryImage = image.imageA == 0 && image.imageB == 0 && image.imageC == 0;
        const bool selected = primaryImage && selectedIt != m_selectedAtomIds.end();
        paintAtoms.push_back({
            projectPoint(cartesian - center, viewport, scale),
            rotated.z(),
            screenAtomRadius(atom, scale, depthPerspective(rotated.z()), m_displayOptions.atomScale, densityScale),
            atom.color,
            primaryImage && atom.atomId == m_focusAtomId,
            selected,
            false,
            atom.atomId,
            selected ? static_cast<int>(std::distance(m_selectedAtomIds.begin(), selectedIt)) + 1 : 0,
            atom.tag.trimmed().isEmpty()
                ? QString("%1%2").arg(atom.element).arg(atom.atomId)
                : atom.tag
        });
    }
    for (const auto& atom : m_previewAtoms) {
        if (!shouldDisplayAtom(atom, m_displayOptions, m_structure.cellVectors)) {
            continue;
        }
        const auto rotated = rotatePoint(atom.cartesian - center);
        QColor previewColor = atom.color.isValid() ? atom.color : QColor("#2D7FF9");
        previewColor.setAlpha(105);
        paintAtoms.push_back({
            projectPoint(atom.cartesian - center, viewport, scale),
            rotated.z(),
            screenAtomRadius(atom, scale, depthPerspective(rotated.z()), m_displayOptions.atomScale, densityScale),
            previewColor,
            false,
            false,
            true,
            atom.atomId,
            0,
            atom.tag
        });
    }

    std::vector<PaintItem> paintItems;
    paintItems.reserve(paintBonds.size() + paintAtoms.size());
    for (int i = 0; i < static_cast<int>(paintBonds.size()); ++i) {
        paintItems.push_back({paintBonds[static_cast<std::size_t>(i)].depth, false, i});
    }
    for (int i = 0; i < static_cast<int>(paintAtoms.size()); ++i) {
        paintItems.push_back({paintAtoms[static_cast<std::size_t>(i)].depth, true, i});
    }
    std::sort(paintItems.begin(), paintItems.end(), [](const PaintItem& a, const PaintItem& b) {
        if (std::abs(a.depth - b.depth) > 1.0e-9) {
            return a.depth < b.depth;  // back-to-front: larger depth is closer to the viewer
        }
        if (a.atom != b.atom) {
            return !a.atom && b.atom;  // draw bonds first at the same depth so atoms cap bond ends
        }
        return a.index < b.index;
    });

    auto drawTextOutline = [&](const QPointF& centerPoint, const QString& text, const QColor& fillColor, const QColor& outlineColor, const QFont& font) {
        painter.setFont(font);
        const QFontMetrics fm(font);
        const QRectF box = fm.boundingRect(text).adjusted(-8, -4, 8, 4);
        QRectF rect(centerPoint.x() - box.width() * 0.5, centerPoint.y() - box.height() * 0.5, box.width(), box.height());
        painter.setPen(outlineColor);
        painter.drawText(rect.translated(-1, 0), Qt::AlignCenter, text);
        painter.drawText(rect.translated(1, 0), Qt::AlignCenter, text);
        painter.drawText(rect.translated(0, -1), Qt::AlignCenter, text);
        painter.drawText(rect.translated(0, 1), Qt::AlignCenter, text);
        painter.setPen(fillColor);
        painter.drawText(rect, Qt::AlignCenter, text);
    };

    auto drawBond = [&](const PaintedBond& bond) {
        if (bond.preview) {
            QColor bondColor("#2D7FF9");
            bondColor.setAlpha(150);
            painter.setPen(QPen(bondColor, kPreviewBondWidthPixels, Qt::DashLine));
            painter.drawLine(bond.a, bond.b);
            return;
        }
        QColor colorA = bond.colorA.isValid() ? bond.colorA : QColor("#707070");
        QColor colorB = bond.colorB.isValid() ? bond.colorB : QColor("#707070");
        if (m_displayOptions.depthCue) {
            const int alpha = static_cast<int>(std::clamp(140.0 + bond.depth * 8.0, 90.0, 220.0));
            colorA.setAlpha(alpha);
            colorB.setAlpha(alpha);
        } else {
            colorA.setAlpha(255);
            colorB.setAlpha(255);
        }
        QLinearGradient gradient(bond.a, bond.b);
        gradient.setColorAt(0.0, colorA);
        gradient.setColorAt(0.499, colorA);
        gradient.setColorAt(0.501, colorB);
        gradient.setColorAt(1.0, colorB);
        painter.setPen(QPen(QBrush(gradient), kBondWidthPixels, Qt::SolidLine, Qt::FlatCap));
        painter.drawLine(bond.a, bond.b);
    };

    auto drawAtom = [&](const PaintedAtom& atom) {
        QColor atomColor = atom.color;
        if (!atom.preview) {
            // Normal atoms must be fully opaque. A semi-transparent highlight can
            // let a previously drawn rear atom bleed through the front atom and
            // visually look like an incorrect z-order.
            atomColor.setAlpha(255);
        }
        if (atom.preview) {
            painter.setPen(QPen(QColor("#2D7FF9"), 1.8, Qt::DashLine));
        } else {
            const double outlineWidth = std::clamp(atom.radius * 0.18, 0.25, 1.0);
            const double focusWidth = std::clamp(atom.radius * 0.34, 0.8, 2.0);
            painter.setPen(atom.focus ? QPen(QColor("#000000"), focusWidth) : QPen(QColor("#404040"), outlineWidth));
        }
        QRadialGradient gradient(atom.pos - QPointF(atom.radius * 0.35, atom.radius * 0.35), atom.radius * 1.10);
        gradient.setColorAt(0.0, atom.preview ? QColor(255, 255, 255, 185) : QColor(255, 255, 255, 255));
        gradient.setColorAt(0.18, atomColor.lighter(185));
        gradient.setColorAt(0.55, atomColor.lighter(128));
        gradient.setColorAt(0.90, atomColor);
        gradient.setColorAt(1.0, atomColor.darker(112));
        painter.setBrush(gradient);
        painter.drawEllipse(atom.pos, atom.radius, atom.radius);
        if (atom.preview) {
            painter.setPen(QPen(QColor("#2D7FF9"), 1.2, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(atom.pos, atom.radius + 4.0, atom.radius + 4.0);
            if (!atom.label.trimmed().isEmpty()) {
                QFont font = painter.font();
                font.setBold(true);
                font.setPointSizeF(8.5);
                drawTextOutline(
                    atom.pos + QPointF(0.0, -atom.radius - 14.0),
                    atom.label,
                    QColor("#2D7FF9"),
                    QColor("#FFFFFF"),
                    font);
            }
            return;
        }
        if (atom.selected) {
            painter.setPen(QPen(QColor("#FFD400"), atom.focus ? 3.4 : 2.8));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(atom.pos, atom.radius + 5.0, atom.radius + 5.0);
            painter.setPen(QPen(QColor("#7A5A00"), 1.1));
            painter.drawEllipse(atom.pos, atom.radius + 8.0, atom.radius + 8.0);
            if (atom.selectionOrder > 0) {
                const double badgeRadius = std::clamp(atom.radius * 0.23, 8.0, 13.0);
                const QPointF badgeCenter = atom.pos + QPointF(atom.radius * 0.64, -atom.radius * 0.64);
                painter.setPen(QPen(QColor("#7A5A00"), 1.1));
                painter.setBrush(QColor("#FFD400"));
                painter.drawEllipse(badgeCenter, badgeRadius, badgeRadius);
                QFont badgeFont = painter.font();
                badgeFont.setBold(true);
                badgeFont.setPointSizeF(std::clamp(badgeRadius * 0.92, 7.0, 11.0));
                painter.setFont(badgeFont);
                painter.setPen(QColor("#202020"));
                painter.drawText(
                    QRectF(
                        badgeCenter.x() - badgeRadius,
                        badgeCenter.y() - badgeRadius,
                        badgeRadius * 2.0,
                        badgeRadius * 2.0),
                    Qt::AlignCenter,
                    QString::number(atom.selectionOrder));
            }
        }
        if (atom.focus) {
            painter.setPen(QPen(QColor("#000000"), 1.2));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(atom.pos, atom.radius + 6.0, atom.radius + 6.0);
        }
        const bool shouldLabel = m_displayOptions.showLabels
            && (m_structure.atoms.size() <= 2000 || atom.selected || atom.focus);
        if (shouldLabel) {
            QFont font = painter.font();
            font.setBold(true);
            font.setPointSizeF(std::clamp(atom.radius * 0.14, 7.0, 10.5));
            drawTextOutline(
                atom.pos,
                atom.label,
                QColor("#FFFFFF"),
                QColor("#000000"),
                font);
        }
    };

    for (const auto& item : paintItems) {
        if (item.atom) {
            drawAtom(paintAtoms[static_cast<std::size_t>(item.index)]);
        } else {
            drawBond(paintBonds[static_cast<std::size_t>(item.index)]);
        }
    }

    if (m_displayOptions.showAxes) {
        const QPointF origin(viewport.left() + 58.0, viewport.bottom() - 58.0);
        const double axisLength = 34.0;
        const auto normalizedOr = [](const QVector3D& axis, const QVector3D& fallback) {
            return axis.lengthSquared() > 1.0e-8f ? axis.normalized() : fallback;
        };
        const auto drawAxis = [&](const QVector3D& dir, const QColor& color, const QString& label) {
            const auto rotated = rotatePoint(dir);
            const QPointF end(origin.x() + rotated.x() * axisLength, origin.y() - rotated.y() * axisLength);
            painter.setPen(QPen(color, 2.2));
            painter.drawLine(origin, end);
            painter.setPen(QColor("#000000"));
            painter.drawText(QRectF(end.x() - 8.0, end.y() - 16.0, 16.0, 16.0), Qt::AlignCenter, label);
        };
        painter.setPen(QPen(QColor("#A0A0A0"), 1.0));
        painter.setBrush(QColor("#D0D0D0"));
        painter.drawEllipse(origin, 4.0, 4.0);
        drawAxis(normalizedOr(m_structure.cellVectors[0], QVector3D(1.0f, 0.0f, 0.0f)), QColor("#CC1D1D"), "a");
        drawAxis(normalizedOr(m_structure.cellVectors[1], QVector3D(0.0f, 1.0f, 0.0f)), QColor("#1DA11D"), "b");
        drawAxis(normalizedOr(m_structure.cellVectors[2], QVector3D(0.0f, 0.0f, 1.0f)), QColor("#1D57D9"), "c");
        const QVector3D normal = QVector3D::crossProduct(m_structure.cellVectors[0], m_structure.cellVectors[1]);
        if (normal.lengthSquared() > 1.0e-8f) {
            drawAxis(normal.normalized(), QColor("#FF9900"), "n");
        }
    }

    if (m_ctrlSelectingAtoms && m_dragged) {
        const QRect selection = visibleCtrlSelectionRect();
        if (!selection.isEmpty() && (selection.width() >= 4 || selection.height() >= 4)) {
            painter.setPen(QPen(QColor("#1D57D9"), 1.4, Qt::DashLine));
            painter.setBrush(QColor(29, 87, 217, 32));
            painter.drawRect(selection);
            painter.setBrush(Qt::NoBrush);
        }
    }

    painter.setPen(QColor("#000000"));
    QString footer;
    if (m_japanese) {
        if (m_interactionMode == InteractionMode::MoveModel) {
            footer = QStringLiteral("モデル表示移動モード: 左ドラッグ=モデル全体の表示位置を移動   Ctrl+クリック/ドラッグ=重なりも選択   Esc=選択解除   ホイール/ピンチ=ズーム   F=フィット");
        } else if (m_interactionMode == InteractionMode::MoveAtoms) {
            footer = QStringLiteral("原子移動モード: 左ドラッグ=選択原子を移動   Ctrl+クリック/ドラッグ=重なりも選択   Esc=選択解除   右/中ドラッグ=パン   ホイール/ピンチ=ズーム   F=フィット");
        } else {
            footer = QStringLiteral("視点モード: 左ドラッグ=回転   Ctrl+クリック/ドラッグ=重なりも選択   Esc=選択解除   Shift+左ドラッグ=選択原子を移動   右/中ドラッグ=パン   ホイール/ピンチ=ズーム   F=フィット");
        }
    } else {
        if (m_interactionMode == InteractionMode::MoveModel) {
            footer = QStringLiteral("Move model display mode: Left drag pans the whole model display   Ctrl+click/drag selects overlaps   Esc clears selection   Wheel/pinch zooms   F fits");
        } else if (m_interactionMode == InteractionMode::MoveAtoms) {
            footer = QStringLiteral("Move atoms mode: Left drag moves selected atoms   Ctrl+click/drag selects overlaps   Esc clears selection   Right/Middle drag pans   Wheel/pinch zooms   F fits");
        } else {
            footer = QStringLiteral("Move view mode: Left drag rotates   Ctrl+click/drag selects overlaps   Esc clears selection   Shift+left drag moves selected atoms   Right/Middle drag pans   Wheel/pinch zooms   F fits");
        }
    }
    painter.drawText(
        rect().adjusted(18, 0, -18, -14),
        Qt::AlignBottom | Qt::AlignLeft,
        footer);
}

QRect StructureCanvas::ctrlSelectionRect() const {
    return QRect(m_ctrlSelectionStart, m_ctrlSelectionEnd).normalized();
}

QRect StructureCanvas::visibleCtrlSelectionRect() const {
    const QRect viewport = rect().adjusted(18, 18, -18, -18);
    return ctrlSelectionRect().intersected(viewport);
}

void StructureCanvas::addAtomsInCtrlSelectionRect() {
    if (m_structure.atoms.empty()) {
        return;
    }

    QRect selection = visibleCtrlSelectionRect();
    if (selection.isEmpty() || (selection.width() < 4 && selection.height() < 4)) {
        return;
    }
    const QRectF selectionTarget = QRectF(selection).adjusted(-3.0, -3.0, 3.0, 3.0);

    const QRectF viewport = rect().adjusted(18, 18, -18, -18);
    const QVector3D center = sceneCenter();
    const double scale = sceneScale(viewport, center);
    const double densityScale = atomRadiusDensityScale(
        scale,
        m_displayOptions.atomScale,
        m_cachedNearestAtomDistance,
        m_cachedMaxAtomRadius);
    struct Candidate {
        int atomId = -1;
        double depth = 0.0;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(m_cachedAtomImages.size());
    for (const auto& image : m_cachedAtomImages) {
        if (image.atom < 0 || image.atom >= static_cast<int>(m_structure.atoms.size())) {
            continue;
        }
        const auto& atom = m_structure.atoms[static_cast<std::size_t>(image.atom)];
        if (isAtomSelected(atom.atomId)) {
            continue;
        }
        const QVector3D local = atom.cartesian + image.shift - center;
        const auto rotated = rotatePoint(local);
        const double perspective = depthPerspective(rotated.z());
        const QPointF point = projectPoint(local, viewport, scale);
        const double radius = hitAtomRadius(atom, scale, perspective, m_displayOptions.atomScale, densityScale);
        const QRectF atomBounds(point.x() - radius, point.y() - radius, radius * 2.0, radius * 2.0);
        if (!selectionTarget.contains(point) && !selectionTarget.intersects(atomBounds)) {
            continue;
        }
        candidates.push_back({atom.atomId, rotated.z()});
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (std::abs(a.depth - b.depth) > 1.0e-9) {
            return a.depth > b.depth;  // add near atoms first so focus lands on a visible target
        }
        return a.atomId < b.atomId;
    });
    std::vector<int> emittedAtomIds;
    emittedAtomIds.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        if (isAtomSelected(candidate.atomId)) {
            continue;
        }
        if (std::find(emittedAtomIds.begin(), emittedAtomIds.end(), candidate.atomId) != emittedAtomIds.end()) {
            continue;
        }
        emittedAtomIds.push_back(candidate.atomId);
        emit atomActivated(candidate.atomId);
    }
}

void StructureCanvas::addAtomsAt(const QPoint& pos) {
    for (int atomId : pickAtomsAt(pos)) {
        if (!isAtomSelected(atomId)) {
            emit atomActivated(atomId);
        }
    }
}

bool StructureCanvas::isAtomSelected(int atomId) const {
    return std::find(m_selectedAtomIds.begin(), m_selectedAtomIds.end(), atomId) != m_selectedAtomIds.end();
}

void StructureCanvas::mousePressEvent(QMouseEvent* event) {
    m_mousePressPos = event->pos();
    m_lastMousePos = event->pos();
    m_dragged = false;
    m_draggingSelection = false;
    m_ctrlSelectingAtoms = false;
    m_ctrlPressAtomId = -1;
    m_ctrlSelectionStart = event->pos();
    m_ctrlSelectionEnd = event->pos();
    m_activeButton = event->button();
    if (event->button() == Qt::LeftButton && (event->modifiers() & Qt::ControlModifier)) {
        m_ctrlPressAtomId = pickNextCtrlAtomAt(event->pos());
        m_ctrlSelectingAtoms = true;
        setCursor(Qt::CrossCursor);
        update();
        return;
    }
    if (event->button() == Qt::LeftButton &&
        m_interactionMode == InteractionMode::MoveModel &&
        !(event->modifiers() & Qt::ShiftModifier)) {
        setCursor(Qt::SizeAllCursor);
        return;
    }
    const bool moveAtomsGesture = event->button() == Qt::LeftButton
        && ((event->modifiers() & Qt::ShiftModifier) || m_interactionMode == InteractionMode::MoveAtoms);
    if (moveAtomsGesture) {
        const int atomId = pickAtomAt(event->pos());
        if (atomId > 0) {
            if (!isAtomSelected(atomId)) {
                emit atomPrimarySelected(atomId);
            }
            m_draggingSelection = true;
            setCursor(Qt::SizeAllCursor);
            return;
        }
        if (m_interactionMode == InteractionMode::MoveAtoms) {
            setCursor(Qt::ForbiddenCursor);
            return;
        }
    }
}

void StructureCanvas::mouseMoveEvent(QMouseEvent* event) {
    if (!(event->buttons() & (Qt::LeftButton | Qt::RightButton | Qt::MiddleButton))) {
        return;
    }
    const QPoint delta = event->pos() - m_lastMousePos;
    if ((event->pos() - m_mousePressPos).manhattanLength() > 2) {
        m_dragged = true;
    }
    if (m_ctrlSelectingAtoms && (event->buttons() & Qt::LeftButton)) {
        m_ctrlSelectionEnd = event->pos();
    } else if (m_draggingSelection && (event->buttons() & Qt::LeftButton)) {
        const QRectF viewport = rect().adjusted(18, 18, -18, -18);
        const double scale = sceneScale(viewport, sceneCenter());
        if (scale > 1.0e-8) {
            const QVector3D worldDelta =
                m_viewRight * static_cast<float>(delta.x() / scale)
                - m_viewUp * static_cast<float>(delta.y() / scale);
            emit selectedAtomsTranslated(worldDelta);
        }
    } else if (event->buttons() & Qt::LeftButton) {
        if (m_interactionMode == InteractionMode::View) {
            rotateBasisFromDrag(delta);
        } else if (m_interactionMode == InteractionMode::MoveModel) {
            m_panOffset += QPointF(delta.x(), delta.y());
        }
    } else {
        m_panOffset += QPointF(delta.x(), delta.y());
    }
    m_lastMousePos = event->pos();
    update();
}

void StructureCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        if (event->button() == m_activeButton) {
            m_activeButton = Qt::NoButton;
        }
        return;
    }
    if (m_ctrlSelectingAtoms) {
        m_ctrlSelectionEnd = event->pos();
        if (!m_dragged) {
            if (m_ctrlPressAtomId > 0) {
                emit atomActivated(m_ctrlPressAtomId);
            }
        } else {
            const QRect selection = visibleCtrlSelectionRect();
            if (!selection.isEmpty() && (selection.width() >= 4 || selection.height() >= 4)) {
                addAtomsInCtrlSelectionRect();
            } else {
                addAtomsAt(event->pos());
            }
        }
        m_ctrlSelectingAtoms = false;
        m_ctrlPressAtomId = -1;
        updateInteractionCursor();
        if (event->button() == m_activeButton) {
            m_activeButton = Qt::NoButton;
        }
        update();
        return;
    }
    if (m_draggingSelection) {
        m_draggingSelection = false;
        updateInteractionCursor();
        emit selectedAtomsTranslationFinished();
        if (event->button() == m_activeButton) {
            m_activeButton = Qt::NoButton;
        }
        return;
    }
    if (!m_dragged) {
        if (m_structure.atoms.empty()) {
            emit emptyCanvasActivated();
        }
        const int atomId = pickAtomAt(event->pos());
        if (atomId > 0) {
            emit atomPrimarySelected(atomId);
        }
    }
    if (event->button() == m_activeButton) {
        m_activeButton = Qt::NoButton;
    }
    updateInteractionCursor();
    update();
}

void StructureCanvas::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (m_structure.atoms.empty()) {
            emit emptyCanvasActivated();
            return;
        }
        fitToStructure();
    }
}

void StructureCanvas::wheelEvent(QWheelEvent* event) {
    const double steps = event->angleDelta().y() / 120.0;
    if (std::abs(steps) <= 1.0e-9) {
        event->accept();
        return;
    }
    const double factor = std::max(0.05, 1.0 + steps * 0.12);
    zoomAt(factor, event->position());
    event->accept();
}

bool StructureCanvas::event(QEvent* event) {
    if (event->type() == QEvent::NativeGesture) {
        auto* gesture = static_cast<QNativeGestureEvent*>(event);
        if (gesture->gestureType() == Qt::ZoomNativeGesture) {
            const double value = gesture->value();
            if (std::abs(value) > 1.0e-6) {
                zoomAt(std::clamp(std::exp(value), 0.05, 20.0), gesture->position());
            }
            event->accept();
            return true;
        }
    }

    return QWidget::event(event);
}
