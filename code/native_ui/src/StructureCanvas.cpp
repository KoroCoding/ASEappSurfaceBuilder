#include "StructureCanvas.h"
#include "ElementStyle.h"

#include <QEvent>
#include <QDebug>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <QHash>
#include <QQuaternion>
#include <QRect>
#include <QSurfaceFormat>
#include <QVector2D>
#include <QWheelEvent>
#include <QtMath>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <unordered_set>

namespace {
constexpr double kAtomRadiusSceneFactor = 0.50;
// Some symmetry-expanded CIFs contain almost duplicated sites when an input
// coordinate is close to a special position. Those sub-angstrom artifacts must
// not shrink all atoms or become visible bonds.
constexpr double kMinimumPhysicalAtomDistance = 0.50;
constexpr double kBondWidthPixels = 2.8;
constexpr double kPreviewBondWidthPixels = 2.4;
constexpr double kPickIndexCellSize = 48.0;
constexpr int kGpuSinglePickAtomLimit = 20000;
constexpr int kGpuRectPickAtomLimit = 12000;
constexpr double kGpuRectPickPixelLimit = 180000.0;

qint64 screenGridKey(int x, int y) {
    return (static_cast<qint64>(x) << 32) ^ static_cast<quint32>(y);
}

int screenGridCoord(double value) {
    return static_cast<int>(std::floor(value / kPickIndexCellSize));
}

void setColor(float (&dst)[4], QColor color, double alpha = 1.0) {
    if (!color.isValid()) {
        color = QColor("#707070");
    }
    dst[0] = static_cast<float>(color.redF());
    dst[1] = static_cast<float>(color.greenF());
    dst[2] = static_cast<float>(color.blueF());
    dst[3] = static_cast<float>(std::clamp(alpha, 0.0, 1.0));
}

void setVector(float (&dst)[3], const QVector3D& value) {
    dst[0] = value.x();
    dst[1] = value.y();
    dst[2] = value.z();
}

void setPickColor(float (&dst)[3], int atomId) {
    const unsigned int id = static_cast<unsigned int>(std::max(0, atomId));
    dst[0] = static_cast<float>(id & 0xFFu) / 255.0f;
    dst[1] = static_cast<float>((id >> 8) & 0xFFu) / 255.0f;
    dst[2] = static_cast<float>((id >> 16) & 0xFFu) / 255.0f;
}

int decodePickColor(const unsigned char* rgba) {
    if (rgba == nullptr) {
        return -1;
    }
    const int id = static_cast<int>(rgba[0])
        | (static_cast<int>(rgba[1]) << 8)
        | (static_cast<int>(rgba[2]) << 16);
    return id > 0 ? id : -1;
}

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

double periodicBoundaryThreshold(const QVector3D& cellVector, double cutoff) {
    const double length = std::max(1.0e-6, static_cast<double>(cellVector.length()));
    // A half-cell threshold keeps the optimisation exact even for very small
    // or highly dense cells.  Larger thresholds collapse to the previous
    // all-images behaviour instead of dropping possible boundary bonds.
    return std::clamp(cutoff / length + 1.0e-6, 0.0, 0.5);
}

bool periodicImageCouldBond(
    const NativeAtom& atom,
    int imageA,
    int imageB,
    int imageC,
    double cutoff,
    const std::array<QVector3D, 3>& cell)
{
    const double thresholds[3] = {
        periodicBoundaryThreshold(cell[0], cutoff),
        periodicBoundaryThreshold(cell[1], cutoff),
        periodicBoundaryThreshold(cell[2], cutoff)
    };
    const int images[3] = {imageA, imageB, imageC};
    const double fractional[3] = {
        static_cast<double>(atom.fractional.x()),
        static_cast<double>(atom.fractional.y()),
        static_cast<double>(atom.fractional.z())
    };
    for (int axis = 0; axis < 3; ++axis) {
        if (images[axis] == 0 || thresholds[axis] >= 0.5) {
            continue;
        }
        if (!std::isfinite(fractional[axis])) {
            return true;
        }
        const double wrapped = fractional[axis] - std::floor(fractional[axis]);
        if (images[axis] < 0) {
            if (wrapped < 1.0 - thresholds[axis]) {
                return false;
            }
        } else if (wrapped > thresholds[axis]) {
            return false;
        }
    }
    return true;
}

bool canPrunePeriodicImagesByFractionalBoundary(const std::array<QVector3D, 3>& cell) {
    for (const auto& vector : cell) {
        if (vector.lengthSquared() <= 1.0e-8f) {
            return false;
        }
    }
    for (int i = 0; i < 3; ++i) {
        const QVector3D a = cell[static_cast<std::size_t>(i)].normalized();
        for (int j = i + 1; j < 3; ++j) {
            const QVector3D b = cell[static_cast<std::size_t>(j)].normalized();
            if (std::abs(static_cast<double>(QVector3D::dotProduct(a, b))) > 0.10) {
                return false;
            }
        }
    }
    return true;
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

double screenAtomRadius(const NativeAtom& atom, double scale, double perspective, double atomScale, double densityScale) {
    const double styledRadius = std::max(0.05, atom.radius);
    // Keep atom size purely scene/physics-scaled. A screen-space pixel floor or
    // density-based zoom compensation makes atoms look inflated when zooming out
    // or viewing very large supercells.
    return styledRadius * scale * perspective * kAtomRadiusSceneFactor * atomScale * densityScale;
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

double maximumBondCutoffForAtoms(
    const std::vector<NativeAtom>& atoms,
    const QHash<QString, BondDistanceRange>& customRanges)
{
    std::vector<QString> elements;
    elements.reserve(16);
    QHash<QString, bool> seen;
    for (const auto& atom : atoms) {
        const QString element = vestaNormalizeElement(atom.element);
        if (element.isEmpty() || seen.contains(element)) {
            continue;
        }
        seen.insert(element, true);
        elements.push_back(element);
    }

    double maxCutoff = 0.0;
    for (int i = 0; i < static_cast<int>(elements.size()); ++i) {
        for (int j = i; j < static_cast<int>(elements.size()); ++j) {
            BondDistanceRange range;
            if (effectiveBondRange(customRanges, elements[static_cast<std::size_t>(i)], elements[static_cast<std::size_t>(j)], &range)) {
                maxCutoff = std::max(maxCutoff, range.maxDistance);
            }
        }
    }
    // Fall back to a small search cell when no pair is bondable. This keeps the
    // nearest-neighbour density calculation fast without changing bond output
    // (effectiveBondRange still decides whether a candidate can bond).
    return maxCutoff > 0.0 ? maxCutoff : 1.5;
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

    // Use a robust typical nearest-neighbor distance, not the single closest
    // pair. While inserting or dragging an atom between lattice sites, one
    // temporary near-overlap should not shrink every atom in the scene.
    const auto medianIt = finiteNearestDistances.begin()
        + static_cast<std::ptrdiff_t>(finiteNearestDistances.size() / 2);
    std::nth_element(finiteNearestDistances.begin(), medianIt, finiteNearestDistances.end());
    return *medianIt;
}
}


struct StructureCanvas::OpenGLResources {
    QOpenGLShaderProgram atomProgram;
    QOpenGLShaderProgram lineProgram;
    QOpenGLShaderProgram pickAtomProgram;
    QOpenGLShaderProgram labelProgram;
    QOpenGLVertexArrayObject atomVao;
    QOpenGLVertexArrayObject previewAtomVao;
    QOpenGLVertexArrayObject lineVao;
    QOpenGLVertexArrayObject previewLineVao;
    QOpenGLVertexArrayObject labelVao;
    QOpenGLBuffer atomVertexBuffer{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer atomInstanceBuffer{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer previewAtomInstanceBuffer{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer lineVertexBuffer{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer lineInstanceBuffer{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer previewLineInstanceBuffer{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer labelVertexBuffer{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer labelInstanceBuffer{QOpenGLBuffer::VertexBuffer};
    GLuint labelTexture = 0;
    GLuint pickFramebuffer = 0;
    GLuint pickColorTexture = 0;
    GLuint pickDepthRenderbuffer = 0;
    QSize pickFramebufferSize;
    int maxTextureSize = 4096;
    int atomInstanceCapacityBytes = 0;
    int lineInstanceCapacityBytes = 0;
    int previewAtomInstanceCapacityBytes = 0;
    int previewLineInstanceCapacityBytes = 0;
    int labelInstanceCapacityBytes = 0;
    GLsizei atomInstanceCount = 0;
    GLsizei lineInstanceCount = 0;
    GLsizei previewAtomInstanceCount = 0;
    GLsizei previewLineInstanceCount = 0;
    GLsizei labelInstanceCount = 0;
    bool initialized = false;

    static void setCommonUniforms(QOpenGLShaderProgram& program, const GpuViewState& view) {
        program.setUniformValue("uCanvasPx", QVector2D(static_cast<float>(view.canvasSize.width()), static_cast<float>(view.canvasSize.height())));
        program.setUniformValue("uViewportCenterPx", QVector2D(static_cast<float>(view.viewportCenter.x()), static_cast<float>(view.viewportCenter.y())));
        program.setUniformValue("uSceneCenter", view.sceneCenter);
        program.setUniformValue("uViewRight", view.viewRight);
        program.setUniformValue("uViewUp", view.viewUp);
        program.setUniformValue("uViewForward", view.viewForward);
        program.setUniformValue("uPanPx", QVector2D(static_cast<float>(view.panOffset.x()), static_cast<float>(view.panOffset.y())));
        program.setUniformValue("uScalePxPerScene", view.scale);
        program.setUniformValue("uAtomScale", view.atomScale);
        program.setUniformValue("uDensityScale", view.densityScale);
        program.setUniformValue("uDepthMax", view.depthMax);
        program.setUniformValue("uInvDepthRange", view.invDepthRange);
        program.setUniformValue("uPerspective", view.perspective ? 1 : 0);
        program.setUniformValue("uDepthCue", view.depthCue ? 1 : 0);
    }

    static bool uploadBuffer(QOpenGLBuffer& buffer, int& capacityBytes, const void* data, int byteCount) {
        if (!buffer.bind()) return false;
        if (byteCount <= 0 || data == nullptr) {
            buffer.release();
            return true;
        }
        if (byteCount > capacityBytes) {
            const int newCapacity = std::max(byteCount, std::max(4096, capacityBytes * 2));
            buffer.allocate(nullptr, newCapacity);
            capacityBytes = newCapacity;
        }
        buffer.write(0, data, byteCount);
        buffer.release();
        return true;
    }

    template <typename Instance>
    static bool uploadInstanceRanges(QOpenGLBuffer& buffer, int capacityBytes, const std::vector<Instance>& instances, const std::vector<int>& indices) {
        if (indices.empty()) return true;
        const int totalBytes = static_cast<int>(instances.size() * sizeof(Instance));
        if (totalBytes <= 0 || totalBytes > capacityBytes || !buffer.bind()) return false;

        std::vector<int> sorted = indices;
        std::sort(sorted.begin(), sorted.end());
        sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
        int rangeStart = -1;
        int previous = -1;
        const auto flushRange = [&]() {
            if (rangeStart < 0 || previous < rangeStart) return;
            const int count = previous - rangeStart + 1;
            buffer.write(
                static_cast<int>(rangeStart * sizeof(Instance)),
                instances.data() + rangeStart,
                static_cast<int>(count * sizeof(Instance)));
        };
        for (int index : sorted) {
            if (index < 0 || index >= static_cast<int>(instances.size())) {
                continue;
            }
            if (rangeStart < 0) {
                rangeStart = index;
                previous = index;
                continue;
            }
            if (index == previous + 1) {
                previous = index;
                continue;
            }
            flushRange();
            rangeStart = index;
            previous = index;
        }
        flushRange();
        buffer.release();
        return true;
    }

    bool linkProgram(QOpenGLShaderProgram& program, const char* vertexSource, const char* fragmentSource, const char* name) {
        if (!program.addShaderFromSourceCode(QOpenGLShader::Vertex, vertexSource)
            || !program.addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentSource)
            || !program.link()) {
            qWarning() << "Failed to initialize" << name << "shader:" << program.log();
            return false;
        }
        return true;
    }

    bool initialize(QOpenGLExtraFunctions* f) {
        if (initialized) return true;
        if (f == nullptr) return false;
        GLint textureLimit = 0;
        f->glGetIntegerv(GL_MAX_TEXTURE_SIZE, &textureLimit);
        if (textureLimit > 0) maxTextureSize = textureLimit;

        static constexpr const char* commonProjection = R"GLSL(
float perspectiveForDepth(float z) { return uPerspective != 0 ? 1.0 / max(0.35, 2.6 - z * 0.08) : 1.0; }
float sceneDepthToNdc(float depth) { float farToNear = clamp((uDepthMax - depth) * uInvDepthRange, 0.0, 1.0); return -0.86 + farToNear * 1.72; }
vec3 rotateWorld(vec3 world) { vec3 local = world - uSceneCenter; return vec3(dot(local, uViewRight), dot(local, uViewUp), dot(local, uViewForward)); }
vec2 projectRotated(vec3 rotated) { float p = perspectiveForDepth(rotated.z); return uViewportCenterPx + uPanPx + vec2(rotated.x, -rotated.y) * uScalePxPerScene * p; }
)GLSL";
        Q_UNUSED(commonProjection);

        static constexpr const char* atomVertexShader = R"GLSL(
#version 330 core
layout(location=0) in vec2 aCorner; layout(location=1) in vec3 iCenterWorld; layout(location=2) in float iRadius; layout(location=3) in vec4 iColor;
uniform vec2 uCanvasPx; uniform vec2 uViewportCenterPx; uniform vec3 uSceneCenter; uniform vec3 uViewRight; uniform vec3 uViewUp; uniform vec3 uViewForward; uniform vec2 uPanPx; uniform float uScalePxPerScene; uniform float uAtomScale; uniform float uDensityScale; uniform float uDepthMax; uniform float uInvDepthRange; uniform int uPerspective;
out vec2 vLocal; out vec4 vColor; out float vCenterDepth; out float vVisualRadiusScene;
float perspectiveForDepth(float z){return uPerspective!=0?1.0/max(0.35,2.6-z*0.08):1.0;} float sceneDepthToNdc(float d){float f=clamp((uDepthMax-d)*uInvDepthRange,0.0,1.0);return -0.86+f*1.72;} vec3 rotateWorld(vec3 w){vec3 l=w-uSceneCenter;return vec3(dot(l,uViewRight),dot(l,uViewUp),dot(l,uViewForward));}
void main(){vec3 r=rotateWorld(iCenterWorld);float p=perspectiveForDepth(r.z);vec2 c=uViewportCenterPx+uPanPx+vec2(r.x,-r.y)*uScalePxPerScene*p;float sr=max(0.05,iRadius);float vr=sr*0.50*uAtomScale*uDensityScale;float rp=max(0.0,vr*uScalePxPerScene*p);vec2 pos=c+aCorner*rp;vec2 ndc=vec2(pos.x/uCanvasPx.x*2.0-1.0,1.0-pos.y/uCanvasPx.y*2.0);gl_Position=vec4(ndc,sceneDepthToNdc(r.z),1.0);vLocal=aCorner;vColor=iColor;vCenterDepth=r.z;vVisualRadiusScene=vr;}
)GLSL";
        static constexpr const char* atomFragmentShader = R"GLSL(
#version 330 core
in vec2 vLocal; in vec4 vColor; in float vCenterDepth; in float vVisualRadiusScene; uniform float uDepthMax; uniform float uInvDepthRange; uniform int uDepthCue; out vec4 fragColor;
float sceneDepthToWindow(float d){float f=clamp((uDepthMax-d)*uInvDepthRange,0.0,1.0);float ndc=-0.86+f*1.72;return ndc*0.5+0.5;}
void main(){float r2=dot(vLocal,vLocal);if(r2>1.0)discard;float r=sqrt(r2);float z=sqrt(max(0.0,1.0-r2));vec3 n=normalize(vec3(vLocal.x,-vLocal.y,z));vec3 light=normalize(vec3(-0.48,0.58,0.82));vec3 hi=normalize(vec3(-0.62,0.70,0.68));float diff=max(dot(n,light),0.0);vec3 shaded=vColor.rgb*(0.48+0.58*diff);float h=pow(max(dot(n,hi),0.0),38.0);shaded=mix(shaded,vec3(1.0),h*0.72);float rim=smoothstep(0.78,0.995,r);shaded=mix(shaded,vec3(0.10),rim*0.30);float cue=uDepthCue!=0?clamp((vCenterDepth+uDepthMax)*uInvDepthRange,0.0,1.0):1.0;shaded=mix(vec3(0.96),shaded,0.38+0.62*cue);float a=vColor.a*smoothstep(1.0,0.965,r);if(a<=0.015)discard;gl_FragDepth=sceneDepthToWindow(vCenterDepth+z*vVisualRadiusScene);fragColor=vec4(shaded,a);}
)GLSL";
        static constexpr const char* pickVertexShader = R"GLSL(
#version 330 core
layout(location=0) in vec2 aCorner; layout(location=1) in vec3 iCenterWorld; layout(location=2) in float iRadius; layout(location=4) in vec3 iPickColor;
uniform vec2 uCanvasPx; uniform vec2 uViewportCenterPx; uniform vec3 uSceneCenter; uniform vec3 uViewRight; uniform vec3 uViewUp; uniform vec3 uViewForward; uniform vec2 uPanPx; uniform float uScalePxPerScene; uniform float uAtomScale; uniform float uDensityScale; uniform float uDepthMax; uniform float uInvDepthRange; uniform int uPerspective;
out vec2 vLocal; flat out vec3 vPickColor; out float vCenterDepth; out float vVisualRadiusScene;
float perspectiveForDepth(float z){return uPerspective!=0?1.0/max(0.35,2.6-z*0.08):1.0;} float sceneDepthToNdc(float d){float f=clamp((uDepthMax-d)*uInvDepthRange,0.0,1.0);return -0.86+f*1.72;} vec3 rotateWorld(vec3 w){vec3 l=w-uSceneCenter;return vec3(dot(l,uViewRight),dot(l,uViewUp),dot(l,uViewForward));}
void main(){vec3 r=rotateWorld(iCenterWorld);float p=perspectiveForDepth(r.z);vec2 c=uViewportCenterPx+uPanPx+vec2(r.x,-r.y)*uScalePxPerScene*p;float sr=max(0.05,iRadius);float vr=sr*0.50*uAtomScale*uDensityScale;float rp=max(0.0,vr*uScalePxPerScene*p);vec2 pos=c+aCorner*rp;vec2 ndc=vec2(pos.x/uCanvasPx.x*2.0-1.0,1.0-pos.y/uCanvasPx.y*2.0);gl_Position=vec4(ndc,sceneDepthToNdc(r.z),1.0);vLocal=aCorner;vPickColor=iPickColor;vCenterDepth=r.z;vVisualRadiusScene=vr;}
)GLSL";
        static constexpr const char* pickFragmentShader = R"GLSL(
#version 330 core
in vec2 vLocal; flat in vec3 vPickColor; in float vCenterDepth; in float vVisualRadiusScene; uniform float uDepthMax; uniform float uInvDepthRange; out vec4 fragColor;
float sceneDepthToWindow(float d){float f=clamp((uDepthMax-d)*uInvDepthRange,0.0,1.0);float ndc=-0.86+f*1.72;return ndc*0.5+0.5;}
void main(){float r2=dot(vLocal,vLocal);if(r2>1.0)discard;float z=sqrt(max(0.0,1.0-r2));gl_FragDepth=sceneDepthToWindow(vCenterDepth+z*vVisualRadiusScene);fragColor=vec4(vPickColor,1.0);}
)GLSL";
        static constexpr const char* lineVertexShader = R"GLSL(
#version 330 core
layout(location=0) in vec2 aVertex; layout(location=1) in vec3 iStartWorld; layout(location=2) in vec3 iEndWorld; layout(location=3) in vec2 iTrimRadii; layout(location=4) in float iHalfWidthPx; layout(location=5) in float iDepthBias; layout(location=6) in vec4 iColor; layout(location=7) in float iDash;
uniform vec2 uCanvasPx; uniform vec2 uViewportCenterPx; uniform vec3 uSceneCenter; uniform vec3 uViewRight; uniform vec3 uViewUp; uniform vec3 uViewForward; uniform vec2 uPanPx; uniform float uScalePxPerScene; uniform float uAtomScale; uniform float uDensityScale; uniform float uDepthMax; uniform float uInvDepthRange; uniform int uPerspective;
out vec4 vColor; out float vDash; out float vAlongPx; out float vAcross; out float vDepthScene; out float vHalfWidthPx;
float perspectiveForDepth(float z){return uPerspective!=0?1.0/max(0.35,2.6-z*0.08):1.0;} float sceneDepthToNdc(float d){float f=clamp((uDepthMax-d)*uInvDepthRange,0.0,1.0);return -0.86+f*1.72;} vec3 rotateWorld(vec3 w){vec3 l=w-uSceneCenter;return vec3(dot(l,uViewRight),dot(l,uViewUp),dot(l,uViewForward));} vec2 projectRotated(vec3 r){float p=perspectiveForDepth(r.z);return uViewportCenterPx+uPanPx+vec2(r.x,-r.y)*uScalePxPerScene*p;}
void main(){vec3 segW=iEndWorld-iStartWorld;float lenS=length(segW);vec3 dir=lenS>0.000001?segW/lenS:vec3(1,0,0);float ts=max(0.0,iTrimRadii.x)*0.50*uAtomScale*uDensityScale;float te=max(0.0,iTrimRadii.y)*0.50*uAtomScale*uDensityScale;float mt=max(0.0,lenS*0.46);vec3 sw=iStartWorld+dir*min(ts,mt);vec3 ew=iEndWorld-dir*min(te,mt);vec3 sr=rotateWorld(sw);vec3 er=rotateWorld(ew);vec2 sp=projectRotated(sr);vec2 ep=projectRotated(er);vec2 seg=ep-sp;float lenPx=max(length(seg),0.0001);vec2 d=seg/lenPx;vec2 n=vec2(-d.y,d.x);vec2 pos=mix(sp,ep,aVertex.y)+n*aVertex.x*iHalfWidthPx;vec2 ndc=vec2(pos.x/uCanvasPx.x*2.0-1.0,1.0-pos.y/uCanvasPx.y*2.0);float dep=mix(sr.z,er.z,aVertex.y)+iDepthBias;gl_Position=vec4(ndc,sceneDepthToNdc(dep),1.0);vColor=iColor;vDash=iDash;vAlongPx=aVertex.y*lenPx;vAcross=aVertex.x;vDepthScene=dep;vHalfWidthPx=iHalfWidthPx;}
)GLSL";
        static constexpr const char* lineFragmentShader = R"GLSL(
#version 330 core
in vec4 vColor; in float vDash; in float vAlongPx; in float vAcross; in float vDepthScene; in float vHalfWidthPx; uniform float uScalePxPerScene; uniform float uDepthMax; uniform float uInvDepthRange; uniform int uDepthCue; out vec4 fragColor;
float sceneDepthToWindow(float d){float f=clamp((uDepthMax-d)*uInvDepthRange,0.0,1.0);float ndc=-0.86+f*1.72;return ndc*0.5+0.5;}
void main(){if(vDash>0.5&&mod(vAlongPx,12.0)>7.0)discard;float aa=abs(vAcross);if(aa>1.0)discard;float cz=sqrt(max(0.0,1.0-aa*aa));vec3 n=normalize(vec3(vAcross,0.18,cz));vec3 light=normalize(vec3(-0.38,0.52,0.76));float diff=max(dot(n,light),0.0);vec3 shaded=vColor.rgb*(0.58+0.42*diff);float cue=uDepthCue!=0?clamp((vDepthScene+uDepthMax)*uInvDepthRange,0.0,1.0):1.0;shaded=mix(vec3(0.94),shaded,0.42+0.58*cue);float edge=smoothstep(1.0,0.82,aa);float dr=max(0.002,vHalfWidthPx/max(uScalePxPerScene,0.0001));gl_FragDepth=sceneDepthToWindow(vDepthScene+cz*dr);fragColor=vec4(shaded,vColor.a*edge);}
)GLSL";
        static constexpr const char* labelVertexShader = R"GLSL(
#version 330 core
layout(location=0) in vec2 aCorner; layout(location=1) in vec3 iCenterWorld; layout(location=2) in float iDepthBias; layout(location=3) in vec2 iSizePx; layout(location=4) in vec2 iUvMin; layout(location=5) in vec2 iUvMax;
uniform vec2 uCanvasPx; uniform vec2 uViewportCenterPx; uniform vec3 uSceneCenter; uniform vec3 uViewRight; uniform vec3 uViewUp; uniform vec3 uViewForward; uniform vec2 uPanPx; uniform float uScalePxPerScene; uniform float uDepthMax; uniform float uInvDepthRange; uniform int uPerspective; out vec2 vUv;
float perspectiveForDepth(float z){return uPerspective!=0?1.0/max(0.35,2.6-z*0.08):1.0;} float sceneDepthToNdc(float d){float f=clamp((uDepthMax-d)*uInvDepthRange,0.0,1.0);return -0.86+f*1.72;} vec3 rotateWorld(vec3 w){vec3 l=w-uSceneCenter;return vec3(dot(l,uViewRight),dot(l,uViewUp),dot(l,uViewForward));}
void main(){vec3 r=rotateWorld(iCenterWorld);float p=perspectiveForDepth(r.z);vec2 c=uViewportCenterPx+uPanPx+vec2(r.x,-r.y)*uScalePxPerScene*p;vec2 pos=c+aCorner*iSizePx*0.5;vec2 ndc=vec2(pos.x/uCanvasPx.x*2.0-1.0,1.0-pos.y/uCanvasPx.y*2.0);gl_Position=vec4(ndc,sceneDepthToNdc(r.z+iDepthBias),1.0);vec2 tt=(aCorner+vec2(1.0))*0.5;vUv=vec2(mix(iUvMin.x,iUvMax.x,tt.x),mix(iUvMax.y,iUvMin.y,tt.y));}
)GLSL";
        static constexpr const char* labelFragmentShader = R"GLSL(
#version 330 core
in vec2 vUv; uniform sampler2D uLabelAtlas; out vec4 fragColor; void main(){vec4 c=texture(uLabelAtlas,vUv);if(c.a<=0.01)discard;fragColor=c;}
)GLSL";

        if (!linkProgram(atomProgram, atomVertexShader, atomFragmentShader, "atom")) return false;
        if (!linkProgram(pickAtomProgram, pickVertexShader, pickFragmentShader, "atom picking")) return false;
        if (!linkProgram(lineProgram, lineVertexShader, lineFragmentShader, "bond")) return false;
        if (!linkProgram(labelProgram, labelVertexShader, labelFragmentShader, "label")) return false;

        static constexpr float quad[] = {-1.0f,-1.0f, 1.0f,-1.0f, -1.0f,1.0f, 1.0f,1.0f};
        static constexpr float lineQuad[] = {-1.0f,0.0f, 1.0f,0.0f, -1.0f,1.0f, 1.0f,1.0f};

        if (!atomVao.create() || !atomVertexBuffer.create() || !atomInstanceBuffer.create()) return false;
        atomInstanceBuffer.setUsagePattern(QOpenGLBuffer::DynamicDraw);
        atomVao.bind(); atomVertexBuffer.bind(); atomVertexBuffer.allocate(quad, static_cast<int>(sizeof(quad)));
        f->glEnableVertexAttribArray(0); f->glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*static_cast<GLsizei>(sizeof(float)),nullptr); f->glVertexAttribDivisor(0,0);
        atomInstanceBuffer.bind(); const GLsizei as=static_cast<GLsizei>(sizeof(AtomRenderInstance));
        f->glEnableVertexAttribArray(1); f->glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,as,reinterpret_cast<const void*>(offsetof(AtomRenderInstance, center))); f->glVertexAttribDivisor(1,1);
        f->glEnableVertexAttribArray(2); f->glVertexAttribPointer(2,1,GL_FLOAT,GL_FALSE,as,reinterpret_cast<const void*>(offsetof(AtomRenderInstance, radius))); f->glVertexAttribDivisor(2,1);
        f->glEnableVertexAttribArray(3); f->glVertexAttribPointer(3,4,GL_FLOAT,GL_FALSE,as,reinterpret_cast<const void*>(offsetof(AtomRenderInstance, color))); f->glVertexAttribDivisor(3,1);
        f->glEnableVertexAttribArray(4); f->glVertexAttribPointer(4,3,GL_FLOAT,GL_FALSE,as,reinterpret_cast<const void*>(offsetof(AtomRenderInstance, pickColor))); f->glVertexAttribDivisor(4,1);
        atomInstanceBuffer.release(); atomVertexBuffer.release(); atomVao.release();

        if (!previewAtomVao.create() || !previewAtomInstanceBuffer.create()) return false;
        previewAtomInstanceBuffer.setUsagePattern(QOpenGLBuffer::DynamicDraw);
        previewAtomVao.bind(); atomVertexBuffer.bind();
        f->glEnableVertexAttribArray(0); f->glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*static_cast<GLsizei>(sizeof(float)),nullptr); f->glVertexAttribDivisor(0,0);
        previewAtomInstanceBuffer.bind();
        f->glEnableVertexAttribArray(1); f->glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,as,reinterpret_cast<const void*>(offsetof(AtomRenderInstance, center))); f->glVertexAttribDivisor(1,1);
        f->glEnableVertexAttribArray(2); f->glVertexAttribPointer(2,1,GL_FLOAT,GL_FALSE,as,reinterpret_cast<const void*>(offsetof(AtomRenderInstance, radius))); f->glVertexAttribDivisor(2,1);
        f->glEnableVertexAttribArray(3); f->glVertexAttribPointer(3,4,GL_FLOAT,GL_FALSE,as,reinterpret_cast<const void*>(offsetof(AtomRenderInstance, color))); f->glVertexAttribDivisor(3,1);
        f->glEnableVertexAttribArray(4); f->glVertexAttribPointer(4,3,GL_FLOAT,GL_FALSE,as,reinterpret_cast<const void*>(offsetof(AtomRenderInstance, pickColor))); f->glVertexAttribDivisor(4,1);
        previewAtomInstanceBuffer.release(); atomVertexBuffer.release(); previewAtomVao.release();

        if (!lineVao.create() || !lineVertexBuffer.create() || !lineInstanceBuffer.create()) return false;
        lineInstanceBuffer.setUsagePattern(QOpenGLBuffer::DynamicDraw);
        lineVao.bind(); lineVertexBuffer.bind(); lineVertexBuffer.allocate(lineQuad, static_cast<int>(sizeof(lineQuad)));
        f->glEnableVertexAttribArray(0); f->glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*static_cast<GLsizei>(sizeof(float)),nullptr); f->glVertexAttribDivisor(0,0);
        lineInstanceBuffer.bind(); const GLsizei ls=static_cast<GLsizei>(sizeof(LineRenderInstance));
        f->glEnableVertexAttribArray(1); f->glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,ls,reinterpret_cast<const void*>(offsetof(LineRenderInstance, start))); f->glVertexAttribDivisor(1,1);
        f->glEnableVertexAttribArray(2); f->glVertexAttribPointer(2,3,GL_FLOAT,GL_FALSE,ls,reinterpret_cast<const void*>(offsetof(LineRenderInstance, end))); f->glVertexAttribDivisor(2,1);
        f->glEnableVertexAttribArray(3); f->glVertexAttribPointer(3,2,GL_FLOAT,GL_FALSE,ls,reinterpret_cast<const void*>(offsetof(LineRenderInstance, startRadius))); f->glVertexAttribDivisor(3,1);
        f->glEnableVertexAttribArray(4); f->glVertexAttribPointer(4,1,GL_FLOAT,GL_FALSE,ls,reinterpret_cast<const void*>(offsetof(LineRenderInstance, halfWidth))); f->glVertexAttribDivisor(4,1);
        f->glEnableVertexAttribArray(5); f->glVertexAttribPointer(5,1,GL_FLOAT,GL_FALSE,ls,reinterpret_cast<const void*>(offsetof(LineRenderInstance, depthBias))); f->glVertexAttribDivisor(5,1);
        f->glEnableVertexAttribArray(6); f->glVertexAttribPointer(6,4,GL_FLOAT,GL_FALSE,ls,reinterpret_cast<const void*>(offsetof(LineRenderInstance, color))); f->glVertexAttribDivisor(6,1);
        f->glEnableVertexAttribArray(7); f->glVertexAttribPointer(7,1,GL_FLOAT,GL_FALSE,ls,reinterpret_cast<const void*>(offsetof(LineRenderInstance, dash))); f->glVertexAttribDivisor(7,1);
        lineInstanceBuffer.release(); lineVertexBuffer.release(); lineVao.release();

        if (!previewLineVao.create() || !previewLineInstanceBuffer.create()) return false;
        previewLineInstanceBuffer.setUsagePattern(QOpenGLBuffer::DynamicDraw);
        previewLineVao.bind(); lineVertexBuffer.bind();
        f->glEnableVertexAttribArray(0); f->glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*static_cast<GLsizei>(sizeof(float)),nullptr); f->glVertexAttribDivisor(0,0);
        previewLineInstanceBuffer.bind();
        f->glEnableVertexAttribArray(1); f->glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,ls,reinterpret_cast<const void*>(offsetof(LineRenderInstance, start))); f->glVertexAttribDivisor(1,1);
        f->glEnableVertexAttribArray(2); f->glVertexAttribPointer(2,3,GL_FLOAT,GL_FALSE,ls,reinterpret_cast<const void*>(offsetof(LineRenderInstance, end))); f->glVertexAttribDivisor(2,1);
        f->glEnableVertexAttribArray(3); f->glVertexAttribPointer(3,2,GL_FLOAT,GL_FALSE,ls,reinterpret_cast<const void*>(offsetof(LineRenderInstance, startRadius))); f->glVertexAttribDivisor(3,1);
        f->glEnableVertexAttribArray(4); f->glVertexAttribPointer(4,1,GL_FLOAT,GL_FALSE,ls,reinterpret_cast<const void*>(offsetof(LineRenderInstance, halfWidth))); f->glVertexAttribDivisor(4,1);
        f->glEnableVertexAttribArray(5); f->glVertexAttribPointer(5,1,GL_FLOAT,GL_FALSE,ls,reinterpret_cast<const void*>(offsetof(LineRenderInstance, depthBias))); f->glVertexAttribDivisor(5,1);
        f->glEnableVertexAttribArray(6); f->glVertexAttribPointer(6,4,GL_FLOAT,GL_FALSE,ls,reinterpret_cast<const void*>(offsetof(LineRenderInstance, color))); f->glVertexAttribDivisor(6,1);
        f->glEnableVertexAttribArray(7); f->glVertexAttribPointer(7,1,GL_FLOAT,GL_FALSE,ls,reinterpret_cast<const void*>(offsetof(LineRenderInstance, dash))); f->glVertexAttribDivisor(7,1);
        previewLineInstanceBuffer.release(); lineVertexBuffer.release(); previewLineVao.release();

        if (!labelVao.create() || !labelVertexBuffer.create() || !labelInstanceBuffer.create()) return false;
        labelInstanceBuffer.setUsagePattern(QOpenGLBuffer::DynamicDraw);
        labelVao.bind(); labelVertexBuffer.bind(); labelVertexBuffer.allocate(quad, static_cast<int>(sizeof(quad)));
        f->glEnableVertexAttribArray(0); f->glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*static_cast<GLsizei>(sizeof(float)),nullptr); f->glVertexAttribDivisor(0,0);
        labelInstanceBuffer.bind(); const GLsizei lbs=static_cast<GLsizei>(sizeof(LabelRenderInstance));
        f->glEnableVertexAttribArray(1); f->glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,lbs,reinterpret_cast<const void*>(offsetof(LabelRenderInstance, center))); f->glVertexAttribDivisor(1,1);
        f->glEnableVertexAttribArray(2); f->glVertexAttribPointer(2,1,GL_FLOAT,GL_FALSE,lbs,reinterpret_cast<const void*>(offsetof(LabelRenderInstance, depthBias))); f->glVertexAttribDivisor(2,1);
        f->glEnableVertexAttribArray(3); f->glVertexAttribPointer(3,2,GL_FLOAT,GL_FALSE,lbs,reinterpret_cast<const void*>(offsetof(LabelRenderInstance, size))); f->glVertexAttribDivisor(3,1);
        f->glEnableVertexAttribArray(4); f->glVertexAttribPointer(4,2,GL_FLOAT,GL_FALSE,lbs,reinterpret_cast<const void*>(offsetof(LabelRenderInstance, uvMin))); f->glVertexAttribDivisor(4,1);
        f->glEnableVertexAttribArray(5); f->glVertexAttribPointer(5,2,GL_FLOAT,GL_FALSE,lbs,reinterpret_cast<const void*>(offsetof(LabelRenderInstance, uvMax))); f->glVertexAttribDivisor(5,1);
        labelInstanceBuffer.release(); labelVertexBuffer.release(); labelVao.release();

        initialized = true;
        return true;
    }

    void destroyPickFramebuffer(QOpenGLExtraFunctions* f) {
        if (f == nullptr) return;
        if (pickDepthRenderbuffer != 0) { f->glDeleteRenderbuffers(1, &pickDepthRenderbuffer); pickDepthRenderbuffer = 0; }
        if (pickColorTexture != 0) { f->glDeleteTextures(1, &pickColorTexture); pickColorTexture = 0; }
        if (pickFramebuffer != 0) { f->glDeleteFramebuffers(1, &pickFramebuffer); pickFramebuffer = 0; }
        pickFramebufferSize = {};
    }

    void destroy(QOpenGLExtraFunctions* f) {
        destroyPickFramebuffer(f);
        if (f != nullptr && labelTexture != 0) { f->glDeleteTextures(1, &labelTexture); labelTexture = 0; }
        if (previewAtomInstanceBuffer.isCreated()) previewAtomInstanceBuffer.destroy();
        if (atomInstanceBuffer.isCreated()) atomInstanceBuffer.destroy();
        if (atomVertexBuffer.isCreated()) atomVertexBuffer.destroy();
        if (previewLineInstanceBuffer.isCreated()) previewLineInstanceBuffer.destroy();
        if (lineInstanceBuffer.isCreated()) lineInstanceBuffer.destroy();
        if (lineVertexBuffer.isCreated()) lineVertexBuffer.destroy();
        if (labelInstanceBuffer.isCreated()) labelInstanceBuffer.destroy();
        if (labelVertexBuffer.isCreated()) labelVertexBuffer.destroy();
        if (previewAtomVao.isCreated()) previewAtomVao.destroy();
        if (atomVao.isCreated()) atomVao.destroy();
        if (previewLineVao.isCreated()) previewLineVao.destroy();
        if (lineVao.isCreated()) lineVao.destroy();
        if (labelVao.isCreated()) labelVao.destroy();
        atomProgram.removeAllShaders(); lineProgram.removeAllShaders(); pickAtomProgram.removeAllShaders(); labelProgram.removeAllShaders();
        atomInstanceCapacityBytes = lineInstanceCapacityBytes = previewAtomInstanceCapacityBytes = previewLineInstanceCapacityBytes = labelInstanceCapacityBytes = 0;
        atomInstanceCount = lineInstanceCount = previewAtomInstanceCount = previewLineInstanceCount = labelInstanceCount = 0;
        initialized = false;
    }

    bool uploadAtoms(const std::vector<AtomRenderInstance>& instances) {
        const int bytes = static_cast<int>(instances.size() * sizeof(AtomRenderInstance));
        const bool ok = uploadBuffer(atomInstanceBuffer, atomInstanceCapacityBytes, instances.data(), bytes);
        atomInstanceCount = ok ? static_cast<GLsizei>(instances.size()) : 0;
        return ok;
    }
    bool uploadAtomsAt(const std::vector<AtomRenderInstance>& instances, const std::vector<int>& indices) {
        if (atomInstanceCount != static_cast<GLsizei>(instances.size())) return uploadAtoms(instances);
        return uploadInstanceRanges(atomInstanceBuffer, atomInstanceCapacityBytes, instances, indices);
    }
    bool uploadLines(const std::vector<LineRenderInstance>& instances) {
        const int bytes = static_cast<int>(instances.size() * sizeof(LineRenderInstance));
        const bool ok = uploadBuffer(lineInstanceBuffer, lineInstanceCapacityBytes, instances.data(), bytes);
        lineInstanceCount = ok ? static_cast<GLsizei>(instances.size()) : 0;
        return ok;
    }
    bool uploadLinesAt(const std::vector<LineRenderInstance>& instances, const std::vector<int>& indices) {
        if (lineInstanceCount != static_cast<GLsizei>(instances.size())) return uploadLines(instances);
        return uploadInstanceRanges(lineInstanceBuffer, lineInstanceCapacityBytes, instances, indices);
    }
    bool uploadPreviewAtoms(const std::vector<AtomRenderInstance>& instances) {
        const int bytes = static_cast<int>(instances.size() * sizeof(AtomRenderInstance));
        const bool ok = uploadBuffer(previewAtomInstanceBuffer, previewAtomInstanceCapacityBytes, instances.data(), bytes);
        previewAtomInstanceCount = ok ? static_cast<GLsizei>(instances.size()) : 0;
        return ok;
    }
    bool uploadPreviewLines(const std::vector<LineRenderInstance>& instances) {
        const int bytes = static_cast<int>(instances.size() * sizeof(LineRenderInstance));
        const bool ok = uploadBuffer(previewLineInstanceBuffer, previewLineInstanceCapacityBytes, instances.data(), bytes);
        previewLineInstanceCount = ok ? static_cast<GLsizei>(instances.size()) : 0;
        return ok;
    }
    bool uploadLabels(const std::vector<LabelRenderInstance>& instances) {
        const int bytes = static_cast<int>(instances.size() * sizeof(LabelRenderInstance));
        const bool ok = uploadBuffer(labelInstanceBuffer, labelInstanceCapacityBytes, instances.data(), bytes);
        labelInstanceCount = ok ? static_cast<GLsizei>(instances.size()) : 0;
        return ok;
    }
    bool uploadLabelsAt(const std::vector<LabelRenderInstance>& instances, const std::vector<int>& indices) {
        if (labelInstanceCount != static_cast<GLsizei>(instances.size())) return uploadLabels(instances);
        return uploadInstanceRanges(labelInstanceBuffer, labelInstanceCapacityBytes, instances, indices);
    }
    bool uploadLabelAtlas(QOpenGLExtraFunctions* f, const QImage& image) {
        if (f == nullptr) return false;
        if (image.isNull() || image.width() <= 0 || image.height() <= 0) return true;
        if (labelTexture == 0) f->glGenTextures(1, &labelTexture);
        const QImage glImage = image.convertToFormat(QImage::Format_RGBA8888).mirrored(false, true);
        f->glBindTexture(GL_TEXTURE_2D, labelTexture);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        f->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        f->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, glImage.width(), glImage.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, glImage.constBits());
        f->glBindTexture(GL_TEXTURE_2D, 0);
        return true;
    }
    bool ensurePickFramebuffer(QOpenGLExtraFunctions* f, const QSize& size) {
        if (f == nullptr || size.width() <= 0 || size.height() <= 0) return false;
        if (pickFramebuffer != 0 && pickFramebufferSize == size) return true;
        destroyPickFramebuffer(f);
        pickFramebufferSize = size;
        f->glGenFramebuffers(1, &pickFramebuffer); f->glBindFramebuffer(GL_FRAMEBUFFER, pickFramebuffer);
        f->glGenTextures(1, &pickColorTexture); f->glBindTexture(GL_TEXTURE_2D, pickColorTexture);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        f->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size.width(), size.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pickColorTexture, 0);
        f->glGenRenderbuffers(1, &pickDepthRenderbuffer); f->glBindRenderbuffer(GL_RENDERBUFFER, pickDepthRenderbuffer);
        f->glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, size.width(), size.height());
        f->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, pickDepthRenderbuffer);
        const GLenum drawBuffer = GL_COLOR_ATTACHMENT0; f->glDrawBuffers(1, &drawBuffer);
        const GLenum status = f->glCheckFramebufferStatus(GL_FRAMEBUFFER);
        f->glBindRenderbuffer(GL_RENDERBUFFER, 0); f->glBindTexture(GL_TEXTURE_2D, 0); f->glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (status != GL_FRAMEBUFFER_COMPLETE) { qWarning() << "Atom picking framebuffer is incomplete:" << Qt::hex << status; destroyPickFramebuffer(f); return false; }
        return true;
    }
    void drawLines(QOpenGLExtraFunctions* f, GLsizei count, const GpuViewState& view) {
        if (!initialized || count <= 0 || f == nullptr || view.canvasSize.width() <= 0.0 || view.canvasSize.height() <= 0.0) return;
        lineProgram.bind(); setCommonUniforms(lineProgram, view); lineVao.bind(); f->glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, count); lineVao.release(); lineProgram.release();
    }
    void drawAtoms(QOpenGLExtraFunctions* f, GLsizei count, const GpuViewState& view) {
        if (!initialized || count <= 0 || f == nullptr || view.canvasSize.width() <= 0.0 || view.canvasSize.height() <= 0.0) return;
        atomProgram.bind(); setCommonUniforms(atomProgram, view); atomVao.bind(); f->glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, count); atomVao.release(); atomProgram.release();
    }
    void drawPreviewLines(QOpenGLExtraFunctions* f, const GpuViewState& view) {
        if (!initialized || previewLineInstanceCount <= 0 || f == nullptr || view.canvasSize.width() <= 0.0 || view.canvasSize.height() <= 0.0) return;
        lineProgram.bind(); setCommonUniforms(lineProgram, view); previewLineVao.bind(); f->glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, previewLineInstanceCount); previewLineVao.release(); lineProgram.release();
    }
    void drawPreviewAtoms(QOpenGLExtraFunctions* f, const GpuViewState& view) {
        if (!initialized || previewAtomInstanceCount <= 0 || f == nullptr || view.canvasSize.width() <= 0.0 || view.canvasSize.height() <= 0.0) return;
        atomProgram.bind(); setCommonUniforms(atomProgram, view); previewAtomVao.bind(); f->glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, previewAtomInstanceCount); previewAtomVao.release(); atomProgram.release();
    }
    void drawLabels(QOpenGLExtraFunctions* f, GLsizei count, const GpuViewState& view) {
        if (!initialized || count <= 0 || f == nullptr || labelTexture == 0 || view.canvasSize.width() <= 0.0 || view.canvasSize.height() <= 0.0) return;
        labelProgram.bind(); setCommonUniforms(labelProgram, view); labelProgram.setUniformValue("uLabelAtlas", 0); f->glActiveTexture(GL_TEXTURE0); f->glBindTexture(GL_TEXTURE_2D, labelTexture); labelVao.bind(); f->glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, count); labelVao.release(); f->glBindTexture(GL_TEXTURE_2D, 0); labelProgram.release();
    }
    bool renderPickAtoms(QOpenGLExtraFunctions* f, const GpuViewState& view, const QSize& pickSize) {
        if (!initialized || atomInstanceCount <= 0 || f == nullptr || !ensurePickFramebuffer(f, pickSize)) return false;
        f->glBindFramebuffer(GL_FRAMEBUFFER, pickFramebuffer); f->glViewport(0, 0, pickSize.width(), pickSize.height()); f->glClearColor(0.0f,0.0f,0.0f,1.0f); f->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); f->glDisable(GL_BLEND); f->glEnable(GL_DEPTH_TEST); f->glDepthFunc(GL_LEQUAL); f->glDepthMask(GL_TRUE);
        pickAtomProgram.bind(); setCommonUniforms(pickAtomProgram, view); atomVao.bind(); f->glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, atomInstanceCount); atomVao.release(); pickAtomProgram.release(); return true;
    }
    void finishPickRender(QOpenGLExtraFunctions* f) { if (f != nullptr) f->glBindFramebuffer(GL_FRAMEBUFFER, 0); }
};


StructureCanvas::StructureCanvas(QWidget* parent)
    : QOpenGLWidget(parent),
      m_openGL(std::make_unique<OpenGLResources>())
{
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setSamples(4);
    setFormat(format);
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAutoFillBackground(false);
    setBasisFromView(QVector3D(0.0f, 0.0f, 1.0f), QVector3D(0.0f, 1.0f, 0.0f));
}

StructureCanvas::~StructureCanvas() {
    releaseOpenGLResources();
}

void StructureCanvas::releaseOpenGLResources() {
    if (m_openGL == nullptr || !m_openGL->initialized || context() == nullptr) {
        return;
    }
    makeCurrent();
    m_openGL->destroy(this);
    doneCurrent();
}

void StructureCanvas::initializeGL() {
    initializeOpenGLFunctions();
    if (m_openGL == nullptr) {
        m_openGL = std::make_unique<OpenGLResources>();
    }
    m_openGL->initialize(this);
    m_atomRenderInstancesDirty = true;
    m_lineRenderInstancesDirty = true;
    m_previewAtomRenderInstancesDirty = true;
    m_previewLineRenderInstancesDirty = true;
    m_labelRenderInstancesDirty = true;
    m_labelTextureDirty = true;
}

void StructureCanvas::resizeGL(int width, int height) {
    const qreal dpr = devicePixelRatioF();
    glViewport(0, 0, static_cast<GLsizei>(std::max(1, static_cast<int>(std::lround(width * dpr)))),
        static_cast<GLsizei>(std::max(1, static_cast<int>(std::lround(height * dpr)))));
}

void StructureCanvas::setDisplayOptions(const DisplayOptions& options) {
    const bool rebuildBonds = m_displayOptions.showBonds != options.showBonds
        || m_displayOptions.showOutsideCell != options.showOutsideCell
        || m_displayOptions.customBondRanges != options.customBondRanges;
    const bool rebuildStaticInstances = rebuildBonds
        || m_displayOptions.showCell != options.showCell
        || (m_displayOptions.showLabels != options.showLabels && options.showLabels);
    const bool rebuildPreviewInstances = m_displayOptions.customBondRanges != options.customBondRanges
        || m_displayOptions.showBonds != options.showBonds;
    const bool labelVisibilityChanged = m_displayOptions.showLabels != options.showLabels;
    m_displayOptions = options;
    if (rebuildBonds) {
        rebuildSceneCache();
    } else if (rebuildStaticInstances) {
        rebuildRenderInstances();
    }
    if (rebuildPreviewInstances) {
        rebuildPreviewRenderInstances();
    }
    if (labelVisibilityChanged) {
        m_labelAtlasDirty = true;
        if (!m_displayOptions.showLabels) {
            m_labelSources.clear();
            m_labelRenderInstances.clear();
            m_labelRenderSourceIndices.clear();
            m_labelSourceToRenderInstanceIndices.clear();
            m_labelAtlasImage = {};
            m_labelTextureDirty = true;
            m_labelRenderInstancesDirty = true;
            m_dirtyLabelRenderInstanceIndices.clear();
        }
    }
    invalidatePickIndex();
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
    m_selectedAtomOrder.clear();
    updateInteractionCursor();
    rebuildSceneCache();
    rebuildPreviewRenderInstances();
    invalidatePickIndex();
    update();
}

void StructureCanvas::updateStructureCoordinates(const StructureData& structure) {
    const bool canPartiallyUpdateDrag =
        m_structure.atoms.size() == structure.atoms.size()
        && !m_selectedAtomOrder.isEmpty()
        && !m_cachedAtomImages.empty();

    if (canPartiallyUpdateDrag) {
        std::unordered_set<int> selectedAtomIds;
        selectedAtomIds.reserve(static_cast<std::size_t>(m_selectedAtomOrder.size()));
        for (auto it = m_selectedAtomOrder.cbegin(); it != m_selectedAtomOrder.cend(); ++it) {
            selectedAtomIds.insert(it.key());
        }

        m_structure = structure;

        std::vector<int> dirtyAtomInstances;
        std::vector<int> dirtyLabelSources;
        for (int atomId : selectedAtomIds) {
            const auto renderIt = m_atomIdToRenderInstanceIndices.constFind(atomId);
            if (renderIt == m_atomIdToRenderInstanceIndices.cend()) {
                continue;
            }
            for (int atomInstanceIndex : *renderIt) {
                if (atomInstanceIndex < 0
                    || atomInstanceIndex >= static_cast<int>(m_atomRenderInstances.size())
                    || atomInstanceIndex >= static_cast<int>(m_atomRenderInstanceImageIndices.size())) {
                    continue;
                }
                const int imageIndex = m_atomRenderInstanceImageIndices[static_cast<std::size_t>(atomInstanceIndex)];
                if (imageIndex < 0 || imageIndex >= static_cast<int>(m_cachedAtomImages.size())) {
                    continue;
                }
                const auto& image = m_cachedAtomImages[static_cast<std::size_t>(imageIndex)];
                if (image.atom < 0 || image.atom >= static_cast<int>(m_structure.atoms.size())) {
                    continue;
                }
                const auto& atom = m_structure.atoms[static_cast<std::size_t>(image.atom)];
                const QVector3D center = atom.cartesian + image.shift;
                setVector(m_atomRenderInstances[static_cast<std::size_t>(atomInstanceIndex)].center, center);
                dirtyAtomInstances.push_back(atomInstanceIndex);
                if (atomInstanceIndex < static_cast<int>(m_labelSources.size())) {
                    setVector(m_labelSources[static_cast<std::size_t>(atomInstanceIndex)].center, center);
                    dirtyLabelSources.push_back(atomInstanceIndex);
                }
            }
        }

        std::vector<int> dirtyLabelInstances;
        if (!dirtyLabelSources.empty() && !m_labelRenderInstances.empty() && !m_labelSourceToRenderInstanceIndices.empty()) {
            std::sort(dirtyLabelSources.begin(), dirtyLabelSources.end());
            dirtyLabelSources.erase(std::unique(dirtyLabelSources.begin(), dirtyLabelSources.end()), dirtyLabelSources.end());
            for (int sourceIndex : dirtyLabelSources) {
                if (sourceIndex < 0 || sourceIndex >= static_cast<int>(m_labelSourceToRenderInstanceIndices.size())) {
                    continue;
                }
                const auto& labelIndices = m_labelSourceToRenderInstanceIndices[static_cast<std::size_t>(sourceIndex)];
                if (sourceIndex >= static_cast<int>(m_labelSources.size())) {
                    continue;
                }
                const auto& source = m_labelSources[static_cast<std::size_t>(sourceIndex)];
                for (int labelIndex : labelIndices) {
                    if (labelIndex < 0 || labelIndex >= static_cast<int>(m_labelRenderInstances.size())) {
                        continue;
                    }
                    auto& label = m_labelRenderInstances[static_cast<std::size_t>(labelIndex)];
                    label.center[0] = source.center[0];
                    label.center[1] = source.center[1];
                    label.center[2] = source.center[2];
                    dirtyLabelInstances.push_back(labelIndex);
                }
            }
        }

        std::vector<int> dirtyLineInstances;
        if (m_displayOptions.showBonds && !m_atomIdToCachedBondIndices.empty()) {
            std::unordered_set<int> dirtyBonds;
            for (int atomId : selectedAtomIds) {
                const auto bondIt = m_atomIdToCachedBondIndices.constFind(atomId);
                if (bondIt == m_atomIdToCachedBondIndices.cend()) {
                    continue;
                }
                for (int bondIndex : *bondIt) {
                    dirtyBonds.insert(bondIndex);
                }
            }
            for (int bondIndex : dirtyBonds) {
                if (bondIndex < 0
                    || bondIndex >= static_cast<int>(m_cachedBonds.size())
                    || bondIndex >= static_cast<int>(m_cachedBondLineStartIndices.size())
                    || bondIndex >= static_cast<int>(m_cachedBondLineCounts.size())) {
                    continue;
                }
                const int lineIndex = m_cachedBondLineStartIndices[static_cast<std::size_t>(bondIndex)];
                const int lineCount = m_cachedBondLineCounts[static_cast<std::size_t>(bondIndex)];
                if (lineIndex < 0
                    || lineCount <= 0
                    || lineIndex + lineCount > static_cast<int>(m_lineRenderInstances.size())) {
                    continue;
                }
                const auto& bond = m_cachedBonds[static_cast<std::size_t>(bondIndex)];
                if (bond.atomA < 0 || bond.atomA >= static_cast<int>(m_structure.atoms.size()) ||
                    bond.atomB < 0 || bond.atomB >= static_cast<int>(m_structure.atoms.size())) {
                    continue;
                }
                const auto& a = m_structure.atoms[static_cast<std::size_t>(bond.atomA)];
                const auto& b = m_structure.atoms[static_cast<std::size_t>(bond.atomB)];
                const QVector3D aCenter = a.cartesian;
                const QVector3D bCenter = b.cartesian + bond.shiftB;
                if (lineCount == 1) {
                    auto& line = m_lineRenderInstances[static_cast<std::size_t>(lineIndex)];
                    setVector(line.start, aCenter);
                    setVector(line.end, bCenter);
                    line.startRadius = static_cast<float>(std::max(0.05, a.radius));
                    line.endRadius = static_cast<float>(std::max(0.05, b.radius));
                    dirtyLineInstances.push_back(lineIndex);
                } else {
                    const QVector3D mid = (aCenter + bCenter) * 0.5f;
                    auto& firstHalf = m_lineRenderInstances[static_cast<std::size_t>(lineIndex)];
                    auto& secondHalf = m_lineRenderInstances[static_cast<std::size_t>(lineIndex + 1)];
                    setVector(firstHalf.start, aCenter);
                    setVector(firstHalf.end, mid);
                    firstHalf.startRadius = static_cast<float>(std::max(0.05, a.radius));
                    firstHalf.endRadius = 0.0f;
                    setVector(secondHalf.start, mid);
                    setVector(secondHalf.end, bCenter);
                    secondHalf.startRadius = 0.0f;
                    secondHalf.endRadius = static_cast<float>(std::max(0.05, b.radius));
                    dirtyLineInstances.push_back(lineIndex);
                    dirtyLineInstances.push_back(lineIndex + 1);
                }
            }
        }

        if (m_atomRenderInstancesDirty) {
            m_dirtyAtomRenderInstanceIndices.clear();
        } else {
            m_dirtyAtomRenderInstanceIndices.insert(m_dirtyAtomRenderInstanceIndices.end(), dirtyAtomInstances.begin(), dirtyAtomInstances.end());
        }
        if (m_lineRenderInstancesDirty) {
            m_dirtyLineRenderInstanceIndices.clear();
        } else {
            m_dirtyLineRenderInstanceIndices.insert(m_dirtyLineRenderInstanceIndices.end(), dirtyLineInstances.begin(), dirtyLineInstances.end());
        }
        if (m_labelRenderInstancesDirty) {
            m_dirtyLabelRenderInstanceIndices.clear();
        } else {
            m_dirtyLabelRenderInstanceIndices.insert(m_dirtyLabelRenderInstanceIndices.end(), dirtyLabelInstances.begin(), dirtyLabelInstances.end());
        }
        invalidatePickIndex();
        update();
        return;
    }

    m_structure = structure;
    rebuildSceneCache(false);
    rebuildPreviewRenderInstances();
    invalidatePickIndex();
    update();
}

void StructureCanvas::setSelectedAtomIds(const std::vector<int>& atomIds) {
    m_selectedAtomIds = atomIds;
    m_selectedAtomOrder.clear();
    m_selectedAtomOrder.reserve(static_cast<int>(m_selectedAtomIds.size()));
    for (int i = 0; i < static_cast<int>(m_selectedAtomIds.size()); ++i) {
        m_selectedAtomOrder.insert(m_selectedAtomIds[static_cast<std::size_t>(i)], i + 1);
    }
    updateInteractionCursor();
    update();
}

void StructureCanvas::setPreviewAtoms(const std::vector<NativeAtom>& atoms) {
    m_previewAtoms = atoms;
    rebuildPreviewRenderInstances();
    update();
}

void StructureCanvas::resetView() {
    setBasisFromView(QVector3D(0.0f, 0.0f, 1.0f), QVector3D(0.0f, 1.0f, 0.0f));
    m_zoom = 1.0;
    m_panOffset = {};
    m_focusAtomId = -1;
    invalidatePickIndex();
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
    invalidatePickIndex();
    update();
}

void StructureCanvas::focusAtom(int atomId) {
    m_focusAtomId = atomId;
    update();
}

void StructureCanvas::rotateBy(double yawDelta, double pitchDelta) {
    const QPoint delta(static_cast<int>(std::lround(yawDelta)), static_cast<int>(std::lround(pitchDelta)));
    rotateBasisFromDrag(delta);
    invalidatePickIndex();
    update();
}

void StructureCanvas::panBy(double dx, double dy) {
    m_panOffset += QPointF(dx, dy);
    invalidatePickIndex();
    update();
}

void StructureCanvas::zoomBy(double factor) {
    if (factor <= 0.0) {
        return;
    }
    m_zoom = std::clamp(m_zoom * factor, 0.12, 10.0);
    invalidatePickIndex();
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
    m_zoom = std::clamp(m_zoom * factor, 0.12, 10.0);
    const double appliedFactor = oldZoom <= 1.0e-9 ? 1.0 : (m_zoom / oldZoom);
    m_panOffset += (focusFromViewCenter - m_panOffset) * (1.0 - appliedFactor);
    invalidatePickIndex();
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
    invalidatePickIndex();
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
    invalidatePickIndex();
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
    QQuaternion rotation = QQuaternion::fromAxisAndAngle(worldAxis, static_cast<float>(distance * 0.42));
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
    invalidatePickIndex();
}

void StructureCanvas::rebuildSceneCache(bool rebuildBonds) {
    invalidatePickIndex();
    const double previousNearestAtomDistance = m_cachedNearestAtomDistance;
    if (rebuildBonds) {
        m_cachedBonds.clear();
    }
    m_cachedAtomImages.clear();
    m_cachedCenter = {};
    m_cachedRadius = 1.0;
    m_cachedNearestAtomDistance = 0.0;
    m_cachedMaxAtomRadius = 1.0;

    if (rebuildBonds) {
        m_cachedBonds = buildBondPairs();
    }

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
        if (!rebuildBonds) {
            m_cachedNearestAtomDistance = previousNearestAtomDistance;
        }
        rebuildRenderInstances();
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
        maximumBondCutoffForAtoms(visibleAtoms, m_displayOptions.customBondRanges));
    m_cachedNearestAtomDistance = (rebuildBonds || previousNearestAtomDistance <= 0.0)
        ? typicalNearestNeighborDistance(visibleAtoms, neighborCellSize)
        : previousNearestAtomDistance;
    rebuildRenderInstances();
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
    const bool usePeriodicImages = m_displayOptions.showOutsideCell
        && hasNonDegenerateCell(m_structure.cellVectors);
    const bool prunePeriodicImages = usePeriodicImages
        && canPrunePeriodicImagesByFractionalBoundary(m_structure.cellVectors);
    const int minImage = usePeriodicImages ? -1 : 0;
    const int maxImage = usePeriodicImages ? 1 : 0;
    const double cellSize = std::max(0.5, maximumBondCutoffForAtoms(atoms, m_displayOptions.customBondRanges));

    std::vector<int> atomElementTypes(atoms.size(), -1);
    std::vector<QString> elementTypes;
    elementTypes.reserve(16);
    QHash<QString, int> elementTypeByName;
    for (int i = 0; i < static_cast<int>(atoms.size()); ++i) {
        const QString element = vestaNormalizeElement(atoms[static_cast<std::size_t>(i)].element);
        int typeIndex = elementTypeByName.value(element, -1);
        if (typeIndex < 0) {
            typeIndex = static_cast<int>(elementTypes.size());
            elementTypes.push_back(element);
            elementTypeByName.insert(element, typeIndex);
        }
        atomElementTypes[static_cast<std::size_t>(i)] = typeIndex;
    }

    struct CachedBondRange {
        bool checked = false;
        bool bondable = false;
        BondDistanceRange range;
    };
    const int elementTypeCount = static_cast<int>(elementTypes.size());
    std::vector<CachedBondRange> rangeCache(
        static_cast<std::size_t>(std::max(0, elementTypeCount * elementTypeCount)));
    const auto rangeCacheIndex = [elementTypeCount](int typeA, int typeB) {
        const int first = std::min(typeA, typeB);
        const int second = std::max(typeA, typeB);
        return static_cast<std::size_t>(first * elementTypeCount + second);
    };
    const auto cachedBondRange = [&](int atomA, int atomB, BondDistanceRange* range) {
        if (range == nullptr) {
            return false;
        }
        const int typeA = atomElementTypes[static_cast<std::size_t>(atomA)];
        const int typeB = atomElementTypes[static_cast<std::size_t>(atomB)];
        if (typeA < 0 || typeB < 0) {
            return false;
        }
        auto& cached = rangeCache[rangeCacheIndex(typeA, typeB)];
        if (!cached.checked) {
            cached.bondable = effectiveBondRange(
                m_displayOptions.customBondRanges,
                elementTypes[static_cast<std::size_t>(typeA)],
                elementTypes[static_cast<std::size_t>(typeB)],
                &cached.range);
            cached.checked = true;
        }
        if (!cached.bondable) {
            return false;
        }
        *range = cached.range;
        return true;
    };

    struct BondCandidate {
        int atom = -1;
        int imageA = 0;
        int imageB = 0;
        int imageC = 0;
        QVector3D shift;
        QVector3D cartesian;
    };

    std::vector<BondCandidate> candidates;
    candidates.reserve(atoms.size() * static_cast<std::size_t>(usePeriodicImages ? 4 : 1));
    QHash<SpatialCellKey, std::vector<int>> grid;
    grid.reserve(static_cast<int>(atoms.size() * static_cast<std::size_t>(usePeriodicImages ? 4 : 1)));

    for (int j = 0; j < static_cast<int>(atoms.size()); ++j) {
        const auto& atom = atoms[static_cast<std::size_t>(j)];
        if (!shouldDisplayAtom(atom, m_displayOptions, m_structure.cellVectors)) {
            continue;
        }
        for (int imageA = minImage; imageA <= maxImage; ++imageA) {
            for (int imageB = minImage; imageB <= maxImage; ++imageB) {
                for (int imageC = minImage; imageC <= maxImage; ++imageC) {
                    if (prunePeriodicImages
                        && (imageA != 0 || imageB != 0 || imageC != 0)
                        && !periodicImageCouldBond(atom, imageA, imageB, imageC, cellSize, m_structure.cellVectors)) {
                        continue;
                    }
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
                        BondDistanceRange range;
                        if (!cachedBondRange(i, candidate.atom, &range)) {
                            continue;
                        }
                        const QVector3D diff = candidate.cartesian - a.cartesian;
                        const double distSq = static_cast<double>(diff.lengthSquared());
                        const double minDistance = std::max(kMinimumPhysicalAtomDistance, range.minDistance - 1.0e-9);
                        const double maxDistance = std::max(minDistance, range.maxDistance + 1.0e-9);
                        if (distSq >= minDistance * minDistance
                            && distSq <= maxDistance * maxDistance) {
                            const double dist = std::sqrt(distSq);
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

void StructureCanvas::markStaticRenderInstancesDirty() {
    m_atomRenderInstancesDirty = true;
    m_lineRenderInstancesDirty = true;
    m_dirtyAtomRenderInstanceIndices.clear();
    m_dirtyLineRenderInstanceIndices.clear();
}

void StructureCanvas::markPreviewRenderInstancesDirty() {
    m_previewAtomRenderInstancesDirty = true;
    m_previewLineRenderInstancesDirty = true;
}

StructureCanvas::GpuViewState StructureCanvas::currentGpuViewState() const {
    const QRectF viewport = rect().adjusted(18, 18, -18, -18);
    const double scale = sceneScale(viewport, sceneCenter());
    constexpr double densityScale = 1.0;
    const float depthPad = static_cast<float>(std::max(
        2.0,
        m_cachedRadius + m_cachedMaxAtomRadius * kAtomRadiusSceneFactor * std::max(1.0, m_displayOptions.atomScale) + 2.0));

    GpuViewState view;
    view.canvasSize = size();
    view.viewportCenter = viewport.center();
    view.sceneCenter = sceneCenter();
    view.viewRight = m_viewRight;
    view.viewUp = m_viewUp;
    view.viewForward = m_viewForward;
    view.panOffset = m_panOffset;
    view.scale = static_cast<float>(std::max(1.0e-6, scale));
    view.atomScale = static_cast<float>(m_displayOptions.atomScale);
    view.densityScale = static_cast<float>(densityScale);
    view.depthMax = depthPad;
    view.invDepthRange = 1.0f / std::max(1.0e-6f, depthPad * 2.0f);
    view.perspective = m_displayOptions.perspective;
    view.depthCue = m_displayOptions.depthCue;
    return view;
}

void StructureCanvas::rebuildRenderInstances() {
    std::vector<QString> previousLabelTexts;
    if (m_displayOptions.showLabels) {
        previousLabelTexts.reserve(m_labelSources.size());
        for (const auto& source : m_labelSources) {
            previousLabelTexts.push_back(source.text);
        }
    }

    m_atomRenderInstances.clear();
    m_lineRenderInstances.clear();
    m_labelSources.clear();
    m_atomRenderInstanceImageIndices.clear();
    m_atomIdToRenderInstanceIndices.clear();
    m_cachedBondLineStartIndices.assign(m_cachedBonds.size(), -1);
    m_cachedBondLineCounts.assign(m_cachedBonds.size(), 0);
    m_atomIdToCachedBondIndices.clear();
    m_labelSourceToRenderInstanceIndices.clear();

    const auto addLine = [&](const QVector3D& start,
                             const QVector3D& end,
                             double startRadius,
                             double endRadius,
                             QColor color,
                             double halfWidth,
                             double alpha,
                             bool dashed,
                             double depthBias) {
        if ((end - start).lengthSquared() <= 1.0e-10f) {
            return;
        }
        LineRenderInstance instance;
        setVector(instance.start, start);
        setVector(instance.end, end);
        instance.startRadius = static_cast<float>(std::max(0.0, startRadius));
        instance.endRadius = static_cast<float>(std::max(0.0, endRadius));
        instance.halfWidth = static_cast<float>(std::max(0.5, halfWidth));
        instance.depthBias = static_cast<float>(depthBias);
        instance.dash = dashed ? 1.0f : 0.0f;
        setColor(instance.color, color, alpha);
        m_lineRenderInstances.push_back(instance);
    };

    if (m_displayOptions.showCell) {
        const int edgePairs[][2] = {{0,1},{0,2},{0,4},{1,3},{1,5},{2,3},{2,6},{3,7},{4,5},{4,6},{5,7},{6,7}};
        for (const auto& pair : edgePairs) {
            addLine(
                cellPoint(m_structure.cellVectors, pair[0]),
                cellPoint(m_structure.cellVectors, pair[1]),
                0.0,
                0.0,
                QColor("#202020"),
                0.55,
                1.0,
                false,
                -0.60);
        }
    }

    if (m_displayOptions.showBonds) {
        for (int bondIndex = 0; bondIndex < static_cast<int>(m_cachedBonds.size()); ++bondIndex) {
            const auto& bond = m_cachedBonds[static_cast<std::size_t>(bondIndex)];
            if (bond.atomA < 0 || bond.atomA >= static_cast<int>(m_structure.atoms.size()) ||
                bond.atomB < 0 || bond.atomB >= static_cast<int>(m_structure.atoms.size())) {
                continue;
            }
            const auto& a = m_structure.atoms[static_cast<std::size_t>(bond.atomA)];
            const auto& b = m_structure.atoms[static_cast<std::size_t>(bond.atomB)];
            const QVector3D aCenter = a.cartesian;
            const QVector3D bCenter = b.cartesian + bond.shiftB;
            const QVector3D mid = (aCenter + bCenter) * 0.5f;
            const QColor colorA = a.color.isValid() ? a.color : QColor("#707070");
            const QColor colorB = b.color.isValid() ? b.color : QColor("#707070");
            const int firstLineIndex = static_cast<int>(m_lineRenderInstances.size());
            if (colorA.rgba() == colorB.rgba()) {
                addLine(
                    aCenter,
                    bCenter,
                    std::max(0.05, a.radius),
                    std::max(0.05, b.radius),
                    colorA,
                    kBondWidthPixels * 0.5,
                    1.0,
                    false,
                    -0.35);
            } else {
                addLine(aCenter, mid, std::max(0.05, a.radius), 0.0, colorA, kBondWidthPixels * 0.5, 1.0, false, -0.35);
                addLine(mid, bCenter, 0.0, std::max(0.05, b.radius), colorB, kBondWidthPixels * 0.5, 1.0, false, -0.35);
            }
            const int lineCount = static_cast<int>(m_lineRenderInstances.size()) - firstLineIndex;
            if (lineCount > 0) {
                m_cachedBondLineStartIndices[static_cast<std::size_t>(bondIndex)] = firstLineIndex;
                m_cachedBondLineCounts[static_cast<std::size_t>(bondIndex)] = lineCount;
                m_atomIdToCachedBondIndices[a.atomId].push_back(bondIndex);
                m_atomIdToCachedBondIndices[b.atomId].push_back(bondIndex);
            }
        }
    }

    m_atomRenderInstances.reserve(m_cachedAtomImages.size());
    if (m_displayOptions.showLabels) {
        m_labelSources.reserve(m_cachedAtomImages.size());
    }
    m_atomRenderInstanceImageIndices.reserve(m_cachedAtomImages.size());
    m_atomIdToRenderInstanceIndices.reserve(static_cast<int>(m_structure.atoms.size()));
    for (int imageIndex = 0; imageIndex < static_cast<int>(m_cachedAtomImages.size()); ++imageIndex) {
        const auto& image = m_cachedAtomImages[static_cast<std::size_t>(imageIndex)];
        if (image.atom < 0 || image.atom >= static_cast<int>(m_structure.atoms.size())) {
            continue;
        }
        const auto& atom = m_structure.atoms[static_cast<std::size_t>(image.atom)];
        const QVector3D center = atom.cartesian + image.shift;
        AtomRenderInstance instance;
        setVector(instance.center, center);
        instance.radius = static_cast<float>(std::max(0.05, atom.radius));
        setColor(instance.color, atom.color.isValid() ? atom.color : QColor("#C9D3E6"), 1.0);
        setPickColor(instance.pickColor, atom.atomId);
        const int renderIndex = static_cast<int>(m_atomRenderInstances.size());
        m_atomRenderInstances.push_back(instance);
        m_atomRenderInstanceImageIndices.push_back(imageIndex);
        m_atomIdToRenderInstanceIndices[atom.atomId].push_back(renderIndex);

        if (m_displayOptions.showLabels) {
            LabelSource label;
            setVector(label.center, center);
            label.text = atom.tag.trimmed().isEmpty()
                ? QString("%1%2").arg(atom.element).arg(atom.atomId)
                : atom.tag;
            m_labelSources.push_back(std::move(label));
        }
    }

    bool labelTextsUnchanged = m_displayOptions.showLabels
        && previousLabelTexts.size() == m_labelSources.size();
    if (labelTextsUnchanged) {
        for (int i = 0; i < static_cast<int>(m_labelSources.size()); ++i) {
            if (previousLabelTexts[static_cast<std::size_t>(i)] != m_labelSources[static_cast<std::size_t>(i)].text) {
                labelTextsUnchanged = false;
                break;
            }
        }
    }

    if (labelTextsUnchanged && !m_labelRenderSourceIndices.empty()) {
        m_labelSourceToRenderInstanceIndices.assign(m_labelSources.size(), {});
        for (int i = 0; i < static_cast<int>(m_labelRenderInstances.size()) && i < static_cast<int>(m_labelRenderSourceIndices.size()); ++i) {
            const int sourceIndex = m_labelRenderSourceIndices[static_cast<std::size_t>(i)];
            if (sourceIndex < 0 || sourceIndex >= static_cast<int>(m_labelSources.size())) {
                continue;
            }
            const auto& source = m_labelSources[static_cast<std::size_t>(sourceIndex)];
            m_labelRenderInstances[static_cast<std::size_t>(i)].center[0] = source.center[0];
            m_labelRenderInstances[static_cast<std::size_t>(i)].center[1] = source.center[1];
            m_labelRenderInstances[static_cast<std::size_t>(i)].center[2] = source.center[2];
            m_labelSourceToRenderInstanceIndices[static_cast<std::size_t>(sourceIndex)].push_back(i);
        }
        m_labelRenderInstancesDirty = true;
        m_dirtyLabelRenderInstanceIndices.clear();
    } else {
        m_labelRenderInstances.clear();
        m_labelRenderSourceIndices.clear();
        m_labelSourceToRenderInstanceIndices.clear();
        m_labelAtlasDirty = true;
        m_labelTextureDirty = true;
        m_labelRenderInstancesDirty = true;
        m_dirtyLabelRenderInstanceIndices.clear();
    }

    markStaticRenderInstancesDirty();
}

void StructureCanvas::rebuildPreviewRenderInstances() {
    m_previewAtomRenderInstances.clear();
    m_previewLineRenderInstances.clear();

    for (int i = 0; i < static_cast<int>(m_previewAtoms.size()); ++i) {
        const auto& atom = m_previewAtoms[static_cast<std::size_t>(i)];
        AtomRenderInstance instance;
        setVector(instance.center, atom.cartesian);
        instance.radius = static_cast<float>(std::max(0.05, atom.radius));
        setColor(instance.color, atom.color.isValid() ? atom.color : QColor("#2D7FF9"), 105.0 / 255.0);
        setPickColor(instance.pickColor, 0);
        m_previewAtomRenderInstances.push_back(instance);
    }

    if (m_displayOptions.showBonds) {
        for (int i = 0; i < static_cast<int>(m_previewAtoms.size()); ++i) {
            for (int j = i + 1; j < static_cast<int>(m_previewAtoms.size()); ++j) {
                const auto& a = m_previewAtoms[static_cast<std::size_t>(i)];
                const auto& b = m_previewAtoms[static_cast<std::size_t>(j)];
                BondDistanceRange range;
                if (!effectiveBondRange(m_displayOptions.customBondRanges, a.element, b.element, &range)) {
                    range = BondDistanceRange{0.0, (vestaElementRadius(a.element) + vestaElementRadius(b.element)) * 0.85};
                }
                const double distance = (b.cartesian - a.cartesian).length();
                range.maxDistance += 0.15;
                if (distance <= 1.0e-6 || !distanceInRange(distance, range)) {
                    continue;
                }
                LineRenderInstance instance;
                setVector(instance.start, a.cartesian);
                setVector(instance.end, b.cartesian);
                instance.startRadius = static_cast<float>(std::max(0.05, a.radius));
                instance.endRadius = static_cast<float>(std::max(0.05, b.radius));
                instance.halfWidth = static_cast<float>(kPreviewBondWidthPixels * 0.5);
                instance.depthBias = -0.35f;
                instance.dash = 1.0f;
                const bool outOfCell = !fractionalInsideUnitCell(a.fractional) || !fractionalInsideUnitCell(b.fractional);
                setColor(instance.color, outOfCell ? QColor("#D92D20") : QColor("#2D7FF9"), 150.0 / 255.0);
                m_previewLineRenderInstances.push_back(instance);
            }
        }
    }

    markPreviewRenderInstancesDirty();
}

void StructureCanvas::rebuildLabelAtlas(int maxTextureSize) {
    m_labelRenderInstances.clear();
    m_labelRenderSourceIndices.clear();
    m_labelSourceToRenderInstanceIndices.assign(m_labelSources.size(), {});
    m_dirtyLabelRenderInstanceIndices.clear();
    m_labelAtlasImage = {};
    m_labelAtlasDirty = false;
    m_labelTextureDirty = true;
    m_labelRenderInstancesDirty = true;
    if (m_labelSources.empty()) {
        return;
    }

    QFont font = this->font();
    font.setBold(true);
    font.setPointSizeF(10.0);
    QFontMetrics metrics(font);
    struct PackedLabel {
        int source = -1;
        QRect rect;
    };
    std::vector<PackedLabel> packed;
    packed.reserve(m_labelSources.size());

    const int textureLimit = std::clamp(maxTextureSize, 1024, 16384);
    const int atlasWidth = std::clamp(textureLimit, 1024, 4096);
    constexpr int padding = 4;
    int cursorX = padding;
    int cursorY = padding;
    int rowHeight = 0;
    for (int i = 0; i < static_cast<int>(m_labelSources.size()); ++i) {
        const QString text = m_labelSources[static_cast<std::size_t>(i)].text;
        if (text.trimmed().isEmpty()) {
            continue;
        }
        const QRect bounds = metrics.boundingRect(text).adjusted(-8, -4, 8, 4);
        const int w = std::clamp(bounds.width(), 8, atlasWidth - padding * 2);
        const int h = std::max(8, bounds.height());
        if (cursorX + w + padding > atlasWidth) {
            cursorX = padding;
            cursorY += rowHeight + padding;
            rowHeight = 0;
        }
        if (cursorY + h + padding > textureLimit) {
            break;
        }
        packed.push_back({i, QRect(cursorX, cursorY, w, h)});
        cursorX += w + padding;
        rowHeight = std::max(rowHeight, h);
    }

    if (packed.empty()) {
        return;
    }
    const int atlasHeight = std::max(16, cursorY + rowHeight + padding);
    m_labelAtlasImage = QImage(atlasWidth, atlasHeight, QImage::Format_RGBA8888);
    m_labelAtlasImage.fill(Qt::transparent);

    QPainter painter(&m_labelAtlasImage);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setFont(font);
    for (const auto& label : packed) {
        const QString text = m_labelSources[static_cast<std::size_t>(label.source)].text;
        QRect textRect = label.rect;
        painter.setPen(QColor("#000000"));
        painter.drawText(textRect.translated(-1, 0), Qt::AlignCenter, text);
        painter.drawText(textRect.translated(1, 0), Qt::AlignCenter, text);
        painter.drawText(textRect.translated(0, -1), Qt::AlignCenter, text);
        painter.drawText(textRect.translated(0, 1), Qt::AlignCenter, text);
        painter.setPen(QColor("#FFFFFF"));
        painter.drawText(textRect, Qt::AlignCenter, text);

        LabelRenderInstance instance;
        const auto& source = m_labelSources[static_cast<std::size_t>(label.source)];
        instance.center[0] = source.center[0];
        instance.center[1] = source.center[1];
        instance.center[2] = source.center[2];
        instance.depthBias = 0.04f;
        instance.size[0] = static_cast<float>(label.rect.width());
        instance.size[1] = static_cast<float>(label.rect.height());
        instance.uvMin[0] = static_cast<float>(label.rect.left()) / static_cast<float>(atlasWidth);
        instance.uvMin[1] = static_cast<float>(label.rect.top()) / static_cast<float>(atlasHeight);
        instance.uvMax[0] = static_cast<float>(label.rect.right() + 1) / static_cast<float>(atlasWidth);
        instance.uvMax[1] = static_cast<float>(label.rect.bottom() + 1) / static_cast<float>(atlasHeight);
        const int labelRenderIndex = static_cast<int>(m_labelRenderInstances.size());
        m_labelRenderInstances.push_back(instance);
        m_labelRenderSourceIndices.push_back(label.source);
        if (label.source >= 0 && label.source < static_cast<int>(m_labelSourceToRenderInstanceIndices.size())) {
            m_labelSourceToRenderInstanceIndices[static_cast<std::size_t>(label.source)].push_back(labelRenderIndex);
        }
    }
}

int StructureCanvas::pickAtomAt(const QPoint& pos) {
    const auto candidates = pickAtomsAtCpu(pos);
    if (candidates.empty()) {
        return -1;
    }
    if (candidates.size() == 1 || static_cast<int>(m_structure.atoms.size()) > kGpuSinglePickAtomLimit) {
        return candidates.front();
    }
    const int gpuAtomId = pickAtomAtGpu(pos);
    if (gpuAtomId > 0) {
        return gpuAtomId;
    }
    return candidates.front();
}

void StructureCanvas::invalidatePickIndex() const {
    m_pickIndexDirty = true;
}

void StructureCanvas::rebuildPickIndex() const {
    m_pickIndexEntries.clear();
    m_pickIndexGrid.clear();
    m_pickIndexCanvasSize = size();
    m_pickIndexDirty = false;
    if (m_structure.atoms.empty()) {
        return;
    }

    const QRectF viewport = rect().adjusted(18, 18, -18, -18);
    const QVector3D center = sceneCenter();
    const double scale = sceneScale(viewport, center);
    constexpr double densityScale = 1.0;

    m_pickIndexEntries.reserve(m_cachedAtomImages.size());
    m_pickIndexGrid.reserve(static_cast<int>(m_cachedAtomImages.size() * 2));
    for (const auto& image : m_cachedAtomImages) {
        if (image.atom < 0 || image.atom >= static_cast<int>(m_structure.atoms.size())) {
            continue;
        }
        const auto& atom = m_structure.atoms[static_cast<std::size_t>(image.atom)];
        const QVector3D local = atom.cartesian + image.shift - center;
        const auto rotated = rotatePoint(local);
        const double perspective = depthPerspective(rotated.z());
        const QPointF point(
            viewport.center().x() + m_panOffset.x() + rotated.x() * scale * perspective,
            viewport.center().y() + m_panOffset.y() - rotated.y() * scale * perspective);
        const double radius = hitAtomRadius(atom, scale, perspective, m_displayOptions.atomScale, densityScale);
        const int entryIndex = static_cast<int>(m_pickIndexEntries.size());
        m_pickIndexEntries.push_back({atom.atomId, point, radius, rotated.z()});

        const QRectF hitBounds(
            point.x() - radius - 6.0,
            point.y() - radius - 6.0,
            (radius + 6.0) * 2.0,
            (radius + 6.0) * 2.0);
        const int minX = screenGridCoord(hitBounds.left());
        const int maxX = screenGridCoord(hitBounds.right());
        const int minY = screenGridCoord(hitBounds.top());
        const int maxY = screenGridCoord(hitBounds.bottom());
        for (int gx = minX; gx <= maxX; ++gx) {
            for (int gy = minY; gy <= maxY; ++gy) {
                m_pickIndexGrid[screenGridKey(gx, gy)].push_back(entryIndex);
            }
        }
    }
}

int StructureCanvas::pickAtomAtGpu(const QPoint& pos) {
    if (m_structure.atoms.empty()) {
        return -1;
    }
    if (m_openGL != nullptr && context() != nullptr && pos.x() >= 0 && pos.y() >= 0 && pos.x() < width() && pos.y() < height()) {
        makeCurrent();
        if (!m_openGL->initialized) {
            m_openGL->initialize(this);
        }
        if (m_openGL->initialized) {
            if (m_atomRenderInstancesDirty) {
                m_openGL->uploadAtoms(m_atomRenderInstances);
                m_atomRenderInstancesDirty = false;
            }
            const QSize pickSize = size();
            const GpuViewState view = currentGpuViewState();
            if (m_openGL->renderPickAtoms(this, view, pickSize)) {
                unsigned char pixel[4] = {};
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glReadPixels(pos.x(), pickSize.height() - 1 - pos.y(), 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
                m_openGL->finishPickRender(this);
                doneCurrent();
                return decodePickColor(pixel);
            }
        }
        doneCurrent();
    }
    return -1;
}

std::vector<int> StructureCanvas::pickAtomsAtCpu(const QPoint& pos) {
    if (m_structure.atoms.empty()) {
        return {};
    }
    if (m_pickIndexDirty || m_pickIndexCanvasSize != size()) {
        rebuildPickIndex();
    }

    struct Candidate {
        int atomId = -1;
        double depth = 0.0;
        double distance = 0.0;
    };
    std::vector<Candidate> candidates;
    const auto gridIt = m_pickIndexGrid.constFind(screenGridKey(screenGridCoord(pos.x()), screenGridCoord(pos.y())));
    if (gridIt == m_pickIndexGrid.cend()) {
        return {};
    }
    candidates.reserve(static_cast<std::size_t>(gridIt.value().size()));
    for (int entryIndex : gridIt.value()) {
        if (entryIndex < 0 || entryIndex >= static_cast<int>(m_pickIndexEntries.size())) {
            continue;
        }
        const auto& entry = m_pickIndexEntries[static_cast<std::size_t>(entryIndex)];
        const double distance = QLineF(entry.point, pos).length();
        if (distance > entry.radius + 6.0) {
            continue;
        }
        candidates.push_back({entry.atomId, entry.depth, distance});
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
    std::unordered_set<int> emittedAtomIds;
    emittedAtomIds.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        if (!emittedAtomIds.insert(candidate.atomId).second) {
            continue;
        }
        atomIds.push_back(candidate.atomId);
    }
    return atomIds;
}

std::vector<int> StructureCanvas::pickAtomsAt(const QPoint& pos) {
    std::vector<int> atomIds = pickAtomsAtCpu(pos);
    if (atomIds.size() <= 1 || static_cast<int>(m_structure.atoms.size()) > kGpuSinglePickAtomLimit) {
        return atomIds;
    }
    const int gpuAtomId = pickAtomAtGpu(pos);
    if (gpuAtomId <= 0) {
        return atomIds;
    }

    const auto it = std::find(atomIds.begin(), atomIds.end(), gpuAtomId);
    if (it == atomIds.end()) {
        atomIds.insert(atomIds.begin(), gpuAtomId);
    } else if (it != atomIds.begin()) {
        atomIds.erase(it);
        atomIds.insert(atomIds.begin(), gpuAtomId);
    }
    return atomIds;
}

std::vector<int> StructureCanvas::pickAtomsInScreenRect(const QRectF& selection) {
    if (m_structure.atoms.empty()) {
        return {};
    }
    const QRectF normalizedSelection = selection.normalized().intersected(QRectF(rect()));
    const double selectionPixelArea = normalizedSelection.width() * normalizedSelection.height();
    const bool useGpuRectPick =
        !normalizedSelection.isEmpty()
        && static_cast<int>(m_structure.atoms.size()) <= kGpuRectPickAtomLimit
        && selectionPixelArea <= kGpuRectPickPixelLimit
        && m_openGL != nullptr
        && context() != nullptr;
    if (useGpuRectPick) {
        makeCurrent();
        if (!m_openGL->initialized) {
            m_openGL->initialize(this);
        }
        if (m_openGL->initialized) {
            if (m_atomRenderInstancesDirty) {
                m_openGL->uploadAtoms(m_atomRenderInstances);
                m_atomRenderInstancesDirty = false;
            }
            const QSize pickSize = size();
            const GpuViewState view = currentGpuViewState();
            if (m_openGL->renderPickAtoms(this, view, pickSize)) {
                const int x = std::clamp(static_cast<int>(std::floor(normalizedSelection.left())), 0, std::max(0, pickSize.width() - 1));
                const int yTop = std::clamp(static_cast<int>(std::floor(normalizedSelection.top())), 0, std::max(0, pickSize.height() - 1));
                const int x2 = std::clamp(static_cast<int>(std::ceil(normalizedSelection.right())), 0, std::max(0, pickSize.width() - 1));
                const int yBottom = std::clamp(static_cast<int>(std::ceil(normalizedSelection.bottom())), 0, std::max(0, pickSize.height() - 1));
                const int readWidth = std::max(1, x2 - x + 1);
                const int readHeight = std::max(1, yBottom - yTop + 1);
                std::vector<unsigned char> pixels(static_cast<std::size_t>(readWidth * readHeight * 4), 0);
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glReadPixels(x, pickSize.height() - yBottom - 1, readWidth, readHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                m_openGL->finishPickRender(this);
                doneCurrent();

                std::vector<int> atomIds;
                std::unordered_set<int> seen;
                seen.reserve(static_cast<std::size_t>(readWidth * readHeight / 8 + 8));
                for (int row = 0; row < readHeight; ++row) {
                    for (int col = 0; col < readWidth; ++col) {
                        const auto offset = static_cast<std::size_t>((row * readWidth + col) * 4);
                        const int atomId = decodePickColor(pixels.data() + offset);
                        if (atomId > 0 && seen.insert(atomId).second) {
                            atomIds.push_back(atomId);
                        }
                    }
                }
                std::sort(atomIds.begin(), atomIds.end());
                return atomIds;
            }
        }
        doneCurrent();
    }
    if (m_pickIndexDirty || m_pickIndexCanvasSize != size()) {
        rebuildPickIndex();
    }
    const QRectF selectionTarget = normalizedSelection.isEmpty() ? selection.normalized() : normalizedSelection;
    const int minX = screenGridCoord(selectionTarget.left());
    const int maxX = screenGridCoord(selectionTarget.right());
    const int minY = screenGridCoord(selectionTarget.top());
    const int maxY = screenGridCoord(selectionTarget.bottom());
    std::vector<unsigned char> visited(m_pickIndexEntries.size(), 0);

    struct Candidate {
        int atomId = -1;
        double depth = 0.0;
    };
    std::vector<Candidate> candidates;
    for (int gx = minX; gx <= maxX; ++gx) {
        for (int gy = minY; gy <= maxY; ++gy) {
            const auto it = m_pickIndexGrid.constFind(screenGridKey(gx, gy));
            if (it == m_pickIndexGrid.cend()) {
                continue;
            }
            for (int entryIndex : it.value()) {
                if (entryIndex < 0 || entryIndex >= static_cast<int>(m_pickIndexEntries.size())) {
                    continue;
                }
                if (visited[static_cast<std::size_t>(entryIndex)] != 0) {
                    continue;
                }
                visited[static_cast<std::size_t>(entryIndex)] = 1;
                const auto& entry = m_pickIndexEntries[static_cast<std::size_t>(entryIndex)];
                if (!selectionTarget.contains(entry.point)) {
                    continue;
                }
                candidates.push_back({entry.atomId, entry.depth});
            }
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (std::abs(a.depth - b.depth) > 1.0e-6) {
            return a.depth > b.depth;
        }
        return a.atomId < b.atomId;
    });

    std::vector<int> atomIds;
    atomIds.reserve(candidates.size());
    std::unordered_set<int> seenAtomIds;
    seenAtomIds.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        if (!seenAtomIds.insert(candidate.atomId).second) {
            continue;
        }
        atomIds.push_back(candidate.atomId);
    }
    return atomIds;
}

int StructureCanvas::pickNextCtrlAtomAt(const QPoint& pos) {
    const auto candidates = pickAtomsAt(pos);
    for (int atomId : candidates) {
        if (!isAtomSelected(atomId)) {
            return atomId;
        }
    }
    return candidates.empty() ? -1 : candidates.front();
}

void StructureCanvas::paintGL() {
    QElapsedTimer frameTimer;
    frameTimer.start();
    const auto publishFrameMetrics = [&]() {
        Q_EMIT frameRendered(
            static_cast<double>(frameTimer.nsecsElapsed()) / 1000000.0,
            static_cast<int>(m_atomRenderInstances.size()),
            static_cast<int>(m_lineRenderInstances.size()));
    };

    const QColor background = backgroundColor();
    const qreal dpr = devicePixelRatioF();
    glViewport(
        0,
        0,
        static_cast<GLsizei>(std::max(1, static_cast<int>(std::lround(width() * dpr)))),
        static_cast<GLsizei>(std::max(1, static_cast<int>(std::lround(height() * dpr)))));
    glClearColor(
        static_cast<GLfloat>(background.redF()),
        static_cast<GLfloat>(background.greenF()),
        static_cast<GLfloat>(background.blueF()),
        1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    if (m_openGL != nullptr && !m_openGL->initialized) {
        m_openGL->initialize(this);
    }

    const QRectF viewport = rect().adjusted(18, 18, -18, -18);

    auto drawTextOutline = [](QPainter& painter, const QPointF& centerPoint, const QString& text, const QColor& fillColor, const QColor& outlineColor, const QFont& font) {
        painter.setFont(font);
        const QFontMetrics fm(font);
        const QRectF box = fm.boundingRect(text).adjusted(-8, -4, 8, 4);
        QRectF textRect(centerPoint.x() - box.width() * 0.5, centerPoint.y() - box.height() * 0.5, box.width(), box.height());
        painter.setPen(outlineColor);
        painter.drawText(textRect.translated(-1, 0), Qt::AlignCenter, text);
        painter.drawText(textRect.translated(1, 0), Qt::AlignCenter, text);
        painter.drawText(textRect.translated(0, -1), Qt::AlignCenter, text);
        painter.drawText(textRect.translated(0, 1), Qt::AlignCenter, text);
        painter.setPen(fillColor);
        painter.drawText(textRect, Qt::AlignCenter, text);
    };

    if (m_structure.atoms.empty()) {
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        painter.setPen(QColor("#404040"));
        painter.drawText(
            viewport,
            Qt::AlignCenter,
            m_japanese
                ? QStringLiteral("Open or drop a structure file to start.\nSupported: ASE project, CIF, XYZ, POSCAR/CONTCAR, PDB, XSF.")
                : QStringLiteral("Open or drop a structure file to start.\nSupported: ASE project, CIF, XYZ, POSCAR/CONTCAR, PDB, XSF."));
        painter.drawText(
            rect().adjusted(18, 0, -18, -14),
            Qt::AlignBottom | Qt::AlignLeft,
            QStringLiteral("Click: open file   Left drag: rotate   Alt+left/Right/Middle drag: pan   Wheel: zoom"));
        painter.end();
        publishFrameMetrics();
        return;
    }

    const GpuViewState view = currentGpuViewState();
    if (m_openGL != nullptr && m_openGL->initialized) {
        if (m_lineRenderInstancesDirty) {
            m_openGL->uploadLines(m_lineRenderInstances);
            m_lineRenderInstancesDirty = false;
            m_dirtyLineRenderInstanceIndices.clear();
        } else if (!m_dirtyLineRenderInstanceIndices.empty()) {
            if (!m_openGL->uploadLinesAt(m_lineRenderInstances, m_dirtyLineRenderInstanceIndices)) {
                m_lineRenderInstancesDirty = true;
            }
            m_dirtyLineRenderInstanceIndices.clear();
        }
        if (m_atomRenderInstancesDirty) {
            m_openGL->uploadAtoms(m_atomRenderInstances);
            m_atomRenderInstancesDirty = false;
            m_dirtyAtomRenderInstanceIndices.clear();
        } else if (!m_dirtyAtomRenderInstanceIndices.empty()) {
            if (!m_openGL->uploadAtomsAt(m_atomRenderInstances, m_dirtyAtomRenderInstanceIndices)) {
                m_atomRenderInstancesDirty = true;
            }
            m_dirtyAtomRenderInstanceIndices.clear();
        }
        if (m_displayOptions.showLabels && m_labelAtlasDirty) {
            rebuildLabelAtlas(m_openGL->maxTextureSize);
        }
        if (m_displayOptions.showLabels && m_labelTextureDirty) {
            m_openGL->uploadLabelAtlas(this, m_labelAtlasImage);
            m_labelTextureDirty = false;
        }
        if (m_displayOptions.showLabels && m_labelRenderInstancesDirty) {
            m_openGL->uploadLabels(m_labelRenderInstances);
            m_labelRenderInstancesDirty = false;
            m_dirtyLabelRenderInstanceIndices.clear();
        } else if (m_displayOptions.showLabels && !m_dirtyLabelRenderInstanceIndices.empty()) {
            if (!m_openGL->uploadLabelsAt(m_labelRenderInstances, m_dirtyLabelRenderInstanceIndices)) {
                m_labelRenderInstancesDirty = true;
            }
            m_dirtyLabelRenderInstanceIndices.clear();
        }

        glEnable(GL_MULTISAMPLE);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_TRUE);
        m_openGL->drawLines(this, static_cast<GLsizei>(m_lineRenderInstances.size()), view);
        m_openGL->drawAtoms(this, static_cast<GLsizei>(m_atomRenderInstances.size()), view);

        if (!m_previewLineRenderInstances.empty() || !m_previewAtomRenderInstances.empty()) {
            glDepthMask(GL_FALSE);
            if (!m_previewLineRenderInstances.empty()) {
                if (m_previewLineRenderInstancesDirty) {
                    m_openGL->uploadPreviewLines(m_previewLineRenderInstances);
                    m_previewLineRenderInstancesDirty = false;
                }
                m_openGL->drawPreviewLines(this, view);
            }
            if (!m_previewAtomRenderInstances.empty()) {
                if (m_previewAtomRenderInstancesDirty) {
                    m_openGL->uploadPreviewAtoms(m_previewAtomRenderInstances);
                    m_previewAtomRenderInstancesDirty = false;
                }
                m_openGL->drawPreviewAtoms(this, view);
            }
            glDepthMask(GL_TRUE);
        }

        if (m_displayOptions.showLabels && !m_labelRenderInstances.empty()) {
            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            m_openGL->drawLabels(this, static_cast<GLsizei>(m_labelRenderInstances.size()), view);
            glDepthMask(GL_TRUE);
            glEnable(GL_DEPTH_TEST);
        }
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
    }

    struct PaintedAtom {
        QPointF pos;
        double depth = 0.0;
        double radius = 0.0;
        QColor color;
        bool focus = false;
        bool selected = false;
        bool preview = false;
        bool outOfCell = false;
        int atomId = 0;
        int selectionOrder = 0;
        QString label;
    };

    std::vector<PaintedAtom> overlayAtoms;
    overlayAtoms.reserve(m_selectedAtomIds.size() + m_previewAtoms.size() + 4);
    const QVector3D center = sceneCenter();
    const double scale = sceneScale(viewport, center);
    constexpr double densityScale = 1.0;
    const QRectF cullRect = viewport.adjusted(-96.0, -96.0, 96.0, 96.0);
    const auto projectRotatedPoint = [&](const QVector3D& rotated, double perspective) {
        return QPointF(
            viewport.center().x() + m_panOffset.x() + rotated.x() * scale * perspective,
            viewport.center().y() + m_panOffset.y() - rotated.y() * scale * perspective);
    };
    const auto addOverlayAtom = [&](const NativeAtom& atom, const QVector3D& cartesian, bool selected, bool focus, bool preview, bool outOfCell, int order, const QString& label) {
        const QVector3D rotated = rotatePoint(cartesian - center);
        const double perspective = depthPerspective(rotated.z());
        const QPointF point = projectRotatedPoint(rotated, perspective);
        const double radius = screenAtomRadius(atom, scale, perspective, m_displayOptions.atomScale, densityScale);
        if (!QRectF(point.x() - radius - 16.0, point.y() - radius - 16.0, radius * 2.0 + 32.0, radius * 2.0 + 32.0).intersects(cullRect)) {
            return;
        }
        overlayAtoms.push_back({
            point,
            rotated.z(),
            radius,
            atom.color.isValid() ? atom.color : QColor("#C9D3E6"),
            focus,
            selected,
            preview,
            outOfCell,
            atom.atomId,
            order,
            label
        });
    };

    if (!m_selectedAtomOrder.isEmpty() || m_focusAtomId > 0) {
        for (const auto& image : m_cachedAtomImages) {
            if (image.atom < 0 || image.atom >= static_cast<int>(m_structure.atoms.size())) {
                continue;
            }
            if (image.imageA != 0 || image.imageB != 0 || image.imageC != 0) {
                continue;
            }
            const auto& atom = m_structure.atoms[static_cast<std::size_t>(image.atom)];
            const int order = m_selectedAtomOrder.value(atom.atomId, 0);
            const bool selected = order > 0;
            const bool focus = atom.atomId == m_focusAtomId;
            if (!selected && !focus) {
                continue;
            }
            addOverlayAtom(
                atom,
                atom.cartesian,
                selected,
                focus,
                false,
                false,
                order,
                atom.tag.trimmed().isEmpty() ? QString("%1%2").arg(atom.element).arg(atom.atomId) : atom.tag);
        }
    }

    for (const auto& atom : m_previewAtoms) {
        addOverlayAtom(
            atom,
            atom.cartesian,
            false,
            false,
            true,
            !fractionalInsideUnitCell(atom.fractional),
            0,
            atom.tag);
    }

    std::sort(overlayAtoms.begin(), overlayAtoms.end(), [](const PaintedAtom& a, const PaintedAtom& b) {
        if (std::abs(a.depth - b.depth) > 1.0e-6) {
            return a.depth < b.depth;
        }
        return a.atomId < b.atomId;
    });

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    for (const auto& atom : overlayAtoms) {
        if (atom.preview) {
            const QColor previewAccent = atom.outOfCell ? QColor("#D92D20") : QColor("#2D7FF9");
            painter.setPen(QPen(previewAccent, 1.2, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(atom.pos, atom.radius + 4.0, atom.radius + 4.0);
            if (!atom.label.trimmed().isEmpty()) {
                QFont font = painter.font();
                font.setBold(true);
                font.setPointSizeF(8.5);
                drawTextOutline(
                    painter,
                    atom.pos + QPointF(0.0, -atom.radius - 14.0),
                    atom.label,
                    previewAccent,
                    QColor("#FFFFFF"),
                    font);
            }
            continue;
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
    }

    if (m_displayOptions.showAxes) {
        const QPointF origin(viewport.left() + 58.0, viewport.bottom() - 58.0);
        const double axisLength = 34.0;
        const auto normalizedOr = [](const QVector3D& axis, const QVector3D& fallback) {
            return axis.lengthSquared() > 1.0e-8f ? axis.normalized() : fallback;
        };
        const auto drawAxis = [&](const QVector3D& dir, const QColor& color, const QString& label) {
            const QVector3D rotated = rotatePoint(dir);
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
    if (m_interactionMode == InteractionMode::MoveModel) {
        footer = m_japanese
            ? QStringLiteral("Move model display mode: Left drag pans the whole model display   Ctrl+click/drag selects overlaps   Esc clears selection   Wheel/pinch zooms   F fits")
            : QStringLiteral("Move model display mode: Left drag pans the whole model display   Ctrl+click/drag selects overlaps   Esc clears selection   Wheel/pinch zooms   F fits");
    } else if (m_interactionMode == InteractionMode::MoveAtoms) {
        footer = m_japanese
            ? QStringLiteral("Move atoms mode: Left drag moves selected atoms   Ctrl+click/drag selects overlaps   Esc clears selection   Right/Middle drag pans   Wheel/pinch zooms   F fits")
            : QStringLiteral("Move atoms mode: Left drag moves selected atoms   Ctrl+click/drag selects overlaps   Esc clears selection   Right/Middle drag pans   Wheel/pinch zooms   F fits");
    } else {
        footer = m_japanese
            ? QStringLiteral("Move view mode: Left drag rotates   Alt+left/Right/Middle drag pans   Ctrl+click/drag selects overlaps   Esc clears selection   Shift+left drag moves selected atoms   Wheel/pinch zooms   F fits")
            : QStringLiteral("Move view mode: Left drag rotates   Alt+left/Right/Middle drag pans   Ctrl+click/drag selects overlaps   Esc clears selection   Shift+left drag moves selected atoms   Wheel/pinch zooms   F fits");
    }
    painter.drawText(
        rect().adjusted(18, 0, -18, -14),
        Qt::AlignBottom | Qt::AlignLeft,
        footer);
    painter.end();
    publishFrameMetrics();
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
    // Range selection follows the visible blue rectangle exactly.  Do not
    // expand by atom radius/tolerance here: that made atoms just above the
    // drawn rectangle get selected.
    const QRectF selectionTarget = QRectF(selection);

    const std::vector<int> candidates = pickAtomsInScreenRect(selectionTarget);
    for (int atomId : candidates) {
        if (isAtomSelected(atomId)) {
            continue;
        }
        emit atomActivated(atomId);
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
    return m_selectedAtomOrder.contains(atomId);
}

void StructureCanvas::mousePressEvent(QMouseEvent* event) {
    setFocus(Qt::MouseFocusReason);
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
    } else if ((event->buttons() & Qt::LeftButton) && (event->modifiers() & Qt::AltModifier)) {
        m_panOffset += QPointF(delta.x(), delta.y());
        invalidatePickIndex();
    } else if (event->buttons() & Qt::LeftButton) {
        if (m_interactionMode == InteractionMode::View) {
            rotateBasisFromDrag(delta);
        } else if (m_interactionMode == InteractionMode::MoveModel) {
            m_panOffset += QPointF(delta.x(), delta.y());
            invalidatePickIndex();
        }
    } else {
        m_panOffset += QPointF(delta.x(), delta.y());
        invalidatePickIndex();
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

void StructureCanvas::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        m_draggingSelection = false;
        m_ctrlSelectingAtoms = false;
        m_ctrlPressAtomId = -1;
        updateInteractionCursor();
        emit clearSelectionRequested();
        event->accept();
        update();
        return;
    }
    QOpenGLWidget::keyPressEvent(event);
}

void StructureCanvas::wheelEvent(QWheelEvent* event) {
    double steps = event->angleDelta().y() / 120.0;
    if (!event->pixelDelta().isNull()) {
        steps = event->pixelDelta().y() / 96.0;
    }
    if (std::abs(steps) <= 1.0e-9) {
        event->accept();
        return;
    }
    const double factor = std::clamp(std::pow(1.16, steps), 0.05, 20.0);
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

    return QOpenGLWidget::event(event);
}
