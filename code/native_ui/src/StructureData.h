#pragma once

#include <QColor>
#include <QVector3D>
#include <QString>
#include <array>
#include <vector>

struct NativeAtom {
    int atomId = 0;
    QString element;
    QString tag;
    QVector3D fractional;
    QVector3D cartesian;
    QColor color;
    double radius = 1.0;
    // VASP Selective dynamics flags. true = movable (T), false = fixed (F).
    std::array<bool, 3> movable{true, true, true};
};

struct StructureData {
    QString sourcePath;
    QString title;
    std::array<QVector3D, 3> cellVectors{};
    std::vector<NativeAtom> atoms;
    bool dirty = false;
};
