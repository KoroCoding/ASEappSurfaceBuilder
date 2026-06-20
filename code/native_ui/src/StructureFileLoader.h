#pragma once

#include <QObject>
#include <QString>
#include <optional>

#include "StructureData.h"

enum class StructureTrailingFlagInterpretation {
    PreserveOrIgnoreUnknown,
    IgnoreTrailingFlags,
    NumericOneMeansFixed,
    NumericOneMeansMovable,
    VaspSelectiveDynamics,
    CustomMapping,
};

struct StructureImportOptions {
    StructureTrailingFlagInterpretation trailingFlagInterpretation = StructureTrailingFlagInterpretation::PreserveOrIgnoreUnknown;
};

class StructureFileLoader : public QObject {
    Q_OBJECT
public:
    explicit StructureFileLoader(QObject* parent = nullptr);

    std::optional<StructureData> load(const QString& path, QString* errorMessage = nullptr) const;
    std::optional<StructureData> load(const QString& path, QString* errorMessage, const StructureImportOptions& options) const;
};
