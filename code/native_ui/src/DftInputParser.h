#pragma once

#include "DftInputTypes.h"

struct DftImportResult {
    bool ok = false;
    QString sourceKind;
    QStringList messages;
    QStringList diffLines;
    DftSettings settings;
};

class DftInputParser {
public:
    static DftImportResult parseFile(const QString& path, const DftSettings& baseSettings);
    static DftImportResult parseSiestaFdfText(const QString& text, const DftSettings& baseSettings, DftParameterSource source);
    static DftImportResult parseSiestaLogText(const QString& text, const DftSettings& baseSettings);
    static DftImportResult parseQeInputText(const QString& text, const DftSettings& baseSettings);
    static DftImportResult parseProfileJsonText(const QString& text, const DftSettings& baseSettings);
};
