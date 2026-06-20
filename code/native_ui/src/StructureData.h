#pragma once

#include <QColor>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector3D>
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
    // Per-axis mobility flags. true = movable, false = fixed.
    std::array<bool, 3> movable{true, true, true};
};

struct StructureData {
    QString sourcePath;
    QString title;
    std::array<QVector3D, 3> cellVectors{};
    std::vector<NativeAtom> atoms;
    QString trailingFlagInterpretation = QStringLiteral("preserve_or_ignore_unknown");
    QMap<int, QStringList> importedExtraColumns;
    bool dirty = false;
};
