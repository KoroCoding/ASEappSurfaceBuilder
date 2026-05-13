#include "ElementStyle.h"

#include <algorithm>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QStringList>
#include <QTextStream>

namespace {
struct ElementStyle {
    QColor color = QColor("#C9D3E6");
    double radius = 1.0;
};
}  // namespace

QString vestaNormalizeElement(const QString& element) {
    QString text = element.trimmed();
    if (text.isEmpty()) {
        return QStringLiteral("X");
    }
    text = text.left(2);
    return text.left(1).toUpper() + text.mid(1).toLower();
}

QString vestaBondKey(const QString& elementA, const QString& elementB) {
    const QString a = vestaNormalizeElement(elementA);
    const QString b = vestaNormalizeElement(elementB);
    return (a <= b) ? (a + QLatin1Char('|') + b) : (b + QLatin1Char('|') + a);
}

namespace {
QHash<QString, ElementStyle> loadVestaStylesFromPath(const QString& path) {
    QHash<QString, ElementStyle> styles;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return styles;
    }

    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#')) {
            continue;
        }
        const QStringList parts = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (parts.size() < 8) {
            continue;
        }

        bool okRadius = false;
        bool okR = false;
        bool okG = false;
        bool okB = false;
        const double radius = parts.at(2).toDouble(&okRadius);
        const double r = parts.at(parts.size() - 3).toDouble(&okR);
        const double g = parts.at(parts.size() - 2).toDouble(&okG);
        const double b = parts.at(parts.size() - 1).toDouble(&okB);
        if (!(okRadius && okR && okG && okB)) {
            continue;
        }

        styles.insert(
            vestaNormalizeElement(parts.at(1)),
            ElementStyle{
                QColor::fromRgbF(
                    std::clamp(r, 0.0, 1.0),
                    std::clamp(g, 0.0, 1.0),
                    std::clamp(b, 0.0, 1.0),
                    1.0),
                radius,
            });
    }

    return styles;
}

QHash<QString, BondDistanceRange> loadVestaBondRangesFromPath(const QString& path) {
    QHash<QString, BondDistanceRange> ranges;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return ranges;
    }

    QTextStream stream(&file);
    const QRegularExpression elementRx(QStringLiteral("^[A-Za-z]{1,2}$"));
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#') || line == QStringLiteral("SBOND")) {
            continue;
        }

        const QStringList parts = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (parts.size() < 5) {
            continue;
        }

        bool okIndex = false;
        const int index = parts.at(0).toInt(&okIndex);
        if (!okIndex || index <= 0) {
            continue;
        }

        if (!elementRx.match(parts.at(1)).hasMatch() || !elementRx.match(parts.at(2)).hasMatch()) {
            continue;
        }

        bool okMin = false;
        bool okMax = false;
        const double minDistance = parts.at(3).toDouble(&okMin);
        const double maxDistance = parts.at(4).toDouble(&okMax);
        if (!(okMin && okMax)) {
            continue;
        }
        if (maxDistance <= 0.0 || maxDistance < minDistance) {
            continue;
        }

        ranges.insert(vestaBondKey(parts.at(1), parts.at(2)), BondDistanceRange{minDistance, maxDistance});
    }

    return ranges;
}

QHash<QString, BondDistanceRange> loadVestaBondRanges() {
    const QStringList candidates = {
        QDir::homePath() + QStringLiteral("/AppData/Roaming/VESTA/style/default.ini"),
        QDir::homePath() + QStringLiteral("/AppData/Roaming/VESTA/style.ini"),
        QStringLiteral("C:/VESTA-win64/style.ini"),
        QStringLiteral("C:/Program Files/VESTA/style.ini"),
        QStringLiteral("C:/Program Files (x86)/VESTA/style.ini"),
        QStringLiteral(":/data/style.ini"),
    };

    for (const auto& candidate : candidates) {
        auto ranges = loadVestaBondRangesFromPath(candidate);
        if (!ranges.isEmpty()) {
            return ranges;
        }
    }
    return {};
}

QHash<QString, ElementStyle> loadVestaStyles() {
    const QStringList candidates = {
        QStringLiteral("C:/VESTA-win64/elements.ini"),
        QStringLiteral("C:/Program Files/VESTA/elements.ini"),
        QStringLiteral("C:/Program Files (x86)/VESTA/elements.ini"),
        QStringLiteral(":/data/elements.ini"),
    };

    for (const auto& candidate : candidates) {
        auto styles = loadVestaStylesFromPath(candidate);
        if (!styles.isEmpty()) {
            return styles;
        }
    }
    return {};
}

const QHash<QString, ElementStyle>& styleMap() {
    static const QHash<QString, ElementStyle> styles = loadVestaStyles();
    return styles;
}

const QHash<QString, BondDistanceRange>& bondMap() {
    static const QHash<QString, BondDistanceRange> ranges = loadVestaBondRanges();
    return ranges;
}
}  // namespace

QColor vestaElementColor(const QString& element) {
    const auto key = vestaNormalizeElement(element);
    const auto it = styleMap().find(key);
    return it != styleMap().end() ? it->color : QColor("#C9D3E6");
}

double vestaElementRadius(const QString& element) {
    const auto key = vestaNormalizeElement(element);
    const auto it = styleMap().find(key);
    return it != styleMap().end() ? it->radius : 1.0;
}

bool vestaBondCutoff(const QString& elementA, const QString& elementB, double* cutoff) {
    if (cutoff == nullptr) {
        return false;
    }
    const auto it = bondMap().find(vestaBondKey(elementA, elementB));
    if (it == bondMap().end()) {
        return false;
    }
    *cutoff = it->maxDistance;
    return true;
}

bool vestaBondDistanceRange(const QString& elementA, const QString& elementB, BondDistanceRange* range) {
    if (range == nullptr) {
        return false;
    }
    const auto it = bondMap().find(vestaBondKey(elementA, elementB));
    if (it == bondMap().end()) {
        return false;
    }
    *range = it.value();
    return true;
}

double vestaMaximumBondCutoff() {
    double maxCutoff = 0.0;
    for (auto it = bondMap().cbegin(); it != bondMap().cend(); ++it) {
        maxCutoff = std::max(maxCutoff, it->maxDistance);
    }
    return maxCutoff > 0.0 ? maxCutoff : 4.5;
}
