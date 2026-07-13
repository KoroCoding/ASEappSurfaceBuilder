#include "VaspStructureWriter.h"

#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTextStream>

#include <algorithm>
#include <vector>

namespace {
bool atomHasFixedAxis(const NativeAtom& atom) {
    return std::any_of(atom.movable.begin(), atom.movable.end(), [](bool movable) { return !movable; });
}
}

bool VaspStructureWriter::write(const StructureData& structure,
                                const QString& path,
                                const VaspWriteOptions& options,
                                QString* errorMessage) const {
    if (structure.atoms.empty()) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot write an empty structure.");
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (errorMessage) *errorMessage = QStringLiteral("Failed to write %1").arg(path);
        return false;
    }

    QStringList elements;
    std::vector<int> counts;
    for (const NativeAtom& atom : structure.atoms) {
        const int index = elements.indexOf(atom.element);
        if (index < 0) {
            elements << atom.element;
            counts.push_back(1);
        } else {
            ++counts[static_cast<std::size_t>(index)];
        }
    }

    QTextStream out(&file);
    out.setRealNumberNotation(QTextStream::FixedNotation);
    out.setRealNumberPrecision(10);
    QString title = QFileInfo(path).completeBaseName().trimmed();
    if (title.isEmpty()) title = QFileInfo(path).fileName().trimmed();
    if (title.isEmpty()) title = structure.title.trimmed();
    if (title.isEmpty()) title = QStringLiteral("POSCAR");
    out << title << "\n1.0\n";
    for (const QVector3D& vector : structure.cellVectors) {
        out << QStringLiteral("  %1  %2  %3\n")
                   .arg(vector.x(), 16, 'f', 10)
                   .arg(vector.y(), 16, 'f', 10)
                   .arg(vector.z(), 16, 'f', 10);
    }
    out << "  " << elements.join(QStringLiteral("  ")) << "\n";
    for (int count : counts) out << QStringLiteral("  %1").arg(count);
    out << "\n";

    const bool hasFixedAtoms = std::any_of(structure.atoms.begin(), structure.atoms.end(), atomHasFixedAxis);
    if (options.standardSelectiveDynamics && hasFixedAtoms) out << "Selective dynamics\n";
    out << (options.coordinateMode == StructureCoordinateMode::Cartesian ? "Cartesian\n" : "Direct\n");
    for (const QString& element : elements) {
        for (const NativeAtom& atom : structure.atoms) {
            if (atom.element != element) continue;
            const QVector3D coordinates = options.coordinateMode == StructureCoordinateMode::Cartesian
                ? atom.cartesian : atom.fractional;
            out << QStringLiteral("  %1  %2  %3")
                       .arg(coordinates.x(), 16, 'f', 10)
                       .arg(coordinates.y(), 16, 'f', 10)
                       .arg(coordinates.z(), 16, 'f', 10);
            if (options.standardSelectiveDynamics && hasFixedAtoms) {
                out << QStringLiteral("  %1  %2  %3")
                           .arg(atom.movable[0] ? QStringLiteral("T") : QStringLiteral("F"))
                           .arg(atom.movable[1] ? QStringLiteral("T") : QStringLiteral("F"))
                           .arg(atom.movable[2] ? QStringLiteral("T") : QStringLiteral("F"));
            } else if (atomHasFixedAxis(atom)) {
                out << QStringLiteral("  %1  %2  %3")
                           .arg(atom.movable[0] ? 0 : 1)
                           .arg(atom.movable[1] ? 0 : 1)
                           .arg(atom.movable[2] ? 0 : 1);
            }
            out << "\n";
        }
    }
    if (out.status() != QTextStream::Ok) {
        if (errorMessage) *errorMessage = QStringLiteral("Failed while writing %1").arg(path);
        return false;
    }
    return true;
}
