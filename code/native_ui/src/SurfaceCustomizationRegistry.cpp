#include "SurfaceCustomizationRegistry.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QStandardPaths>

namespace {
QString modeSummary(const QString& mode) {
    const QString normalized = mode.trimmed().toLower();
    if (normalized == "single_above") {
        return QStringLiteral("1 atom / above");
    }
    if (normalized == "single_below") {
        return QStringLiteral("1 atom / below");
    }
    if (normalized == "selection_centroid") {
        return QStringLiteral("Selected atoms / center");
    }
    if (normalized == "pair_midpoint") {
        return QStringLiteral("Selected atoms / center (legacy pair)");
    }
    if (normalized == "pair_fraction") {
        return QStringLiteral("Selected atoms / center (legacy pair)");
    }
    if (normalized == "triple_centroid") {
        return QStringLiteral("Selected atoms / center (legacy 3 atoms)");
    }
    if (normalized == "triple_weighted") {
        return QStringLiteral("Selected atoms / center (legacy 3 atoms)");
    }
    if (normalized == "multi_centroid") {
        return QStringLiteral("N atoms / centroid");
    }
    if (normalized == "multi_weighted") {
        return QStringLiteral("N atoms / centroid (legacy weighted)");
    }
    if (normalized == "multi_plane_normal") {
        return QStringLiteral("N atoms / plane normal");
    }
    return QStringLiteral("custom");
}

void ensureParentDirectory(const QString& path) {
    QDir parent = QFileInfo(path).dir();
    if (!parent.exists()) {
        parent.mkpath(".");
    }
}
}  // namespace

QString SurfaceCustomizationRegistry::defaultFilePath() {
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (base.isEmpty()) {
        base = QDir::homePath();
    }
    QDir dir(base);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    const QString appName = QCoreApplication::applicationName().isEmpty()
        ? QStringLiteral("ASEappSurfaceBuilder")
        : QCoreApplication::applicationName().simplified().replace(' ', '_');
    return dir.filePath(appName + QStringLiteral("_surface_customizations.json"));
}

QVector<SurfacePlacementRule> SurfaceCustomizationRegistry::defaultPresets() {
    return {
        {
            QStringLiteral("Single above"),
            QStringLiteral("Place one atom above the selected atom along the surface normal."),
            QStringLiteral("H"),
            QStringLiteral("single_above"),
            1,
            1.05,
            0.5,
            {1.0, 1.0, 1.0}
        },
        {
            QStringLiteral("Single below"),
            QStringLiteral("Place one atom below the selected atom along the surface normal."),
            QStringLiteral("H"),
            QStringLiteral("single_below"),
            1,
            1.05,
            0.5,
            {1.0, 1.0, 1.0}
        },
        {
            QStringLiteral("Bridge midpoint"),
            QStringLiteral("Place one atom at the midpoint between two selected atoms."),
            QStringLiteral("H"),
            QStringLiteral("pair_midpoint"),
            2,
            1.00,
            0.5,
            {1.0, 1.0, 1.0}
        },
        {
            QStringLiteral("Bridge fraction"),
            QStringLiteral("Place one atom at a custom fraction between two selected atoms."),
            QStringLiteral("H"),
            QStringLiteral("pair_fraction"),
            2,
            1.00,
            0.35,
            {1.0, 1.0, 1.0}
        },
        {
            QStringLiteral("Three-atom centroid"),
            QStringLiteral("Place one atom at the centroid of three selected atoms."),
            QStringLiteral("H"),
            QStringLiteral("triple_centroid"),
            3,
            1.00,
            0.5,
            {1.0, 1.0, 1.0}
        },
        {
            QStringLiteral("Three-atom weighted"),
            QStringLiteral("Place one atom at a weighted centroid of three selected atoms."),
            QStringLiteral("H"),
            QStringLiteral("triple_weighted"),
            3,
            1.00,
            0.5,
            {1.0, 1.0, 2.0}
        },
        {
            QStringLiteral("Four-atom hollow"),
            QStringLiteral("Place one atom at the centroid of four or more selected atoms."),
            QStringLiteral("H"),
            QStringLiteral("multi_centroid"),
            4,
            1.00,
            0.5,
            {1.0, 1.0, 1.0}
        },
        {
            QStringLiteral("N-atom weighted"),
            QStringLiteral("Place one atom at a weighted centroid of all selected atoms."),
            QStringLiteral("H"),
            QStringLiteral("multi_weighted"),
            4,
            1.00,
            0.5,
            {1.0, 1.0, 1.0}
        },
        {
            QStringLiteral("N-atom plane normal"),
            QStringLiteral("Place one atom above the centroid of selected atoms along the selected plane normal."),
            QStringLiteral("H"),
            QStringLiteral("multi_plane_normal"),
            3,
            1.00,
            0.5,
            {1.0, 1.0, 1.0}
        }
    };
}

QJsonObject SurfaceCustomizationRegistry::ruleToJson(const SurfacePlacementRule& rule) {
    QJsonObject object;
    object["name"] = rule.name;
    object["description"] = rule.description;
    object["element"] = rule.element;
    object["mode"] = rule.mode;
    object["selection_count"] = rule.selectionCount;
    object["height"] = rule.height;
    object["fraction"] = rule.fraction;
    object["tilt_degrees"] = rule.tiltDegrees;
    QJsonArray weights;
    weights.append(rule.weights[0]);
    weights.append(rule.weights[1]);
    weights.append(rule.weights[2]);
    object["weights"] = weights;
    object["mode_summary"] = modeSummary(rule.mode);
    return object;
}

SurfacePlacementRule SurfaceCustomizationRegistry::ruleFromJson(const QJsonObject& object) {
    SurfacePlacementRule rule;
    rule.name = object.value("name").toString();
    rule.description = object.value("description").toString();
    rule.element = object.value("element").toString(QStringLiteral("H"));
    rule.mode = object.value("mode").toString(QStringLiteral("single_above"));
    rule.selectionCount = object.value("selection_count").toInt(0);
    rule.height = object.value("height").toDouble(1.0);
    rule.fraction = object.value("fraction").toDouble(0.5);
    rule.tiltDegrees = object.value("tilt_degrees").toDouble(0.0);
    const QJsonArray weights = object.value("weights").toArray();
    for (int i = 0; i < 3; ++i) {
        rule.weights[static_cast<std::size_t>(i)] = weights.size() > i ? weights.at(i).toDouble(1.0) : 1.0;
    }
    if (rule.selectionCount <= 0) {
        const QString normalized = rule.mode.trimmed().toLower();
        if (normalized == "selection_centroid") {
            rule.selectionCount = 1;
        } else if (normalized == "pair_midpoint" || normalized == "pair_fraction") {
            rule.selectionCount = 2;
        } else if (normalized == "triple_centroid" || normalized == "triple_weighted" || normalized == "multi_plane_normal") {
            rule.selectionCount = 3;
        } else if (normalized == "multi_centroid" || normalized == "multi_weighted") {
            rule.selectionCount = 1;
        } else {
            rule.selectionCount = 1;
        }
    }
    return rule;
}

bool SurfaceCustomizationRegistry::loadOrCreateDefault(const QString& path, QString* message) {
    m_filePath = path.isEmpty() ? defaultFilePath() : path;
    m_presets.clear();

    QFile file(m_filePath);
    if (!file.exists()) {
        m_presets = defaultPresets();
        QString saveMessage;
        if (!save(m_filePath, &saveMessage) && message != nullptr) {
            *message = QStringLiteral("Preset file was missing and the default file could not be created: %1").arg(saveMessage);
        } else if (message != nullptr) {
            *message = QStringLiteral("Preset file was created at %1").arg(m_filePath);
        }
        return true;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        m_presets = defaultPresets();
        if (message != nullptr) {
            *message = QStringLiteral("Failed to open preset file, so the built-in defaults are being used: %1")
                .arg(file.errorString());
        }
        return true;
    }

    const auto document = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!document.isObject()) {
        m_presets = defaultPresets();
        if (message != nullptr) {
            *message = QStringLiteral("Preset file is not a JSON object. Built-in defaults are being used.");
        }
        return true;
    }

    const QJsonObject root = document.object();
    const QJsonArray presets = root.value("presets").toArray();
    if (presets.isEmpty()) {
        m_presets = defaultPresets();
        if (message != nullptr) {
            *message = QStringLiteral("Preset file has no presets. Built-in defaults are being used.");
        }
        return true;
    }

    for (const auto& value : presets) {
        if (value.isObject()) {
            m_presets.push_back(ruleFromJson(value.toObject()));
        }
    }
    if (m_presets.isEmpty()) {
        m_presets = defaultPresets();
        if (message != nullptr) {
            *message = QStringLiteral("Preset file did not contain usable entries. Built-in defaults are being used.");
        }
        return true;
    }
    int appendedDefaults = 0;
    const QStringList existingNames = presetNames();
    for (const auto& preset : defaultPresets()) {
        const QString name = preset.name.isEmpty() ? preset.mode : preset.name;
        if (!existingNames.contains(name)) {
            m_presets.push_back(preset);
            ++appendedDefaults;
        }
    }
    if (appendedDefaults > 0) {
        QString saveMessage;
        save(m_filePath, &saveMessage);
    }
    if (message != nullptr) {
        *message = appendedDefaults > 0
            ? QStringLiteral("Loaded %1 preset(s) from %2 and added %3 new default preset(s).").arg(m_presets.size()).arg(m_filePath).arg(appendedDefaults)
            : QStringLiteral("Loaded %1 preset(s) from %2").arg(m_presets.size()).arg(m_filePath);
    }
    return true;
}

bool SurfaceCustomizationRegistry::save(const QString& path, QString* message) const {
    const QString filePath = path.isEmpty() ? m_filePath : path;
    if (filePath.isEmpty()) {
        if (message != nullptr) {
            *message = QStringLiteral("No output path is available.");
        }
        return false;
    }

    ensureParentDirectory(filePath);

    QJsonArray presets;
    for (const auto& preset : m_presets.isEmpty() ? defaultPresets() : m_presets) {
        presets.append(ruleToJson(preset));
    }

    QJsonObject root;
    root["version"] = 2;
    root["presets"] = presets;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (message != nullptr) {
            *message = file.errorString();
        }
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    if (message != nullptr) {
        *message = QStringLiteral("Saved %1").arg(filePath);
    }
    return true;
}

const QVector<SurfacePlacementRule>& SurfaceCustomizationRegistry::presets() const {
    return m_presets;
}

QStringList SurfaceCustomizationRegistry::presetNames() const {
    QStringList names;
    for (const auto& preset : m_presets) {
        names.push_back(preset.name.isEmpty() ? preset.mode : preset.name);
    }
    return names;
}

QString SurfaceCustomizationRegistry::filePath() const {
    return m_filePath;
}
