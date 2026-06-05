#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTextStream>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

#include "ElementStyle.h"
#include "StructureCanvas.h"

namespace {

StructureData makeCubicStructure(int edge, double spacing) {
    StructureData structure;
    structure.title = QStringLiteral("canvas-performance-%1").arg(edge);
    structure.cellVectors = {
        QVector3D(static_cast<float>(edge * spacing), 0.0f, 0.0f),
        QVector3D(0.0f, static_cast<float>(edge * spacing), 0.0f),
        QVector3D(0.0f, 0.0f, static_cast<float>(edge * spacing))
    };
    structure.atoms.reserve(static_cast<std::size_t>(edge * edge * edge));
    int atomId = 1;
    for (int z = 0; z < edge; ++z) {
        for (int y = 0; y < edge; ++y) {
            for (int x = 0; x < edge; ++x) {
                NativeAtom atom;
                atom.atomId = atomId++;
                atom.element = QStringLiteral("C");
                atom.fractional = QVector3D(
                    static_cast<float>((x + 0.5) / edge),
                    static_cast<float>((y + 0.5) / edge),
                    static_cast<float>((z + 0.5) / edge));
                atom.cartesian = QVector3D(
                    static_cast<float>((x + 0.5) * spacing),
                    static_cast<float>((y + 0.5) * spacing),
                    static_cast<float>((z + 0.5) * spacing));
                atom.color = QColor("#606060");
                atom.radius = 0.70;
                structure.atoms.push_back(atom);
            }
        }
    }
    return structure;
}

int optionInt(const QStringList& arguments, const QString& name, int fallback) {
    for (int i = 0; i < arguments.size(); ++i) {
        const QString argument = arguments.at(i);
        if (argument == name && i + 1 < arguments.size()) {
            bool ok = false;
            const int value = arguments.at(i + 1).toInt(&ok);
            if (ok) return value;
        }
        if (argument.startsWith(name + QStringLiteral("="))) {
            bool ok = false;
            const int value = argument.mid(name.size() + 1).toInt(&ok);
            if (ok) return value;
        }
    }
    return fallback;
}

bool optionPresent(const QStringList& arguments, const QString& name) {
    return std::any_of(arguments.begin(), arguments.end(), [&](const QString& argument) {
        return argument == name;
    });
}

void pumpEvents(QApplication& app, int iterations = 2) {
    for (int i = 0; i < iterations; ++i) {
        app.processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(1);
    }
}

struct FrameProbe {
    int frameCount = 0;
    int atomInstances = 0;
    int lineInstances = 0;
    std::vector<double> cpuFrameMs;
};

bool waitForNextFrame(QApplication& app, const FrameProbe& probe, int previousFrameCount, int timeoutMs = 250) {
    QElapsedTimer timer;
    timer.start();
    while (probe.frameCount <= previousFrameCount && timer.elapsed() < timeoutMs) {
        app.processEvents(QEventLoop::AllEvents, 25);
        QThread::msleep(1);
    }
    return probe.frameCount > previousFrameCount;
}

bool waitForNextFrameOrGrab(
    QApplication& app,
    StructureCanvas& canvas,
    FrameProbe& probe,
    int previousFrameCount,
    int timeoutMs = 250)
{
    if (waitForNextFrame(app, probe, previousFrameCount, timeoutMs)) {
        return true;
    }
    // Some headless/offscreen Qt platforms do not schedule QOpenGLWidget
    // updates by exposure.  grabFramebuffer() is used only as a deterministic
    // fallback to enter paintGL(); CPU frame timings still come from the
    // StructureCanvas::frameRendered signal emitted inside paintGL().
    const QImage frame = canvas.grabFramebuffer();
    if (frame.isNull()) {
        return false;
    }
    pumpEvents(app, 2);
    return probe.frameCount > previousFrameCount;
}

bool forceFrame(QApplication& app, StructureCanvas& canvas, FrameProbe& probe, int timeoutMs = 250) {
    const int before = probe.frameCount;
    canvas.update();
    return waitForNextFrameOrGrab(app, canvas, probe, before, timeoutMs);
}

double average(const std::vector<double>& values) {
    if (values.empty()) {
        return -1.0;
    }
    const double sum = std::accumulate(values.begin(), values.end(), 0.0);
    return sum / static_cast<double>(values.size());
}

struct FrameMeasurement {
    double wallFrameMs = -1.0;
    double cpuFrameMs = -1.0;
};

FrameMeasurement measureRotationFrames(QApplication& app, StructureCanvas& canvas, FrameProbe& probe, int warmupFrames, int frames) {
    for (int i = 0; i < warmupFrames; ++i) {
        const int before = probe.frameCount;
        canvas.rotateBy(2.0, 1.0);
        if (!waitForNextFrameOrGrab(app, canvas, probe, before)) {
            return {};
        }
    }

    probe.cpuFrameMs.clear();
    QElapsedTimer timer;
    timer.start();
    for (int i = 0; i < frames; ++i) {
        const int before = probe.frameCount;
        canvas.rotateBy(2.0, 1.0);
        if (!waitForNextFrameOrGrab(app, canvas, probe, before)) {
            return {};
        }
    }
    return {
        static_cast<double>(timer.nsecsElapsed()) / 1000000.0 / std::max(1, frames),
        average(probe.cpuFrameMs)
    };
}

FrameMeasurement measureCoordinateDragFrames(
    QApplication& app,
    StructureCanvas& canvas,
    FrameProbe& probe,
    StructureData structure,
    int selectedCount,
    int warmupFrames,
    int frames)
{
    std::vector<int> selectedAtomIds;
    selectedAtomIds.reserve(static_cast<std::size_t>(selectedCount));
    for (int i = 0; i < selectedCount && i < static_cast<int>(structure.atoms.size()); ++i) {
        selectedAtomIds.push_back(structure.atoms[static_cast<std::size_t>(i)].atomId);
    }
    canvas.setSelectedAtomIds(selectedAtomIds);
    forceFrame(app, canvas, probe);

    auto stepCoordinates = [&]() {
        const QVector3D delta(0.01f, -0.006f, 0.004f);
        for (int i = 0; i < selectedCount && i < static_cast<int>(structure.atoms.size()); ++i) {
            auto& atom = structure.atoms[static_cast<std::size_t>(i)];
            atom.cartesian += delta;
            atom.fractional += QVector3D(
                delta.x() / std::max(1.0f, structure.cellVectors[0].x()),
                delta.y() / std::max(1.0f, structure.cellVectors[1].y()),
                delta.z() / std::max(1.0f, structure.cellVectors[2].z()));
        }
    };

    for (int frame = 0; frame < warmupFrames; ++frame) {
        const int before = probe.frameCount;
        stepCoordinates();
        canvas.updateStructureCoordinates(structure);
        if (!waitForNextFrameOrGrab(app, canvas, probe, before)) {
            return {};
        }
    }

    probe.cpuFrameMs.clear();
    QElapsedTimer timer;
    timer.start();
    for (int frame = 0; frame < frames; ++frame) {
        const int before = probe.frameCount;
        stepCoordinates();
        canvas.updateStructureCoordinates(structure);
        if (!waitForNextFrameOrGrab(app, canvas, probe, before)) {
            return {};
        }
    }
    return {
        static_cast<double>(timer.nsecsElapsed()) / 1000000.0 / std::max(1, frames),
        average(probe.cpuFrameMs)
    };
}

QJsonObject runScenario(
    QApplication& app,
    int edge,
    bool showBonds,
    bool showOutsideCell,
    int warmupFrames,
    int rotationFrames,
    int dragFrames)
{
    StructureCanvas canvas;
    canvas.resize(1280, 800);
    auto options = canvas.displayOptions();
    options.showCell = false;
    options.showAxes = false;
    options.showLabels = false;
    options.showOutsideCell = showOutsideCell;
    options.showBonds = showBonds;
    options.perspective = false;
    options.depthCue = false;
    options.atomScale = 1.0;
    if (showBonds) {
        options.customBondRanges.insert(vestaBondKey(QStringLiteral("C"), QStringLiteral("C")), BondDistanceRange{0.5, 1.25});
    }
    canvas.setDisplayOptions(options);

    FrameProbe probe;
    QObject::connect(&canvas, &StructureCanvas::frameRendered, &canvas, [&](double cpuFrameMs, int atomInstances, int lineInstances) {
        ++probe.frameCount;
        probe.atomInstances = atomInstances;
        probe.lineInstances = lineInstances;
        probe.cpuFrameMs.push_back(cpuFrameMs);
    });

    canvas.show();
    pumpEvents(app, 8);
    StructureData structure = makeCubicStructure(edge, 1.0);

    QElapsedTimer loadTimer;
    const int beforeLoadFrame = probe.frameCount;
    loadTimer.start();
    canvas.setStructure(structure);
    const bool firstPaintOk = waitForNextFrameOrGrab(app, canvas, probe, beforeLoadFrame, 500);
    const double loadAndFirstPaintMs = static_cast<double>(loadTimer.nsecsElapsed()) / 1000000.0;

    const FrameMeasurement rotation = firstPaintOk
        ? measureRotationFrames(app, canvas, probe, warmupFrames, rotationFrames)
        : FrameMeasurement{};
    const FrameMeasurement drag = firstPaintOk
        ? measureCoordinateDragFrames(app, canvas, probe, structure, 64, warmupFrames, dragFrames)
        : FrameMeasurement{};

    QJsonObject result;
    result.insert(QStringLiteral("atoms"), static_cast<int>(structure.atoms.size()));
    result.insert(QStringLiteral("showBonds"), showBonds);
    result.insert(QStringLiteral("showOutsideCell"), showOutsideCell);
    result.insert(QStringLiteral("bondCount"), canvas.bondCount());
    result.insert(QStringLiteral("atomInstances"), probe.atomInstances);
    result.insert(QStringLiteral("lineInstances"), probe.lineInstances);
    result.insert(QStringLiteral("loadAndFirstPaintMs"), loadAndFirstPaintMs);
    result.insert(QStringLiteral("firstPaintOk"), firstPaintOk);
    result.insert(QStringLiteral("rotationFrameAvgMs"), rotation.wallFrameMs);
    result.insert(QStringLiteral("rotationCpuFrameAvgMs"), rotation.cpuFrameMs);
    result.insert(QStringLiteral("dragFrameAvgMs"), drag.wallFrameMs);
    result.insert(QStringLiteral("dragCpuFrameAvgMs"), drag.cpuFrameMs);
    result.insert(QStringLiteral("rotationApproxFps"), rotation.wallFrameMs > 0.0 ? 1000.0 / rotation.wallFrameMs : -1.0);
    result.insert(QStringLiteral("dragApproxFps"), drag.wallFrameMs > 0.0 ? 1000.0 / drag.wallFrameMs : -1.0);
    return result;
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("ASEapp"));
    app.setApplicationName(QStringLiteral("ASEapp Canvas Performance Self Test"));

    const QStringList arguments = app.arguments();
    const int warmupFrames = std::max(0, optionInt(arguments, QStringLiteral("--warmup-frames"), 5));
    const int rotationFrames = std::max(1, optionInt(arguments, QStringLiteral("--rotation-frames"), 45));
    const int dragFrames = std::max(1, optionInt(arguments, QStringLiteral("--drag-steps"), 45));
    const bool skipDenseBonds = optionPresent(arguments, QStringLiteral("--skip-dense-bonds"));

    const QJsonObject atomOnly = runScenario(app, 40, false, false, warmupFrames, rotationFrames, dragFrames);
    const QJsonObject withBonds = runScenario(app, 22, true, true, warmupFrames, rotationFrames, dragFrames);
    const QJsonObject denseWithBonds = skipDenseBonds
        ? QJsonObject{{QStringLiteral("skipped"), true}}
        : runScenario(app, 40, true, true, warmupFrames, rotationFrames, dragFrames);

    QJsonObject report;
    report.insert(QStringLiteral("atomOnly"), atomOnly);
    report.insert(QStringLiteral("withBonds"), withBonds);
    report.insert(QStringLiteral("denseWithBonds"), denseWithBonds);
    QTextStream(stdout) << QJsonDocument(report).toJson(QJsonDocument::Indented);

    const bool denseOk = skipDenseBonds
        || (denseWithBonds.value(QStringLiteral("rotationFrameAvgMs")).toDouble(-1.0) > 0.0
            && denseWithBonds.value(QStringLiteral("dragFrameAvgMs")).toDouble(-1.0) > 0.0
            && denseWithBonds.value(QStringLiteral("bondCount")).toInt(0) > 0);
    const bool ok =
        atomOnly.value(QStringLiteral("rotationFrameAvgMs")).toDouble(-1.0) > 0.0
        && atomOnly.value(QStringLiteral("dragFrameAvgMs")).toDouble(-1.0) > 0.0
        && withBonds.value(QStringLiteral("rotationFrameAvgMs")).toDouble(-1.0) > 0.0
        && withBonds.value(QStringLiteral("dragFrameAvgMs")).toDouble(-1.0) > 0.0
        && denseOk;
    return ok ? 0 : 2;
}
