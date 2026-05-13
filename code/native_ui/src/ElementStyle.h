#pragma once

#include <QColor>
#include <QString>

struct BondDistanceRange {
    double minDistance = 0.0;
    double maxDistance = 0.0;
};

inline bool operator==(const BondDistanceRange& lhs, const BondDistanceRange& rhs) {
    return lhs.minDistance == rhs.minDistance && lhs.maxDistance == rhs.maxDistance;
}

QString vestaNormalizeElement(const QString& element);
QString vestaBondKey(const QString& elementA, const QString& elementB);
QColor vestaElementColor(const QString& element);
double vestaElementRadius(const QString& element);
bool vestaBondCutoff(const QString& elementA, const QString& elementB, double* cutoff);
bool vestaBondDistanceRange(const QString& elementA, const QString& elementB, BondDistanceRange* range);
double vestaMaximumBondCutoff();
