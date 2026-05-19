#pragma once

#include <array>
#include <vector>

#include <QVector3D>
#include <QString>

#include "StructureData.h"

using SupercellTransformMatrix = std::array<std::array<int, 3>, 3>;

struct SurfacePlacementRule {
    QString name;
    QString description;
    QString element = "H";
    QString mode = "single_above";
    int selectionCount = 1;
    double height = 1.0;
    double fraction = 0.5;
    std::array<double, 3> weights{1.0, 1.0, 1.0};
    double tiltDegrees = 0.0;
};

struct VacuumAdjustmentOptions {
    // 0=a, 1=b, 2=c. Ignored when fitAllAxes is true.
    int axisIndex = 2;
    bool fitAllAxes = false;
    bool fitTight = false;
    bool moveOnly = false;
    // Vacuum thickness in Å. Used when fitTight is false.
    double vacuumAngstrom = 12.0;
    // 0=vacuum on positive side, 1=center slab, 2=vacuum on negative side, 3=custom slab center fraction.
    int placementMode = 1;
    double customCenterFraction = 0.5;
    // Whole-slab translation along normalized a/b/c directions in Å.
    std::array<double, 3> translationAngstrom{0.0, 0.0, 0.0};
};

struct CellAxisTiltOptions {
    // Axis to tilt: 0=a, 1=b, 2=c.
    int targetAxisIndex = 2;
    // Lattice-axis direction toward which the target axis is tilted.
    int directionAxisIndex = 0;
    // Tilt angle in degrees. The target axis length is preserved.
    double angleDegrees = 10.0;
};

StructureData makeDefaultStructure();
SupercellTransformMatrix identitySupercellTransformMatrix();
SupercellTransformMatrix diagonalSupercellTransformMatrix(int aMult, int bMult, int cMult);
long long supercellTransformDeterminant(const SupercellTransformMatrix& matrix);
bool isIdentitySupercellTransform(const SupercellTransformMatrix& matrix);
bool isDiagonalSupercellTransform(const SupercellTransformMatrix& matrix, std::array<int, 3>* factors = nullptr);
StructureData makeSupercellStructure(const StructureData& source, int aMult, int bMult, int cMult);
StructureData makeSupercellStructure(
    const StructureData& source,
    const SupercellTransformMatrix& matrix,
    QString* errorMessage = nullptr);
StructureData makeVacuumReboxedStructure(
    const StructureData& source,
    const QVector3D& direction,
    double thicknessAngstrom,
    int placementMode);
StructureData adjustVacuumAndSlab(
    const StructureData& source,
    const VacuumAdjustmentOptions& options);
StructureData applyCellAxisTilt(
    const StructureData& source,
    const CellAxisTiltOptions& options,
    QString* errorMessage = nullptr);
StructureData shiftSurfaceTopLayer(
    const StructureData& source,
    double shiftAlongA,
    double shiftAlongB,
    double layerThicknessAngstrom);
StructureData addHydrogenTermination(
    const StructureData& source,
    double bondLengthAngstrom,
    bool top,
    bool bottom,
    double layerThicknessAngstrom);
QVector3D surfaceNormalForStructure(const StructureData& source);
StructureData addPlacementAtom(
    const StructureData& source,
    const std::vector<int>& selectedAtomIds,
    const SurfacePlacementRule& rule,
    QString* errorMessage = nullptr);
