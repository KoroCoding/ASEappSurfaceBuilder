#pragma once

#include "StructureData.h"

#include <QString>

enum class StructureCoordinateMode {
    Direct,
    Cartesian,
};

struct VaspWriteOptions {
    StructureCoordinateMode coordinateMode = StructureCoordinateMode::Direct;
    bool standardSelectiveDynamics = false;
};

class VaspStructureWriter final {
public:
    bool write(const StructureData& structure,
               const QString& path,
               const VaspWriteOptions& options = {},
               QString* errorMessage = nullptr) const;
};
