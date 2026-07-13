#include "SiestaFinalConverter.h"

#include "StructureFileLoader.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include <cmath>

namespace {
QVector3D fractionalForCell(const std::array<QVector3D, 3>& cell, const QVector3D& cartesian) {
    const double determinant = QVector3D::dotProduct(cell[0], QVector3D::crossProduct(cell[1], cell[2]));
    if (std::abs(determinant) < 1.0e-12) return {};
    return QVector3D(
        QVector3D::dotProduct(cartesian, QVector3D::crossProduct(cell[1], cell[2])) / determinant,
        QVector3D::dotProduct(cartesian, QVector3D::crossProduct(cell[2], cell[0])) / determinant,
        QVector3D::dotProduct(cartesian, QVector3D::crossProduct(cell[0], cell[1])) / determinant);
}
}

SiestaFinalAnalysis SiestaFinalConverter::analyze(const QString& sourcePath) const {
    SiestaFinalAnalysis analysis;
    analysis.sourcePath = QFileInfo(sourcePath).absoluteFilePath();
    StructureFileLoader loader;
    const auto structure = loader.load(analysis.sourcePath, &analysis.errorMessage);
    if (!structure) return analysis;
    analysis.loaded = true;
    analysis.structure = *structure;
    if (!analysis.structure.cellWasGenerated) {
        analysis.cellSourcePath = analysis.sourcePath;
        return analysis;
    }
    const QString detectedCell = autoDetectCellFile(analysis.sourcePath, analysis.structure);
    if (!detectedCell.isEmpty()) {
        QString ignoredError;
        applyCellFile(&analysis, detectedCell, &ignoredError);
    }
    return analysis;
}

QString SiestaFinalConverter::autoDetectCellFile(const QString& sourcePath, const StructureData& structure) const {
    const QFileInfo sourceInfo(sourcePath);
    const QDir directory(sourceInfo.absolutePath());
    QStringList candidates{
        directory.filePath(structure.title + QStringLiteral(".fdf")),
        directory.filePath(sourceInfo.completeBaseName() + QStringLiteral(".fdf")),
        directory.filePath(QStringLiteral("POSCAR")),
        directory.filePath(QStringLiteral("CONTCAR")),
    };
    const QFileInfoList entries = directory.entryInfoList(
        QStringList{QStringLiteral("*.fdf"), QStringLiteral("*.vasp"), QStringLiteral("*.poscar"), QStringLiteral("*.contcar")},
        QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& entry : entries) candidates << entry.absoluteFilePath();

    QSet<QString> visited;
    StructureFileLoader loader;
    for (const QString& candidate : candidates) {
        const QFileInfo info(candidate);
        if (!info.exists() || !info.isFile()) continue;
        const QString key = QDir::cleanPath(info.absoluteFilePath()).toLower();
        if (visited.contains(key) || key == QDir::cleanPath(sourceInfo.absoluteFilePath()).toLower()) continue;
        visited.insert(key);
        QString ignoredError;
        if (loader.loadCellVectors(info.absoluteFilePath(), &ignoredError)) return info.absoluteFilePath();
    }
    return {};
}

bool SiestaFinalConverter::applyCellFile(SiestaFinalAnalysis* analysis,
                                         const QString& cellPath,
                                         QString* errorMessage) const {
    if (!analysis || !analysis->loaded) {
        if (errorMessage) *errorMessage = QStringLiteral("Load a SIESTA geometry first.");
        return false;
    }
    StructureFileLoader loader;
    const auto cell = loader.loadCellVectors(cellPath, errorMessage);
    if (!cell) return false;
    analysis->structure.cellVectors = *cell;
    analysis->structure.cellWasGenerated = false;
    analysis->cellSourcePath = QFileInfo(cellPath).absoluteFilePath();
    for (NativeAtom& atom : analysis->structure.atoms) {
        atom.fractional = fractionalForCell(analysis->structure.cellVectors, atom.cartesian);
    }
    return true;
}

bool SiestaFinalConverter::writeVasp(const SiestaFinalAnalysis& analysis,
                                     const QString& outputPath,
                                     StructureCoordinateMode coordinateMode,
                                     QString* errorMessage) const {
    if (!analysis.readyToWrite()) {
        if (errorMessage) *errorMessage = QStringLiteral("A physical cell is required before VASP export.");
        return false;
    }
    return VaspStructureWriter().write(analysis.structure, outputPath,
        VaspWriteOptions{coordinateMode, true}, errorMessage);
}

QString SiestaFinalConverter::suggestedOutputPath(const QString& sourcePath, const QString& inputLabel) {
    const QFileInfo info(sourcePath);
    QString label = inputLabel.trimmed();
    label.remove(QRegularExpression(QStringLiteral(R"(^["']+|["']+$)")));
    label.replace(QRegularExpression(QStringLiteral(R"([<>:"/\\|?*\x00-\x1F]+)")), QStringLiteral("_"));
    label.replace(QRegularExpression(QStringLiteral("[. ]+$")), QString());
    if (!label.isEmpty()) {
        return QDir(info.absolutePath()).filePath(label + QStringLiteral(".vasp"));
    }
    QString baseName = info.completeBaseName();
    if (baseName.endsWith(QStringLiteral(".STRUCT_OUT"), Qt::CaseInsensitive)) baseName.chop(11);
    if (baseName.endsWith(QStringLiteral(".XV"), Qt::CaseInsensitive)) baseName.chop(3);
    if (baseName.trimmed().isEmpty()) baseName = QStringLiteral("siesta_final");
    return QDir(info.absolutePath()).filePath(baseName + QStringLiteral("_VASP.vasp"));
}
