#pragma once

#include <QColor>
#include <QString>

QColor vestaElementColor(const QString& element);
double vestaElementRadius(const QString& element);
bool vestaBondCutoff(const QString& elementA, const QString& elementB, double* cutoff);
double vestaMaximumBondCutoff();
