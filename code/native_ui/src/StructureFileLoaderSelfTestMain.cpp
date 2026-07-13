#include "StructureFileLoader.h"

#include "SiestaFinalConverter.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTextStream>
#include <cmath>

namespace {
bool near(double a, double b) { return std::abs(a - b) < 1.0e-5; }
bool writeText(const QString& path, const QString& text) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream stream(&file);
    stream << text;
    return true;
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    if (!dir.isValid()) return 1;
    const QString structPath = dir.filePath(QStringLiteral("final_structure.txt"));
    const QString structText = QStringLiteral(
        "%block ChemicalSpeciesLabel\n"
        " 1 31 Ga\n"
        " 2 201 H-0.750\n"
        "%endblock ChemicalSpeciesLabel\n"
        "%block Geometry.Constraints\n"
        " atom 2\n"
        "%endblock Geometry.Constraints\n"
        "----- sample.STRUCT_OUT -----\n"
        " 4 0 0\n 0 5 0\n 0 0 6\n"
        "2\n"
        "1 31 0.25 0.20 0.50\n"
        "2 201 0.50 0.40 0.25\n"
        "----- later_sample.XV -----\n"
        " 2 0 0\n 0 2 0\n 0 0 2\n"
        "1\n"
        "1 8 1 1 1 0 0 0\n");
    if (!writeText(structPath, structText)) return 2;
    StructureFileLoader loader;
    QString error;
    const auto structure = loader.load(structPath, &error);
    if (!structure || structure->atoms.size() != 2 || structure->cellWasGenerated) return 3;
    if (structure->atoms[0].element != QStringLiteral("Ga") || !near(structure->atoms[0].cartesian.x(), 1.0)) return 4;
    if (structure->atoms[1].element != QStringLiteral("H") || structure->atoms[1].movable[0]) return 5;

    const QString xvPath = dir.filePath(QStringLiteral("embedded.txt"));
    const QString xvText = QStringLiteral(
        "----- sample.XV -----\n"
        " 2 0 0\n 0 4 0\n 0 0 6\n"
        "1\n"
        "1 8 1 2 3 0 0 0\n");
    if (!writeText(xvPath, xvText)) return 6;
    const auto xv = loader.load(xvPath, &error);
    if (!xv || xv->atoms.size() != 1 || xv->atoms[0].element != QStringLiteral("O")) return 7;
    if (!near(xv->cellVectors[0].x(), 2.0 * 0.529177210903) || !near(xv->atoms[0].cartesian.z(), 3.0 * 0.529177210903)) return 8;

    const QString outputPath = dir.filePath(QStringLiteral("relax.out"));
    const QString fdfPath = dir.filePath(QStringLiteral("relax.fdf"));
    if (!writeText(outputPath, QStringLiteral(
            "SystemLabel ideal_9layer\n"
            "%block ChemicalSpeciesLabel\n1 31 Ga\n%endblock ChemicalSpeciesLabel\n"
            "outcoor: Relaxed atomic coordinates (Ang)\n"
            " 1.0 2.0 3.0 1 1 Ga\nend\n"))) return 9;
    if (!writeText(fdfPath, QStringLiteral(
            "LatticeConstant 1.0 Ang\n%block LatticeVectors\n"
            "4 0 0\n0 5 0\n0 0 6\n%endblock LatticeVectors\n"))) return 10;
    SiestaFinalConverter converter;
    const SiestaFinalAnalysis analysis = converter.analyze(outputPath);
    if (!analysis.readyToWrite() || QFileInfo(analysis.cellSourcePath) != QFileInfo(fdfPath)) {
        QTextStream(stderr) << "analysis loaded=" << analysis.loaded
                            << " generated=" << analysis.structure.cellWasGenerated
                            << " cell=" << analysis.cellSourcePath
                            << " expected=" << fdfPath
                            << " error=" << analysis.errorMessage << "\n";
        return 11;
    }
    const QString vaspPath = dir.filePath(QStringLiteral("relax_VASP.vasp"));
    if (!converter.writeVasp(analysis, vaspPath, StructureCoordinateMode::Cartesian, &error)) return 12;
    QFile vaspFile(vaspPath);
    if (!vaspFile.open(QIODevice::ReadOnly | QIODevice::Text)) return 13;
    const QString vaspText = QString::fromUtf8(vaspFile.readAll());
    if (!vaspText.contains(QStringLiteral("Cartesian")) || !vaspText.contains(QStringLiteral("Ga"))) return 14;
    if (QFileInfo(SiestaFinalConverter::suggestedOutputPath(outputPath, analysis.structure.title)).fileName()
        != QStringLiteral("ideal_9layer.vasp")) return 15;

    QTextStream(stdout) << "SIESTA final OO conversion pipeline self-test passed\n";
    return 0;
}



