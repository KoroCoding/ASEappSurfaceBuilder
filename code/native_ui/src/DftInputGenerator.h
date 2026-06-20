#pragma once

#include "DftInputTypes.h"
#include "StructureData.h"

class DftInputGenerator {
public:
    static QString sanitizeTargetName(const QString& input);
    static QVector<DftHydrogenAssignment> inferHydrogenRoles(const StructureData& structure, const DftSettings& settings);
    static DftGeneratedInput generate(const StructureData& structure, DftSettings settings);
    static bool writeGeneratedFiles(const QString& outputDirectory, const DftSettings& settings,
                                    const DftGeneratedInput& generated, QString* errorMessage = nullptr);
    static bool hasNoExplanatoryComments(const QString& text, DftCode code, QStringList* offendingLines = nullptr);
    static QString structureHash(const StructureData& structure);
};
