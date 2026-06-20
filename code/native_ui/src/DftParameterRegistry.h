#pragma once

#include "DftInputTypes.h"

class DftParameterRegistry {
public:
    static QVector<DftParameterSpec> specsForCode(DftCode code, const QString& version);
    static DftSettings defaultSettings(DftCode code, const QString& version, const QString& targetName);
    static void applyCalculationModeDefaults(DftSettings* settings, const QString& mode);
    static QStringList calculationModes(DftCode code);
    static QStringList versionsForCode(DftCode code);
    static QStringList builtInProfiles(DftCode code, const QString& version);
    static bool applyBuiltInProfile(const QString& profileName, DftSettings* settings, QStringList* messages = nullptr);
    static QVector<DftSiestaSpecies> kangawaSiestaSpecies();
    static double defaultAtomicMass(const QString& element);
    static QVector<DftQeSpecies> defaultQeSpecies();
    static void resetQeSpeciesToProjectDefaults(DftSettings* settings);
    static QString parameterValue(const DftSettings& settings, const QString& id, const QString& fallback = QString());
    static void setParameterValue(DftSettings* settings, const QString& id, const QString& value, DftParameterSource source);
};
