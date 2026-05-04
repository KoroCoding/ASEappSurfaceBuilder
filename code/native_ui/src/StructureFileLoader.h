#pragma once

#include <QObject>
#include <QString>
#include <optional>

#include "StructureData.h"

class StructureFileLoader : public QObject {
    Q_OBJECT
public:
    explicit StructureFileLoader(QObject* parent = nullptr);

    std::optional<StructureData> load(const QString& path, QString* errorMessage = nullptr) const;
};
