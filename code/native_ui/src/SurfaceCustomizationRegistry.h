#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QJsonObject>

#include "SurfaceOperations.h"

class SurfaceCustomizationRegistry {
public:
    static QString defaultFilePath();
    static QVector<SurfacePlacementRule> defaultPresets();

    bool loadOrCreateDefault(const QString& path, QString* message = nullptr);
    bool save(const QString& path, QString* message = nullptr) const;

    const QVector<SurfacePlacementRule>& presets() const;
    QStringList presetNames() const;
    QString filePath() const;

private:
    static QJsonObject ruleToJson(const SurfacePlacementRule& rule);
    static SurfacePlacementRule ruleFromJson(const QJsonObject& object);

    QString m_filePath;
    QVector<SurfacePlacementRule> m_presets;
};
