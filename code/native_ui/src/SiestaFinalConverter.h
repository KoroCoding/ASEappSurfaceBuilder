#pragma once

#include "StructureData.h"
#include "VaspStructureWriter.h"

#include <QString>

struct SiestaFinalAnalysis {
    bool loaded = false;
    StructureData structure;
    QString sourcePath;
    QString cellSourcePath;
    QString errorMessage;

    bool readyToWrite() const { return loaded && !structure.cellWasGenerated; }
};

class SiestaFinalConverter final {
public:
    SiestaFinalAnalysis analyze(const QString& sourcePath) const;
    bool applyCellFile(SiestaFinalAnalysis* analysis,
                       const QString& cellPath,
                       QString* errorMessage = nullptr) const;
    bool writeVasp(const SiestaFinalAnalysis& analysis,
                   const QString& outputPath,
                   StructureCoordinateMode coordinateMode,
                   QString* errorMessage = nullptr) const;

    static QString suggestedOutputPath(const QString& sourcePath, const QString& inputLabel = {});

private:
    QString autoDetectCellFile(const QString& sourcePath, const StructureData& structure) const;
};
