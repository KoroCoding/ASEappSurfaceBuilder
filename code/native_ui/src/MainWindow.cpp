#include "MainWindow.h"

#include "ElementStyle.h"
#include "StructureCanvas.h"
#include "StructureFileLoader.h"
#include "SurfaceOperations.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QFont>
#include <QFontMetrics>
#include <QGroupBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QMouseEvent>
#include <QMimeData>
#include <QMenuBar>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPen>
#include <QPushButton>
#include <QQuaternion>
#include <QRadialGradient>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScreen>
#include <QShortcut>
#include <QRadioButton>
#include <QAbstractSpinBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTextStream>
#include <QToolBar>
#include <QIcon>
#include <QUrl>
#include <QSettings>
#include <QShowEvent>
#include <QSizePolicy>
#include <QSignalBlocker>
#include <QStringConverter>
#include <QTimer>
#include <QVBoxLayout>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <deque>
#include <functional>
#include <vector>
#include <utility>

namespace {
QVector3D reciprocalDirection(const std::array<QVector3D, 3>& cell, int axisIndex) {
    switch (axisIndex) {
    case 0: return QVector3D::crossProduct(cell[1], cell[2]);
    case 1: return QVector3D::crossProduct(cell[2], cell[0]);
    default: return QVector3D::crossProduct(cell[0], cell[1]);
    }
}

std::pair<QVector3D, QVector3D> canonicalDirectViewDirection(const std::array<QVector3D, 3>& cell, int axisIndex) {
    const int upIndex = (axisIndex + 2) % 3;
    const int idx = std::clamp(axisIndex, 0, 2);
    return {cell[static_cast<std::size_t>(idx)], cell[static_cast<std::size_t>(upIndex)]};
}

std::pair<QVector3D, QVector3D> canonicalReciprocalViewDirection(const std::array<QVector3D, 3>& cell, int axisIndex) {
    const int upIndex = (axisIndex + 2) % 3;
    return {reciprocalDirection(cell, axisIndex), cell[static_cast<std::size_t>(upIndex)]};
}

bool hasNonIdentitySupercellFactors(const std::array<int, 3>& factors) {
    return factors[0] != 1 || factors[1] != 1 || factors[2] != 1;
}

bool editPreservesSupercellBase(const QString& label) {
    return label == QStringLiteral("vacuum")
        || label == QStringLiteral("remove_vacuum")
        || label == QStringLiteral("axis_tilt");
}

QVector3D safeNormalized(const QVector3D& vector, const QVector3D& fallback) {
    return vector.lengthSquared() > 1.0e-8f ? vector.normalized() : fallback;
}

bool textInputHasFocus() {
    for (const QWidget* widget = QApplication::focusWidget(); widget != nullptr; widget = widget->parentWidget()) {
        if (qobject_cast<const QLineEdit*>(widget) != nullptr ||
            qobject_cast<const QAbstractSpinBox*>(widget) != nullptr ||
            qobject_cast<const QComboBox*>(widget) != nullptr) {
            return true;
        }
    }
    return false;
}

QVector3D solveFractionalForCell(const std::array<QVector3D, 3>& cell, const QVector3D& cart) {
    const QVector3D a = cell[0];
    const QVector3D b = cell[1];
    const QVector3D c = cell[2];
    const float det = QVector3D::dotProduct(a, QVector3D::crossProduct(b, c));
    if (std::abs(det) < 1.0e-8f) {
        return cart;
    }
    return {
        QVector3D::dotProduct(cart, QVector3D::crossProduct(b, c)) / det,
        QVector3D::dotProduct(cart, QVector3D::crossProduct(c, a)) / det,
        QVector3D::dotProduct(cart, QVector3D::crossProduct(a, b)) / det
    };
}

QVector3D orthogonalUpHint(const QVector3D& forward, const QVector3D& preferred, const QVector3D& fallback) {
    QVector3D up = preferred;
    if (up.lengthSquared() <= 1.0e-8f) {
        up = fallback;
    }
    up -= QVector3D::dotProduct(up, forward.normalized()) * forward.normalized();
    if (up.lengthSquared() <= 1.0e-8f) {
        up = fallback;
        up -= QVector3D::dotProduct(up, forward.normalized()) * forward.normalized();
    }
    return safeNormalized(up, fallback);
}

int defaultSelectionCountForMode(const QString& mode) {
    const QString normalized = mode.trimmed().toLower();
    if (normalized == "selection_centroid") {
        return 1;
    }
    if (normalized == "pair_midpoint" || normalized == "pair_fraction") {
        return 2;
    }
    if (normalized == "triple_centroid" || normalized == "triple_weighted" || normalized == "multi_plane_normal") {
        return 3;
    }
    if (normalized == "multi_centroid" || normalized == "multi_weighted") {
        return 1;
    }
    return 1;
}

bool isLowerPrecursorCoordinate(const QVector3D& candidate, const QVector3D& current) {
    constexpr float kTieEpsilon = 1.0e-6f;
    if (candidate.z() < current.z() - kTieEpsilon) return true;
    if (candidate.z() > current.z() + kTieEpsilon) return false;
    if (candidate.y() < current.y() - kTieEpsilon) return true;
    if (candidate.y() > current.y() + kTieEpsilon) return false;
    return candidate.x() < current.x();
}

QVector3D lowestPrecursorOrigin(const std::vector<const NativeAtom*>& atoms) {
    const NativeAtom* lowest = nullptr;
    for (const NativeAtom* atom : atoms) {
        if (atom != nullptr && (lowest == nullptr || isLowerPrecursorCoordinate(atom->cartesian, lowest->cartesian))) {
            lowest = atom;
        }
    }
    return lowest != nullptr ? lowest->cartesian : QVector3D();
}

QVector3D lowestPrecursorOrigin(const std::vector<NativeAtom>& atoms) {
    if (atoms.empty()) {
        return {};
    }
    QVector3D lowest = atoms.front().cartesian;
    for (const NativeAtom& atom : atoms) {
        if (isLowerPrecursorCoordinate(atom.cartesian, lowest)) {
            lowest = atom.cartesian;
        }
    }
    return lowest;
}

struct PeriodicElementCell {
    int atomicNumber;
    const char* symbol;
    int row;
    int column;
};

struct PeriodicElementDetails {
    const char* symbol;
    const char* englishName;
    const char* atomicMass;
    const char* shells;
    int valenceElectrons;
};

constexpr PeriodicElementCell kPeriodicElements[] = {
    {1, "H", 1, 1}, {2, "He", 1, 18},
    {3, "Li", 2, 1}, {4, "Be", 2, 2}, {5, "B", 2, 13}, {6, "C", 2, 14}, {7, "N", 2, 15}, {8, "O", 2, 16}, {9, "F", 2, 17}, {10, "Ne", 2, 18},
    {11, "Na", 3, 1}, {12, "Mg", 3, 2}, {13, "Al", 3, 13}, {14, "Si", 3, 14}, {15, "P", 3, 15}, {16, "S", 3, 16}, {17, "Cl", 3, 17}, {18, "Ar", 3, 18},
    {19, "K", 4, 1}, {20, "Ca", 4, 2}, {21, "Sc", 4, 3}, {22, "Ti", 4, 4}, {23, "V", 4, 5}, {24, "Cr", 4, 6}, {25, "Mn", 4, 7}, {26, "Fe", 4, 8}, {27, "Co", 4, 9}, {28, "Ni", 4, 10}, {29, "Cu", 4, 11}, {30, "Zn", 4, 12}, {31, "Ga", 4, 13}, {32, "Ge", 4, 14}, {33, "As", 4, 15}, {34, "Se", 4, 16}, {35, "Br", 4, 17}, {36, "Kr", 4, 18},
    {37, "Rb", 5, 1}, {38, "Sr", 5, 2}, {39, "Y", 5, 3}, {40, "Zr", 5, 4}, {41, "Nb", 5, 5}, {42, "Mo", 5, 6}, {43, "Tc", 5, 7}, {44, "Ru", 5, 8}, {45, "Rh", 5, 9}, {46, "Pd", 5, 10}, {47, "Ag", 5, 11}, {48, "Cd", 5, 12}, {49, "In", 5, 13}, {50, "Sn", 5, 14}, {51, "Sb", 5, 15}, {52, "Te", 5, 16}, {53, "I", 5, 17}, {54, "Xe", 5, 18},
    {55, "Cs", 6, 1}, {56, "Ba", 6, 2}, {72, "Hf", 6, 4}, {73, "Ta", 6, 5}, {74, "W", 6, 6}, {75, "Re", 6, 7}, {76, "Os", 6, 8}, {77, "Ir", 6, 9}, {78, "Pt", 6, 10}, {79, "Au", 6, 11}, {80, "Hg", 6, 12}, {81, "Tl", 6, 13}, {82, "Pb", 6, 14}, {83, "Bi", 6, 15}, {84, "Po", 6, 16}, {85, "At", 6, 17}, {86, "Rn", 6, 18},
    {87, "Fr", 7, 1}, {88, "Ra", 7, 2}, {104, "Rf", 7, 4}, {105, "Db", 7, 5}, {106, "Sg", 7, 6}, {107, "Bh", 7, 7}, {108, "Hs", 7, 8}, {109, "Mt", 7, 9}, {110, "Ds", 7, 10}, {111, "Rg", 7, 11}, {112, "Cn", 7, 12}, {113, "Nh", 7, 13}, {114, "Fl", 7, 14}, {115, "Mc", 7, 15}, {116, "Lv", 7, 16}, {117, "Ts", 7, 17}, {118, "Og", 7, 18},
    {57, "La", 8, 4}, {58, "Ce", 8, 5}, {59, "Pr", 8, 6}, {60, "Nd", 8, 7}, {61, "Pm", 8, 8}, {62, "Sm", 8, 9}, {63, "Eu", 8, 10}, {64, "Gd", 8, 11}, {65, "Tb", 8, 12}, {66, "Dy", 8, 13}, {67, "Ho", 8, 14}, {68, "Er", 8, 15}, {69, "Tm", 8, 16}, {70, "Yb", 8, 17}, {71, "Lu", 8, 18},
    {89, "Ac", 9, 4}, {90, "Th", 9, 5}, {91, "Pa", 9, 6}, {92, "U", 9, 7}, {93, "Np", 9, 8}, {94, "Pu", 9, 9}, {95, "Am", 9, 10}, {96, "Cm", 9, 11}, {97, "Bk", 9, 12}, {98, "Cf", 9, 13}, {99, "Es", 9, 14}, {100, "Fm", 9, 15}, {101, "Md", 9, 16}, {102, "No", 9, 17}, {103, "Lr", 9, 18}
};

constexpr PeriodicElementDetails kPeriodicElementDetails[] = {
    {"H", "Hydrogen", "1.008", "1", 1}, {"He", "Helium", "4.0026", "2", 2},
    {"Li", "Lithium", "6.94", "2,1", 1}, {"Be", "Beryllium", "9.0122", "2,2", 2}, {"B", "Boron", "10.81", "2,3", 3}, {"C", "Carbon", "12.011", "2,4", 4}, {"N", "Nitrogen", "14.007", "2,5", 5}, {"O", "Oxygen", "15.999", "2,6", 6}, {"F", "Fluorine", "18.998", "2,7", 7}, {"Ne", "Neon", "20.180", "2,8", 8},
    {"Na", "Sodium", "22.990", "2,8,1", 1}, {"Mg", "Magnesium", "24.305", "2,8,2", 2}, {"Al", "Aluminum", "26.982", "2,8,3", 3}, {"Si", "Silicon", "28.085", "2,8,4", 4}, {"P", "Phosphorus", "30.974", "2,8,5", 5}, {"S", "Sulfur", "32.06", "2,8,6", 6}, {"Cl", "Chlorine", "35.45", "2,8,7", 7}, {"Ar", "Argon", "39.948", "2,8,8", 8},
    {"K", "Potassium", "39.098", "2,8,8,1", 1}, {"Ca", "Calcium", "40.078", "2,8,8,2", 2}, {"Sc", "Scandium", "44.956", "2,8,9,2", 2}, {"Ti", "Titanium", "47.867", "2,8,10,2", 2}, {"V", "Vanadium", "50.942", "2,8,11,2", 2}, {"Cr", "Chromium", "51.996", "2,8,13,1", 1}, {"Mn", "Manganese", "54.938", "2,8,13,2", 2}, {"Fe", "Iron", "55.845", "2,8,14,2", 2}, {"Co", "Cobalt", "58.933", "2,8,15,2", 2}, {"Ni", "Nickel", "58.693", "2,8,16,2", 2}, {"Cu", "Copper", "63.546", "2,8,18,1", 1}, {"Zn", "Zinc", "65.38", "2,8,18,2", 2}, {"Ga", "Gallium", "69.723", "2,8,18,3", 3}, {"Ge", "Germanium", "72.630", "2,8,18,4", 4}, {"As", "Arsenic", "74.922", "2,8,18,5", 5}, {"Se", "Selenium", "78.971", "2,8,18,6", 6}, {"Br", "Bromine", "79.904", "2,8,18,7", 7}, {"Kr", "Krypton", "83.798", "2,8,18,8", 8},
    {"Rb", "Rubidium", "85.468", "2,8,18,8,1", 1}, {"Sr", "Strontium", "87.62", "2,8,18,8,2", 2}, {"Y", "Yttrium", "88.906", "2,8,18,9,2", 2}, {"Zr", "Zirconium", "91.224", "2,8,18,10,2", 2}, {"Nb", "Niobium", "92.906", "2,8,18,12,1", 1}, {"Mo", "Molybdenum", "95.95", "2,8,18,13,1", 1}, {"Tc", "Technetium", "(98)", "2,8,18,13,2", 2}, {"Ru", "Ruthenium", "101.07", "2,8,18,15,1", 1}, {"Rh", "Rhodium", "102.91", "2,8,18,16,1", 1}, {"Pd", "Palladium", "106.42", "2,8,18,18", 0}, {"Ag", "Silver", "107.87", "2,8,18,18,1", 1}, {"Cd", "Cadmium", "112.41", "2,8,18,18,2", 2}, {"In", "Indium", "114.82", "2,8,18,18,3", 3}, {"Sn", "Tin", "118.71", "2,8,18,18,4", 4}, {"Sb", "Antimony", "121.76", "2,8,18,18,5", 5}, {"Te", "Tellurium", "127.60", "2,8,18,18,6", 6}, {"I", "Iodine", "126.90", "2,8,18,18,7", 7}, {"Xe", "Xenon", "131.29", "2,8,18,18,8", 8},
    {"Cs", "Cesium", "132.91", "2,8,18,18,8,1", 1}, {"Ba", "Barium", "137.33", "2,8,18,18,8,2", 2}, {"La", "Lanthanum", "138.91", "2,8,18,18,9,2", 2}, {"Ce", "Cerium", "140.12", "2,8,18,19,9,2", 2}, {"Pr", "Praseodymium", "140.91", "2,8,18,21,8,2", 2}, {"Nd", "Neodymium", "144.24", "2,8,18,22,8,2", 2}, {"Pm", "Promethium", "(145)", "2,8,18,23,8,2", 2}, {"Sm", "Samarium", "150.36", "2,8,18,24,8,2", 2}, {"Eu", "Europium", "151.96", "2,8,18,25,8,2", 2}, {"Gd", "Gadolinium", "157.25", "2,8,18,25,9,2", 2}, {"Tb", "Terbium", "158.93", "2,8,18,27,8,2", 2}, {"Dy", "Dysprosium", "162.50", "2,8,18,28,8,2", 2}, {"Ho", "Holmium", "164.93", "2,8,18,29,8,2", 2}, {"Er", "Erbium", "167.26", "2,8,18,30,8,2", 2}, {"Tm", "Thulium", "168.93", "2,8,18,31,8,2", 2}, {"Yb", "Ytterbium", "173.05", "2,8,18,32,8,2", 2}, {"Lu", "Lutetium", "174.97", "2,8,18,32,9,2", 2}, {"Hf", "Hafnium", "178.49", "2,8,18,32,10,2", 2}, {"Ta", "Tantalum", "180.95", "2,8,18,32,11,2", 2}, {"W", "Tungsten", "183.84", "2,8,18,32,12,2", 2}, {"Re", "Rhenium", "186.21", "2,8,18,32,13,2", 2}, {"Os", "Osmium", "190.23", "2,8,18,32,14,2", 2}, {"Ir", "Iridium", "192.22", "2,8,18,32,15,2", 2}, {"Pt", "Platinum", "195.08", "2,8,18,32,17,1", 1}, {"Au", "Gold", "196.97", "2,8,18,32,18,1", 1}, {"Hg", "Mercury", "200.59", "2,8,18,32,18,2", 2}, {"Tl", "Thallium", "204.38", "2,8,18,32,18,3", 3}, {"Pb", "Lead", "207.2", "2,8,18,32,18,4", 4}, {"Bi", "Bismuth", "208.98", "2,8,18,32,18,5", 5}, {"Po", "Polonium", "(209)", "2,8,18,32,18,6", 6}, {"At", "Astatine", "(210)", "2,8,18,32,18,7", 7}, {"Rn", "Radon", "(222)", "2,8,18,32,18,8", 8},
    {"Fr", "Francium", "(223)", "2,8,18,32,18,8,1", 1}, {"Ra", "Radium", "(226)", "2,8,18,32,18,8,2", 2}, {"Ac", "Actinium", "(227)", "2,8,18,32,18,9,2", 2}, {"Th", "Thorium", "232.04", "2,8,18,32,18,10,2", 2}, {"Pa", "Protactinium", "231.04", "2,8,18,32,20,9,2", 2}, {"U", "Uranium", "238.03", "2,8,18,32,21,9,2", 2}, {"Np", "Neptunium", "(237)", "2,8,18,32,22,9,2", 2}, {"Pu", "Plutonium", "(244)", "2,8,18,32,24,8,2", 2}, {"Am", "Americium", "(243)", "2,8,18,32,25,8,2", 2}, {"Cm", "Curium", "(247)", "2,8,18,32,25,9,2", 2}, {"Bk", "Berkelium", "(247)", "2,8,18,32,27,8,2", 2}, {"Cf", "Californium", "(251)", "2,8,18,32,28,8,2", 2}, {"Es", "Einsteinium", "(252)", "2,8,18,32,29,8,2", 2}, {"Fm", "Fermium", "(257)", "2,8,18,32,30,8,2", 2}, {"Md", "Mendelevium", "(258)", "2,8,18,32,31,8,2", 2}, {"No", "Nobelium", "(259)", "2,8,18,32,32,8,2", 2}, {"Lr", "Lawrencium", "(266)", "2,8,18,32,32,8,3", 3}, {"Rf", "Rutherfordium", "(267)", "2,8,18,32,32,10,2", 2}, {"Db", "Dubnium", "(268)", "2,8,18,32,32,11,2", 2}, {"Sg", "Seaborgium", "(269)", "2,8,18,32,32,12,2", 2}, {"Bh", "Bohrium", "(270)", "2,8,18,32,32,13,2", 2}, {"Hs", "Hassium", "(277)", "2,8,18,32,32,14,2", 2}, {"Mt", "Meitnerium", "(278)", "2,8,18,32,32,15,2", 2}, {"Ds", "Darmstadtium", "(281)", "2,8,18,32,32,17,1", 1}, {"Rg", "Roentgenium", "(282)", "2,8,18,32,32,18,1", 1}, {"Cn", "Copernicium", "(285)", "2,8,18,32,32,18,2", 2}, {"Nh", "Nihonium", "(286)", "2,8,18,32,32,18,3", 3}, {"Fl", "Flerovium", "(289)", "2,8,18,32,32,18,4", 4}, {"Mc", "Moscovium", "(290)", "2,8,18,32,32,18,5", 5}, {"Lv", "Livermorium", "(293)", "2,8,18,32,32,18,6", 6}, {"Ts", "Tennessine", "(294)", "2,8,18,32,32,18,7", 7}, {"Og", "Oganesson", "(294)", "2,8,18,32,32,18,8", 8}
};

QStringList periodicElementSymbols() {
    QStringList symbols;
    for (const auto& element : kPeriodicElements) {
        symbols << QString::fromLatin1(element.symbol);
    }
    return symbols;
}

const PeriodicElementDetails* periodicElementDetails(const QString& symbol) {
    for (const auto& element : kPeriodicElementDetails) {
        if (symbol.compare(QString::fromLatin1(element.symbol), Qt::CaseInsensitive) == 0) {
            return &element;
        }
    }
    return nullptr;
}

QString periodicElementEnglishName(const QString& symbol) {
    if (const auto* details = periodicElementDetails(symbol)) {
        return QString::fromLatin1(details->englishName);
    }
    return symbol;
}

template <std::size_t N>
bool periodicSymbolIn(const QString& symbol, const char* const (&symbols)[N]) {
    for (const char* candidate : symbols) {
        if (symbol.compare(QString::fromLatin1(candidate), Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

enum class PeriodicElementCategory {
    AlkaliMetal,
    AlkalineEarthMetal,
    TransitionMetal,
    PostTransitionMetal,
    Metalloid,
    ReactiveNonmetal,
    Halogen,
    NobleGas,
    Lanthanide,
    Actinide,
    Other
};

PeriodicElementCategory periodicElementCategory(const PeriodicElementCell& element) {
    const QString symbol = QString::fromLatin1(element.symbol);
    if (element.row == 8) return PeriodicElementCategory::Lanthanide;
    if (element.row == 9) return PeriodicElementCategory::Actinide;
    if (element.column == 18) return PeriodicElementCategory::NobleGas;
    if (element.column == 17) return PeriodicElementCategory::Halogen;
    if (element.column == 1 && symbol != QStringLiteral("H")) return PeriodicElementCategory::AlkaliMetal;
    if (element.column == 2) return PeriodicElementCategory::AlkalineEarthMetal;
    if (element.column >= 3 && element.column <= 12) return PeriodicElementCategory::TransitionMetal;

    static constexpr const char* kMetalloids[] = {"B", "Si", "Ge", "As", "Sb", "Te", "Po"};
    static constexpr const char* kReactiveNonmetals[] = {"H", "C", "N", "O", "P", "S", "Se"};
    static constexpr const char* kPostTransitionMetals[] = {"Al", "Ga", "In", "Sn", "Tl", "Pb", "Bi", "Nh", "Fl", "Mc", "Lv"};
    if (periodicSymbolIn(symbol, kMetalloids)) return PeriodicElementCategory::Metalloid;
    if (periodicSymbolIn(symbol, kReactiveNonmetals)) return PeriodicElementCategory::ReactiveNonmetal;
    if (periodicSymbolIn(symbol, kPostTransitionMetals)) return PeriodicElementCategory::PostTransitionMetal;
    return PeriodicElementCategory::Other;
}

QColor periodicElementAccent(PeriodicElementCategory category) {
    switch (category) {
    case PeriodicElementCategory::AlkaliMetal: return QColor("#F6C453");
    case PeriodicElementCategory::AlkalineEarthMetal: return QColor("#D8F94F");
    case PeriodicElementCategory::TransitionMetal: return QColor("#FF735D");
    case PeriodicElementCategory::PostTransitionMetal: return QColor("#36D9F5");
    case PeriodicElementCategory::Metalloid: return QColor("#22F5C4");
    case PeriodicElementCategory::ReactiveNonmetal: return QColor("#4CF85D");
    case PeriodicElementCategory::Halogen: return QColor("#39F966");
    case PeriodicElementCategory::NobleGas: return QColor("#6C9DFF");
    case PeriodicElementCategory::Lanthanide: return QColor("#FF68A8");
    case PeriodicElementCategory::Actinide: return QColor("#D87CFF");
    default: return QColor("#C7D0D9");
    }
}

QColor periodicElementAccent(const PeriodicElementCell& element) {
    return periodicElementAccent(periodicElementCategory(element));
}

QString periodicElementCategoryLabel(PeriodicElementCategory category, bool japanese) {
    switch (category) {
    case PeriodicElementCategory::AlkaliMetal: return japanese ? QStringLiteral("アルカリ金属") : QStringLiteral("Alkali metal");
    case PeriodicElementCategory::AlkalineEarthMetal: return japanese ? QStringLiteral("アルカリ土類金属") : QStringLiteral("Alkaline earth metal");
    case PeriodicElementCategory::TransitionMetal: return japanese ? QStringLiteral("遷移金属") : QStringLiteral("Transition metal");
    case PeriodicElementCategory::PostTransitionMetal: return japanese ? QStringLiteral("ポスト遷移金属") : QStringLiteral("Post-transition metal");
    case PeriodicElementCategory::Metalloid: return japanese ? QStringLiteral("半金属") : QStringLiteral("Metalloid");
    case PeriodicElementCategory::ReactiveNonmetal: return japanese ? QStringLiteral("非金属") : QStringLiteral("Reactive nonmetal");
    case PeriodicElementCategory::Halogen: return japanese ? QStringLiteral("ハロゲン") : QStringLiteral("Halogen");
    case PeriodicElementCategory::NobleGas: return japanese ? QStringLiteral("希ガス") : QStringLiteral("Noble gas");
    case PeriodicElementCategory::Lanthanide: return japanese ? QStringLiteral("ランタノイド") : QStringLiteral("Lanthanide");
    case PeriodicElementCategory::Actinide: return japanese ? QStringLiteral("アクチノイド") : QStringLiteral("Actinide");
    default: return japanese ? QStringLiteral("元素") : QStringLiteral("Element");
    }
}

QString periodicElementFamily(const PeriodicElementCell& element, bool japanese) {
    return periodicElementCategoryLabel(periodicElementCategory(element), japanese);
}

class PeriodicElementInfoPopup final : public QFrame {
public:
    explicit PeriodicElementInfoPopup(bool japanese, QWidget* parent = nullptr)
        : QFrame(parent, Qt::ToolTip | Qt::FramelessWindowHint),
          m_japanese(japanese)
    {
        setObjectName(QStringLiteral("periodicElementInfoPopup"));
        setAttribute(Qt::WA_ShowWithoutActivating, true);
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setMinimumWidth(300);
        setStyleSheet(QStringLiteral(
            "QFrame#periodicElementInfoPopup { background:#08111A; border:1px solid #2DD4BF; border-radius:10px; }"
            "QFrame#symbolPanel { border:0; border-radius:8px; }"
            "QLabel { background:transparent; border:0; color:#E5F6FF; font-family:'Segoe UI','Meiryo'; }"
            "QLabel#detailLabel { color:#8EA8BA; font-size:11px; }"
            "QLabel#detailValue { color:#F0FDFF; font-size:11px; font-weight:650; }"
            "QLabel#nameLabel { color:#FFFFFF; font-size:15px; font-weight:700; }"
            "QLabel#categoryLabel { color:#B6C8D5; font-size:11px; }"
            "QLabel#hintLabel { color:#A7F3D0; background:#0E1D2A; border-top:1px solid #203445; padding:6px 9px; }"));

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(8, 8, 8, 8);
        root->setSpacing(8);

        auto* header = new QHBoxLayout();
        header->setContentsMargins(0, 0, 0, 0);
        header->setSpacing(10);

        m_symbolPanel = new QFrame(this);
        m_symbolPanel->setObjectName(QStringLiteral("symbolPanel"));
        m_symbolPanel->setFixedSize(82, 82);
        auto* symbolLayout = new QVBoxLayout(m_symbolPanel);
        symbolLayout->setContentsMargins(7, 6, 7, 6);
        symbolLayout->setSpacing(0);

        m_numberLabel = new QLabel(m_symbolPanel);
        m_numberLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_numberLabel->setStyleSheet(QStringLiteral("font-size:10px; font-weight:650;"));
        symbolLayout->addWidget(m_numberLabel);

        m_symbolLabel = new QLabel(m_symbolPanel);
        m_symbolLabel->setAlignment(Qt::AlignCenter);
        m_symbolLabel->setStyleSheet(QStringLiteral("font-size:33px; font-weight:800;"));
        symbolLayout->addWidget(m_symbolLabel, 1);

        header->addWidget(m_symbolPanel);

        auto* headerText = new QVBoxLayout();
        headerText->setContentsMargins(0, 4, 0, 4);
        headerText->setSpacing(4);
        m_nameLabel = new QLabel(this);
        m_nameLabel->setObjectName(QStringLiteral("nameLabel"));
        m_categoryLabel = new QLabel(this);
        m_categoryLabel->setObjectName(QStringLiteral("categoryLabel"));
        m_categoryLabel->setWordWrap(true);
        headerText->addWidget(m_nameLabel);
        headerText->addWidget(m_categoryLabel);
        headerText->addStretch(1);
        header->addLayout(headerText, 1);
        root->addLayout(header);

        auto* grid = new QGridLayout();
        grid->setContentsMargins(2, 0, 2, 0);
        grid->setHorizontalSpacing(14);
        grid->setVerticalSpacing(4);

        auto addRow = [&](int row, const QString& label, QLabel*& valueLabel) {
            auto* labelWidget = new QLabel(label, this);
            labelWidget->setObjectName(QStringLiteral("detailLabel"));
            valueLabel = new QLabel(this);
            valueLabel->setObjectName(QStringLiteral("detailValue"));
            valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
            grid->addWidget(labelWidget, row, 0);
            grid->addWidget(valueLabel, row, 1);
        };
        addRow(0, japanese ? QStringLiteral("分類") : QStringLiteral("Category"), m_familyValue);
        addRow(1, japanese ? QStringLiteral("周期") : QStringLiteral("Period"), m_periodValue);
        addRow(2, japanese ? QStringLiteral("族/ブロック") : QStringLiteral("Group / block"), m_groupValue);
        addRow(3, japanese ? QStringLiteral("原子量") : QStringLiteral("Atomic mass"), m_massValue);
        addRow(4, japanese ? QStringLiteral("電子殻") : QStringLiteral("Electron shells"), m_shellValue);
        addRow(5, japanese ? QStringLiteral("最外殻電子") : QStringLiteral("Outermost electrons"), m_valenceValue);
        addRow(6, japanese ? QStringLiteral("表示半径") : QStringLiteral("Display radius"), m_radiusValue);
        addRow(7, japanese ? QStringLiteral("カード色") : QStringLiteral("Card color"), m_colorValue);
        root->addLayout(grid);

        m_hintLabel = new QLabel(
            japanese ? QStringLiteral("クリックで生成元素に設定します") : QStringLiteral("Click to use this as the placement element"),
            this);
        m_hintLabel->setObjectName(QStringLiteral("hintLabel"));
        root->addWidget(m_hintLabel);
    }

    void showElement(const PeriodicElementCell& element, const QPoint& globalPos) {
        const QString symbol = QString::fromLatin1(element.symbol);
        const auto* details = periodicElementDetails(symbol);
        const QString name = periodicElementEnglishName(symbol);
        const QString family = periodicElementFamily(element, m_japanese);
        const QColor accent = periodicElementAccent(element);
        const QString colorHex = accent.name(QColor::HexRgb).toUpper();
        const QColor headerTextColor = accent.lightness() > 170 ? QColor("#06111A") : QColor("#FFFFFF");
        const int period = element.row == 8 ? 6 : (element.row == 9 ? 7 : element.row);
        const QString group = element.row >= 8
            ? (m_japanese ? QStringLiteral("fブロック") : QStringLiteral("f-block"))
            : QString::number(element.column);

        m_symbolPanel->setStyleSheet(QStringLiteral("QFrame#symbolPanel { background:%1; border:0; border-radius:8px; }").arg(colorHex));
        m_numberLabel->setStyleSheet(QStringLiteral("font-size:10px; font-weight:650; color:%1;").arg(headerTextColor.name(QColor::HexRgb)));
        m_symbolLabel->setStyleSheet(QStringLiteral("font-size:33px; font-weight:800; color:%1;").arg(headerTextColor.name(QColor::HexRgb)));
        m_numberLabel->setText(QStringLiteral("No. %1").arg(element.atomicNumber));
        m_symbolLabel->setText(symbol);
        m_nameLabel->setText(name);
        m_categoryLabel->setText(family);

        m_familyValue->setText(family);
        m_periodValue->setText(QString::number(period));
        m_groupValue->setText(group);
        m_massValue->setText(details != nullptr ? QString::fromLatin1(details->atomicMass) : QStringLiteral("-"));
        m_shellValue->setText(details != nullptr ? QString::fromLatin1(details->shells) : QStringLiteral("-"));
        m_valenceValue->setText(details != nullptr ? QString::number(details->valenceElectrons) : QStringLiteral("-"));
        m_radiusValue->setText(QStringLiteral("%1 Å").arg(QString::number(vestaElementRadius(symbol), 'f', 2)));
        m_colorValue->setText(colorHex);
        m_colorValue->setStyleSheet(QStringLiteral("color:%1; font-size:11px; font-weight:650;").arg(colorHex));

        adjustSize();
        moveNearCursor(globalPos);
        show();
        raise();
    }

private:
    void moveNearCursor(const QPoint& globalPos) {
        QPoint pos = globalPos + QPoint(16, 16);
        QScreen* screen = QApplication::screenAt(globalPos);
        if (screen == nullptr) {
            screen = QApplication::primaryScreen();
        }
        const QRect available = screen != nullptr ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);
        if (pos.x() + width() > available.right()) {
            pos.setX(std::max(available.left(), globalPos.x() - width() - 16));
        }
        if (pos.y() + height() > available.bottom()) {
            pos.setY(std::max(available.top(), globalPos.y() - height() - 16));
        }
        move(pos);
    }

    bool m_japanese = true;
    QFrame* m_symbolPanel = nullptr;
    QLabel* m_numberLabel = nullptr;
    QLabel* m_symbolLabel = nullptr;
    QLabel* m_nameLabel = nullptr;
    QLabel* m_categoryLabel = nullptr;
    QLabel* m_familyValue = nullptr;
    QLabel* m_periodValue = nullptr;
    QLabel* m_groupValue = nullptr;
    QLabel* m_massValue = nullptr;
    QLabel* m_shellValue = nullptr;
    QLabel* m_valenceValue = nullptr;
    QLabel* m_radiusValue = nullptr;
    QLabel* m_colorValue = nullptr;
    QLabel* m_hintLabel = nullptr;
};

class PeriodicElementButton final : public QPushButton {
public:
    using HoverCallback = std::function<void(const PeriodicElementCell&, const QPoint&)>;
    using LeaveCallback = std::function<void()>;

    PeriodicElementButton(const PeriodicElementCell& element, bool selected, QWidget* parent = nullptr)
        : QPushButton(parent),
          m_element(element),
          m_symbol(QString::fromLatin1(element.symbol)),
          m_name(periodicElementEnglishName(QString::fromLatin1(element.symbol))),
          m_accent(periodicElementAccent(element)),
          m_selected(selected)
    {
        setText(m_symbol);
        setCursor(Qt::PointingHandCursor);
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
        setFixedSize(kCardSize);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }

    QSize sizeHint() const override {
        return kCardSize;
    }

    void setHoverCallbacks(HoverCallback hoverCallback, LeaveCallback leaveCallback) {
        m_hoverCallback = std::move(hoverCallback);
        m_leaveCallback = std::move(leaveCallback);
    }

protected:
    bool event(QEvent* event) override {
        if (event->type() == QEvent::Enter) {
            showHover(mapToGlobal(QPoint(width(), 0)));
        } else if (event->type() == QEvent::MouseMove) {
            if (auto* mouseEvent = dynamic_cast<QMouseEvent*>(event)) {
                showHover(mouseEvent->globalPosition().toPoint());
            }
        } else if (event->type() == QEvent::Leave) {
            if (m_leaveCallback) {
                m_leaveCallback();
            }
        }
        return QPushButton::event(event);
    }

    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);

        const QRectF card = rect().adjusted(2, 2, -2, -2);
        QColor accent = m_accent;
        if (underMouse()) {
            accent = accent.lighter(128);
        }

        QPainterPath path;
        path.addRoundedRect(card, 5, 5);

        painter.fillPath(path, underMouse() ? QColor("#101B27") : QColor("#0B1118"));
        const QColor glowColor(accent.red(), accent.green(), accent.blue(), underMouse() ? 70 : 24);
        for (int i = 0; i < (underMouse() ? 3 : 1); ++i) {
            QPen glowPen(glowColor, 3 + i * 2);
            glowPen.setJoinStyle(Qt::RoundJoin);
            painter.setPen(glowPen);
            painter.drawPath(path);
        }

        QPen borderPen(m_selected ? QColor("#FFD166") : accent, m_selected ? 2.4 : 1.3);
        borderPen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(borderPen);
        painter.drawPath(path);

        if (hasFocus()) {
            QPen focusPen(QColor("#FFFFFF"), 1.0, Qt::DashLine);
            painter.setPen(focusPen);
            painter.drawRoundedRect(card.adjusted(3, 3, -3, -3), 3, 3);
        }

        auto setFont = [&painter](double pointSize, QFont::Weight weight) {
            QFont font(QStringLiteral("Segoe UI"));
            font.setPointSizeF(pointSize);
            font.setWeight(weight);
            painter.setFont(font);
        };

        setFont(6.0, QFont::DemiBold);
        painter.setPen(accent.lighter(130));
        painter.drawText(QRectF(5, 3, 20, 9), Qt::AlignLeft | Qt::AlignVCenter, QString::number(m_element.atomicNumber));

        setFont(m_symbol.size() >= 2 ? 16.0 : 19.0, QFont::Bold);
        const QRectF symbolRect(3, 15, width() - 6, 23);
        painter.setPen(QColor(accent.red(), accent.green(), accent.blue(), 70));
        painter.drawText(symbolRect.translated(0, 1), Qt::AlignCenter, m_symbol);
        painter.setPen(accent.lighter(138));
        painter.drawText(symbolRect, Qt::AlignCenter, m_symbol);

        setFont(5.2, QFont::DemiBold);
        const QString shortName = QFontMetrics(painter.font()).elidedText(m_name, Qt::ElideRight, width() - 8);
        painter.setPen(QColor("#C8E5EF"));
        painter.drawText(QRectF(4, 41, width() - 8, 9), Qt::AlignCenter, shortName);
    }

private:
    void showHover(const QPoint& globalPos) {
        if (m_hoverCallback) {
            m_hoverCallback(m_element, globalPos);
        }
    }

    static const QSize kCardSize;
    PeriodicElementCell m_element;
    QString m_symbol;
    QString m_name;
    QColor m_accent;
    bool m_selected = false;
    HoverCallback m_hoverCallback;
    LeaveCallback m_leaveCallback;
};

const QSize PeriodicElementButton::kCardSize(50, 56);

class PeriodicElementDialog final : public QDialog {
public:
    PeriodicElementDialog(bool japanese, const QString& currentElement, QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle(japanese ? QStringLiteral("周期表から生成元素を選択") : QStringLiteral("Select placement element"));
        setStyleSheet(QStringLiteral(
            "QDialog { background:#0B1017; color:#E5F6FF; }"
            "QLabel { color:#D7ECF5; }"));
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(4, 4, 4, 4);
        layout->setSpacing(0);
        m_infoPopup = new PeriodicElementInfoPopup(japanese, this);

        auto* tableWidget = new QWidget(this);
        auto* grid = new QGridLayout(tableWidget);
        grid->setContentsMargins(8, 8, 8, 8);
        grid->setHorizontalSpacing(3);
        grid->setVerticalSpacing(3);

        auto addSeriesPlaceholder = [&](int row, const QString& text, PeriodicElementCategory category) {
            const QColor color = periodicElementAccent(category);
            auto* label = new QLabel(text, tableWidget);
            label->setFixedSize(50, 56);
            label->setAlignment(Qt::AlignCenter);
            label->setStyleSheet(QStringLiteral(
                "QLabel { background:#0B1118; color:%1; border:1.3px solid %1; border-radius:5px; font-size:10px; font-weight:700; }")
                .arg(color.name(QColor::HexRgb)));
            grid->addWidget(label, row, 2);
        };
        addSeriesPlaceholder(5, QStringLiteral("57-71"), PeriodicElementCategory::Lanthanide);
        addSeriesPlaceholder(6, QStringLiteral("89-103"), PeriodicElementCategory::Actinide);

        for (const auto& element : kPeriodicElements) {
            const QString symbol = QString::fromLatin1(element.symbol);
            const bool selected = symbol.compare(currentElement, Qt::CaseInsensitive) == 0;
            auto* button = new PeriodicElementButton(element, selected, tableWidget);
            button->setHoverCallbacks(
                [this](const PeriodicElementCell& hoveredElement, const QPoint& globalPos) {
                    m_infoPopup->showElement(hoveredElement, globalPos);
                },
                [this]() {
                    m_infoPopup->hide();
                });
            connect(button, &QPushButton::clicked, this, [this, symbol]() {
                m_infoPopup->hide();
                m_selectedElement = symbol;
                accept();
            });
            grid->addWidget(button, element.row - 1, element.column - 1);
        }
        tableWidget->setFixedSize(tableWidget->sizeHint());
        layout->addWidget(tableWidget);
        setFixedSize(layout->sizeHint());
    }

    QString selectedElement() const {
        return m_selectedElement;
    }

private:
    QString m_selectedElement;
    PeriodicElementInfoPopup* m_infoPopup = nullptr;
};

bool isPoscarPath(const QString& path) {
    const QFileInfo info(path);
    const QString fileName = info.fileName().toLower();
    const QString suffix = info.suffix().toLower();
    return fileName == QStringLiteral("poscar")
        || fileName == QStringLiteral("contcar")
        || suffix == QStringLiteral("vasp")
        || suffix == QStringLiteral("poscar")
        || suffix == QStringLiteral("contcar");
}

bool writePoscarFile(const StructureData& structure, const QString& path, QString* errorMessage) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Failed to write %1").arg(path);
        }
        return false;
    }

    QStringList elements;
    std::vector<int> counts;
    for (const auto& atom : structure.atoms) {
        const int index = elements.indexOf(atom.element);
        if (index < 0) {
            elements << atom.element;
            counts.push_back(1);
        } else {
            counts[static_cast<std::size_t>(index)] += 1;
        }
    }
    const bool hasSelectiveFlags = std::any_of(structure.atoms.begin(), structure.atoms.end(), [](const NativeAtom& atom) {
        return !atom.movable[0] || !atom.movable[1] || !atom.movable[2];
    });

    QTextStream out(&file);
    out.setRealNumberNotation(QTextStream::FixedNotation);
    out.setRealNumberPrecision(10);
    out << (structure.title.isEmpty() ? QStringLiteral("ASEapp Surface Builder") : structure.title) << "\n";
    out << "1.0\n";
    for (const auto& vec : structure.cellVectors) {
        out << QStringLiteral("  %1  %2  %3\n").arg(vec.x(), 16, 'f', 10).arg(vec.y(), 16, 'f', 10).arg(vec.z(), 16, 'f', 10);
    }
    out << "  " << elements.join(QStringLiteral("  ")) << "\n";
    for (int count : counts) {
        out << QStringLiteral("  %1").arg(count);
    }
    out << "\n";
    if (hasSelectiveFlags) {
        out << "Selective dynamics\n";
    }
    out << "Direct\n";
    for (const auto& element : elements) {
        for (const auto& atom : structure.atoms) {
            if (atom.element != element) {
                continue;
            }
            out << QStringLiteral("  %1  %2  %3")
                .arg(atom.fractional.x(), 16, 'f', 10)
                .arg(atom.fractional.y(), 16, 'f', 10)
                .arg(atom.fractional.z(), 16, 'f', 10);
            if (hasSelectiveFlags) {
                out << QStringLiteral("  %1  %2  %3")
                    .arg(atom.movable[0] ? "T" : "F")
                    .arg(atom.movable[1] ? "T" : "F")
                    .arg(atom.movable[2] ? "T" : "F");
            }
            out << QStringLiteral("  # %1\n").arg(atom.tag);
        }
    }
    return true;
}

bool isXyzPath(const QString& path) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QStringLiteral("xyz") || suffix == QStringLiteral("extxyz");
}

QString extXyzEscaped(QString value) {
    value.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    value.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    value.replace(QLatin1Char('\n'), QLatin1Char(' '));
    value.replace(QLatin1Char('\r'), QLatin1Char(' '));
    return value;
}

QString xyzElement(const NativeAtom& atom) {
    const QString element = atom.element.trimmed();
    return element.isEmpty() ? QStringLiteral("X") : element;
}

bool writeXyzFile(const StructureData& structure, const QString& path, bool extended, QString* errorMessage) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Failed to write %1").arg(path);
        }
        return false;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out.setRealNumberNotation(QTextStream::FixedNotation);
    out.setRealNumberPrecision(10);
    out << structure.atoms.size() << '\n';
    if (extended) {
        QStringList latticeValues;
        for (const auto& vec : structure.cellVectors) {
            latticeValues << QString::number(vec.x(), 'f', 10)
                          << QString::number(vec.y(), 'f', 10)
                          << QString::number(vec.z(), 'f', 10);
        }
        out << "Lattice=\"" << latticeValues.join(QLatin1Char(' ')) << "\" "
            << "Properties=species:S:1:pos:R:3:atom_id:I:1:tag:S:1 "
            << "pbc=\"T T T\" "
            << "Title=\"" << extXyzEscaped(structure.title.isEmpty() ? QStringLiteral("ASEapp Surface Builder") : structure.title) << "\"\n";
    } else {
        out << (structure.title.isEmpty() ? QStringLiteral("ASEapp Surface Builder XYZ") : structure.title) << '\n';
    }
    for (const auto& atom : structure.atoms) {
        out << xyzElement(atom) << ' '
            << atom.cartesian.x() << ' '
            << atom.cartesian.y() << ' '
            << atom.cartesian.z();
        if (extended) {
            const QString safeTag = atom.tag.trimmed().isEmpty()
                ? QStringLiteral("%1-%2").arg(xyzElement(atom)).arg(atom.atomId)
                : atom.tag.trimmed().replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral("_"));
            out << ' ' << atom.atomId << ' ' << safeTag;
        }
        out << '\n';
    }
    return true;
}

QVector3D normalizedOrFallback(const QVector3D& value, const QVector3D& fallback = QVector3D(0.0f, 0.0f, 1.0f)) {
    return value.lengthSquared() > 1.0e-12f ? value.normalized() : fallback;
}

bool isSelectedBondAxisKey(const QString& axisKey) {
    const QString normalized = axisKey.trimmed().toLower();
    return normalized == QStringLiteral("bond_selected_two") || normalized == QStringLiteral("bond_first_two");
}

QVector3D cellAxisDirection(const StructureData& structure, int axisIndex) {
    const int idx = std::clamp(axisIndex, 0, 2);
    return normalizedOrFallback(structure.cellVectors[static_cast<std::size_t>(idx)]);
}

QVector3D slabNormalDirection(const StructureData& structure) {
    return normalizedOrFallback(QVector3D::crossProduct(structure.cellVectors[0], structure.cellVectors[1]));
}

QJsonArray jsonVector3(const QVector3D& value) {
    return QJsonArray{value.x(), value.y(), value.z()};
}

QJsonArray jsonQuaternionWxyz(const QVector4D& value) {
    return QJsonArray{value.x(), value.y(), value.z(), value.w()};
}

struct ElementLegendEntry {
    QString element;
    int count = 0;
    QColor color;
    double radius = 1.0;
};

struct ElementLegendRenderOptions {
    int widthPx = 1200;
    int dpi = 300;
    int columns = 1;
    bool transparentBackground = false;
    bool includeCounts = false;
};

std::vector<ElementLegendEntry> elementLegendEntries(const StructureData& structure) {
    std::vector<ElementLegendEntry> entries;
    for (const auto& atom : structure.atoms) {
        QString element = atom.element.trimmed();
        if (element.isEmpty()) {
            element = QStringLiteral("X");
        }
        auto found = std::find_if(entries.begin(), entries.end(), [&element](const ElementLegendEntry& entry) {
            return entry.element.compare(element, Qt::CaseInsensitive) == 0;
        });
        if (found == entries.end()) {
            entries.push_back(ElementLegendEntry{
                element,
                1,
                atom.color.isValid() ? atom.color : vestaElementColor(element),
                atom.radius > 0.0 ? atom.radius : vestaElementRadius(element)});
        } else {
            found->count += 1;
            found->radius = std::max(found->radius, atom.radius > 0.0 ? atom.radius : vestaElementRadius(element));
        }
    }
    return entries;
}

void drawLegendSphere(QPainter& painter, const QRectF& rect, const QColor& baseColor) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    const double diameter = rect.width();
    const QRectF shadowRect = rect.translated(diameter * 0.08, diameter * 0.10);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 38));
    painter.drawEllipse(shadowRect);

    const QPointF lightCenter(rect.left() + diameter * 0.34, rect.top() + diameter * 0.30);
    QRadialGradient gradient(lightCenter, diameter * 0.76, lightCenter);
    gradient.setColorAt(0.00, QColor(255, 255, 255, 245));
    gradient.setColorAt(0.18, baseColor.lighter(165));
    gradient.setColorAt(0.58, baseColor);
    gradient.setColorAt(1.00, baseColor.darker(155));
    painter.setBrush(gradient);
    painter.setPen(QPen(baseColor.darker(135), std::max(1.0, diameter * 0.018)));
    painter.drawEllipse(rect);

    QRadialGradient highlight(lightCenter, diameter * 0.27, lightCenter);
    highlight.setColorAt(0.0, QColor(255, 255, 255, 205));
    highlight.setColorAt(1.0, QColor(255, 255, 255, 0));
    painter.setPen(Qt::NoPen);
    painter.setBrush(highlight);
    painter.drawEllipse(QRectF(rect.left() + diameter * 0.20, rect.top() + diameter * 0.16, diameter * 0.35, diameter * 0.35));

    painter.restore();
}

QImage renderElementLegendImage(const std::vector<ElementLegendEntry>& entries, const ElementLegendRenderOptions& options) {
    const int entryCount = static_cast<int>(entries.size());
    const int width = std::clamp(options.widthPx, 400, 8000);
    const int columns = std::clamp(options.columns, 1, std::max(1, std::min(4, entryCount)));
    const int rows = std::max(1, (entryCount + columns - 1) / columns);
    const int margin = std::max(32, width / 14);
    const int cellWidth = std::max(1, (width - margin * 2) / columns);
    const int maxSphereDiameter = std::clamp(cellWidth / 5, 42, 320);
    const int rowHeight = static_cast<int>(maxSphereDiameter * 1.55);
    const int height = std::max(margin * 2 + rows * rowHeight, maxSphereDiameter + margin * 2);
    double maxElementRadius = 0.0;
    for (const auto& entry : entries) {
        maxElementRadius = std::max(maxElementRadius, entry.radius);
    }
    maxElementRadius = std::max(maxElementRadius, 1.0e-6);

    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(options.transparentBackground ? Qt::transparent : Qt::white);
    const int dotsPerMeter = static_cast<int>(std::clamp(options.dpi, 72, 1200) / 0.0254);
    image.setDotsPerMeterX(dotsPerMeter);
    image.setDotsPerMeterY(dotsPerMeter);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setPen(QColor(25, 25, 25));
    QFont font = painter.font();
    font.setPixelSize(std::clamp(static_cast<int>(maxSphereDiameter * 0.42), 22, 150));
    font.setWeight(QFont::Medium);
    painter.setFont(font);
    const QFontMetricsF fontMetrics(font);
    const double textHeight = fontMetrics.height() * 1.35;

    for (int i = 0; i < entryCount; ++i) {
        const int column = i / rows;
        const int row = i % rows;
        const int x = margin + column * cellWidth;
        const double centerY = margin + row * rowHeight + rowHeight / 2.0;
        const auto& entry = entries[static_cast<std::size_t>(i)];
        const double sphereDiameter = std::max(6.0, maxSphereDiameter * entry.radius / maxElementRadius);
        const double symbolCenterX = x + maxSphereDiameter / 2.0;
        const QRectF sphereRect(
            symbolCenterX - sphereDiameter / 2.0,
            centerY - sphereDiameter / 2.0,
            sphereDiameter,
            sphereDiameter);
        drawLegendSphere(painter, sphereRect, entry.color);

        QString label = entry.element;
        if (options.includeCounts) {
            label += QStringLiteral(" (%1)").arg(entry.count);
        }
        const double textX = x + maxSphereDiameter + maxSphereDiameter * 0.35;
        const QRectF textRect(
            textX,
            centerY - textHeight / 2.0,
            std::max(1.0, static_cast<double>(x + cellWidth - textX)),
            textHeight);
        painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, label);
    }

    return image;
}

class SupercellDialog final : public QDialog {
public:
    explicit SupercellDialog(
        bool japanese,
        const StructureData& baseStructure,
        const std::array<int, 3>& currentFactors,
        QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle(japanese ? QStringLiteral("スーパーセル作成") : QStringLiteral("Create Supercell"));
        auto* layout = new QVBoxLayout(this);
        auto* info = new QLabel(
            japanese
                ? QStringLiteral("倍率は現在の拡大後構造ではなく、初期/基準構造に対する絶対倍率です。\n基準原子数: %1 / 現在倍率: %2 × %3 × %4")
                    .arg(baseStructure.atoms.size())
                    .arg(currentFactors[0])
                    .arg(currentFactors[1])
                    .arg(currentFactors[2])
                : QStringLiteral("Multipliers are absolute values relative to the initial/base structure, not relative to the current enlarged cell.\nBase atoms: %1 / Current factors: %2 × %3 × %4")
                    .arg(baseStructure.atoms.size())
                    .arg(currentFactors[0])
                    .arg(currentFactors[1])
                    .arg(currentFactors[2]),
            this);
        info->setWordWrap(true);
        layout->addWidget(info);
        auto* form = new QFormLayout();
        m_a = new QSpinBox(this); m_b = new QSpinBox(this); m_c = new QSpinBox(this);
        for (auto* spin : {m_a, m_b, m_c}) { spin->setRange(1, 20); }
        m_a->setValue(std::clamp(currentFactors[0], 1, 20));
        m_b->setValue(std::clamp(currentFactors[1], 1, 20));
        m_c->setValue(std::clamp(currentFactors[2], 1, 20));
        m_a->setToolTip(japanese ? QStringLiteral("a軸方向の繰り返し数です。") : QStringLiteral("Repeat the structure along the a axis."));
        m_b->setToolTip(japanese ? QStringLiteral("b軸方向の繰り返し数です。") : QStringLiteral("Repeat the structure along the b axis."));
        m_c->setToolTip(japanese ? QStringLiteral("c軸方向の繰り返し数です。") : QStringLiteral("Repeat the structure along the c axis."));
        form->addRow(japanese ? QStringLiteral("a方向倍率") : QStringLiteral("a multiplier"), m_a);
        form->addRow(japanese ? QStringLiteral("b方向倍率") : QStringLiteral("b multiplier"), m_b);
        form->addRow(japanese ? QStringLiteral("c方向倍率") : QStringLiteral("c multiplier"), m_c);
        layout->addLayout(form);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);
    }
    std::tuple<int, int, int> values() const { return {m_a->value(), m_b->value(), m_c->value()}; }
private:
    QSpinBox* m_a = nullptr;
    QSpinBox* m_b = nullptr;
    QSpinBox* m_c = nullptr;
};

class HydrogenTerminationDialog final : public QDialog {
public:
    explicit HydrogenTerminationDialog(QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle("Hydrogen Termination");
        auto* layout = new QVBoxLayout(this);
        auto* form = new QFormLayout();
        m_bondLength = new QDoubleSpinBox(this);
        m_bondLength->setRange(0.5, 3.0);
        m_bondLength->setDecimals(2);
        m_bondLength->setSingleStep(0.05);
        m_bondLength->setValue(1.0);
        m_bondLength->setToolTip("Distance from the selected surface atom to the added hydrogen.");
        m_layerThickness = new QDoubleSpinBox(this);
        m_layerThickness->setRange(0.2, 5.0);
        m_layerThickness->setDecimals(2);
        m_layerThickness->setSingleStep(0.1);
        m_layerThickness->setValue(1.2);
        m_layerThickness->setToolTip("Atoms closer than this to the top/bottom are treated as surface atoms.");
        m_top = new QCheckBox("Top surface", this);
        m_bottom = new QCheckBox("Bottom surface", this);
        m_top->setChecked(true);
        m_bottom->setChecked(true);
        m_top->setToolTip("Add hydrogen termination to the top surface.");
        m_bottom->setToolTip("Add hydrogen termination to the bottom surface.");
        form->addRow("H bond length (Å)", m_bondLength);
        form->addRow("Top", m_top);
        form->addRow("Bottom", m_bottom);
        form->addRow("Surface-layer thickness (Å)", m_layerThickness);
        layout->addLayout(form);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);
    }
    double bondLength() const { return m_bondLength->value(); }
    double layerThickness() const { return m_layerThickness->value(); }
    bool top() const { return m_top->isChecked(); }
    bool bottom() const { return m_bottom->isChecked(); }
private:
    QDoubleSpinBox* m_bondLength = nullptr;
    QDoubleSpinBox* m_layerThickness = nullptr;
    QCheckBox* m_top = nullptr;
    QCheckBox* m_bottom = nullptr;
};

class VacuumDialog final : public QDialog {
public:
    explicit VacuumDialog(bool japanese, QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle(japanese ? QStringLiteral("真空層調整") : QStringLiteral("Vacuum adjustment"));
        auto* layout = new QVBoxLayout(this);
        auto* form = new QFormLayout();

        m_operation = new QComboBox(this);
        m_operation->addItem(japanese ? QStringLiteral("真空層を設定/追加") : QStringLiteral("Set/add vacuum"), 0);
        m_operation->addItem(japanese ? QStringLiteral("選択軸の真空層をなくす") : QStringLiteral("Remove vacuum on selected axis"), 1);
        m_operation->addItem(japanese ? QStringLiteral("スラブ全体を移動のみ") : QStringLiteral("Move slab only"), 3);
        m_operation->setToolTip(japanese
            ? QStringLiteral("真空層追加、真空除去、またはスラブ全体の平行移動を選びます。")
            : QStringLiteral("Choose whether to add vacuum, remove vacuum, or translate the whole slab."));

        m_direction = new QComboBox(this);
        m_direction->addItem(QStringLiteral("c"), 2);
        m_direction->addItem(QStringLiteral("a"), 0);
        m_direction->addItem(QStringLiteral("b"), 1);
        m_direction->setToolTip(japanese
            ? QStringLiteral("真空層調整を行うセル軸です。通常の表面スラブでは c を選びます。")
            : QStringLiteral("Cell axis used for the vacuum adjustment. Use c for ordinary slabs."));

        m_thickness = new QDoubleSpinBox(this);
        m_thickness->setRange(0.0, 200.0);
        m_thickness->setDecimals(2);
        m_thickness->setSingleStep(0.5);
        m_thickness->setValue(12.0);
        m_thickness->setSuffix(QStringLiteral(" Å"));
        m_thickness->setToolTip(japanese
            ? QStringLiteral("追加/設定する真空層の厚さです。")
            : QStringLiteral("Vacuum thickness to add/set."));

        m_placement = new QComboBox(this);
        m_placement->addItem(japanese ? QStringLiteral("+側に真空") : QStringLiteral("Vacuum on + side"), 0);
        m_placement->addItem(japanese ? QStringLiteral("両側に均等") : QStringLiteral("Centered / both sides"), 1);
        m_placement->addItem(japanese ? QStringLiteral("-側に真空") : QStringLiteral("Vacuum on - side"), 2);
        m_placement->addItem(japanese ? QStringLiteral("任意位置") : QStringLiteral("Custom position"), 3);
        m_placement->setToolTip(japanese
            ? QStringLiteral("スラブをセル内のどこに置き、真空をどちら側へ作るかを選びます。")
            : QStringLiteral("Choose where the slab sits in the cell and where the vacuum is placed."));

        m_customCenter = new QDoubleSpinBox(this);
        m_customCenter->setRange(0.0, 1.0);
        m_customCenter->setDecimals(3);
        m_customCenter->setSingleStep(0.05);
        m_customCenter->setValue(0.5);
        m_customCenter->setToolTip(japanese
            ? QStringLiteral("任意位置のとき、選択軸方向でスラブ中心を置く比率です。0=-側、1=+側。")
            : QStringLiteral("For custom placement, slab-center fraction along the selected axis. 0=- side, 1=+ side."));

        m_shiftA = createShiftSpin(japanese);
        m_shiftB = createShiftSpin(japanese);
        m_shiftC = createShiftSpin(japanese);

        form->addRow(japanese ? QStringLiteral("操作") : QStringLiteral("Operation"), m_operation);
        form->addRow(japanese ? QStringLiteral("対象軸") : QStringLiteral("Axis"), m_direction);
        form->addRow(japanese ? QStringLiteral("真空層の厚さ") : QStringLiteral("Vacuum thickness"), m_thickness);
        form->addRow(japanese ? QStringLiteral("スラブ位置") : QStringLiteral("Slab position"), m_placement);
        form->addRow(japanese ? QStringLiteral("任意位置 0-1") : QStringLiteral("Custom position 0-1"), m_customCenter);
        form->addRow(japanese ? QStringLiteral("全体移動 a方向") : QStringLiteral("Move along a"), m_shiftA);
        form->addRow(japanese ? QStringLiteral("全体移動 b方向") : QStringLiteral("Move along b"), m_shiftB);
        form->addRow(japanese ? QStringLiteral("全体移動 c方向") : QStringLiteral("Move along c"), m_shiftC);
        layout->addLayout(form);

        auto refreshEnabled = [this]() {
            const int op = m_operation->currentData().toInt();
            const bool vacuumMode = op == 0;
            const bool moveOnly = op == 3;
            m_direction->setEnabled(!moveOnly);
            m_thickness->setEnabled(vacuumMode);
            m_placement->setEnabled(vacuumMode);
            m_customCenter->setEnabled(vacuumMode && m_placement->currentData().toInt() == 3);
        };
        connect(m_operation, QOverload<int>::of(&QComboBox::currentIndexChanged), this, refreshEnabled);
        connect(m_placement, QOverload<int>::of(&QComboBox::currentIndexChanged), this, refreshEnabled);
        refreshEnabled();

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);
    }

    VacuumAdjustmentOptions options() const {
        VacuumAdjustmentOptions options;
        const int operation = m_operation->currentData().toInt();
        options.axisIndex = m_direction->currentData().toInt();
        options.fitTight = operation == 1;
        options.fitAllAxes = false;
        options.moveOnly = operation == 3;
        options.vacuumAngstrom = operation == 0 ? m_thickness->value() : 0.0;
        options.placementMode = m_placement->currentData().toInt();
        options.customCenterFraction = m_customCenter->value();
        options.translationAngstrom = {m_shiftA->value(), m_shiftB->value(), m_shiftC->value()};
        return options;
    }

private:
    QDoubleSpinBox* createShiftSpin(bool japanese) {
        auto* spin = new QDoubleSpinBox(this);
        spin->setRange(-200.0, 200.0);
        spin->setDecimals(3);
        spin->setSingleStep(0.1);
        spin->setValue(0.0);
        spin->setSuffix(QStringLiteral(" Å"));
        spin->setToolTip(japanese
            ? QStringLiteral("スラブモデル全体を指定した格子軸方向へ平行移動します。")
            : QStringLiteral("Translate the whole slab along the selected lattice-axis direction."));
        return spin;
    }

    QComboBox* m_operation = nullptr;
    QComboBox* m_direction = nullptr;
    QDoubleSpinBox* m_thickness = nullptr;
    QComboBox* m_placement = nullptr;
    QDoubleSpinBox* m_customCenter = nullptr;
    QDoubleSpinBox* m_shiftA = nullptr;
    QDoubleSpinBox* m_shiftB = nullptr;
    QDoubleSpinBox* m_shiftC = nullptr;
};

class CellAxisTiltDialog final : public QDialog {
public:
    explicit CellAxisTiltDialog(bool japanese, QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle(japanese ? QStringLiteral("セル軸傾き / ステップテラス") : QStringLiteral("Cell-axis tilt / step terrace"));
        auto* layout = new QVBoxLayout(this);
        auto* note = new QLabel(japanese
            ? QStringLiteral("セル軸そのものを指定方向へ傾けます。fractional座標を保ったままcartesian座標を更新するため、slab全体をせん断したステップテラス候補を作れます。\n例: c軸をa方向へ傾ける。")
            : QStringLiteral("Tilt a cell axis toward another lattice direction. Fractional coordinates are preserved while Cartesian coordinates are updated, which shears the whole slab for step-terrace candidates.\nExample: tilt c toward a."),
            this);
        note->setWordWrap(true);
        layout->addWidget(note);

        auto* form = new QFormLayout();
        m_targetAxis = new QComboBox(this);
        m_directionAxis = new QComboBox(this);
        for (auto* combo : {m_targetAxis, m_directionAxis}) {
            combo->addItem(QStringLiteral("a"), 0);
            combo->addItem(QStringLiteral("b"), 1);
            combo->addItem(QStringLiteral("c"), 2);
        }
        m_targetAxis->setCurrentIndex(2);
        m_directionAxis->setCurrentIndex(0);
        m_targetAxis->setToolTip(japanese
            ? QStringLiteral("傾けるセル軸です。ステップテラス候補では通常 c を選びます。")
            : QStringLiteral("Cell axis to tilt. Use c for typical step-terrace candidates."));
        m_directionAxis->setToolTip(japanese
            ? QStringLiteral("傾ける方向です。例: a方向へ傾ける。")
            : QStringLiteral("Direction toward which the target axis is tilted. Example: toward a."));

        m_angle = new QDoubleSpinBox(this);
        m_angle->setRange(-75.0, 75.0);
        m_angle->setDecimals(2);
        m_angle->setSingleStep(1.0);
        m_angle->setValue(10.0);
        m_angle->setSuffix(QStringLiteral(" °"));
        m_angle->setToolTip(japanese
            ? QStringLiteral("軸長を保ったまま傾ける角度です。負値で逆方向に傾けます。")
            : QStringLiteral("Tilt angle while preserving the target-axis length. Negative values tilt the opposite way."));

        form->addRow(japanese ? QStringLiteral("傾ける軸") : QStringLiteral("Target axis"), m_targetAxis);
        form->addRow(japanese ? QStringLiteral("倒す方向") : QStringLiteral("Tilt toward"), m_directionAxis);
        form->addRow(japanese ? QStringLiteral("傾き角") : QStringLiteral("Angle"), m_angle);
        layout->addLayout(form);

        auto keepAxesDifferent = [this]() {
            if (m_targetAxis->currentData().toInt() != m_directionAxis->currentData().toInt()) {
                return;
            }
            const int nextAxis = (m_targetAxis->currentData().toInt() + 1) % 3;
            const int row = m_directionAxis->findData(nextAxis);
            if (row >= 0) {
                m_directionAxis->setCurrentIndex(row);
            }
        };
        connect(m_targetAxis, QOverload<int>::of(&QComboBox::currentIndexChanged), this, keepAxesDifferent);
        connect(m_directionAxis, QOverload<int>::of(&QComboBox::currentIndexChanged), this, keepAxesDifferent);
        keepAxesDifferent();

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);
    }

    CellAxisTiltOptions options() const {
        CellAxisTiltOptions options;
        options.targetAxisIndex = m_targetAxis->currentData().toInt();
        options.directionAxisIndex = m_directionAxis->currentData().toInt();
        options.angleDegrees = m_angle->value();
        return options;
    }

private:
    QComboBox* m_targetAxis = nullptr;
    QComboBox* m_directionAxis = nullptr;
    QDoubleSpinBox* m_angle = nullptr;
};

class ElementLegendExportDialog final : public QDialog {
public:
    explicit ElementLegendExportDialog(bool japanese, int elementCount, QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle(japanese ? QStringLiteral("原子一覧画像を出力") : QStringLiteral("Export atom legend image"));
        auto* layout = new QVBoxLayout(this);
        auto* note = new QLabel(japanese
            ? QStringLiteral("現在の構造に含まれる元素を、発表資料向けの球＋ラベルのPNG画像として出力します。")
            : QStringLiteral("Export the elements in the current structure as a publication-ready PNG legend with spheres and labels."),
            this);
        note->setWordWrap(true);
        layout->addWidget(note);

        auto* form = new QFormLayout();
        m_widthPreset = new QComboBox(this);
        m_widthPreset->addItem(japanese ? QStringLiteral("800 px（小）") : QStringLiteral("800 px (small)"), 800);
        m_widthPreset->addItem(japanese ? QStringLiteral("1200 px（標準）") : QStringLiteral("1200 px (standard)"), 1200);
        m_widthPreset->addItem(japanese ? QStringLiteral("2000 px（高解像度）") : QStringLiteral("2000 px (high resolution)"), 2000);
        m_widthPreset->addItem(japanese ? QStringLiteral("4000 px（印刷向け）") : QStringLiteral("4000 px (print)"), 4000);
        m_widthPreset->addItem(japanese ? QStringLiteral("カスタム") : QStringLiteral("Custom"), 0);
        m_widthPreset->setCurrentIndex(1);

        m_width = new QSpinBox(this);
        m_width->setRange(400, 8000);
        m_width->setSingleStep(100);
        m_width->setSuffix(QStringLiteral(" px"));
        m_width->setValue(1200);
        m_width->setEnabled(false);

        m_dpi = new QSpinBox(this);
        m_dpi->setRange(72, 1200);
        m_dpi->setSingleStep(50);
        m_dpi->setSuffix(QStringLiteral(" dpi"));
        m_dpi->setValue(300);

        m_columns = new QSpinBox(this);
        m_columns->setRange(1, std::max(1, std::min(4, elementCount)));
        m_columns->setValue(1);
        m_columns->setToolTip(japanese
            ? QStringLiteral("1列は添付画像のような縦一覧、複数列は元素数が多い発表図向けです。")
            : QStringLiteral("One column makes a vertical list like the sample; multiple columns are useful for many elements."));

        m_background = new QComboBox(this);
        m_background->addItem(japanese ? QStringLiteral("白背景") : QStringLiteral("White background"), 0);
        m_background->addItem(japanese ? QStringLiteral("透明背景") : QStringLiteral("Transparent background"), 1);

        m_includeCounts = new QCheckBox(japanese ? QStringLiteral("原子数も表示") : QStringLiteral("Show atom counts"), this);
        m_includeCounts->setChecked(false);

        auto* widthRow = new QHBoxLayout();
        widthRow->addWidget(m_widthPreset, 1);
        widthRow->addWidget(m_width);

        form->addRow(japanese ? QStringLiteral("横解像度") : QStringLiteral("Width"), widthRow);
        form->addRow(QStringLiteral("DPI"), m_dpi);
        form->addRow(japanese ? QStringLiteral("列数") : QStringLiteral("Columns"), m_columns);
        form->addRow(japanese ? QStringLiteral("背景") : QStringLiteral("Background"), m_background);
        form->addRow(QString(), m_includeCounts);
        layout->addLayout(form);

        connect(m_widthPreset, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
            const int presetWidth = m_widthPreset->currentData().toInt();
            if (presetWidth > 0) {
                m_width->setValue(presetWidth);
                m_width->setEnabled(false);
            } else {
                m_width->setEnabled(true);
                m_width->setFocus();
            }
        });

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);
    }

    ElementLegendRenderOptions options() const {
        ElementLegendRenderOptions options;
        options.widthPx = m_widthPreset->currentData().toInt() > 0 ? m_widthPreset->currentData().toInt() : m_width->value();
        options.dpi = m_dpi->value();
        options.columns = m_columns->value();
        options.transparentBackground = m_background->currentData().toInt() == 1;
        options.includeCounts = m_includeCounts->isChecked();
        return options;
    }

private:
    QComboBox* m_widthPreset = nullptr;
    QSpinBox* m_width = nullptr;
    QSpinBox* m_dpi = nullptr;
    QSpinBox* m_columns = nullptr;
    QComboBox* m_background = nullptr;
    QCheckBox* m_includeCounts = nullptr;
};

class BondDistanceDialog final : public QDialog {
public:
    BondDistanceDialog(
        bool japanese,
        QStringList elements,
        const QHash<QString, BondDistanceRange>& customRanges,
        const QString& initialElementA,
        const QString& initialElementB,
        double selectedDistance,
        QWidget* parent = nullptr)
        : QDialog(parent),
          m_japanese(japanese),
          m_customRanges(customRanges),
          m_selectedDistance(selectedDistance)
    {
        setWindowTitle(japanese ? QStringLiteral("ボンド距離設定") : QStringLiteral("Bond distance settings"));
        elements.removeAll(QString());
        for (QString& element : elements) {
            element = vestaNormalizeElement(element);
        }
        elements.removeDuplicates();
        elements.sort(Qt::CaseInsensitive);
        for (const QString& fallback : {QStringLiteral("H"), QStringLiteral("C"), QStringLiteral("N"), QStringLiteral("O")}) {
            if (!elements.contains(fallback)) {
                elements << fallback;
            }
        }
        elements.removeDuplicates();
        elements.sort(Qt::CaseInsensitive);

        auto* layout = new QVBoxLayout(this);
        auto* note = new QLabel(japanese
            ? QStringLiteral("元素ペアごとのボンド表示距離を VESTA の SBOND と同じく min/max Å で上書きします。設定は次回起動にも保存されます。")
            : QStringLiteral("Override visible bond distances per element pair using VESTA-style min/max Å ranges. Settings are saved for the next launch."),
            this);
        note->setWordWrap(true);
        layout->addWidget(note);

        m_elementA = new QComboBox(this);
        m_elementB = new QComboBox(this);
        m_elementA->addItems(elements);
        m_elementB->addItems(elements);
        setComboText(m_elementA, initialElementA.isEmpty() ? elements.value(0, QStringLiteral("C")) : initialElementA);
        setComboText(m_elementB, initialElementB.isEmpty() ? elements.value(1, QStringLiteral("O")) : initialElementB);

        m_minDistance = new QDoubleSpinBox(this);
        m_minDistance->setRange(0.0, 20.0);
        m_minDistance->setDecimals(3);
        m_minDistance->setSingleStep(0.01);
        m_minDistance->setSuffix(QStringLiteral(" Å"));
        m_maxDistance = new QDoubleSpinBox(this);
        m_maxDistance->setRange(0.01, 40.0);
        m_maxDistance->setDecimals(3);
        m_maxDistance->setSingleStep(0.01);
        m_maxDistance->setSuffix(QStringLiteral(" Å"));
        m_resetCustom = new QCheckBox(japanese ? QStringLiteral("この元素ペアをVESTA既定値へ戻す") : QStringLiteral("Reset this pair to the VESTA default"), this);
        m_statusLabel = new QLabel(this);
        m_statusLabel->setWordWrap(true);

        auto* form = new QFormLayout();
        form->addRow(japanese ? QStringLiteral("元素A") : QStringLiteral("Element A"), m_elementA);
        form->addRow(japanese ? QStringLiteral("元素B") : QStringLiteral("Element B"), m_elementB);
        form->addRow(japanese ? QStringLiteral("最小距離") : QStringLiteral("Minimum distance"), m_minDistance);
        form->addRow(japanese ? QStringLiteral("最大距離") : QStringLiteral("Maximum distance"), m_maxDistance);
        layout->addLayout(form);
        layout->addWidget(m_resetCustom);

        if (m_selectedDistance > 0.0) {
            auto* useSelectedButton = new QPushButton(japanese ? QStringLiteral("選択中2原子距離を最大値に使う") : QStringLiteral("Use selected 2-atom distance as maximum"), this);
            connect(useSelectedButton, &QPushButton::clicked, this, [this]() {
                if (m_selectedDistance > 0.0) {
                    m_maxDistance->setValue(std::max(m_selectedDistance, m_minDistance->value()));
                }
            });
            layout->addWidget(useSelectedButton);
        }
        layout->addWidget(m_statusLabel);

        connect(m_elementA, &QComboBox::currentTextChanged, this, [this]() { refreshRangeFromPair(); });
        connect(m_elementB, &QComboBox::currentTextChanged, this, [this]() { refreshRangeFromPair(); });
        connect(m_resetCustom, &QCheckBox::toggled, this, [this](bool checked) {
            m_minDistance->setEnabled(!checked);
            m_maxDistance->setEnabled(!checked);
        });

        refreshRangeFromPair();
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);
        resize(460, 280);
    }

    QString elementA() const { return vestaNormalizeElement(m_elementA->currentText()); }
    QString elementB() const { return vestaNormalizeElement(m_elementB->currentText()); }
    BondDistanceRange range() const { return BondDistanceRange{m_minDistance->value(), m_maxDistance->value()}; }
    bool resetCustom() const { return m_resetCustom->isChecked(); }

private:
    void setComboText(QComboBox* combo, const QString& element) {
        const QString normalized = vestaNormalizeElement(element);
        int row = combo->findText(normalized, Qt::MatchFixedString);
        if (row < 0) {
            combo->addItem(normalized);
            row = combo->findText(normalized, Qt::MatchFixedString);
        }
        if (row >= 0) {
            combo->setCurrentIndex(row);
        }
    }

    void refreshRangeFromPair() {
        const QSignalBlocker blockMin(m_minDistance);
        const QSignalBlocker blockMax(m_maxDistance);
        const QString key = vestaBondKey(elementA(), elementB());
        const auto custom = m_customRanges.constFind(key);
        BondDistanceRange range;
        QString source;
        if (custom != m_customRanges.cend()) {
            range = custom.value();
            source = m_japanese ? QStringLiteral("現在: カスタム設定") : QStringLiteral("Current: custom override");
        } else if (vestaBondDistanceRange(elementA(), elementB(), &range)) {
            source = m_japanese ? QStringLiteral("現在: VESTA既定値") : QStringLiteral("Current: VESTA default");
        } else {
            range = BondDistanceRange{0.0, (vestaElementRadius(elementA()) + vestaElementRadius(elementB())) * 0.85};
            source = m_japanese ? QStringLiteral("現在: 未定義（半径から仮入力）") : QStringLiteral("Current: undefined (seeded from radii)");
        }
        m_minDistance->setValue(std::clamp(range.minDistance, m_minDistance->minimum(), m_minDistance->maximum()));
        m_maxDistance->setValue(std::clamp(range.maxDistance, m_maxDistance->minimum(), m_maxDistance->maximum()));
        m_resetCustom->setChecked(false);
        const QString selectedText = m_selectedDistance > 0.0
            ? (m_japanese
                ? QStringLiteral("\n選択中2原子距離: %1 Å").arg(m_selectedDistance, 0, 'f', 3)
                : QStringLiteral("\nSelected 2-atom distance: %1 Å").arg(m_selectedDistance, 0, 'f', 3))
            : QString();
        m_statusLabel->setText(QStringLiteral("%1: %2-%3  min %4 Å / max %5 Å%6")
            .arg(source)
            .arg(elementA())
            .arg(elementB())
            .arg(range.minDistance, 0, 'f', 3)
            .arg(range.maxDistance, 0, 'f', 3)
            .arg(selectedText));
    }

    bool m_japanese = true;
    QHash<QString, BondDistanceRange> m_customRanges;
    double m_selectedDistance = 0.0;
    QComboBox* m_elementA = nullptr;
    QComboBox* m_elementB = nullptr;
    QDoubleSpinBox* m_minDistance = nullptr;
    QDoubleSpinBox* m_maxDistance = nullptr;
    QCheckBox* m_resetCustom = nullptr;
    QLabel* m_statusLabel = nullptr;
};

QString selectionText(const std::vector<int>& atomIds, bool japanese) {
    if (atomIds.empty()) return japanese ? QStringLiteral("選択原子: なし") : QStringLiteral("Selected atoms: none");
    QStringList items;
    for (int id : atomIds) items << QStringLiteral("#%1").arg(id);
    return QStringLiteral("%1: %2")
        .arg(japanese ? QStringLiteral("選択原子") : QStringLiteral("Selected atoms"))
        .arg(items.join(QStringLiteral(", ")));
}

const NativeAtom* findAtomByIdInStructure(const StructureData& structure, int atomId) {
    for (const auto& atom : structure.atoms) {
        if (atom.atomId == atomId) {
            return &atom;
        }
    }
    return nullptr;
}

int nextAtomIdForStructure(const StructureData& structure) {
    int nextId = 1;
    for (const auto& atom : structure.atoms) {
        nextId = std::max(nextId, atom.atomId + 1);
    }
    return nextId;
}

NativeAtom selfTestAtom(const StructureData& structure, int atomId, const QString& element, const QVector3D& cartesian, const QString& tag) {
    NativeAtom atom;
    atom.atomId = atomId;
    atom.element = element;
    atom.tag = tag;
    atom.cartesian = cartesian;
    atom.fractional = solveFractionalForCell(structure.cellVectors, cartesian);
    atom.color = vestaElementColor(element);
    atom.radius = vestaElementRadius(element);
    return atom;
}

StructureData adsorbatePoseSelfTestStructure() {
    StructureData structure;
    structure.title = QStringLiteral("ASEapp methanol-on-Cu adsorbate pose self-test");
    structure.cellVectors = {
        QVector3D(8.0f, 0.0f, 0.0f),
        QVector3D(0.0f, 8.0f, 0.0f),
        QVector3D(0.0f, 0.0f, 15.0f)
    };
    structure.atoms = {
        selfTestAtom(structure, 1, QStringLiteral("Cu"), QVector3D(2.0f, 2.0f, 0.0f), QStringLiteral("slab-Cu-0001")),
        selfTestAtom(structure, 2, QStringLiteral("Cu"), QVector3D(6.0f, 2.0f, 0.0f), QStringLiteral("slab-Cu-0002")),
        selfTestAtom(structure, 3, QStringLiteral("Cu"), QVector3D(2.0f, 6.0f, 0.0f), QStringLiteral("slab-Cu-0003")),
        selfTestAtom(structure, 4, QStringLiteral("Cu"), QVector3D(6.0f, 6.0f, 0.0f), QStringLiteral("slab-Cu-0004")),
        selfTestAtom(structure, 5, QStringLiteral("C"), QVector3D(4.0f, 4.0f, 3.0f), QStringLiteral("methanol-C")),
        selfTestAtom(structure, 6, QStringLiteral("O"), QVector3D(5.43f, 4.0f, 3.0f), QStringLiteral("methanol-O")),
        selfTestAtom(structure, 7, QStringLiteral("H"), QVector3D(3.65f, 5.03f, 3.0f), QStringLiteral("methanol-Hc1")),
        selfTestAtom(structure, 8, QStringLiteral("H"), QVector3D(3.65f, 3.49f, 3.89f), QStringLiteral("methanol-Hc2")),
        selfTestAtom(structure, 9, QStringLiteral("H"), QVector3D(3.65f, 3.49f, 2.11f), QStringLiteral("methanol-Hc3")),
        selfTestAtom(structure, 10, QStringLiteral("H"), QVector3D(6.35f, 4.0f, 3.0f), QStringLiteral("methanol-Ho"))
    };
    return structure;
}

double atomDistance(const StructureData& structure, int firstAtomId, int secondAtomId) {
    const NativeAtom* first = findAtomByIdInStructure(structure, firstAtomId);
    const NativeAtom* second = findAtomByIdInStructure(structure, secondAtomId);
    if (first == nullptr || second == nullptr) {
        return -1.0;
    }
    return static_cast<double>((first->cartesian - second->cartesian).length());
}

QString csvEscaped(QString value) {
    const bool needsQuotes = value.contains(QLatin1Char(','))
        || value.contains(QLatin1Char('"'))
        || value.contains(QLatin1Char('\n'))
        || value.contains(QLatin1Char('\r'));
    value.replace(QStringLiteral("\""), QStringLiteral("\"\""));
    return needsQuotes ? QStringLiteral("\"%1\"").arg(value) : value;
}

QString csvNumber(double value) {
    return QString::number(value, 'f', 6);
}

QString safeBaseFileName(QString value, const QString& fallback) {
    value = value.trimmed();
    const QString invalidChars = QStringLiteral("<>:/\\|?*");
    QString safe;
    safe.reserve(value.size());
    for (const QChar ch : value) {
        safe.append(ch == QLatin1Char('"') || invalidChars.contains(ch) ? QLatin1Char('_') : ch);
    }
    safe = safe.trimmed();
    return safe.isEmpty() ? fallback : safe;
}

QStringList parseCsvLine(const QString& line, bool* ok) {
    QStringList cells;
    QString cell;
    bool inQuotes = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar ch = line.at(i);
        if (ch == QLatin1Char('"')) {
            if (inQuotes && i + 1 < line.size() && line.at(i + 1) == QLatin1Char('"')) {
                cell.append(QLatin1Char('"'));
                ++i;
            } else {
                inQuotes = !inQuotes;
            }
        } else if (ch == QLatin1Char(',') && !inQuotes) {
            cells << cell.trimmed();
            cell.clear();
        } else {
            cell.append(ch);
        }
    }
    cells << cell.trimmed();
    if (ok != nullptr) {
        *ok = !inQuotes;
    }
    return cells;
}

class UsageHelpDialog final : public QDialog {
public:
    explicit UsageHelpDialog(bool japanese, QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle(japanese ? QStringLiteral("ヘルプ") : QStringLiteral("Help"));
        auto* layout = new QVBoxLayout(this);
        auto* label = new QLabel(this);
        label->setWordWrap(true);
        label->setText(japanese
            ? QStringLiteral(
            "使い方:\n"
            "1. 構造ファイルをドラッグ&ドロップ、または Open で読み込みます。\n"
            "2. 左クリックで単一選択、Ctrl + 左クリックで追加/解除、Ctrl + ドラッグで重なった奥の原子も追加選択します。Esc で選択解除できます。選択順は黄色バッジで表示されます。\n"
            "3. 配置位置を選び、Apply で選択原子上/原子間/多点中心に原子を配置します。直上/直下は複数選択の各原子へ一括配置します。\n"
            "4. 前駆体CSV保存で名称を付け、読込後は前駆体ドロップダウンから選んで現在の配置位置に置けます。\n"
            "5. 吸着分子ポーズ編集で、選択原子群を剛体グループ化し、XYZ/cell/法線方向の数値並進、pivot固定回転、XYZ/extXYZ/pose JSON/ASE snippet出力を行います。\n"
            "6. Supercell で a/b/c 方向に拡張します。\n"
            "7. セル軸傾きで、c軸などをa/b方向へ傾けてステップテラス候補を作れます。\n"
            "8. 真空層 で真空層の追加・除去、またはスラブ全体の移動を行います。真空層除去ボタンでは c軸方向をすぐ詰められます。\n"
            "9. ボンド距離設定で、元素ペアごとの表示ボンド距離 min/max Å を VESTA の SBOND 風に調整できます。\n"
            "10. 原子一覧PNG で、論文・学会用の球＋ラベル画像を解像度/DPI指定で出力します。\n\n"
            "視点操作:\n"
            "・初期視点: c 方向\n"
            "・左ドラッグ: 回転\n"
            "・Shift + 左ドラッグ: 選択原子を画面平面内で移動\n"
            "・右/中ドラッグ: パン\n"
            "・ホイール: カーソル位置を中心にズーム\n"
            "・ダブルクリック / F: フィット\n"
            "・A/B/C: direct a,b,c 方向へ視点を切り替え\n"
            "・Ctrl+Alt+A/B/C: reciprocal a*,b*,c* 方向へ視点を切り替え\n"
            "・操作モード: 視点移動 / 原子移動 / モデル表示移動を右パネルで切替")
            : QStringLiteral(
            "Usage:\n"
            "1. Drag & drop a structure file, or use Open.\n"
            "2. Left click selects one atom; Ctrl + left click toggles atoms; Ctrl + drag also adds overlapped rear atoms. Esc clears selection. Selection order is shown with yellow badges.\n"
            "3. Choose a placement option and Apply to place atoms directly above/below or at the center of all selected atoms. Directly above/below applies to every selected atom.\n"
            "4. Save precursor CSV with a name, then load and choose it from the precursor dropdown to place it at the current placement position.\n"
            "5. Use Adsorbate pose editor to group selected atoms as a rigid body, translate by XYZ/cell/normal values, rotate around a fixed pivot, and export XYZ/extXYZ/pose JSON/ASE snippets.\n"
            "6. Use Supercell to repeat along a/b/c.\n"
            "7. Use Axis tilt to tilt c or another cell axis toward a/b for step-terrace candidates.\n"
            "8. Use Vacuum to add/remove vacuum or move the whole slab. Remove vacuum immediately tightens the c-axis.\n"
            "9. Use Bond distances to adjust per-element-pair visible bond min/max Å ranges in a VESTA SBOND-style workflow.\n"
            "10. Use Atom legend PNG to export a sphere-and-label image for papers or presentations with selectable resolution/DPI.\n\n"
            "View controls:\n"
            "・Initial view: c-axis\n"
            "・Left drag: rotate\n"
            "・Shift + left drag: move selected atoms in the current screen plane\n"
            "・Right / middle drag: pan\n"
            "・Wheel: zoom at cursor\n"
            "・Double-click / F: fit\n"
            "・A/B/C: switch to direct a,b,c view\n"
            "・Ctrl+Alt+A/B/C: switch to reciprocal a*,b*,c* view\n"
            "・Interaction mode: switch Move view / Move atoms / Move model display on the right panel"));
        layout->addWidget(label);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        layout->addWidget(buttons);
        resize(520, 360);
    }
};

class StartupGuideDialog final : public QDialog {
public:
    explicit StartupGuideDialog(bool japanese, QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle(japanese ? QStringLiteral("クイックスタート") : QStringLiteral("Quick Start"));
        setWindowFlag(Qt::WindowContextHelpButtonHint, false);
        auto* layout = new QVBoxLayout(this);
        auto* title = new QLabel(this);
        title->setText(QStringLiteral("<div style='font-size:18px; font-weight:700; margin-bottom:8px;'>ASEapp Surface Builder</div>")
            + (japanese
                ? QStringLiteral("<div style='color:#6d7788;'>VESTA風の表示 + 表面モデル編集ワークスペース</div>")
                : QStringLiteral("<div style='color:#6d7788;'>A VESTA-style surface model workspace</div>")));
        layout->addWidget(title);

        auto* body = new QLabel(this);
        body->setWordWrap(true);
        body->setTextFormat(Qt::RichText);
        body->setText(japanese ? QStringLiteral(
            "<b>1. 原子配置</b><ul>"
            "<li>左クリックで単一選択、Ctrl + 左クリックで追加/解除、Ctrl + ドラッグで重なった奥の原子も追加選択します。Esc で選択解除できます。選択順は黄色バッジで表示されます。</li>"
            "<li>配置位置を選び、Apply で直上 / 直下 / 原子間 / 多点中心へ配置します。直上/直下は複数選択した全原子に一括適用します。</li>"
            "<li>前駆体CSV保存で名称を付け、読込後は前駆体ドロップダウンから選んで現在の配置位置へ置けます。</li>"
            "<li>Shift + 左ドラッグで選択原子を画面平面内に移動します。</li>"
            "</ul>"
            "<b>2. 吸着分子ポーズ編集</b><ul>"
            "<li>選択原子群をポーズグループ化すると、分子内相対座標を保持したまま剛体並進・pivot回転できます。</li>"
            "<li>奥行き方向は法線方向スピンボックスなどで明示指定します。通常ドラッグだけでは勝手に前後移動しません。</li>"
            "<li>Save As またはポーズ出力から extended XYZ / pose JSON / ASE snippet を保存し、ASE 側の大量 slab 生成へ戻せます。</li>"
            "</ul>"
            "<b>3. スーパーセル/真空</b><ul>"
            "<li>Supercell で a/b/c 方向の倍率を指定します。</li>"
            "<li>セル軸傾きで c軸などを a/b方向へ傾け、fractional座標を保ったままステップテラス候補を作れます。</li>"
            "<li>真空層 で真空層の追加・除去、スラブ全体移動を行います。真空層除去ボタンでは c軸方向をすぐ詰められます。</li>"
            "</ul>"
            "<b>4. 発表用出力</b><ul>"
            "<li>ボンド距離設定で、元素ペアごとの表示ボンド距離 min/max Å を設定できます。選択中の 2 原子距離を最大値として使うこともできます。</li>"
            "<li>原子一覧PNG で、現在の構造に含まれる元素を球＋ラベルの画像として出力します。横解像度、DPI、列数、白/透明背景を選べます。</li>"
            "</ul>"
            "<b>5. 視点操作</b><ul>"
            "<li>初期視点は c 方向です。</li>"
            "<li>左ドラッグ: 回転 / 右・中ドラッグ: パン / ホイール: カーソル位置を中心にズーム</li>"
            "<li>A/B/C: direct a,b,c 方向 / Ctrl+Alt+A/B/C: reciprocal a*,b*,c* 方向 / F: フィット</li>"
            "<li>c と c* は、c 軸が ab 面法線と平行なセルでは同じ向きになります。</li>"
            "<li>右パネルの操作モードで、左ドラッグを視点回転 / 選択原子移動 / モデル全体の表示位置移動に切り替えます。</li>"
            "</ul>")
            : QStringLiteral(
            "<style>"
            "ul{margin-top:4px; margin-bottom:10px; padding-left:20px;}"
            "li{margin-bottom:4px;}"
            "code{background:#1a2233; padding:1px 4px; border-radius:3px;}"
            "</style>"
            "<b>1. Atom placement</b><ul>"
            "<li>Left click selects one atom; Ctrl + left click toggles target atoms; Ctrl + drag also adds overlapped rear atoms. Esc clears selection. Selection order is shown with yellow badges.</li>"
            "<li>Choose a placement option, then Apply to place atoms directly above/below or at the center of all selected atoms. Direct above/below applies to all selected atoms.</li>"
            "<li>Save precursor CSV with a name, then load and choose it from the precursor dropdown to place it at the current placement position.</li>"
            "<li>Shift + left drag moves selected atoms in the current screen plane.</li>"
            "</ul>"
            "<b>2. Adsorbate pose editor</b><ul>"
            "<li>Create a pose group from selected atoms to keep internal relative coordinates while translating and pivot-rotating the adsorbate as a rigid body.</li>"
            "<li>Depth changes are explicit through the normal-direction spin box or numeric inputs; plain dragging does not silently move the molecule forward/backward.</li>"
            "<li>Use Save As or pose export to write extended XYZ, pose JSON, or an ASE snippet for batch slab generation.</li>"
            "</ul>"
            "<b>3. Supercell/Vacuum</b><ul>"
            "<li>Use Supercell to set repeat counts along a/b/c.</li>"
            "<li>Use Axis tilt to tilt c or another cell axis toward a/b while preserving fractional coordinates for step-terrace candidates.</li>"
            "<li>Use Vacuum to add/remove vacuum or translate the whole slab. Remove vacuum immediately tightens the c-axis.</li>"
            "</ul>"
            "<b>4. Presentation output</b><ul>"
            "<li>Use Bond distances to set visible bond min/max Å ranges per element pair. The current selected two-atom distance can be used as the maximum.</li>"
            "<li>Use Atom legend PNG to export the elements in the current structure as a sphere-and-label image. Width, DPI, columns, and white/transparent background are selectable.</li>"
            "</ul>"
            "<b>5. View controls</b><ul>"
            "<li>Initial view is c-axis.</li>"
            "<li>Left drag: rotate / Right or middle drag: pan / Wheel: zoom at cursor</li>"
            "<li>A/B/C: direct a,b,c view / Ctrl+Alt+A/B/C: reciprocal a*,b*,c* view / F: fit</li>"
            "<li>c and c* look the same when c is parallel to the ab-plane normal.</li>"
            "<li>Use Interaction mode on the right panel to choose whether left drag rotates the view, moves selected atoms, or pans the whole model display.</li>"
            "</ul>"));
        layout->addWidget(body, 1);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        layout->addWidget(buttons);
        resize(760, 560);
    }
};
}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    QSettings settings(QStringLiteral("ASEapp"), QStringLiteral("ASEappSurfaceBuilder"));
    m_japanese = settings.value(QStringLiteral("ui/japanese"), true).toBool();
    setAcceptDrops(true);
    setWindowIcon(QIcon(":/icons/aseapp_surface_builder_icon.png"));
    m_loader = new StructureFileLoader(this);
    loadCustomBondRanges();
    buildUi();
    applyTheme();
    reloadPresetRegistry();
    m_structure = makeDefaultStructure();
    m_supercellBaseStructure = m_structure;
    m_hasSupercellBaseStructure = true;
    m_lastEditWasSupercell = false;
    m_supercellFactors = {1, 1, 1};
    applyStructureState(m_structure);
    setCView(true);
    QTimer::singleShot(0, this, [this]() { setCView(true); });
    QTimer::singleShot(100, this, [this]() { setCView(true); });
    statusBar()->showMessage(uiText(QStringLiteral("open_hint")), 4000);
}

QString MainWindow::uiText(const QString& key) const {
    if (m_japanese) {
        if (key == "open") return QStringLiteral("開く");
        if (key == "save") return QStringLiteral("保存");
        if (key == "export_legend") return QStringLiteral("原子一覧PNG");
        if (key == "quick_start") return QStringLiteral("クイックスタート");
        if (key == "fit") return QStringLiteral("フィット");
        if (key == "reset_c") return QStringLiteral("direct c視点に戻す");
        if (key == "supercell") return QStringLiteral("スーパーセル");
        if (key == "vacuum") return QStringLiteral("真空層");
        if (key == "remove_vacuum") return QStringLiteral("真空層除去");
        if (key == "axis_tilt") return QStringLiteral("セル軸傾き");
        if (key == "language") return QStringLiteral("Language: English");
        if (key == "placement_group") return QStringLiteral("1. 原子配置");
        if (key == "pose_group") return QStringLiteral("2. 吸着分子ポーズ編集");
        if (key == "supercell_group") return QStringLiteral("3. スーパーセル/真空");
        if (key == "view_group") return QStringLiteral("4. 視点操作");
        if (key == "interaction_mode") return QStringLiteral("操作モード");
        if (key == "view_mode") return QStringLiteral("視点を動かす");
        if (key == "move_mode") return QStringLiteral("原子を動かす");
        if (key == "model_mode") return QStringLiteral("モデル表示を動かす");
        if (key == "apply") return QStringLiteral("配置する");
        if (key == "clear") return QStringLiteral("選択解除");
        if (key == "delete_selected") return QStringLiteral("選択原子を削除");
        if (key == "undo") return QStringLiteral("元に戻す");
        if (key == "redo") return QStringLiteral("やり直し");
        if (key == "save_precursor") return QStringLiteral("前駆体CSV保存");
        if (key == "load_precursor") return QStringLiteral("前駆体CSV読込");
        if (key == "place_precursor") return QStringLiteral("前駆体配置");
        if (key == "precursor_select") return QStringLiteral("前駆体");
        if (key == "precursor_unloaded") return QStringLiteral("未読込");
        if (key == "create_pose_group") return QStringLiteral("選択原子をポーズグループ化");
        if (key == "apply_pose_translate") return QStringLiteral("並進を適用");
        if (key == "apply_pose_rotate") return QStringLiteral("回転を適用");
        if (key == "apply_pose_bond") return QStringLiteral("結合長を適用");
        if (key == "reset_pose") return QStringLiteral("作成時ポーズへ戻す");
        if (key == "export_pose_xyz") return QStringLiteral("XYZ/extXYZ出力");
        if (key == "export_pose_json") return QStringLiteral("pose JSON出力");
        if (key == "export_pose_snippet") return QStringLiteral("ASE snippet出力");
        if (key == "preview_pose") return QStringLiteral("ポーズプレビューを表示");
        if (key == "preview_pose_tip") return QStringLiteral("オンのときだけ、選択したポーズ操作の適用後位置を半透明で表示します。実座標は適用ボタンを押すまで変わりません。");
        if (key == "pose_preview_mode") return QStringLiteral("プレビュー対象");
        if (key == "pose_select") return QStringLiteral("グループ");
        if (key == "pose_pivot") return QStringLiteral("pivot");
        if (key == "pose_axis") return QStringLiteral("回転軸");
        if (key == "pose_angle") return QStringLiteral("角度 °");
        if (key == "pose_bond_length") return QStringLiteral("結合長 Å");
        if (key == "pose_tip") return QStringLiteral("選択原子群を剛体グループとして保持し、明示的な数値入力で並進・pivot回転します。通常ドラッグだけで奥行きは変えません。");
        if (key == "reload") return QStringLiteral("プリセット再読込");
        if (key == "open_json") return QStringLiteral("JSONを開く");
        if (key == "help") return QStringLiteral("ヘルプ");
        if (key == "usage") return QStringLiteral("使い方");
        if (key == "about") return QStringLiteral("このアプリについて");
        if (key == "open_hint") return QStringLiteral("構造ファイルをドラッグ&ドロップ、または「開く」で読み込みます。初期視点は direct c 方向です。");
        if (key == "selected_none") return QStringLiteral("選択原子: なし");
        if (key == "cell") return QStringLiteral("セル");
        if (key == "bonds") return QStringLiteral("ボンド");
        if (key == "bond_distances") return QStringLiteral("ボンド距離設定");
        if (key == "bond_distances_tip") return QStringLiteral("VESTA風に元素ペアごとのボンド表示距離 min/max Å を設定します。");
        if (key == "axes") return QStringLiteral("軸");
        if (key == "labels") return QStringLiteral("ラベル");
        if (key == "perspective") return QStringLiteral("透視投影");
        if (key == "depth_cue") return QStringLiteral("奥行き");
        if (key == "right_place_tip") return QStringLiteral("左クリックで単一選択、Ctrl+左クリック/ドラッグで重なった奥の原子も追加選択できます。Escで選択解除できます。");
        if (key == "supercell_tip") return QStringLiteral("a/b/c方向に構造を繰り返してスーパーセルを作成します。");
        if (key == "vacuum_tip") return QStringLiteral("真空層をÅ単位で追加/除去し、スラブ全体をa/b/c方向へ移動します。");
        if (key == "remove_vacuum_tip") return QStringLiteral("c軸方向の余分な真空層をなくし、原子範囲にセルを合わせます。");
        if (key == "axis_tilt_tip") return QStringLiteral("セル軸そのものを指定方向へ傾けます。c軸をa/b方向へ傾けるとステップテラス候補を作りやすくなります。");
        if (key == "export_legend_tip") return QStringLiteral("現在の構造に含まれる元素を、発表用の球＋ラベルPNGとして解像度/DPIを選んで出力します。");
        if (key == "periodic_table") return QStringLiteral("周期表...");
        if (key == "periodic_table_tip") return QStringLiteral("小型カードの周期表から生成元素を選択します。詳細は元素にカーソルを合わせると表示します。");
        if (key == "placement_mode") return QStringLiteral("配置位置");
        if (key == "element") return QStringLiteral("生成元素");
        if (key == "height_distance") return QStringLiteral("高さ/距離 Å");
        if (key == "tilt_degrees") return QStringLiteral("傾き角度 °");
        if (key == "fraction") return QStringLiteral("2原子間の比率");
        if (key == "mode_above") return QStringLiteral("直上");
        if (key == "mode_below") return QStringLiteral("直下");
        if (key == "mode_between") return QStringLiteral("2原子の間");
        if (key == "mode_between_fraction") return QStringLiteral("2原子の間・比率指定");
        if (key == "mode_three_center") return QStringLiteral("3原子の中心");
        if (key == "mode_n_center") return QStringLiteral("選択原子の中心");
        if (key == "mode_plane_normal") return QStringLiteral("選択面の法線上");
        if (key == "placement_hint") return QStringLiteral("左クリック=単一選択 / Ctrl+クリック・ドラッグ=重なりも追加 / Esc=解除 / 必要: %1 / 現在: %2");
        if (key == "preview_placement") return QStringLiteral("配置プレビューを表示");
        if (key == "preview_placement_tip") return QStringLiteral("オンのときだけ、配置予定位置を半透明で表示します。通常は謎の選択物に見えないよう非表示です。");
        if (key == "precursor_none") return QStringLiteral("前駆体: 未読込");
        if (key == "precursor_tip") return QStringLiteral("前駆体名と相対座標だけをCSVに保存します。読み込んだ前駆体は生成元素とは別の一覧から選び、現在の配置位置に配置します。");
        if (key == "interaction_tip") return QStringLiteral("視点=左ドラッグで回転、原子=左ドラッグで選択原子を座標移動、モデル表示=左ドラッグで構造データを変えず表示位置だけ移動します。");
    } else {
        if (key == "open") return QStringLiteral("Open");
        if (key == "save") return QStringLiteral("Save");
        if (key == "export_legend") return QStringLiteral("Atom legend PNG");
        if (key == "quick_start") return QStringLiteral("Quick Start");
        if (key == "fit") return QStringLiteral("Fit");
        if (key == "reset_c") return QStringLiteral("Reset to direct c view");
        if (key == "supercell") return QStringLiteral("Supercell");
        if (key == "vacuum") return QStringLiteral("Vacuum");
        if (key == "remove_vacuum") return QStringLiteral("Remove vacuum");
        if (key == "axis_tilt") return QStringLiteral("Axis tilt");
        if (key == "language") return QStringLiteral("Language: 日本語");
        if (key == "placement_group") return QStringLiteral("1. Atom placement");
        if (key == "pose_group") return QStringLiteral("2. Adsorbate pose editor");
        if (key == "supercell_group") return QStringLiteral("3. Supercell/Vacuum");
        if (key == "view_group") return QStringLiteral("4. View controls");
        if (key == "interaction_mode") return QStringLiteral("Interaction mode");
        if (key == "view_mode") return QStringLiteral("Move view");
        if (key == "move_mode") return QStringLiteral("Move atoms");
        if (key == "model_mode") return QStringLiteral("Move model display");
        if (key == "apply") return QStringLiteral("Apply");
        if (key == "clear") return QStringLiteral("Clear selection");
        if (key == "delete_selected") return QStringLiteral("Delete selected atoms");
        if (key == "undo") return QStringLiteral("Undo");
        if (key == "redo") return QStringLiteral("Redo");
        if (key == "save_precursor") return QStringLiteral("Save precursor CSV");
        if (key == "load_precursor") return QStringLiteral("Load precursor CSV");
        if (key == "place_precursor") return QStringLiteral("Place precursor");
        if (key == "precursor_select") return QStringLiteral("Precursor");
        if (key == "precursor_unloaded") return QStringLiteral("Not loaded");
        if (key == "create_pose_group") return QStringLiteral("Create pose group from selection");
        if (key == "apply_pose_translate") return QStringLiteral("Apply translation");
        if (key == "apply_pose_rotate") return QStringLiteral("Apply rotation");
        if (key == "apply_pose_bond") return QStringLiteral("Apply bond length");
        if (key == "reset_pose") return QStringLiteral("Reset to created pose");
        if (key == "export_pose_xyz") return QStringLiteral("Export XYZ/extXYZ");
        if (key == "export_pose_json") return QStringLiteral("Export pose JSON");
        if (key == "export_pose_snippet") return QStringLiteral("Export ASE snippet");
        if (key == "preview_pose") return QStringLiteral("Show pose preview");
        if (key == "preview_pose_tip") return QStringLiteral("Show a translucent preview of the selected pose operation. Coordinates are not changed until an Apply button is pressed.");
        if (key == "pose_preview_mode") return QStringLiteral("Preview target");
        if (key == "pose_select") return QStringLiteral("Group");
        if (key == "pose_pivot") return QStringLiteral("Pivot");
        if (key == "pose_axis") return QStringLiteral("Rotation axis");
        if (key == "pose_angle") return QStringLiteral("Angle °");
        if (key == "pose_bond_length") return QStringLiteral("Bond length Å");
        if (key == "pose_tip") return QStringLiteral("Keep the selected atoms as one rigid adsorbate group and translate/rotate it with explicit numeric controls. Plain dragging does not change depth.");
        if (key == "reload") return QStringLiteral("Reload presets");
        if (key == "open_json") return QStringLiteral("Open JSON");
        if (key == "help") return QStringLiteral("Help");
        if (key == "usage") return QStringLiteral("Usage");
        if (key == "about") return QStringLiteral("About");
        if (key == "open_hint") return QStringLiteral("Drag & drop a structure file, or use Open. Initial view is direct c-axis.");
        if (key == "selected_none") return QStringLiteral("Selected atoms: none");
        if (key == "cell") return QStringLiteral("Cell");
        if (key == "bonds") return QStringLiteral("Bonds");
        if (key == "bond_distances") return QStringLiteral("Bond distances");
        if (key == "bond_distances_tip") return QStringLiteral("Set VESTA-style min/max Å bond display ranges per element pair.");
        if (key == "axes") return QStringLiteral("Axes");
        if (key == "labels") return QStringLiteral("Labels");
        if (key == "perspective") return QStringLiteral("Perspective");
        if (key == "depth_cue") return QStringLiteral("Depth cue");
        if (key == "right_place_tip") return QStringLiteral("Select targets with left click or Ctrl+left click/drag, including overlapped rear atoms. Esc clears selection.");
        if (key == "supercell_tip") return QStringLiteral("Repeat the structure along a/b/c to create a supercell.");
        if (key == "vacuum_tip") return QStringLiteral("Add/remove vacuum in Å and translate the whole slab along a/b/c.");
        if (key == "remove_vacuum_tip") return QStringLiteral("Remove extra vacuum along the c-axis and fit the cell to the atom bounds.");
        if (key == "axis_tilt_tip") return QStringLiteral("Tilt a cell axis toward another lattice direction. Tilting c toward a/b helps create step-terrace candidates.");
        if (key == "export_legend_tip") return QStringLiteral("Export elements in the current structure as a publication-ready sphere-and-label PNG with selectable resolution and DPI.");
        if (key == "periodic_table") return QStringLiteral("Periodic table...");
        if (key == "periodic_table_tip") return QStringLiteral("Choose the placement element from a compact periodic table. Hover an element for details.");
        if (key == "placement_mode") return QStringLiteral("Placement");
        if (key == "element") return QStringLiteral("Element");
        if (key == "height_distance") return QStringLiteral("Height / distance Å");
        if (key == "tilt_degrees") return QStringLiteral("Tilt degrees °");
        if (key == "fraction") return QStringLiteral("Fraction between 2 atoms");
        if (key == "mode_above") return QStringLiteral("Directly above");
        if (key == "mode_below") return QStringLiteral("Directly below");
        if (key == "mode_between") return QStringLiteral("Between 2 atoms");
        if (key == "mode_between_fraction") return QStringLiteral("Between 2 atoms / fraction");
        if (key == "mode_three_center") return QStringLiteral("3-atom center");
        if (key == "mode_n_center") return QStringLiteral("Selected-atom center");
        if (key == "mode_plane_normal") return QStringLiteral("On selected-plane normal");
        if (key == "placement_hint") return QStringLiteral("Left click selects one / Ctrl+click or drag adds overlaps / Esc clears / Required: %1 / Current: %2");
        if (key == "preview_placement") return QStringLiteral("Show placement preview");
        if (key == "preview_placement_tip") return QStringLiteral("Show a translucent preview of the atom that would be placed. Off by default to avoid confusing it with selection.");
        if (key == "precursor_none") return QStringLiteral("Precursor: not loaded");
        if (key == "precursor_tip") return QStringLiteral("Save only the precursor name and relative coordinates to CSV. Loaded precursors are selected from a separate list and placed at the current placement position.");
        if (key == "interaction_tip") return QStringLiteral("Move view rotates with left drag; Move atoms changes selected atom coordinates; Move model display pans the whole model without changing structure data.");
    }
    return key;
}

void MainWindow::buildUi() {
    setWindowTitle("ASEapp Surface Builder");
    if (!isVisible()) {
        resize(1320, 900);
    }
    setMinimumSize(1080, 760);

    m_canvas = new StructureCanvas(this);
    m_canvas->setJapanese(m_japanese);
    m_canvas->setDisplayOptions(StructureCanvas::DisplayOptions{});
    m_canvas->setInteractionMode(m_moveModelMode
        ? StructureCanvas::InteractionMode::MoveModel
        : (m_moveAtomsMode ? StructureCanvas::InteractionMode::MoveAtoms : StructureCanvas::InteractionMode::View));
    connect(m_canvas, &StructureCanvas::emptyCanvasActivated, this, [this]() { openStructure(); });
    connect(m_canvas, &StructureCanvas::atomActivated, this, &MainWindow::toggleAtomSelection);
    connect(m_canvas, &StructureCanvas::atomPrimarySelected, this, [this](int atomId) {
        setSelectedAtomIds({atomId});
        refreshSelectionUi();
        refreshPresetUi();
    });
    connect(m_canvas, &StructureCanvas::selectedAtomsTranslated, this, &MainWindow::translateSelectedAtoms);
    connect(m_canvas, &StructureCanvas::selectedAtomsTranslationFinished, this, &MainWindow::finishSelectedAtomTranslation);

    auto* central = new QWidget(this);
    auto* mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);

    auto* splitter = new QSplitter(Qt::Horizontal, central);
    splitter->addWidget(m_canvas);

    auto* rightScroll = new QScrollArea(central);
    rightScroll->setWidgetResizable(true);
    rightScroll->setFrameShape(QFrame::NoFrame);
    auto* rightPanel = new QWidget(rightScroll);
    rightScroll->setWidget(rightPanel);
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(10);

    auto* fileGroup = new QGroupBox("File", rightPanel);
    auto* fileLayout = new QVBoxLayout(fileGroup);
    m_fileLabel = new QLabel("No structure loaded", fileGroup);
    m_fileLabel->setWordWrap(true);
    auto* openButton = new QPushButton("Open...");
    connect(openButton, &QPushButton::clicked, this, &MainWindow::openStructure);
    auto* saveButton = new QPushButton("Save As...");
    connect(saveButton, &QPushButton::clicked, this, &MainWindow::saveStructureAs);
    fileLayout->addWidget(m_fileLabel);
    fileLayout->addWidget(openButton);
    fileLayout->addWidget(saveButton);
    fileGroup->setVisible(false);

    auto* presetGroup = new QGroupBox(uiText(QStringLiteral("placement_group")), rightPanel);
    auto* presetLayout = new QVBoxLayout(presetGroup);
    m_presetPathLabel = new QLabel("-", presetGroup);
    m_presetPathLabel->setVisible(false);
    m_presetPathLabel->setWordWrap(true);
    m_presetPathLabel->setToolTip("The JSON file used to load and save custom presets.");
    m_presetCombo = new QComboBox(presetGroup);
    m_presetCombo->setVisible(false);
    m_placementModeCombo = new QComboBox(presetGroup);
    m_placementModeCombo->setToolTip(uiText(QStringLiteral("right_place_tip")));
    m_placementModeCombo->addItem(uiText(QStringLiteral("mode_above")), QStringLiteral("single_above"));
    m_placementModeCombo->addItem(uiText(QStringLiteral("mode_below")), QStringLiteral("single_below"));
    m_placementModeCombo->addItem(uiText(QStringLiteral("mode_n_center")), QStringLiteral("selection_centroid"));
    m_placementModeCombo->addItem(uiText(QStringLiteral("mode_plane_normal")), QStringLiteral("multi_plane_normal"));
    m_elementCombo = new QComboBox(presetGroup);
    m_elementCombo->setEditable(true);
    m_elementCombo->addItems(periodicElementSymbols());
    m_elementCombo->setCurrentText(QStringLiteral("H"));
    m_periodicTableButton = new QPushButton(uiText(QStringLiteral("periodic_table")), presetGroup);
    m_periodicTableButton->setToolTip(uiText(QStringLiteral("periodic_table_tip")));
    m_placementHeightSpin = new QDoubleSpinBox(presetGroup);
    m_placementHeightSpin->setRange(-20.0, 20.0);
    m_placementHeightSpin->setDecimals(3);
    m_placementHeightSpin->setSingleStep(0.05);
    m_placementHeightSpin->setValue(1.05);
    m_placementHeightSpin->setToolTip(m_japanese ? QStringLiteral("配置基準点から法線方向へ動かす距離です。0なら原子間の中心そのものに置きます。") : QStringLiteral("Distance from the anchor along the placement normal. Use 0 to place exactly at the center/between atoms."));
    m_placementTiltSpin = new QDoubleSpinBox(presetGroup);
    m_placementTiltSpin->setRange(-89.0, 89.0);
    m_placementTiltSpin->setDecimals(1);
    m_placementTiltSpin->setSingleStep(1.0);
    m_placementTiltSpin->setValue(0.0);
    m_placementTiltSpin->setToolTip(m_japanese ? QStringLiteral("法線方向からの傾き角です。2個以上選択時は1個目から2個目の方向へ傾けます。") : QStringLiteral("Tilt from the surface normal. With 2+ selected atoms, the tilt leans from the 1st toward the 2nd atom."));
    m_placementFractionSpin = new QDoubleSpinBox(presetGroup);
    m_placementFractionSpin->setRange(0.0, 1.0);
    m_placementFractionSpin->setDecimals(3);
    m_placementFractionSpin->setSingleStep(0.05);
    m_placementFractionSpin->setValue(0.5);
    m_placementFractionSpin->setToolTip(m_japanese ? QStringLiteral("2原子間の比率です。0=1個目、1=2個目です。") : QStringLiteral("Fraction between two atoms. 0 = first atom, 1 = second atom."));
    m_presetDetailsLabel = new QLabel("-", presetGroup);
    m_presetDetailsLabel->setWordWrap(true);
    m_presetDetailsLabel->setToolTip(m_japanese ? QStringLiteral("現在の配置条件です。") : QStringLiteral("Shows the current placement condition."));
    m_selectionLabel = new QLabel("-", presetGroup);
    m_selectionLabel->setWordWrap(true);
    m_selectionLabel->setToolTip(m_japanese ? QStringLiteral("左クリックで単一選択、Ctrl+左クリックまたはCtrl+ドラッグで重なった奥の原子も追加選択した原子IDです。Escで選択解除できます。") : QStringLiteral("Atom IDs selected by left click, Ctrl+left click, or Ctrl+drag, including overlapped rear atoms. Esc clears selection."));
    m_previewPlacementCheck = new QCheckBox(uiText(QStringLiteral("preview_placement")), presetGroup);
    m_previewPlacementCheck->setChecked(false);
    m_previewPlacementCheck->setToolTip(uiText(QStringLiteral("preview_placement_tip")));
    m_applyPresetButton = new QPushButton(uiText(QStringLiteral("apply")), presetGroup);
    m_applyPresetButton->setToolTip(uiText(QStringLiteral("right_place_tip")));
    m_reloadPresetButton = new QPushButton(uiText(QStringLiteral("reload")), presetGroup);
    m_reloadPresetButton->setVisible(false);
    m_reloadPresetButton->setToolTip("Reload the preset JSON file from disk.");
    m_openPresetButton = new QPushButton(uiText(QStringLiteral("open_json")), presetGroup);
    m_openPresetButton->setVisible(false);
    m_openPresetButton->setToolTip("Open the preset JSON file in the default editor.");
    m_clearSelectionButton = new QPushButton(uiText(QStringLiteral("clear")), presetGroup);
    m_clearSelectionButton->setToolTip(m_japanese ? QStringLiteral("選択をすべて解除します。Escキーでも実行できます。") : QStringLiteral("Clear all selected atoms. The Esc key does the same."));
    m_deleteSelectedButton = new QPushButton(uiText(QStringLiteral("delete_selected")), presetGroup);
    m_deleteSelectedButton->setToolTip(m_japanese ? QStringLiteral("黄色で選択中の原子を一括削除します。Deleteキーでも実行できます。") : QStringLiteral("Delete all atoms highlighted in yellow. The Delete key does the same."));
    auto* exportLegendButton = new QPushButton(uiText(QStringLiteral("export_legend")), presetGroup);
    exportLegendButton->setToolTip(uiText(QStringLiteral("export_legend_tip")));
    m_precursorLabel = new QLabel(uiText(QStringLiteral("precursor_none")), presetGroup);
    m_precursorLabel->setWordWrap(true);
    m_precursorLabel->setToolTip(uiText(QStringLiteral("precursor_tip")));
    m_precursorCombo = new QComboBox(presetGroup);
    m_precursorCombo->setToolTip(uiText(QStringLiteral("precursor_tip")));
    m_precursorCombo->addItem(uiText(QStringLiteral("precursor_unloaded")), -1);
    m_precursorCombo->setEnabled(false);
    m_savePrecursorButton = new QPushButton(uiText(QStringLiteral("save_precursor")), presetGroup);
    m_savePrecursorButton->setToolTip(uiText(QStringLiteral("precursor_tip")));
    m_loadPrecursorButton = new QPushButton(uiText(QStringLiteral("load_precursor")), presetGroup);
    m_loadPrecursorButton->setToolTip(uiText(QStringLiteral("precursor_tip")));
    m_placePrecursorButton = new QPushButton(uiText(QStringLiteral("place_precursor")), presetGroup);
    m_placePrecursorButton->setToolTip(uiText(QStringLiteral("precursor_tip")));
    connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { refreshPresetUi(); });
    connect(m_placementModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        if (m_placementHeightSpin != nullptr && m_placementModeCombo != nullptr) {
            const QString mode = m_placementModeCombo->currentData().toString().trimmed().toLower();
            if (mode == QStringLiteral("selection_centroid")
                || mode == QStringLiteral("pair_midpoint")
                || mode == QStringLiteral("pair_fraction")
                || mode == QStringLiteral("triple_centroid")
                || mode == QStringLiteral("triple_weighted")
                || mode == QStringLiteral("multi_centroid")
                || mode == QStringLiteral("multi_weighted")) {
                m_placementHeightSpin->setValue(0.0);
            } else {
                m_placementHeightSpin->setValue(1.05);
            }
        }
        refreshPresetUi();
    });
    connect(m_elementCombo, &QComboBox::currentTextChanged, this, [this]() { refreshPresetUi(); });
    connect(m_placementHeightSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this]() { refreshPresetUi(); });
    connect(m_placementTiltSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this]() { refreshPresetUi(); });
    connect(m_placementFractionSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this]() { refreshPresetUi(); });
    connect(m_previewPlacementCheck, &QCheckBox::toggled, this, [this]() { refreshPresetUi(); });
    connect(m_periodicTableButton, &QPushButton::clicked, this, &MainWindow::choosePlacementElement);
    connect(m_applyPresetButton, &QPushButton::clicked, this, &MainWindow::applySelectedPreset);
    connect(m_reloadPresetButton, &QPushButton::clicked, this, &MainWindow::reloadPresetRegistry);
    connect(m_openPresetButton, &QPushButton::clicked, this, &MainWindow::openPresetFile);
    connect(m_clearSelectionButton, &QPushButton::clicked, this, &MainWindow::clearSelection);
    connect(m_deleteSelectedButton, &QPushButton::clicked, this, &MainWindow::deleteSelectedAtoms);
    connect(exportLegendButton, &QPushButton::clicked, this, &MainWindow::exportElementLegendImage);
    connect(m_savePrecursorButton, &QPushButton::clicked, this, &MainWindow::saveSelectedPrecursorCsv);
    connect(m_loadPrecursorButton, &QPushButton::clicked, this, &MainWindow::loadPrecursorCsv);
    connect(m_placePrecursorButton, &QPushButton::clicked, this, &MainWindow::placeLoadedPrecursor);
    connect(m_precursorCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { refreshPrecursorUi(); });
    auto* placementForm = new QFormLayout();
    auto* elementRow = new QWidget(presetGroup);
    auto* elementRowLayout = new QHBoxLayout(elementRow);
    elementRowLayout->setContentsMargins(0, 0, 0, 0);
    elementRowLayout->addWidget(m_elementCombo, 1);
    elementRowLayout->addWidget(m_periodicTableButton);
    placementForm->addRow(uiText(QStringLiteral("precursor_select")), m_precursorCombo);
    placementForm->addRow(uiText(QStringLiteral("element")), elementRow);
    placementForm->addRow(uiText(QStringLiteral("placement_mode")), m_placementModeCombo);
    placementForm->addRow(uiText(QStringLiteral("height_distance")), m_placementHeightSpin);
    placementForm->addRow(uiText(QStringLiteral("tilt_degrees")), m_placementTiltSpin);
    placementForm->addRow(uiText(QStringLiteral("fraction")), m_placementFractionSpin);
    presetLayout->addLayout(placementForm);
    presetLayout->addWidget(m_previewPlacementCheck);
    presetLayout->addWidget(m_selectionLabel);
    presetLayout->addWidget(m_presetDetailsLabel);
    auto* presetButtonRow = new QHBoxLayout();
    presetButtonRow->addWidget(m_applyPresetButton);
    presetLayout->addLayout(presetButtonRow);
    auto* precursorPlaceRow = new QHBoxLayout();
    precursorPlaceRow->addWidget(m_placePrecursorButton);
    presetLayout->addLayout(precursorPlaceRow);
    presetLayout->addWidget(m_precursorLabel);
    auto* precursorCsvRow = new QHBoxLayout();
    precursorCsvRow->addWidget(m_savePrecursorButton);
    precursorCsvRow->addWidget(m_loadPrecursorButton);
    presetLayout->addLayout(precursorCsvRow);
    auto* presetUtilityRow = new QHBoxLayout();
    presetUtilityRow->addWidget(m_clearSelectionButton);
    presetUtilityRow->addWidget(m_deleteSelectedButton);
    presetLayout->addLayout(presetUtilityRow);
    auto* outputRow = new QHBoxLayout();
    outputRow->addWidget(exportLegendButton);
    presetLayout->addLayout(outputRow);
    rightLayout->addWidget(presetGroup);

    auto* poseGroup = new QGroupBox(uiText(QStringLiteral("pose_group")), rightPanel);
    poseGroup->setToolTip(uiText(QStringLiteral("pose_tip")));
    auto* poseLayout = new QVBoxLayout(poseGroup);
    m_poseStatusLabel = new QLabel(m_japanese
        ? QStringLiteral("吸着分子グループ: 未作成")
        : QStringLiteral("Adsorbate pose group: none"), poseGroup);
    m_poseStatusLabel->setWordWrap(true);
    m_poseStatusLabel->setToolTip(uiText(QStringLiteral("pose_tip")));
    m_poseGroupCombo = new QComboBox(poseGroup);
    m_poseGroupCombo->setToolTip(uiText(QStringLiteral("pose_tip")));
    m_posePivotCombo = new QComboBox(poseGroup);
    m_poseAxisCombo = new QComboBox(poseGroup);
    m_posePreviewModeCombo = new QComboBox(poseGroup);
    m_poseAxisCombo->addItem(QStringLiteral("global X"), QStringLiteral("global_x"));
    m_poseAxisCombo->addItem(QStringLiteral("global Y"), QStringLiteral("global_y"));
    m_poseAxisCombo->addItem(QStringLiteral("global Z"), QStringLiteral("global_z"));
    m_poseAxisCombo->addItem(QStringLiteral("cell a"), QStringLiteral("cell_a"));
    m_poseAxisCombo->addItem(QStringLiteral("cell b"), QStringLiteral("cell_b"));
    m_poseAxisCombo->addItem(QStringLiteral("cell c"), QStringLiteral("cell_c"));
    m_poseAxisCombo->addItem(m_japanese ? QStringLiteral("slab法線") : QStringLiteral("slab normal"), QStringLiteral("slab_normal"));
    m_poseAxisCombo->addItem(m_japanese ? QStringLiteral("選択中2原子の結合軸") : QStringLiteral("selected 2-atom bond axis"), QStringLiteral("bond_selected_two"));
    m_poseAxisCombo->addItem(m_japanese ? QStringLiteral("画面手前方向") : QStringLiteral("camera axis"), QStringLiteral("camera"));
    m_posePreviewModeCombo->addItem(m_japanese ? QStringLiteral("並進") : QStringLiteral("Translation"), QStringLiteral("translate"));
    m_posePreviewModeCombo->addItem(m_japanese ? QStringLiteral("回転") : QStringLiteral("Rotation"), QStringLiteral("rotate"));
    m_posePreviewModeCombo->addItem(m_japanese ? QStringLiteral("結合長") : QStringLiteral("Bond length"), QStringLiteral("bond_length"));
    m_posePreviewModeCombo->setToolTip(uiText(QStringLiteral("preview_pose_tip")));
    m_previewPoseCheck = new QCheckBox(uiText(QStringLiteral("preview_pose")), poseGroup);
    m_previewPoseCheck->setChecked(false);
    m_previewPoseCheck->setToolTip(uiText(QStringLiteral("preview_pose_tip")));

    auto createPoseSpin = [](double min, double max, double step, int decimals, const QString& suffix, QWidget* parent) {
        auto* spin = new QDoubleSpinBox(parent);
        spin->setRange(min, max);
        spin->setDecimals(decimals);
        spin->setSingleStep(step);
        spin->setValue(0.0);
        spin->setSuffix(suffix);
        return spin;
    };
    m_poseDxSpin = createPoseSpin(-200.0, 200.0, 0.1, 3, QStringLiteral(" Å"), poseGroup);
    m_poseDySpin = createPoseSpin(-200.0, 200.0, 0.1, 3, QStringLiteral(" Å"), poseGroup);
    m_poseDzSpin = createPoseSpin(-200.0, 200.0, 0.1, 3, QStringLiteral(" Å"), poseGroup);
    m_poseCellASpin = createPoseSpin(-200.0, 200.0, 0.1, 3, QStringLiteral(" Å"), poseGroup);
    m_poseCellBSpin = createPoseSpin(-200.0, 200.0, 0.1, 3, QStringLiteral(" Å"), poseGroup);
    m_poseCellCSpin = createPoseSpin(-200.0, 200.0, 0.1, 3, QStringLiteral(" Å"), poseGroup);
    m_poseNormalSpin = createPoseSpin(-200.0, 200.0, 0.1, 3, QStringLiteral(" Å"), poseGroup);
    m_poseAngleSpin = createPoseSpin(-360.0, 360.0, 5.0, 2, QStringLiteral(" °"), poseGroup);
    m_poseBondLengthSpin = createPoseSpin(0.01, 20.0, 0.01, 3, QStringLiteral(" Å"), poseGroup);
    m_poseBondLengthSpin->setValue(1.0);
    for (auto* spin : {m_poseDxSpin, m_poseDySpin, m_poseDzSpin, m_poseCellASpin, m_poseCellBSpin, m_poseCellCSpin, m_poseNormalSpin}) {
        spin->setToolTip(m_japanese
            ? QStringLiteral("吸着分子グループ全体を剛体として並進します。相対座標は変えません。")
            : QStringLiteral("Translate the whole adsorbate group as a rigid body. Relative coordinates are preserved."));
    }
    m_poseAngleSpin->setToolTip(m_japanese
        ? QStringLiteral("pivot を固定し、選択した軸まわりに剛体回転します。")
        : QStringLiteral("Rotate rigidly around the selected axis while keeping the pivot fixed."));
    m_poseBondLengthSpin->setToolTip(m_japanese
        ? QStringLiteral("グループ内で選択した2原子の結合長です。片側成分を結合方向にスライドします。")
        : QStringLiteral("Bond length for two selected atoms in the group. One side slides along the bond direction."));

    m_createPoseGroupButton = new QPushButton(uiText(QStringLiteral("create_pose_group")), poseGroup);
    m_applyPoseTranslationButton = new QPushButton(uiText(QStringLiteral("apply_pose_translate")), poseGroup);
    m_applyPoseRotationButton = new QPushButton(uiText(QStringLiteral("apply_pose_rotate")), poseGroup);
    m_applyPoseBondLengthButton = new QPushButton(uiText(QStringLiteral("apply_pose_bond")), poseGroup);
    m_resetPoseButton = new QPushButton(uiText(QStringLiteral("reset_pose")), poseGroup);
    m_exportPoseXyzButton = new QPushButton(uiText(QStringLiteral("export_pose_xyz")), poseGroup);
    m_exportPoseJsonButton = new QPushButton(uiText(QStringLiteral("export_pose_json")), poseGroup);
    m_exportPoseSnippetButton = new QPushButton(uiText(QStringLiteral("export_pose_snippet")), poseGroup);
    for (auto* button : {m_createPoseGroupButton, m_applyPoseTranslationButton, m_applyPoseRotationButton,
             m_applyPoseBondLengthButton, m_resetPoseButton, m_exportPoseXyzButton, m_exportPoseJsonButton, m_exportPoseSnippetButton}) {
        button->setToolTip(uiText(QStringLiteral("pose_tip")));
    }
    connect(m_createPoseGroupButton, &QPushButton::clicked, this, &MainWindow::createPoseGroupFromSelection);
    connect(m_applyPoseTranslationButton, &QPushButton::clicked, this, &MainWindow::applyPoseTranslation);
    connect(m_applyPoseRotationButton, &QPushButton::clicked, this, &MainWindow::applyPoseRotation);
    connect(m_applyPoseBondLengthButton, &QPushButton::clicked, this, &MainWindow::applyPoseBondLength);
    connect(m_resetPoseButton, &QPushButton::clicked, this, &MainWindow::resetPoseGroup);
    connect(m_exportPoseXyzButton, &QPushButton::clicked, this, &MainWindow::exportPoseXyz);
    connect(m_exportPoseJsonButton, &QPushButton::clicked, this, &MainWindow::exportPoseJson);
    connect(m_exportPoseSnippetButton, &QPushButton::clicked, this, &MainWindow::exportPoseSnippet);
    connect(m_poseGroupCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { refreshPoseUi(); });
    connect(m_posePivotCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { updatePreviewAtoms(); });
    connect(m_poseAxisCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { refreshPoseUi(); });
    connect(m_posePreviewModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { updatePreviewAtoms(); });
    connect(m_previewPoseCheck, &QCheckBox::toggled, this, [this]() { refreshPoseUi(); });
    auto updatePosePreview = [this](double) { updatePreviewAtoms(); };
    for (auto* spin : {m_poseDxSpin, m_poseDySpin, m_poseDzSpin, m_poseCellASpin, m_poseCellBSpin, m_poseCellCSpin,
             m_poseNormalSpin, m_poseAngleSpin, m_poseBondLengthSpin}) {
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, updatePosePreview);
    }

    auto* poseForm = new QFormLayout();
    poseForm->addRow(uiText(QStringLiteral("pose_select")), m_poseGroupCombo);
    poseForm->addRow(uiText(QStringLiteral("pose_preview_mode")), m_posePreviewModeCombo);
    poseForm->addRow(uiText(QStringLiteral("pose_pivot")), m_posePivotCombo);
    poseForm->addRow(QStringLiteral("dx"), m_poseDxSpin);
    poseForm->addRow(QStringLiteral("dy"), m_poseDySpin);
    poseForm->addRow(QStringLiteral("dz"), m_poseDzSpin);
    poseForm->addRow(QStringLiteral("cell a"), m_poseCellASpin);
    poseForm->addRow(QStringLiteral("cell b"), m_poseCellBSpin);
    poseForm->addRow(QStringLiteral("cell c"), m_poseCellCSpin);
    poseForm->addRow(m_japanese ? QStringLiteral("法線方向") : QStringLiteral("normal"), m_poseNormalSpin);
    poseForm->addRow(uiText(QStringLiteral("pose_axis")), m_poseAxisCombo);
    poseForm->addRow(uiText(QStringLiteral("pose_angle")), m_poseAngleSpin);
    poseForm->addRow(uiText(QStringLiteral("pose_bond_length")), m_poseBondLengthSpin);
    poseLayout->addWidget(m_poseStatusLabel);
    poseLayout->addWidget(m_createPoseGroupButton);
    poseLayout->addWidget(m_previewPoseCheck);
    poseLayout->addLayout(poseForm);
    auto* poseApplyRow = new QHBoxLayout();
    poseApplyRow->addWidget(m_applyPoseTranslationButton);
    poseApplyRow->addWidget(m_applyPoseRotationButton);
    poseLayout->addLayout(poseApplyRow);
    poseLayout->addWidget(m_applyPoseBondLengthButton);
    poseLayout->addWidget(m_resetPoseButton);
    auto* poseExportRow = new QHBoxLayout();
    poseExportRow->addWidget(m_exportPoseXyzButton);
    poseExportRow->addWidget(m_exportPoseJsonButton);
    poseExportRow->addWidget(m_exportPoseSnippetButton);
    poseLayout->addLayout(poseExportRow);
    rightLayout->addWidget(poseGroup);

    auto* surfaceGroup = new QGroupBox(uiText(QStringLiteral("supercell_group")), rightPanel);
    auto* surfaceLayout = new QVBoxLayout(surfaceGroup);
    m_supercellStatusLabel = new QLabel(supercellStatusText(), surfaceGroup);
    m_supercellStatusLabel->setWordWrap(true);
    auto* supercellButton = new QPushButton(uiText(QStringLiteral("supercell")), surfaceGroup);
    supercellButton->setToolTip(uiText(QStringLiteral("supercell_tip")));
    connect(supercellButton, &QPushButton::clicked, this, &MainWindow::createSupercell);
    auto* terminateButton = new QPushButton("Hydrogen termination", surfaceGroup);
    terminateButton->setVisible(false);
    connect(terminateButton, &QPushButton::clicked, this, &MainWindow::terminateHydrogen);
    auto* vacuumButton = new QPushButton(uiText(QStringLiteral("vacuum")), surfaceGroup);
    vacuumButton->setToolTip(uiText(QStringLiteral("vacuum_tip")));
    connect(vacuumButton, &QPushButton::clicked, this, &MainWindow::addVacuumLayer);
    auto* removeVacuumButton = new QPushButton(uiText(QStringLiteral("remove_vacuum")), surfaceGroup);
    removeVacuumButton->setToolTip(uiText(QStringLiteral("remove_vacuum_tip")));
    connect(removeVacuumButton, &QPushButton::clicked, this, &MainWindow::removeVacuumLayer);
    auto* axisTiltButton = new QPushButton(uiText(QStringLiteral("axis_tilt")), surfaceGroup);
    axisTiltButton->setToolTip(uiText(QStringLiteral("axis_tilt_tip")));
    connect(axisTiltButton, &QPushButton::clicked, this, &MainWindow::tiltCellAxis);
    surfaceLayout->addWidget(m_supercellStatusLabel);
    surfaceLayout->addWidget(supercellButton);
    surfaceLayout->addWidget(vacuumButton);
    surfaceLayout->addWidget(removeVacuumButton);
    surfaceLayout->addWidget(axisTiltButton);
    rightLayout->addWidget(surfaceGroup);

    auto* viewGroup = new QGroupBox(uiText(QStringLiteral("view_group")), rightPanel);
    auto* viewLayout = new QVBoxLayout(viewGroup);
    auto* presetRow = new QHBoxLayout();
    auto addPresetButton = [&](const QString& text, int axisIndex) {
        auto* button = new QPushButton(text);
        button->setToolTip(m_japanese
            ? QStringLiteral("実格子 %1 軸方向を見る direct 視点です。非直交セルでは %1* と異なります。").arg(text)
            : QStringLiteral("Direct view along the real-space %1 axis. Differs from %1* for non-orthogonal cells.").arg(text));
        connect(button, &QPushButton::clicked, this, [this, axisIndex]() {
            const auto [direction, upHint] = canonicalDirectViewDirection(m_structure.cellVectors, axisIndex);
            m_canvas->setViewDirection(direction, upHint, true);
        });
        presetRow->addWidget(button);
    };
    addPresetButton("a", 0);
    addPresetButton("b", 1);
    addPresetButton("c", 2);
    viewLayout->addLayout(presetRow);

    auto* reciprocalRow = new QHBoxLayout();
    auto addReciprocalButton = [&](const QString& text, int index) {
        auto* button = new QPushButton(text);
        QString tip = m_japanese
            ? QStringLiteral("逆格子 %1 方向を見る reciprocal 視点です。slab 面法線確認に使います。").arg(text)
            : QStringLiteral("Reciprocal view along %1. Useful for checking slab plane normals.").arg(text);
        if (index == 2) {
            tip += m_japanese
                ? QStringLiteral(" c 軸が ab 面法線と平行なセルでは c と同じ向きに見えます。")
                : QStringLiteral(" It matches c when c is parallel to the ab-plane normal.");
        }
        button->setToolTip(tip);
        connect(button, &QPushButton::clicked, this, [this, index]() {
            const auto [direction, up] = canonicalReciprocalViewDirection(m_structure.cellVectors, index);
            m_canvas->setViewDirection(direction, up, true);
        });
        reciprocalRow->addWidget(button);
    };
    addReciprocalButton("a*", 0);
    addReciprocalButton("b*", 1);
    addReciprocalButton("c*", 2);
    viewLayout->addLayout(reciprocalRow);

    auto* interactionLabel = new QLabel(uiText(QStringLiteral("interaction_mode")), viewGroup);
    interactionLabel->setToolTip(uiText(QStringLiteral("interaction_tip")));
    viewLayout->addWidget(interactionLabel);
    auto* interactionRow = new QVBoxLayout();
    auto* viewModeButton = new QRadioButton(uiText(QStringLiteral("view_mode")), viewGroup);
    auto* moveModeButton = new QRadioButton(uiText(QStringLiteral("move_mode")), viewGroup);
    auto* modelModeButton = new QRadioButton(uiText(QStringLiteral("model_mode")), viewGroup);
    viewModeButton->setToolTip(uiText(QStringLiteral("interaction_tip")));
    moveModeButton->setToolTip(uiText(QStringLiteral("interaction_tip")));
    modelModeButton->setToolTip(uiText(QStringLiteral("interaction_tip")));
    viewModeButton->setChecked(!m_moveAtomsMode && !m_moveModelMode);
    moveModeButton->setChecked(m_moveAtomsMode);
    modelModeButton->setChecked(m_moveModelMode);
    connect(viewModeButton, &QRadioButton::toggled, this, [this](bool checked) {
        if (!checked) {
            return;
        }
        m_moveAtomsMode = false;
        m_moveModelMode = false;
        if (m_canvas != nullptr) {
            m_canvas->setInteractionMode(StructureCanvas::InteractionMode::View);
        }
        statusBar()->showMessage(m_japanese ? QStringLiteral("視点移動モード: 左ドラッグで回転します。") : QStringLiteral("Move view mode: left drag rotates the view."), 3500);
    });
    connect(moveModeButton, &QRadioButton::toggled, this, [this](bool checked) {
        if (!checked) {
            return;
        }
        m_moveAtomsMode = true;
        m_moveModelMode = false;
        if (m_canvas != nullptr) {
            m_canvas->setInteractionMode(StructureCanvas::InteractionMode::MoveAtoms);
        }
        statusBar()->showMessage(m_japanese ? QStringLiteral("原子移動モード: 左ドラッグで選択原子の座標を移動します。") : QStringLiteral("Move atoms mode: left drag changes selected atom coordinates."), 3500);
    });
    connect(modelModeButton, &QRadioButton::toggled, this, [this](bool checked) {
        if (!checked) {
            return;
        }
        m_moveAtomsMode = false;
        m_moveModelMode = true;
        if (m_canvas != nullptr) {
            m_canvas->setInteractionMode(StructureCanvas::InteractionMode::MoveModel);
        }
        statusBar()->showMessage(m_japanese ? QStringLiteral("モデル表示移動モード: 左ドラッグで構造データを変えず表示位置だけ移動します。") : QStringLiteral("Move model display mode: left drag pans the model without changing structure data."), 3500);
    });
    interactionRow->addWidget(viewModeButton);
    interactionRow->addWidget(moveModeButton);
    interactionRow->addWidget(modelModeButton);
    viewLayout->addLayout(interactionRow);

    auto* fitButton = new QPushButton(uiText(QStringLiteral("fit")));
    connect(fitButton, &QPushButton::clicked, this, &MainWindow::fitStructure);
    auto* resetButton = new QPushButton(uiText(QStringLiteral("reset_c")));
    connect(resetButton, &QPushButton::clicked, this, &MainWindow::resetView);
    auto* viewRow = new QHBoxLayout();
    viewRow->addWidget(fitButton);
    viewRow->addWidget(resetButton);
    viewLayout->addLayout(viewRow);
    rightLayout->addWidget(viewGroup);

    auto* displayGroup = new QGroupBox(m_japanese ? QStringLiteral("表示") : QStringLiteral("Display"), rightPanel);
    auto* displayLayout = new QVBoxLayout(displayGroup);
    m_showCellCheck = new QCheckBox(uiText(QStringLiteral("cell")));
    m_showCellCheck->setChecked(true);
    m_showBondsCheck = new QCheckBox(uiText(QStringLiteral("bonds")));
    m_showBondsCheck->setChecked(true);
    m_showAxesCheck = new QCheckBox(uiText(QStringLiteral("axes")));
    m_showAxesCheck->setChecked(true);
    m_showLabelsCheck = new QCheckBox(uiText(QStringLiteral("labels")));
    m_showLabelsCheck->setChecked(true);
    m_perspectiveCheck = new QCheckBox(uiText(QStringLiteral("perspective")));
    m_perspectiveCheck->setChecked(false);
    m_depthCueCheck = new QCheckBox(uiText(QStringLiteral("depth_cue")));
    m_depthCueCheck->setChecked(false);
    for (auto* check : {m_showCellCheck, m_showBondsCheck, m_showAxesCheck, m_showLabelsCheck, m_perspectiveCheck, m_depthCueCheck}) {
        displayLayout->addWidget(check);
        connect(check, &QCheckBox::toggled, this, &MainWindow::syncCanvasDisplayOptions);
    }
    auto* bondDistanceButton = new QPushButton(uiText(QStringLiteral("bond_distances")), displayGroup);
    bondDistanceButton->setToolTip(uiText(QStringLiteral("bond_distances_tip")));
    connect(bondDistanceButton, &QPushButton::clicked, this, &MainWindow::editBondDistances);
    displayLayout->addWidget(bondDistanceButton);
    m_atomScaleSpin = new QDoubleSpinBox();
    m_atomScaleSpin->setRange(60.0, 180.0);
    m_atomScaleSpin->setValue(90.0);
    m_atomScaleSpin->setSuffix("%");
    m_atomScaleSpin->setSingleStep(5.0);
    m_atomScaleSpin->setToolTip("Scale the atom spheres on screen.");
    connect(m_atomScaleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::syncCanvasDisplayOptions);
    auto* atomScaleForm = new QFormLayout();
    atomScaleForm->addRow("Atom size", m_atomScaleSpin);
    displayLayout->addLayout(atomScaleForm);
    displayGroup->setVisible(false);

    auto* infoGroup = new QGroupBox(m_japanese ? QStringLiteral("現在の構造") : QStringLiteral("Current structure"), rightPanel);
    auto* infoLayout = new QVBoxLayout(infoGroup);
    m_summaryLabel = new QLabel("-", infoGroup);
    m_summaryLabel->setWordWrap(true);
    infoLayout->addWidget(m_summaryLabel);
    infoGroup->setVisible(false);
    rightLayout->addStretch(1);

    splitter->addWidget(rightScroll);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({900, 360});
    mainLayout->addWidget(splitter, 1);
    setCentralWidget(central);

    auto* toolbar = addToolBar("Main");
    toolbar->setMovable(false);
    m_languageAction = new QAction(uiText(QStringLiteral("language")), this);
    connect(m_languageAction, &QAction::triggered, this, &MainWindow::toggleLanguage);
    toolbar->addAction(m_languageAction);
    toolbar->addSeparator();

    m_openAction = new QAction(uiText(QStringLiteral("open")), this);
    m_openAction->setShortcut(QKeySequence::Open);
    m_openAction->setToolTip("Open a structure file.");
    connect(m_openAction, &QAction::triggered, this, &MainWindow::openStructure);
    toolbar->addAction(m_openAction);
    m_saveAction = new QAction(uiText(QStringLiteral("save")), this);
    m_saveAction->setShortcut(QKeySequence("Ctrl+Shift+S"));
    m_saveAction->setToolTip("Save the current structure to a file.");
    connect(m_saveAction, &QAction::triggered, this, &MainWindow::saveStructureAs);
    toolbar->addAction(m_saveAction);
    m_exportLegendAction = new QAction(uiText(QStringLiteral("export_legend")), this);
    m_exportLegendAction->setShortcut(QKeySequence("Ctrl+Alt+L"));
    m_exportLegendAction->setToolTip(uiText(QStringLiteral("export_legend_tip")));
    connect(m_exportLegendAction, &QAction::triggered, this, &MainWindow::exportElementLegendImage);
    toolbar->addAction(m_exportLegendAction);
    m_undoAction = new QAction(uiText(QStringLiteral("undo")), this);
    m_undoAction->setShortcut(QKeySequence::Undo);
    connect(m_undoAction, &QAction::triggered, this, &MainWindow::undoEdit);
    toolbar->addAction(m_undoAction);
    m_redoAction = new QAction(uiText(QStringLiteral("redo")), this);
    m_redoAction->setShortcut(QKeySequence::Redo);
    connect(m_redoAction, &QAction::triggered, this, &MainWindow::redoEdit);
    toolbar->addAction(m_redoAction);
    toolbar->addSeparator();
    m_quickStartAction = new QAction(uiText(QStringLiteral("quick_start")), this);
    m_quickStartAction->setToolTip("Show the one-page startup guide.");
    connect(m_quickStartAction, &QAction::triggered, this, &MainWindow::showStartupGuide);
    toolbar->addAction(m_quickStartAction);
    toolbar->addSeparator();
    m_fitAction = new QAction(uiText(QStringLiteral("fit")), this);
    m_fitAction->setShortcut(QKeySequence("F"));
    m_fitAction->setToolTip("Fit the view to the structure.");
    connect(m_fitAction, &QAction::triggered, this, &MainWindow::fitStructure);
    toolbar->addAction(m_fitAction);
    m_resetAction = new QAction(uiText(QStringLiteral("reset_c")), this);
    m_resetAction->setShortcut(QKeySequence("Ctrl+R"));
    m_resetAction->setToolTip("Reset camera rotation and zoom.");
    connect(m_resetAction, &QAction::triggered, this, &MainWindow::resetView);
    toolbar->addAction(m_resetAction);
    toolbar->addSeparator();
    m_supercellAction = new QAction(uiText(QStringLiteral("supercell")), this);
    m_supercellAction->setShortcut(QKeySequence("Ctrl+Alt+S"));
    m_supercellAction->setToolTip("Create a repeated supercell.");
    connect(m_supercellAction, &QAction::triggered, this, &MainWindow::createSupercell);
    toolbar->addAction(m_supercellAction);
    m_terminateAction = new QAction("H terminate", this);
    m_terminateAction->setShortcut(QKeySequence("Ctrl+Alt+H"));
    m_terminateAction->setToolTip("Add hydrogen termination to the surface.");
    connect(m_terminateAction, &QAction::triggered, this, &MainWindow::terminateHydrogen);
    m_terminateAction->setVisible(false);
    m_vacuumAction = new QAction(uiText(QStringLiteral("vacuum")), this);
    m_vacuumAction->setShortcut(QKeySequence("Ctrl+Alt+V"));
    m_vacuumAction->setToolTip(uiText(QStringLiteral("vacuum_tip")));
    connect(m_vacuumAction, &QAction::triggered, this, &MainWindow::addVacuumLayer);
    toolbar->addAction(m_vacuumAction);
    m_removeVacuumAction = new QAction(uiText(QStringLiteral("remove_vacuum")), this);
    m_removeVacuumAction->setShortcut(QKeySequence("Ctrl+Alt+Shift+V"));
    m_removeVacuumAction->setToolTip(uiText(QStringLiteral("remove_vacuum_tip")));
    connect(m_removeVacuumAction, &QAction::triggered, this, &MainWindow::removeVacuumLayer);
    toolbar->addAction(m_removeVacuumAction);
    m_axisTiltAction = new QAction(uiText(QStringLiteral("axis_tilt")), this);
    m_axisTiltAction->setShortcut(QKeySequence("Ctrl+Alt+T"));
    m_axisTiltAction->setToolTip(uiText(QStringLiteral("axis_tilt_tip")));
    connect(m_axisTiltAction, &QAction::triggered, this, &MainWindow::tiltCellAxis);
    toolbar->addAction(m_axisTiltAction);
    m_showCellAction = toolbar->addAction(uiText(QStringLiteral("cell"))); m_showCellAction->setCheckable(true); m_showCellAction->setChecked(true);
    m_showCellAction->setToolTip("Show or hide the unit-cell frame.");
    connect(m_showCellAction, &QAction::toggled, m_showCellCheck, &QCheckBox::setChecked);
    connect(m_showCellCheck, &QCheckBox::toggled, m_showCellAction, &QAction::setChecked);
    m_showBondsAction = toolbar->addAction(uiText(QStringLiteral("bonds"))); m_showBondsAction->setCheckable(true); m_showBondsAction->setChecked(true);
    m_showBondsAction->setToolTip("Show or hide bond connections.");
    connect(m_showBondsAction, &QAction::toggled, m_showBondsCheck, &QCheckBox::setChecked);
    connect(m_showBondsCheck, &QCheckBox::toggled, m_showBondsAction, &QAction::setChecked);
    m_bondDistanceAction = toolbar->addAction(uiText(QStringLiteral("bond_distances")));
    m_bondDistanceAction->setToolTip(uiText(QStringLiteral("bond_distances_tip")));
    connect(m_bondDistanceAction, &QAction::triggered, this, &MainWindow::editBondDistances);
    m_showAxesAction = toolbar->addAction(uiText(QStringLiteral("axes"))); m_showAxesAction->setCheckable(true); m_showAxesAction->setChecked(true);
    m_showAxesAction->setToolTip("Show or hide the XYZ axes.");
    connect(m_showAxesAction, &QAction::toggled, m_showAxesCheck, &QCheckBox::setChecked);
    connect(m_showAxesCheck, &QCheckBox::toggled, m_showAxesAction, &QAction::setChecked);
    m_showLabelsAction = toolbar->addAction(uiText(QStringLiteral("labels"))); m_showLabelsAction->setCheckable(true); m_showLabelsAction->setChecked(true);
    m_showLabelsAction->setToolTip("Show or hide atom labels.");
    connect(m_showLabelsAction, &QAction::toggled, m_showLabelsCheck, &QCheckBox::setChecked);
    connect(m_showLabelsCheck, &QCheckBox::toggled, m_showLabelsAction, &QAction::setChecked);
    m_perspectiveAction = toolbar->addAction(uiText(QStringLiteral("perspective"))); m_perspectiveAction->setCheckable(true); m_perspectiveAction->setChecked(false);
    m_perspectiveAction->setToolTip("Toggle perspective projection.");
    connect(m_perspectiveAction, &QAction::toggled, m_perspectiveCheck, &QCheckBox::setChecked);
    connect(m_perspectiveCheck, &QCheckBox::toggled, m_perspectiveAction, &QAction::setChecked);
    m_depthCueAction = toolbar->addAction(uiText(QStringLiteral("depth_cue"))); m_depthCueAction->setCheckable(true); m_depthCueAction->setChecked(false);
    m_depthCueAction->setToolTip("Toggle depth-based transparency.");
    connect(m_depthCueAction, &QAction::toggled, m_depthCueCheck, &QCheckBox::setChecked);
    connect(m_depthCueCheck, &QCheckBox::toggled, m_depthCueAction, &QAction::setChecked);

    auto* languageMenu = menuBar()->addMenu(QStringLiteral("Language"));
    auto* japaneseAction = languageMenu->addAction(QStringLiteral("日本語"));
    connect(japaneseAction, &QAction::triggered, this, [this]() {
        if (!m_japanese) {
            toggleLanguage();
        }
    });
    auto* englishAction = languageMenu->addAction(QStringLiteral("English"));
    connect(englishAction, &QAction::triggered, this, [this]() {
        if (m_japanese) {
            toggleLanguage();
        }
    });

    auto* helpMenu = menuBar()->addMenu(uiText(QStringLiteral("help")));
    auto* quickStartAction = helpMenu->addAction(uiText(QStringLiteral("quick_start")));
    connect(quickStartAction, &QAction::triggered, this, &MainWindow::showStartupGuide);
    m_helpAction = helpMenu->addAction(uiText(QStringLiteral("usage")));
    m_helpAction->setShortcut(QKeySequence::HelpContents);
    connect(m_helpAction, &QAction::triggered, this, &MainWindow::showUsageHelp);
    m_aboutAction = helpMenu->addAction(uiText(QStringLiteral("about")));
    connect(m_aboutAction, &QAction::triggered, this, &MainWindow::showAboutDialog);

    new QShortcut(QKeySequence(Qt::Key_F), this, [this]() {
        if (!textInputHasFocus()) {
            fitStructure();
        }
    });
    new QShortcut(QKeySequence(Qt::Key_F1), this, [this]() { showUsageHelp(); });
    new QShortcut(QKeySequence(Qt::Key_Delete), this, [this]() {
        if (!textInputHasFocus()) {
            deleteSelectedAtoms();
        }
    });
    new QShortcut(QKeySequence(Qt::Key_Escape), this, [this]() {
        if (!textInputHasFocus()) {
            clearSelection();
        }
    });

    new QShortcut(QKeySequence(Qt::Key_A), this, [this]() {
        if (textInputHasFocus()) {
            return;
        }
        const auto [direction, up] = canonicalDirectViewDirection(m_structure.cellVectors, 0);
        m_canvas->setViewDirection(direction, up, true);
    });
    new QShortcut(QKeySequence(Qt::Key_B), this, [this]() {
        if (textInputHasFocus()) {
            return;
        }
        const auto [direction, up] = canonicalDirectViewDirection(m_structure.cellVectors, 1);
        m_canvas->setViewDirection(direction, up, true);
    });
    new QShortcut(QKeySequence(Qt::Key_C), this, [this]() {
        if (textInputHasFocus()) {
            return;
        }
        const auto [direction, up] = canonicalDirectViewDirection(m_structure.cellVectors, 2);
        m_canvas->setViewDirection(direction, up, true);
    });
    new QShortcut(QKeySequence("Ctrl+Alt+A"), this, [this]() {
        const auto [direction, up] = canonicalReciprocalViewDirection(m_structure.cellVectors, 0);
        m_canvas->setViewDirection(direction, up, true);
    });
    new QShortcut(QKeySequence("Ctrl+Alt+B"), this, [this]() {
        const auto [direction, up] = canonicalReciprocalViewDirection(m_structure.cellVectors, 1);
        m_canvas->setViewDirection(direction, up, true);
    });
    new QShortcut(QKeySequence("Ctrl+Alt+C"), this, [this]() {
        const auto [direction, up] = canonicalReciprocalViewDirection(m_structure.cellVectors, 2);
        m_canvas->setViewDirection(direction, up, true);
    });

    statusBar()->showMessage(uiText(QStringLiteral("open_hint")), 6000);
}
void MainWindow::applyTheme() {
    auto* app = qobject_cast<QApplication*>(QApplication::instance());
    if (app != nullptr) {
        app->setStyle("Fusion");
        app->setPalette(app->style()->standardPalette());
    }
}

bool MainWindow::loadStructureFile(const QString& path) { return loadFromPath(path); }

void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    if (m_initialCViewAppliedAfterShow) {
        return;
    }
    m_initialCViewAppliedAfterShow = true;
    QTimer::singleShot(0, this, [this]() { setCView(true); });
}

bool MainWindow::loadFromPath(const QString& path) {
    if (!QFileInfo(path).exists()) {
        QMessageBox::warning(this, "Open Structure", QStringLiteral("File not found: %1").arg(path));
        return false;
    }
    if (m_structure.dirty && !maybeSaveChanges()) {
        return false;
    }
    QString errorMessage;
    const auto loaded = m_loader->load(path, &errorMessage);
    if (!loaded.has_value()) {
        QMessageBox::warning(this, "Open Structure", errorMessage.isEmpty() ? "Failed to load structure." : errorMessage);
        return false;
    }
    m_structure = *loaded;
    m_structure.dirty = false;
    m_supercellBaseStructure = m_structure;
    m_hasSupercellBaseStructure = true;
    m_lastEditWasSupercell = false;
    m_supercellFactors = {1, 1, 1};
    m_undoStack.clear();
    m_redoStack.clear();
    m_poseGroups.clear();
    applyStructureState(m_structure);
    setCView(true);
    QTimer::singleShot(0, this, [this]() { setCView(true); });
    return true;
}

void MainWindow::applyStructureState(const StructureData& structure) {
    m_selectedAtomIds.clear();
    m_canvas->setStructure(structure);
    m_canvas->setSelectedAtomIds(m_selectedAtomIds);
    syncCanvasDisplayOptions();
    const QFileInfo info(structure.sourcePath);
    m_fileLabel->setText(structure.sourcePath.isEmpty() ? "Built-in / unsaved structure" : info.fileName());
    m_summaryLabel->setText(QString("Atoms: %1\nCell: %2 / %3 / %4\nDirty: %5")
        .arg(structure.atoms.size())
        .arg(formatVector(structure.cellVectors[0]))
        .arg(formatVector(structure.cellVectors[1]))
        .arg(formatVector(structure.cellVectors[2]))
        .arg(structure.dirty ? "yes" : "no"));
    if (m_supercellStatusLabel != nullptr) {
        m_supercellStatusLabel->setText(supercellStatusText());
    }
    refreshSelectionUi();
    refreshPresetUi();
    refreshPoseUi();
    updateUndoRedoActions();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) { if (event->mimeData()->hasUrls()) event->acceptProposedAction(); }

void MainWindow::dropEvent(QDropEvent* event) {
    for (const auto& url : event->mimeData()->urls()) {
        if (url.isLocalFile() && loadFromPath(url.toLocalFile())) {
            event->acceptProposedAction();
            return;
        }
    }
}

void MainWindow::openStructure() {
    const QString path = QFileDialog::getOpenFileName(this, "Open structure", defaultOpenDirectory(),
        "Structure files (*.aseproj *.json *.cif *.xyz *.extxyz *.vasp POSCAR CONTCAR *.poscar *.pdb *.xsf);;All files (*.*)");
    if (!path.isEmpty()) loadFromPath(path);
}

void MainWindow::showUsageHelp() {
    UsageHelpDialog dialog(m_japanese, this);
    dialog.exec();
}

void MainWindow::showStartupGuide() {
    StartupGuideDialog dialog(m_japanese, this);
    dialog.exec();
}

void MainWindow::showAboutDialog() {
    QMessageBox::information(
        this,
        m_japanese ? QStringLiteral("このアプリについて") : QStringLiteral("About ASEapp Surface Builder"),
        m_japanese
            ? QStringLiteral(
                "ASEapp Surface Builder\n\n"
                "・ドラッグ&ドロップで構造を読み込み\n"
                "・選択した原子に対して配置プリセットを適用\n"
                "・スーパーセル作成に対応\n"
                "・初期視点は c 方向です")
            : QStringLiteral(
                "ASEapp Surface Builder\n\n"
                "・Load structures by drag & drop or Open\n"
                "・Apply placement presets to selected atoms\n"
                "・Create supercells\n"
                "・Initial view is c-axis"));
}

void MainWindow::saveStructureAs() {
    QString selectedFilter;
    const QString path = QFileDialog::getSaveFileName(this, "Save structure", defaultOpenDirectory() + "/" +
        (m_structure.title.isEmpty() ? "surface_model.aseproj" : m_structure.title + ".aseproj"),
        "ASEapp project (*.aseproj *.json);;Extended XYZ for ASE (*.extxyz *.xyz);;Plain XYZ coordinates only (*.xyz);;VASP POSCAR (*.vasp POSCAR CONTCAR *.poscar)",
        &selectedFilter);
    if (path.isEmpty()) return;

    const QString diagnostics = structureDiagnosticsText(false);
    const auto saveReply = QMessageBox::question(
        this,
        m_japanese ? QStringLiteral("保存前チェック") : QStringLiteral("Pre-save check"),
        diagnostics + QStringLiteral("\n\n") + (m_japanese ? QStringLiteral("この内容で保存しますか？") : QStringLiteral("Save with these diagnostics?")),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::Yes);
    if (saveReply != QMessageBox::Yes) {
        return;
    }

    if (isPoscarPath(path)) {
        QString errorMessage;
        if (!writePoscarFile(m_structure, path, &errorMessage)) {
            QMessageBox::critical(this, "Save Structure", errorMessage);
            return;
        }
        m_structure.dirty = false;
        m_structure.sourcePath = path;
        applyStructureState(m_structure);
        statusBar()->showMessage(QStringLiteral("Saved POSCAR %1").arg(path), 4000);
        return;
    }

    if (isXyzPath(path)) {
        QString errorMessage;
        const bool extended = QFileInfo(path).suffix().compare(QStringLiteral("extxyz"), Qt::CaseInsensitive) == 0
            || selectedFilter.contains(QStringLiteral("Extended XYZ"), Qt::CaseInsensitive);
        if (!extended) {
            const auto reply = QMessageBox::warning(
                this,
                m_japanese ? QStringLiteral("通常XYZ保存") : QStringLiteral("Plain XYZ save"),
                m_japanese
                    ? QStringLiteral("通常XYZはセル情報とPBCを標準では保持しません。ASEへslabとして戻す用途では extended XYZ を推奨します。\nこのまま通常XYZで保存しますか？")
                    : QStringLiteral("Plain XYZ does not standardly preserve cell vectors or PBC. Extended XYZ is recommended for returning slabs to ASE.\nSave as plain XYZ anyway?"),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No);
            if (reply != QMessageBox::Yes) {
                return;
            }
        }
        if (!writeXyzFile(m_structure, path, extended, &errorMessage)) {
            QMessageBox::critical(this, "Save Structure", errorMessage);
            return;
        }
        m_structure.dirty = false;
        m_structure.sourcePath = path;
        applyStructureState(m_structure);
        statusBar()->showMessage(extended
            ? QStringLiteral("Saved extended XYZ for ASE %1").arg(path)
            : QStringLiteral("Saved plain XYZ %1").arg(path), 5000);
        return;
    }

    QJsonObject root;
    root["name"] = m_structure.title;
    root["source_path"] = m_structure.sourcePath;
    QJsonArray cellArray;
    for (const auto& vec : m_structure.cellVectors) cellArray.append(QJsonArray{vec.x(), vec.y(), vec.z()});
    root["cell_vectors"] = cellArray;
    QJsonArray atomsArray;
    for (const auto& atom : m_structure.atoms) {
        QJsonObject obj;
        obj["atom_id"] = atom.atomId;
        obj["element"] = atom.element;
        obj["tag"] = atom.tag;
        obj["fractional"] = QJsonArray{atom.fractional.x(), atom.fractional.y(), atom.fractional.z()};
        obj["cartesian"] = QJsonArray{atom.cartesian.x(), atom.cartesian.y(), atom.cartesian.z()};
        obj["movable"] = QJsonArray{atom.movable[0], atom.movable[1], atom.movable[2]};
        atomsArray.append(obj);
    }
    root["atoms"] = atomsArray;
    root["dirty"] = false;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::critical(this, "Save Structure", QStringLiteral("Failed to write %1").arg(path));
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    m_structure.dirty = false;
    m_structure.sourcePath = path;
    applyStructureState(m_structure);
    statusBar()->showMessage(QStringLiteral("Saved %1").arg(path), 4000);
}

void MainWindow::exportElementLegendImage() {
    const auto entries = elementLegendEntries(m_structure);
    if (entries.empty()) {
        QMessageBox::information(
            this,
            m_japanese ? QStringLiteral("原子一覧画像") : QStringLiteral("Atom legend image"),
            m_japanese ? QStringLiteral("現在の構造に原子がありません。") : QStringLiteral("The current structure has no atoms."));
        return;
    }

    ElementLegendExportDialog dialog(m_japanese, static_cast<int>(entries.size()), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString defaultName = QDir(defaultOpenDirectory()).filePath(QStringLiteral("atom_legend.png"));
    QString path = QFileDialog::getSaveFileName(
        this,
        m_japanese ? QStringLiteral("原子一覧画像を保存") : QStringLiteral("Save atom legend image"),
        defaultName,
        QStringLiteral("PNG image (*.png);;All files (*.*)"));
    if (path.isEmpty()) {
        return;
    }
    if (QFileInfo(path).suffix().isEmpty()) {
        path += QStringLiteral(".png");
    }

    const ElementLegendRenderOptions options = dialog.options();
    const QImage image = renderElementLegendImage(entries, options);
    if (!image.save(path, "PNG")) {
        QMessageBox::critical(
            this,
            m_japanese ? QStringLiteral("原子一覧画像") : QStringLiteral("Atom legend image"),
            m_japanese ? QStringLiteral("PNG保存に失敗しました: %1").arg(path) : QStringLiteral("Failed to save PNG: %1").arg(path));
        return;
    }

    statusBar()->showMessage(
        m_japanese
            ? QStringLiteral("原子一覧PNGを保存しました: %1").arg(path)
            : QStringLiteral("Saved atom legend PNG: %1").arg(path),
        5000);
}

void MainWindow::fitStructure() { m_canvas->fitToStructure(); }

void MainWindow::setCView(bool resetPan) {
    if (m_canvas == nullptr) {
        return;
    }
    const auto [direction, up] = canonicalDirectViewDirection(m_structure.cellVectors, 2);
    m_canvas->setViewDirection(direction, up, resetPan);
    if (resetPan) {
        m_canvas->fitToStructure();
    }
}

void MainWindow::resetView() { setCView(true); }

void MainWindow::toggleLanguage() {
    const bool wasFullScreen = isFullScreen();
    const bool wasMaximized = isMaximized();
    const QRect previousGeometry = geometry();

    m_japanese = !m_japanese;
    QSettings(QStringLiteral("ASEapp"), QStringLiteral("ASEappSurfaceBuilder"))
        .setValue(QStringLiteral("ui/japanese"), m_japanese);

    const auto selectedAtomIds = m_selectedAtomIds;
    for (auto* shortcut : findChildren<QShortcut*>(QString(), Qt::FindDirectChildrenOnly)) {
        shortcut->deleteLater();
    }
    for (auto* toolbar : findChildren<QToolBar*>(QString(), Qt::FindDirectChildrenOnly)) {
        removeToolBar(toolbar);
        toolbar->deleteLater();
    }
    menuBar()->clear();

    QWidget* previousCentral = centralWidget();
    if (previousCentral != nullptr) {
        previousCentral->setParent(nullptr);
        previousCentral->deleteLater();
    }

    buildUi();
    reloadPresetRegistry();
    applyStructureState(m_structure);
    setSelectedAtomIds(selectedAtomIds);
    refreshSelectionUi();
    refreshPresetUi();
    setCView(true);
    if (wasFullScreen) {
        showFullScreen();
    } else if (wasMaximized) {
        showMaximized();
    } else {
        setGeometry(previousGeometry);
    }
    statusBar()->showMessage(uiText(QStringLiteral("open_hint")), 3000);
}

void MainWindow::createSupercell() {
    if (!m_hasSupercellBaseStructure || !m_lastEditWasSupercell) {
        m_supercellBaseStructure = m_structure;
        m_hasSupercellBaseStructure = true;
        m_supercellFactors = {1, 1, 1};
    }
    SupercellDialog dialog(m_japanese, m_supercellBaseStructure, m_supercellFactors, this);
    if (dialog.exec() != QDialog::Accepted) return;
    const auto [a, b, c] = dialog.values();
    StructureData updated = makeSupercellStructure(m_supercellBaseStructure, a, b, c);
    updated.title = m_supercellBaseStructure.title;
    updated.sourcePath = m_supercellBaseStructure.sourcePath;
    m_supercellFactors = {a, b, c};
    replaceStructureFromEdit(updated, QStringLiteral("supercell"));
    statusBar()->showMessage(
        m_japanese
            ? QStringLiteral("基準構造に対して %1×%2×%3 のスーパーセルを生成しました。").arg(a).arg(b).arg(c)
            : QStringLiteral("Created %1×%2×%3 supercell relative to the base structure.").arg(a).arg(b).arg(c),
        4000);
}

void MainWindow::terminateHydrogen() {
    HydrogenTerminationDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) return;
    replaceStructureFromEdit(addHydrogenTermination(m_structure, dialog.bondLength(), dialog.top(), dialog.bottom(), dialog.layerThickness()), QStringLiteral("termination"));
}

void MainWindow::addVacuumLayer() {
    VacuumDialog dialog(m_japanese, this);
    if (dialog.exec() != QDialog::Accepted) return;
    const VacuumAdjustmentOptions options = dialog.options();
    if (m_lastEditWasSupercell && m_hasSupercellBaseStructure) {
        m_supercellBaseStructure = adjustVacuumAndSlab(m_supercellBaseStructure, options);
    }
    replaceStructureFromEdit(adjustVacuumAndSlab(m_structure, options), QStringLiteral("vacuum"));
}

void MainWindow::removeVacuumLayer() {
    if (m_structure.atoms.empty()) {
        QMessageBox::information(
            this,
            m_japanese ? QStringLiteral("真空層除去") : QStringLiteral("Remove vacuum"),
            m_japanese ? QStringLiteral("先に構造を読み込んでください。") : QStringLiteral("Load a structure first."));
        return;
    }

    VacuumAdjustmentOptions options;
    options.axisIndex = 2;
    options.fitTight = true;
    options.vacuumAngstrom = 0.0;
    options.placementMode = 0;
    if (m_lastEditWasSupercell && m_hasSupercellBaseStructure) {
        m_supercellBaseStructure = adjustVacuumAndSlab(m_supercellBaseStructure, options);
    }
    replaceStructureFromEdit(adjustVacuumAndSlab(m_structure, options), QStringLiteral("remove_vacuum"));
    statusBar()->showMessage(
        m_japanese
            ? QStringLiteral("c軸方向の真空層を除去し、セルを原子範囲に合わせました。")
            : QStringLiteral("Removed c-axis vacuum and fitted the cell to the atom bounds."),
        4000);
}

void MainWindow::tiltCellAxis() {
    CellAxisTiltDialog dialog(m_japanese, this);
    if (dialog.exec() != QDialog::Accepted) return;

    QString errorMessage;
    const CellAxisTiltOptions options = dialog.options();
    const StructureData updated = applyCellAxisTilt(m_structure, options, &errorMessage);
    if (!errorMessage.isEmpty()) {
        QMessageBox::warning(
            this,
            m_japanese ? QStringLiteral("セル軸傾き") : QStringLiteral("Cell-axis tilt"),
            errorMessage);
        return;
    }

    if (m_lastEditWasSupercell && m_hasSupercellBaseStructure) {
        QString baseErrorMessage;
        const StructureData updatedBase = applyCellAxisTilt(m_supercellBaseStructure, options, &baseErrorMessage);
        if (!baseErrorMessage.isEmpty()) {
            QMessageBox::warning(
                this,
                m_japanese ? QStringLiteral("セル軸傾き") : QStringLiteral("Cell-axis tilt"),
                baseErrorMessage);
            return;
        }
        m_supercellBaseStructure = updatedBase;
    }

    replaceStructureFromEdit(updated, QStringLiteral("axis_tilt"));
    const auto axisName = [](int axisIndex) {
        switch (std::clamp(axisIndex, 0, 2)) {
        case 0: return QStringLiteral("a");
        case 1: return QStringLiteral("b");
        default: return QStringLiteral("c");
        }
    };
    statusBar()->showMessage(
        m_japanese
            ? QStringLiteral("%1軸を%2方向へ %3° 傾けました。")
                .arg(axisName(options.targetAxisIndex))
                .arg(axisName(options.directionAxisIndex))
                .arg(options.angleDegrees, 0, 'f', 2)
            : QStringLiteral("Tilted %1 axis toward %2 by %3°.")
                .arg(axisName(options.targetAxisIndex))
                .arg(axisName(options.directionAxisIndex))
                .arg(options.angleDegrees, 0, 'f', 2),
        5000);
}

SurfacePlacementRule MainWindow::currentPlacementRule() const {
    SurfacePlacementRule rule;
    rule.element = m_elementCombo == nullptr ? QStringLiteral("H") : m_elementCombo->currentText().trimmed();
    rule.mode = m_placementModeCombo == nullptr
        ? QStringLiteral("single_above")
        : m_placementModeCombo->currentData().toString();
    if (rule.mode.trimmed().isEmpty()) {
        rule.mode = QStringLiteral("single_above");
    }
    rule.name = m_placementModeCombo == nullptr ? rule.mode : m_placementModeCombo->currentText();
    rule.description = rule.name;
    rule.height = m_placementHeightSpin == nullptr ? 1.05 : m_placementHeightSpin->value();
    rule.tiltDegrees = m_placementTiltSpin == nullptr ? 0.0 : m_placementTiltSpin->value();
    rule.fraction = m_placementFractionSpin == nullptr ? 0.5 : m_placementFractionSpin->value();
    rule.selectionCount = defaultSelectionCountForMode(rule.mode);
    const QString normalizedMode = rule.mode.trimmed().toLower();
    if (normalizedMode == QStringLiteral("multi_centroid") || normalizedMode == QStringLiteral("multi_weighted")) {
        rule.selectionCount = 1;
    } else if (normalizedMode == QStringLiteral("multi_plane_normal")) {
        rule.selectionCount = 3;
    }
    return rule;
}

void MainWindow::choosePlacementElement() {
    if (m_elementCombo == nullptr) {
        return;
    }
    PeriodicElementDialog dialog(m_japanese, m_elementCombo->currentText().trimmed(), this);
    if (dialog.exec() != QDialog::Accepted || dialog.selectedElement().isEmpty()) {
        return;
    }
    const QString element = dialog.selectedElement();
    if (m_elementCombo->findText(element, Qt::MatchFixedString) < 0) {
        m_elementCombo->addItem(element);
    }
    m_elementCombo->setCurrentText(element);
    refreshPresetUi();
    statusBar()->showMessage(
        m_japanese
            ? QStringLiteral("生成元素を %1 にしました。").arg(element)
            : QStringLiteral("Placement element set to %1.").arg(element),
        2500);
}

void MainWindow::applySelectedPreset() {
    if (m_placementModeCombo == nullptr || m_placementModeCombo->currentIndex() < 0) {
        QMessageBox::information(this, m_japanese ? QStringLiteral("原子配置") : QStringLiteral("Atom placement"),
            m_japanese ? QStringLiteral("配置位置が選択されていません。") : QStringLiteral("No placement option is selected."));
        return;
    }
    const auto rule = currentPlacementRule();
    const int required = rule.selectionCount > 0 ? rule.selectionCount : defaultSelectionCountForMode(rule.mode);
    if (static_cast<int>(m_selectedAtomIds.size()) < required) {
        QMessageBox::warning(this, m_japanese ? QStringLiteral("原子配置") : QStringLiteral("Atom placement"),
            (m_japanese
                ? QStringLiteral("「%1」には %2 個以上の原子選択が必要です。")
                : QStringLiteral("\"%1\" requires %2 selected atom(s)."))
            .arg(rule.name.isEmpty() ? rule.mode : rule.name)
            .arg(required));
        return;
    }
    QString errorMessage;
    const int previousAtomCount = static_cast<int>(m_structure.atoms.size());
    const auto updated = addPlacementAtom(m_structure, m_selectedAtomIds, rule, &errorMessage);
    if (!errorMessage.isEmpty() && updated.atoms.size() == m_structure.atoms.size()) {
        QMessageBox::warning(this, m_japanese ? QStringLiteral("原子配置") : QStringLiteral("Atom placement"), errorMessage);
        return;
    }
    replaceStructureFromEdit(updated, QStringLiteral("placement"));
    if (static_cast<int>(m_structure.atoms.size()) > previousAtomCount && !m_structure.atoms.empty()) {
        std::vector<int> addedAtomIds;
        for (int i = previousAtomCount; i < static_cast<int>(m_structure.atoms.size()); ++i) {
            addedAtomIds.push_back(m_structure.atoms[static_cast<std::size_t>(i)].atomId);
        }
        setSelectedAtomIds(addedAtomIds);
        refreshSelectionUi();
        refreshPresetUi();
    }
    const int addedCount = static_cast<int>(m_structure.atoms.size()) - previousAtomCount;
    statusBar()->showMessage(
        m_japanese
            ? QStringLiteral("%1 を %2 個配置しました").arg(rule.element).arg(std::max(addedCount, 1))
            : QStringLiteral("Placed %1 %2 time(s)").arg(rule.element).arg(std::max(addedCount, 1)),
        4000);
}

void MainWindow::saveSelectedPrecursorCsv() {
    if (m_selectedAtomIds.empty()) {
        QMessageBox::information(this,
            m_japanese ? QStringLiteral("前駆体CSV保存") : QStringLiteral("Save precursor CSV"),
            m_japanese ? QStringLiteral("CSV化する前駆体原子を選択してください。") : QStringLiteral("Select precursor atoms to save as CSV."));
        return;
    }

    std::vector<const NativeAtom*> selectedAtoms;
    selectedAtoms.reserve(m_selectedAtomIds.size());
    QStringList suggestedElements;
    for (int atomId : m_selectedAtomIds) {
        const NativeAtom* atom = findAtomByIdInStructure(m_structure, atomId);
        if (atom != nullptr) {
            selectedAtoms.push_back(atom);
            const QString element = atom->element.trimmed();
            if (!element.isEmpty() && !suggestedElements.contains(element, Qt::CaseInsensitive)) {
                suggestedElements << element;
            }
        }
    }
    if (selectedAtoms.empty()) {
        QMessageBox::warning(this,
            m_japanese ? QStringLiteral("前駆体CSV保存") : QStringLiteral("Save precursor CSV"),
            m_japanese ? QStringLiteral("選択原子が現在の構造内に見つかりません。") : QStringLiteral("Selected atoms were not found in the current structure."));
        return;
    }

    const QString defaultName = suggestedElements.isEmpty()
        ? QStringLiteral("Precursor")
        : suggestedElements.join(QStringLiteral("-"));
    bool nameAccepted = false;
    const QString precursorName = QInputDialog::getText(this,
        m_japanese ? QStringLiteral("前駆体名") : QStringLiteral("Precursor name"),
        m_japanese ? QStringLiteral("保存する前駆体の名称:") : QStringLiteral("Name for this precursor:"),
        QLineEdit::Normal,
        defaultName,
        &nameAccepted).trimmed();
    if (!nameAccepted) {
        return;
    }
    if (precursorName.isEmpty()) {
        QMessageBox::warning(this,
            m_japanese ? QStringLiteral("前駆体CSV保存") : QStringLiteral("Save precursor CSV"),
            m_japanese ? QStringLiteral("前駆体名を入力してください。") : QStringLiteral("Enter a precursor name."));
        return;
    }

    const QString invalidChars = QStringLiteral("<>:/\\|?*");
    QString safeFileName;
    safeFileName.reserve(precursorName.size());
    for (const QChar ch : precursorName) {
        safeFileName.append(ch == QLatin1Char('"') || invalidChars.contains(ch) ? QLatin1Char('_') : ch);
    }
    if (safeFileName.trimmed().isEmpty()) {
        safeFileName = QStringLiteral("precursor");
    }

    const QString suggested = defaultOpenDirectory() + QStringLiteral("/") + safeFileName + QStringLiteral(".csv");
    const QString path = QFileDialog::getSaveFileName(this,
        m_japanese ? QStringLiteral("前駆体CSVを保存") : QStringLiteral("Save precursor CSV"),
        suggested,
        QStringLiteral("CSV (*.csv);;All files (*.*)"));
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this,
            m_japanese ? QStringLiteral("前駆体CSV保存") : QStringLiteral("Save precursor CSV"),
            m_japanese ? QStringLiteral("書き込みに失敗しました: %1").arg(path) : QStringLiteral("Failed to write: %1").arg(path));
        return;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << "name,element,dx_angstrom,dy_angstrom,dz_angstrom\n";

    const QVector3D anchor = lowestPrecursorOrigin(selectedAtoms);
    PrecursorTemplate savedTemplate;
    savedTemplate.name = precursorName;
    savedTemplate.sourcePath = path;
    savedTemplate.atoms.reserve(selectedAtoms.size());
    for (const auto* selected : selectedAtoms) {
        const QVector3D offset = selected->cartesian - anchor;
        const QString element = selected->element.trimmed().isEmpty() ? QStringLiteral("H") : selected->element.trimmed();
        out << csvEscaped(precursorName) << ','
            << csvEscaped(element) << ','
            << csvNumber(offset.x()) << ','
            << csvNumber(offset.y()) << ','
            << csvNumber(offset.z()) << '\n';

        NativeAtom templateAtom;
        templateAtom.atomId = static_cast<int>(savedTemplate.atoms.size()) + 1;
        templateAtom.element = element;
        templateAtom.tag = QString();
        templateAtom.cartesian = offset;
        templateAtom.fractional = offset;
        templateAtom.color = vestaElementColor(templateAtom.element);
        templateAtom.radius = vestaElementRadius(templateAtom.element);
        savedTemplate.atoms.push_back(templateAtom);
    }

    auto precursors = m_loadedPrecursors;
    precursors.erase(std::remove_if(precursors.begin(), precursors.end(), [&precursorName](const PrecursorTemplate& precursor) {
        return precursor.name.compare(precursorName, Qt::CaseInsensitive) == 0;
    }), precursors.end());
    const int savedAtomCount = static_cast<int>(savedTemplate.atoms.size());
    precursors.push_back(std::move(savedTemplate));
    setLoadedPrecursors(std::move(precursors), precursorName);
    statusBar()->showMessage(
        m_japanese
            ? QStringLiteral("前駆体CSVを保存しました: %1（%2 原子）").arg(precursorName).arg(savedAtomCount)
            : QStringLiteral("Saved precursor CSV: %1 (%2 atom(s))").arg(precursorName).arg(savedAtomCount),
        4000);
}

void MainWindow::loadPrecursorCsv() {
    const QString path = QFileDialog::getOpenFileName(this,
        m_japanese ? QStringLiteral("前駆体CSVを読み込み") : QStringLiteral("Load precursor CSV"),
        defaultOpenDirectory(),
        QStringLiteral("CSV (*.csv);;All files (*.*)"));
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this,
            m_japanese ? QStringLiteral("前駆体CSV読込") : QStringLiteral("Load precursor CSV"),
            m_japanese ? QStringLiteral("読み込みに失敗しました: %1").arg(path) : QStringLiteral("Failed to read: %1").arg(path));
        return;
    }

    const QString fallbackName = QFileInfo(path).completeBaseName().trimmed().isEmpty()
        ? QStringLiteral("Precursor")
        : QFileInfo(path).completeBaseName().trimmed();
    auto normalizeHeader = [](QString value) {
        value.remove(QChar(0xfeff));
        value = value.trimmed().toLower();
        value.replace(QLatin1Char('-'), QLatin1Char('_'));
        value.replace(QLatin1Char(' '), QLatin1Char('_'));
        return value;
    };
    auto findColumn = [&normalizeHeader](const QStringList& headers, const QStringList& names) {
        for (int i = 0; i < headers.size(); ++i) {
            const QString header = normalizeHeader(headers.at(i));
            for (const QString& name : names) {
                if (header == name) {
                    return i;
                }
            }
        }
        return -1;
    };

    auto formatError = [this](int lineNumber, const QString& expected) {
        QMessageBox::warning(this,
            m_japanese ? QStringLiteral("前駆体CSV読込") : QStringLiteral("Load precursor CSV"),
            m_japanese
                ? QStringLiteral("%1 行目は %2 を指定してください。").arg(lineNumber).arg(expected)
                : QStringLiteral("Line %1 must contain %2.").arg(lineNumber).arg(expected));
    };

    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);
    std::vector<PrecursorTemplate> loaded;
    QStringList headers;
    bool hasHeader = false;
    int nameIndex = -1;
    int elementIndex = -1;
    int dxIndex = -1;
    int dyIndex = -1;
    int dzIndex = -1;
    int tagIndex = -1;
    int lineNumber = 0;
    int atomCount = 0;

    auto findOrAddPrecursor = [&loaded, &fallbackName, &path](const QString& rawName) -> PrecursorTemplate* {
        QString name = rawName.trimmed();
        name.remove(QChar(0xfeff));
        if (name.isEmpty()) {
            name = fallbackName;
        }
        auto it = std::find_if(loaded.begin(), loaded.end(), [&name](const PrecursorTemplate& precursor) {
            return precursor.name.compare(name, Qt::CaseInsensitive) == 0;
        });
        if (it == loaded.end()) {
            PrecursorTemplate precursor;
            precursor.name = name;
            precursor.sourcePath = path;
            loaded.push_back(std::move(precursor));
            return &loaded.back();
        }
        return &(*it);
    };

    while (!in.atEnd()) {
        const QString rawLine = in.readLine();
        ++lineNumber;
        const QString trimmedLine = rawLine.trimmed();
        if (trimmedLine.isEmpty() || trimmedLine.startsWith(QLatin1Char('#'))) {
            continue;
        }

        bool csvOk = true;
        const QStringList cells = parseCsvLine(rawLine, &csvOk);
        if (!csvOk || cells.size() < 4) {
            QMessageBox::warning(this,
                m_japanese ? QStringLiteral("前駆体CSV読込") : QStringLiteral("Load precursor CSV"),
                m_japanese
                    ? QStringLiteral("%1 行目のCSV形式を確認してください。").arg(lineNumber)
                    : QStringLiteral("Check CSV formatting at line %1.").arg(lineNumber));
            return;
        }

        const QString firstHeader = normalizeHeader(cells.value(0));
        if (!hasHeader && (firstHeader == QStringLiteral("name")
                || firstHeader == QStringLiteral("precursor")
                || firstHeader == QStringLiteral("precursor_name")
                || firstHeader == QStringLiteral("element"))) {
            headers = cells;
            hasHeader = true;
            nameIndex = findColumn(headers, QStringList{QStringLiteral("name"), QStringLiteral("precursor"), QStringLiteral("precursor_name")});
            elementIndex = findColumn(headers, QStringList{QStringLiteral("element"), QStringLiteral("symbol")});
            dxIndex = findColumn(headers, QStringList{QStringLiteral("dx_angstrom"), QStringLiteral("dx"), QStringLiteral("x")});
            dyIndex = findColumn(headers, QStringList{QStringLiteral("dy_angstrom"), QStringLiteral("dy"), QStringLiteral("y")});
            dzIndex = findColumn(headers, QStringList{QStringLiteral("dz_angstrom"), QStringLiteral("dz"), QStringLiteral("z")});
            tagIndex = findColumn(headers, QStringList{QStringLiteral("tag"), QStringLiteral("label")});
            if (elementIndex < 0 || dxIndex < 0 || dyIndex < 0 || dzIndex < 0) {
                formatError(lineNumber, nameIndex >= 0
                    ? QStringLiteral("name,element,dx_angstrom,dy_angstrom,dz_angstrom")
                    : QStringLiteral("element,dx_angstrom,dy_angstrom,dz_angstrom"));
                return;
            }
            continue;
        }

        QString precursorName = fallbackName;
        QString element;
        QString tag;
        double dx = 0.0;
        double dy = 0.0;
        double dz = 0.0;
        bool okX = false;
        bool okY = false;
        bool okZ = false;

        if (hasHeader) {
            const int requiredMaxIndex = std::max({elementIndex, dxIndex, dyIndex, dzIndex, nameIndex});
            if (requiredMaxIndex >= cells.size()) {
                formatError(lineNumber, nameIndex >= 0
                    ? QStringLiteral("name,element,dx_angstrom,dy_angstrom,dz_angstrom")
                    : QStringLiteral("element,dx_angstrom,dy_angstrom,dz_angstrom"));
                return;
            }
            precursorName = nameIndex >= 0 ? cells.value(nameIndex).trimmed() : fallbackName;
            element = cells.value(elementIndex).trimmed();
            dx = cells.value(dxIndex).toDouble(&okX);
            dy = cells.value(dyIndex).toDouble(&okY);
            dz = cells.value(dzIndex).toDouble(&okZ);
            if (tagIndex >= 0 && tagIndex < cells.size()) {
                tag = cells.value(tagIndex).trimmed();
            }
        } else {
            if (cells.size() >= 4) {
                const double oldDx = cells.value(1).toDouble(&okX);
                const double oldDy = cells.value(2).toDouble(&okY);
                const double oldDz = cells.value(3).toDouble(&okZ);
                if (okX && okY && okZ) {
                    element = cells.value(0).trimmed();
                    dx = oldDx;
                    dy = oldDy;
                    dz = oldDz;
                    if (cells.size() >= 5) {
                        tag = cells.value(4).trimmed();
                    }
                } else if (cells.size() >= 5) {
                    okX = okY = okZ = false;
                    precursorName = cells.value(0).trimmed();
                    element = cells.value(1).trimmed();
                    dx = cells.value(2).toDouble(&okX);
                    dy = cells.value(3).toDouble(&okY);
                    dz = cells.value(4).toDouble(&okZ);
                }
            }
        }

        if (element.isEmpty() || !okX || !okY || !okZ) {
            formatError(lineNumber, hasHeader && nameIndex < 0
                ? QStringLiteral("element,dx_angstrom,dy_angstrom,dz_angstrom")
                : QStringLiteral("name,element,dx_angstrom,dy_angstrom,dz_angstrom"));
            return;
        }

        PrecursorTemplate* targetTemplate = findOrAddPrecursor(precursorName);
        NativeAtom atom;
        atom.atomId = static_cast<int>(targetTemplate->atoms.size()) + 1;
        atom.element = element;
        atom.tag = tag;
        atom.cartesian = QVector3D(static_cast<float>(dx), static_cast<float>(dy), static_cast<float>(dz));
        atom.fractional = atom.cartesian;
        atom.color = vestaElementColor(atom.element);
        atom.radius = vestaElementRadius(atom.element);
        targetTemplate->atoms.push_back(atom);
        ++atomCount;
    }

    if (loaded.empty() || atomCount <= 0) {
        QMessageBox::warning(this,
            m_japanese ? QStringLiteral("前駆体CSV読込") : QStringLiteral("Load precursor CSV"),
            m_japanese ? QStringLiteral("前駆体原子がCSV内にありません。") : QStringLiteral("No precursor atoms were found in the CSV."));
        return;
    }

    const QString preferredName = loaded.front().name;
    const int templateCount = static_cast<int>(loaded.size());
    setLoadedPrecursors(std::move(loaded), preferredName);
    statusBar()->showMessage(
        m_japanese
            ? QStringLiteral("前駆体CSVを読み込みました: %1 種類 / %2 原子").arg(templateCount).arg(atomCount)
            : QStringLiteral("Loaded precursor CSV: %1 template(s) / %2 atom(s)").arg(templateCount).arg(atomCount),
        4000);
}

void MainWindow::placeLoadedPrecursor() {
    const PrecursorTemplate* precursor = currentPrecursorTemplate();
    if (precursor == nullptr || precursor->atoms.empty()) {
        QMessageBox::information(this,
            m_japanese ? QStringLiteral("前駆体配置") : QStringLiteral("Place precursor"),
            m_japanese ? QStringLiteral("先に前駆体CSVを読み込むか保存してください。") : QStringLiteral("Load or save a precursor CSV first."));
        return;
    }
    if (m_selectedAtomIds.empty()) {
        QMessageBox::information(this,
            m_japanese ? QStringLiteral("前駆体配置") : QStringLiteral("Place precursor"),
            m_japanese ? QStringLiteral("配置先の原子を選択してください。") : QStringLiteral("Select target atoms for placement."));
        return;
    }

    const SurfacePlacementRule rule = currentPlacementRule();
    const int required = rule.selectionCount > 0 ? rule.selectionCount : defaultSelectionCountForMode(rule.mode);
    if (static_cast<int>(m_selectedAtomIds.size()) < required) {
        QMessageBox::warning(this,
            m_japanese ? QStringLiteral("前駆体配置") : QStringLiteral("Place precursor"),
            (m_japanese
                ? QStringLiteral("現在の配置位置「%1」には %2 個以上の原子選択が必要です。")
                : QStringLiteral("The current placement option \"%1\" requires %2 selected atom(s)."))
            .arg(rule.name.isEmpty() ? rule.mode : rule.name)
            .arg(required));
        return;
    }

    QString errorMessage;
    const int baseAtomCount = static_cast<int>(m_structure.atoms.size());
    const StructureData placementPreview = addPlacementAtom(m_structure, m_selectedAtomIds, rule, &errorMessage);
    std::vector<QVector3D> placementPositions;
    if (static_cast<int>(placementPreview.atoms.size()) > baseAtomCount) {
        placementPositions.reserve(placementPreview.atoms.size() - static_cast<std::size_t>(baseAtomCount));
        for (auto it = placementPreview.atoms.begin() + static_cast<std::ptrdiff_t>(baseAtomCount);
             it != placementPreview.atoms.end();
             ++it) {
            placementPositions.push_back(it->cartesian);
        }
    }
    if (!errorMessage.isEmpty() || placementPositions.empty()) {
        QMessageBox::warning(this,
            m_japanese ? QStringLiteral("前駆体配置") : QStringLiteral("Place precursor"),
            errorMessage.isEmpty()
                ? (m_japanese ? QStringLiteral("現在の配置位置を計算できませんでした。") : QStringLiteral("Could not calculate the current placement position."))
                : errorMessage);
        return;
    }

    StructureData updated = m_structure;
    int nextId = nextAtomIdForStructure(updated);
    const QVector3D precursorOrigin = lowestPrecursorOrigin(precursor->atoms);
    std::vector<int> addedAtomIds;
    addedAtomIds.reserve(placementPositions.size() * precursor->atoms.size());

    for (const QVector3D& anchor : placementPositions) {
        for (const auto& templateAtom : precursor->atoms) {
            const QString element = templateAtom.element.trimmed().isEmpty() ? QStringLiteral("H") : templateAtom.element.trimmed();
            NativeAtom atom;
            atom.atomId = nextId++;
            atom.element = element;
            atom.tag = QString("%1-%2").arg(atom.element).arg(atom.atomId, 4, 10, QChar('0'));
            atom.cartesian = anchor + (templateAtom.cartesian - precursorOrigin);
            atom.fractional = solveFractionalForCell(updated.cellVectors, atom.cartesian);
            atom.color = vestaElementColor(atom.element);
            atom.radius = vestaElementRadius(atom.element);
            updated.atoms.push_back(atom);
            addedAtomIds.push_back(atom.atomId);
        }
    }
    updated.dirty = true;

    replaceStructureFromEdit(updated, QStringLiteral("precursor"));
    if (!addedAtomIds.empty()) {
        setSelectedAtomIds(addedAtomIds);
        refreshSelectionUi();
        refreshPresetUi();
    }
    statusBar()->showMessage(
        m_japanese
            ? QStringLiteral("%1 を %2 箇所へ配置しました（追加 %3 原子）").arg(precursor->name).arg(placementPositions.size()).arg(addedAtomIds.size())
            : QStringLiteral("Placed %1 at %2 position(s), adding %3 atom(s)").arg(precursor->name).arg(placementPositions.size()).arg(addedAtomIds.size()),
        5000);
}

void MainWindow::createPoseGroupFromSelection() {
    std::vector<const NativeAtom*> selectedAtoms;
    selectedAtoms.reserve(m_selectedAtomIds.size());
    QStringList elements;
    for (int atomId : m_selectedAtomIds) {
        if (const NativeAtom* atom = findAtomByIdInStructure(m_structure, atomId)) {
            selectedAtoms.push_back(atom);
            if (!elements.contains(atom->element, Qt::CaseInsensitive)) {
                elements << atom->element;
            }
        }
    }
    if (selectedAtoms.size() < 2) {
        QMessageBox::information(this,
            m_japanese ? QStringLiteral("吸着分子ポーズ編集") : QStringLiteral("Adsorbate pose editor"),
            m_japanese ? QStringLiteral("剛体グループ化する吸着分子原子を2個以上選択してください。") : QStringLiteral("Select at least two adsorbate atoms to create a rigid pose group."));
        return;
    }

    const QString defaultName = elements.isEmpty()
        ? QStringLiteral("Adsorbate")
        : elements.join(QStringLiteral("-"));
    bool accepted = false;
    const QString name = QInputDialog::getText(this,
        m_japanese ? QStringLiteral("吸着分子グループ名") : QStringLiteral("Adsorbate group name"),
        m_japanese ? QStringLiteral("グループ名:") : QStringLiteral("Group name:"),
        QLineEdit::Normal,
        defaultName,
        &accepted).trimmed();
    if (!accepted) {
        return;
    }

    PoseGroup group;
    group.name = name.isEmpty() ? defaultName : name;
    group.atomIds.reserve(selectedAtoms.size());
    group.initialCartesian.reserve(selectedAtoms.size());
    for (const NativeAtom* atom : selectedAtoms) {
        group.atomIds.push_back(atom->atomId);
        group.initialCartesian.push_back(atom->cartesian);
    }
    group.pivotAtomId = group.atomIds.front();
    group.rotationQuaternion = QVector4D(1.0f, 0.0f, 0.0f, 0.0f);
    m_poseGroups.push_back(std::move(group));
    refreshPoseUi();
    const int row = m_poseGroupCombo != nullptr ? m_poseGroupCombo->findData(static_cast<int>(m_poseGroups.size()) - 1) : -1;
    if (row >= 0) {
        m_poseGroupCombo->setCurrentIndex(row);
    }
    refreshPoseUi();
    statusBar()->showMessage(m_japanese
        ? QStringLiteral("吸着分子ポーズグループを作成しました。以後は剛体として並進・回転します。")
        : QStringLiteral("Created adsorbate pose group. Translation and rotation will keep it rigid."), 5000);
}

void MainWindow::applyPoseTranslation() {
    PoseGroup* group = currentPoseGroup();
    if (group == nullptr) {
        refreshPoseUi();
        return;
    }
    const QVector3D totalDelta = currentPoseTranslationDelta();
    if (totalDelta.lengthSquared() <= 1.0e-12f) {
        statusBar()->showMessage(m_japanese ? QStringLiteral("並進量が0です。") : QStringLiteral("Translation is zero."), 3000);
        return;
    }

    pushUndoState(QStringLiteral("adsorbate_pose_translate"));
    int movedCount = 0;
    for (auto& atom : m_structure.atoms) {
        if (std::find(group->atomIds.begin(), group->atomIds.end(), atom.atomId) == group->atomIds.end()) {
            continue;
        }
        atom.cartesian += totalDelta;
        atom.fractional = solveFractionalForCell(m_structure.cellVectors, atom.cartesian);
        ++movedCount;
    }
    group->translation += totalDelta;
    m_structure.dirty = true;
    m_canvas->setStructure(m_structure);
    setSelectedAtomIds(group->atomIds);
    syncCanvasDisplayOptions();
    refreshSelectionUi();
    if (m_summaryLabel != nullptr) {
        m_summaryLabel->setText(QString("Atoms: %1\nCell: %2 / %3 / %4\nDirty: %5")
            .arg(m_structure.atoms.size())
            .arg(formatVector(m_structure.cellVectors[0]))
            .arg(formatVector(m_structure.cellVectors[1]))
            .arg(formatVector(m_structure.cellVectors[2]))
            .arg(m_structure.dirty ? "yes" : "no"));
    }
    if (m_poseDxSpin != nullptr) m_poseDxSpin->setValue(0.0);
    if (m_poseDySpin != nullptr) m_poseDySpin->setValue(0.0);
    if (m_poseDzSpin != nullptr) m_poseDzSpin->setValue(0.0);
    if (m_poseCellASpin != nullptr) m_poseCellASpin->setValue(0.0);
    if (m_poseCellBSpin != nullptr) m_poseCellBSpin->setValue(0.0);
    if (m_poseCellCSpin != nullptr) m_poseCellCSpin->setValue(0.0);
    if (m_poseNormalSpin != nullptr) m_poseNormalSpin->setValue(0.0);
    updatePreviewAtoms();
    statusBar()->showMessage(m_japanese
        ? QStringLiteral("吸着分子グループを剛体並進しました（%1 原子）。").arg(movedCount)
        : QStringLiteral("Translated adsorbate group rigidly (%1 atom(s)).").arg(movedCount), 5000);
}

void MainWindow::applyPoseRotation() {
    PoseGroup* group = currentPoseGroup();
    if (group == nullptr) {
        refreshPoseUi();
        return;
    }
    const double angle = m_poseAngleSpin != nullptr ? m_poseAngleSpin->value() : 0.0;
    if (std::abs(angle) <= 1.0e-10) {
        statusBar()->showMessage(m_japanese ? QStringLiteral("回転角が0です。") : QStringLiteral("Rotation angle is zero."), 3000);
        return;
    }
    const QString axisKey = m_poseAxisCombo != nullptr
        ? m_poseAxisCombo->currentData().toString()
        : QStringLiteral("slab_normal");
    std::array<const NativeAtom*, 2> selectedBond{nullptr, nullptr};
    if (isSelectedBondAxisKey(axisKey)) {
        selectedBond = selectedPoseBondAtoms(*group);
        if (selectedBond[0] == nullptr || selectedBond[1] == nullptr) {
            QMessageBox::warning(this,
                m_japanese ? QStringLiteral("吸着分子ポーズ編集") : QStringLiteral("Adsorbate pose editor"),
                m_japanese
                    ? QStringLiteral("結合軸回転には、同じ吸着分子グループ内で軸にする2原子を選択してください。")
                    : QStringLiteral("Select the two axis atoms within the same adsorbate pose group before rotating around a bond axis."));
            return;
        }
    }
    const QVector3D axis = poseRotationAxis(*group);
    if (axis.lengthSquared() <= 1.0e-12f) {
        QMessageBox::warning(this,
            m_japanese ? QStringLiteral("吸着分子ポーズ編集") : QStringLiteral("Adsorbate pose editor"),
            m_japanese ? QStringLiteral("回転軸を決定できません。") : QStringLiteral("Could not determine a rotation axis."));
        return;
    }
    const QVector3D pivot = isSelectedBondAxisKey(axisKey) && selectedBond[0] != nullptr
        ? selectedBond[0]->cartesian
        : posePivotPosition(*group);
    const QQuaternion rotation = QQuaternion::fromAxisAndAngle(axis.normalized(), static_cast<float>(angle));

    const QString pivotKey = m_posePivotCombo != nullptr ? m_posePivotCombo->currentData().toString() : QString();
    if (isSelectedBondAxisKey(axisKey) && selectedBond[0] != nullptr) {
        group->pivotAtomId = selectedBond[0]->atomId;
    } else if (pivotKey.startsWith(QStringLiteral("atom:"))) {
        bool ok = false;
        const int pivotId = pivotKey.mid(5).toInt(&ok);
        if (ok) {
            group->pivotAtomId = pivotId;
        }
    }

    pushUndoState(QStringLiteral("adsorbate_pose_rotate"));
    int rotatedCount = 0;
    for (auto& atom : m_structure.atoms) {
        if (std::find(group->atomIds.begin(), group->atomIds.end(), atom.atomId) == group->atomIds.end()) {
            continue;
        }
        atom.cartesian = pivot + rotation.rotatedVector(atom.cartesian - pivot);
        atom.fractional = solveFractionalForCell(m_structure.cellVectors, atom.cartesian);
        ++rotatedCount;
    }
    const QQuaternion previous(group->rotationQuaternion.x(), group->rotationQuaternion.y(), group->rotationQuaternion.z(), group->rotationQuaternion.w());
    const QQuaternion combined = rotation * previous;
    group->rotationQuaternion = QVector4D(combined.scalar(), combined.x(), combined.y(), combined.z());
    m_structure.dirty = true;
    m_canvas->setStructure(m_structure);
    setSelectedAtomIds(group->atomIds);
    syncCanvasDisplayOptions();
    refreshSelectionUi();
    if (m_poseAngleSpin != nullptr) m_poseAngleSpin->setValue(0.0);
    updatePreviewAtoms();
    statusBar()->showMessage(m_japanese
        ? QStringLiteral("pivot 固定で吸着分子グループを剛体回転しました（%1 原子）。").arg(rotatedCount)
        : QStringLiteral("Rotated adsorbate group rigidly around the pivot (%1 atom(s)).").arg(rotatedCount), 5000);
}

void MainWindow::applyPoseBondLength() {
    PoseGroup* group = currentPoseGroup();
    if (group == nullptr) {
        refreshPoseUi();
        return;
    }

    const double targetLength = m_poseBondLengthSpin != nullptr ? m_poseBondLengthSpin->value() : 0.0;
    PoseBondLengthPlan plan;
    QString planError;
    if (!buildPoseBondLengthPlan(*group, targetLength, &plan, &planError)) {
        QMessageBox::warning(this,
            m_japanese ? QStringLiteral("結合長調整") : QStringLiteral("Bond length adjustment"),
            planError);
        return;
    }
    if (plan.delta.lengthSquared() <= 1.0e-12f) {
        statusBar()->showMessage(m_japanese ? QStringLiteral("結合長は既に指定値です。") : QStringLiteral("Bond length already matches the target."), 3000);
        return;
    }

    const auto atoms = poseAtoms(*group);
    pushUndoState(QStringLiteral("adsorbate_pose_bond_length"));
    int movedCount = 0;
    for (const NativeAtom* sourceAtom : atoms) {
        if (sourceAtom == nullptr
            || std::find(plan.movableAtomIds.begin(), plan.movableAtomIds.end(), sourceAtom->atomId) == plan.movableAtomIds.end()) {
            continue;
        }
        for (auto& atom : m_structure.atoms) {
            if (atom.atomId != sourceAtom->atomId) {
                continue;
            }
            atom.cartesian += plan.delta;
            atom.fractional = solveFractionalForCell(m_structure.cellVectors, atom.cartesian);
            ++movedCount;
            break;
        }
    }
    m_structure.dirty = true;
    m_canvas->setStructure(m_structure);
    setSelectedAtomIds(plan.selectedAtomIds);
    syncCanvasDisplayOptions();
    refreshSelectionUi();
    updatePreviewAtoms();
    statusBar()->showMessage(m_japanese
        ? QStringLiteral("結合長を %1 Å に調整しました（可動側 %2 原子）。").arg(plan.targetLength, 0, 'f', 3).arg(movedCount)
        : QStringLiteral("Adjusted bond length to %1 Å (moved %2 atom(s)).").arg(plan.targetLength, 0, 'f', 3).arg(movedCount), 5000);
}

void MainWindow::resetPoseGroup() {
    PoseGroup* group = currentPoseGroup();
    if (group == nullptr || group->atomIds.empty()) {
        refreshPoseUi();
        return;
    }
    pushUndoState(QStringLiteral("adsorbate_pose_reset"));
    int resetCount = 0;
    for (std::size_t i = 0; i < group->atomIds.size() && i < group->initialCartesian.size(); ++i) {
        const int atomId = group->atomIds[i];
        for (auto& atom : m_structure.atoms) {
            if (atom.atomId != atomId) {
                continue;
            }
            atom.cartesian = group->initialCartesian[i];
            atom.fractional = solveFractionalForCell(m_structure.cellVectors, atom.cartesian);
            ++resetCount;
            break;
        }
    }
    group->translation = {};
    group->rotationQuaternion = QVector4D(1.0f, 0.0f, 0.0f, 0.0f);
    m_structure.dirty = true;
    m_canvas->setStructure(m_structure);
    setSelectedAtomIds(group->atomIds);
    syncCanvasDisplayOptions();
    refreshSelectionUi();
    updatePreviewAtoms();
    statusBar()->showMessage(m_japanese
        ? QStringLiteral("吸着分子グループを作成時ポーズへ戻しました（%1 原子）。").arg(resetCount)
        : QStringLiteral("Reset adsorbate group to the creation pose (%1 atom(s)).").arg(resetCount), 5000);
}

void MainWindow::exportPoseXyz() {
    const PoseGroup* group = currentPoseGroup();
    if (group == nullptr) {
        refreshPoseUi();
        return;
    }
    QString selectedFilter;
    const QString safeName = safeBaseFileName(group->name, QStringLiteral("adsorbate_pose"));
    const QString path = QFileDialog::getSaveFileName(this,
        m_japanese ? QStringLiteral("吸着分子ポーズXYZ出力") : QStringLiteral("Export adsorbate pose XYZ"),
        defaultOpenDirectory() + QStringLiteral("/") + safeName + QStringLiteral(".extxyz"),
        QStringLiteral("Extended XYZ whole structure (*.extxyz *.xyz);;Plain XYZ adsorbate group only (*.xyz)"),
        &selectedFilter);
    if (path.isEmpty()) {
        return;
    }

    StructureData exportStructure = m_structure;
    bool extended = selectedFilter.contains(QStringLiteral("Extended"), Qt::CaseInsensitive)
        || QFileInfo(path).suffix().compare(QStringLiteral("extxyz"), Qt::CaseInsensitive) == 0;
    if (!extended) {
        exportStructure.atoms.clear();
        for (const NativeAtom* atom : poseAtoms(*group)) {
            exportStructure.atoms.push_back(*atom);
        }
        exportStructure.title = group->name;
    }
    QString errorMessage;
    if (!writeXyzFile(exportStructure, path, extended, &errorMessage)) {
        QMessageBox::critical(this, m_japanese ? QStringLiteral("XYZ出力") : QStringLiteral("XYZ export"), errorMessage);
        return;
    }
    statusBar()->showMessage(extended
        ? QStringLiteral("Saved ASE-readable extended XYZ: %1").arg(path)
        : QStringLiteral("Saved adsorbate-only plain XYZ: %1").arg(path), 6000);
}

void MainWindow::exportPoseJson() {
    const PoseGroup* group = currentPoseGroup();
    if (group == nullptr) {
        refreshPoseUi();
        return;
    }
    const QString safeName = safeBaseFileName(group->name, QStringLiteral("adsorbate_pose"));
    const QString path = QFileDialog::getSaveFileName(this,
        m_japanese ? QStringLiteral("pose JSON出力") : QStringLiteral("Export pose JSON"),
        defaultOpenDirectory() + QStringLiteral("/") + safeName + QStringLiteral(".pose.json"),
        QStringLiteral("Pose JSON (*.pose.json *.json);;All files (*.*)"));
    if (path.isEmpty()) {
        return;
    }

    const QVector3D pivot = posePivotPosition(*group);
    QJsonObject root;
    root["format"] = QStringLiteral("aseapp.adsorbate_pose.v1");
    root["name"] = group->name;
    root["source_path"] = m_structure.sourcePath;
    root["unit"] = QStringLiteral("angstrom");
    root["pivot_atom_id"] = group->pivotAtomId;
    root["pivot_cartesian"] = jsonVector3(pivot);
    root["translation"] = jsonVector3(group->translation);
    root["rotation_quaternion_wxyz"] = jsonQuaternionWxyz(group->rotationQuaternion);
    QJsonArray cellArray;
    for (const auto& vec : m_structure.cellVectors) {
        cellArray.append(jsonVector3(vec));
    }
    root["cell_vectors"] = cellArray;
    QJsonArray atomsArray;
    for (std::size_t i = 0; i < group->atomIds.size(); ++i) {
        const NativeAtom* atom = findAtomByIdInStructure(m_structure, group->atomIds[i]);
        if (atom == nullptr) {
            continue;
        }
        QJsonObject atomObject;
        atomObject["atom_id"] = atom->atomId;
        atomObject["element"] = atom->element;
        atomObject["tag"] = atom->tag;
        atomObject["cartesian"] = jsonVector3(atom->cartesian);
        atomObject["relative_to_pivot"] = jsonVector3(atom->cartesian - pivot);
        if (i < group->initialCartesian.size()) {
            atomObject["initial_cartesian"] = jsonVector3(group->initialCartesian[i]);
        }
        atomsArray.append(atomObject);
    }
    root["atoms"] = atomsArray;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::critical(this, m_japanese ? QStringLiteral("pose JSON出力") : QStringLiteral("Pose JSON export"),
            QStringLiteral("Failed to write %1").arg(path));
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    statusBar()->showMessage(QStringLiteral("Saved pose JSON: %1").arg(path), 6000);
}

void MainWindow::exportPoseSnippet() {
    const PoseGroup* group = currentPoseGroup();
    if (group == nullptr) {
        refreshPoseUi();
        return;
    }
    const QString safeName = safeBaseFileName(group->name, QStringLiteral("adsorbate_pose"));
    const QString path = QFileDialog::getSaveFileName(this,
        m_japanese ? QStringLiteral("ASE snippet出力") : QStringLiteral("Export ASE snippet"),
        defaultOpenDirectory() + QStringLiteral("/") + safeName + QStringLiteral("_pose.py"),
        QStringLiteral("Python (*.py);;All files (*.*)"));
    if (path.isEmpty()) {
        return;
    }

    const QVector3D pivot = posePivotPosition(*group);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QMessageBox::critical(this, m_japanese ? QStringLiteral("ASE snippet出力") : QStringLiteral("ASE snippet export"),
            QStringLiteral("Failed to write %1").arg(path));
        return;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out.setRealNumberNotation(QTextStream::FixedNotation);
    out.setRealNumberPrecision(10);
    out << "# Generated by ASEapp Surface Builder.\n";
    out << "# The positions below preserve the GUI-adjusted adsorbate pose relative to pivot.\n";
    out << "import numpy as np\n";
    out << "from ase import Atoms\n";
    out << "from ase.io import read, write\n\n";
    out << "slab = read(\"slab.extxyz\")  # replace with your target slab\n";
    out << "symbols = [";
    const auto atoms = poseAtoms(*group);
    for (std::size_t i = 0; i < atoms.size(); ++i) {
        if (i > 0) out << ", ";
        out << '"' << atoms[i]->element << '"';
    }
    out << "]\n";
    out << "relative_positions = np.array([\n";
    for (const NativeAtom* atom : atoms) {
        const QVector3D rel = atom->cartesian - pivot;
        out << "    [" << rel.x() << ", " << rel.y() << ", " << rel.z() << "],\n";
    }
    out << "], dtype=float)\n";
    out << "gui_pivot = np.array([" << pivot.x() << ", " << pivot.y() << ", " << pivot.z() << "], dtype=float)\n";
    out << "target_pivot = gui_pivot.copy()  # set this to another adsorption-site coordinate for batch generation\n";
    out << "adsorbate = Atoms(symbols=symbols, positions=relative_positions + target_pivot)\n";
    out << "combined = slab + adsorbate\n";
    out << "write(\"slab_with_adsorbate.extxyz\", combined)\n";
    statusBar()->showMessage(QStringLiteral("Saved ASE snippet: %1").arg(path), 6000);
}

void MainWindow::reloadPresetRegistry() {
    QString message;
    m_customizationRegistry.loadOrCreateDefault(SurfaceCustomizationRegistry::defaultFilePath(), &message);
    if (m_presetPathLabel != nullptr) {
        m_presetPathLabel->setText(QStringLiteral("JSON file: %1").arg(m_customizationRegistry.filePath()));
    }
    if (m_presetCombo == nullptr) return;
    const QString previousName = m_presetCombo->currentText();
    m_presetCombo->blockSignals(true);
    m_presetCombo->clear();
    const auto& presets = m_customizationRegistry.presets();
    for (const auto& preset : presets) {
        m_presetCombo->addItem(preset.name.isEmpty() ? preset.mode : preset.name);
        const int row = m_presetCombo->count() - 1;
        m_presetCombo->setItemData(row, preset.description, Qt::ToolTipRole);
        m_presetCombo->setItemData(row, describePlacementRule(preset), Qt::WhatsThisRole);
    }
    if (!presets.isEmpty()) {
        const int found = previousName.isEmpty() ? -1 : m_presetCombo->findText(previousName);
        m_presetCombo->setCurrentIndex(found >= 0 ? found : 0);
    }
    m_presetCombo->blockSignals(false);
    refreshPresetUi();
    if (!message.isEmpty()) statusBar()->showMessage(message, 5000);
}

void MainWindow::openPresetFile() {
    const QString path = m_customizationRegistry.filePath();
    if (path.isEmpty()) {
        QMessageBox::warning(this, "Custom placement", "Preset JSON file path is not available.");
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
        QMessageBox::warning(this, "Custom placement", QStringLiteral("Failed to open %1").arg(path));
    }
}

void MainWindow::toggleAtomSelection(int atomId) {
    auto it = std::find(m_selectedAtomIds.begin(), m_selectedAtomIds.end(), atomId);
    if (it != m_selectedAtomIds.end()) {
        m_selectedAtomIds.erase(it);
    } else {
        m_selectedAtomIds.push_back(atomId);
    }
    m_canvas->setSelectedAtomIds(m_selectedAtomIds);
    m_canvas->focusAtom(m_selectedAtomIds.empty() ? -1 : m_selectedAtomIds.back());
    refreshSelectionUi();
    refreshPresetUi();
}

void MainWindow::translateSelectedAtoms(const QVector3D& delta) {
    if (m_selectedAtomIds.empty() || delta.lengthSquared() <= 1.0e-12f) {
        return;
    }
    if (!m_translationUndoActive) {
        pushUndoState(QStringLiteral("move"));
        m_translationUndoActive = true;
    }
    for (auto& atom : m_structure.atoms) {
        if (std::find(m_selectedAtomIds.begin(), m_selectedAtomIds.end(), atom.atomId) == m_selectedAtomIds.end()) {
            continue;
        }
        atom.cartesian += delta;
        atom.fractional = solveFractionalForCell(m_structure.cellVectors, atom.cartesian);
    }
    for (auto& group : m_poseGroups) {
        const bool wholeGroupSelected = !group.atomIds.empty()
            && std::all_of(group.atomIds.begin(), group.atomIds.end(), [this](int atomId) {
                   return std::find(m_selectedAtomIds.begin(), m_selectedAtomIds.end(), atomId) != m_selectedAtomIds.end();
               });
        if (wholeGroupSelected) {
            group.translation += delta;
        }
    }
    m_structure.dirty = true;
    m_canvas->setStructure(m_structure);
    m_canvas->setSelectedAtomIds(m_selectedAtomIds);
    m_canvas->focusAtom(m_selectedAtomIds.empty() ? -1 : m_selectedAtomIds.back());
    syncCanvasDisplayOptions();
    m_summaryLabel->setText(QString("Atoms: %1\nCell: %2 / %3 / %4\nDirty: %5")
        .arg(m_structure.atoms.size())
        .arg(formatVector(m_structure.cellVectors[0]))
        .arg(formatVector(m_structure.cellVectors[1]))
        .arg(formatVector(m_structure.cellVectors[2]))
        .arg(m_structure.dirty ? "yes" : "no"));
    if (m_supercellStatusLabel != nullptr) {
        m_supercellStatusLabel->setText(supercellStatusText());
    }
}

void MainWindow::finishSelectedAtomTranslation() {
    if (!m_translationUndoActive) {
        return;
    }
    m_translationUndoActive = false;
    m_canvas->setStructure(m_structure);
    m_canvas->setSelectedAtomIds(m_selectedAtomIds);
    m_canvas->focusAtom(m_selectedAtomIds.empty() ? -1 : m_selectedAtomIds.back());
    syncCanvasDisplayOptions();
}

void MainWindow::clearSelection() {
    setSelectedAtomIds({});
    refreshSelectionUi();
    refreshPresetUi();
}

MainWindow::EditSnapshot MainWindow::captureEditSnapshot() const {
    EditSnapshot snapshot;
    snapshot.structure = m_structure;
    snapshot.supercellBaseStructure = m_supercellBaseStructure;
    snapshot.hasSupercellBaseStructure = m_hasSupercellBaseStructure;
    snapshot.lastEditWasSupercell = m_lastEditWasSupercell;
    snapshot.supercellFactors = m_supercellFactors;
    snapshot.selectedAtomIds = m_selectedAtomIds;
    snapshot.poseGroups = m_poseGroups;
    if (m_poseGroupCombo != nullptr) {
        bool ok = false;
        const int poseGroupIndex = m_poseGroupCombo->currentData().toInt(&ok);
        if (ok) {
            snapshot.activePoseGroupIndex = poseGroupIndex;
        }
    }
    if (m_posePivotCombo != nullptr) {
        snapshot.activePosePivotKey = m_posePivotCombo->currentData().toString();
    }
    if (m_poseAxisCombo != nullptr) {
        snapshot.activePoseAxisKey = m_poseAxisCombo->currentData().toString();
    }
    return snapshot;
}

void MainWindow::restoreEditSnapshot(const EditSnapshot& snapshot, bool forceDirty) {
    m_structure = snapshot.structure;
    if (forceDirty) {
        m_structure.dirty = true;
    }
    m_supercellBaseStructure = snapshot.supercellBaseStructure;
    m_hasSupercellBaseStructure = snapshot.hasSupercellBaseStructure;
    m_lastEditWasSupercell = snapshot.lastEditWasSupercell;
    m_supercellFactors = snapshot.supercellFactors;
    m_poseGroups = snapshot.poseGroups;
    m_translationUndoActive = false;

    if (m_poseGroupCombo != nullptr) {
        m_poseGroupCombo->blockSignals(true);
        m_poseGroupCombo->clear();
        m_poseGroupCombo->blockSignals(false);
    }
    applyStructureState(m_structure);

    if (m_poseGroupCombo != nullptr && snapshot.activePoseGroupIndex >= 0) {
        const int row = m_poseGroupCombo->findData(snapshot.activePoseGroupIndex);
        if (row >= 0) {
            m_poseGroupCombo->setCurrentIndex(row);
        }
    }
    refreshPoseUi();
    if (m_posePivotCombo != nullptr && !snapshot.activePosePivotKey.isEmpty()) {
        const int row = m_posePivotCombo->findData(snapshot.activePosePivotKey);
        if (row >= 0) {
            m_posePivotCombo->setCurrentIndex(row);
        }
    }
    if (m_poseAxisCombo != nullptr && !snapshot.activePoseAxisKey.isEmpty()) {
        QString axisKey = snapshot.activePoseAxisKey;
        if (axisKey == QStringLiteral("bond_first_two")) {
            axisKey = QStringLiteral("bond_selected_two");
        }
        const int row = m_poseAxisCombo->findData(axisKey);
        if (row >= 0) {
            m_poseAxisCombo->setCurrentIndex(row);
        }
    }
    setSelectedAtomIds(snapshot.selectedAtomIds);
    refreshSelectionUi();
    refreshPresetUi();
    refreshPoseUi();
    updateUndoRedoActions();
}

void MainWindow::pushUndoState(const QString& label) {
    m_undoStack.push_back(captureEditSnapshot());
    if (m_undoStack.size() > 64) {
        m_undoStack.erase(m_undoStack.begin());
    }
    m_redoStack.clear();
    updateUndoRedoActions();
}

void MainWindow::replaceStructureFromEdit(const StructureData& structure, const QString& label) {
    const bool keepSupercellBase = label == QStringLiteral("supercell")
        || (m_lastEditWasSupercell && editPreservesSupercellBase(label));
    pushUndoState(label);
    m_structure = structure;
    m_structure.dirty = true;
    m_lastEditWasSupercell = keepSupercellBase;
    applyStructureState(m_structure);
}

void MainWindow::updateUndoRedoActions() {
    if (m_undoAction != nullptr) {
        m_undoAction->setEnabled(!m_undoStack.empty());
    }
    if (m_redoAction != nullptr) {
        m_redoAction->setEnabled(!m_redoStack.empty());
    }
}

void MainWindow::undoEdit() {
    if (m_undoStack.empty()) {
        return;
    }
    m_redoStack.push_back(captureEditSnapshot());
    const EditSnapshot snapshot = m_undoStack.back();
    m_undoStack.pop_back();
    restoreEditSnapshot(snapshot, true);
    updateUndoRedoActions();
}

void MainWindow::redoEdit() {
    if (m_redoStack.empty()) {
        return;
    }
    m_undoStack.push_back(captureEditSnapshot());
    const EditSnapshot snapshot = m_redoStack.back();
    m_redoStack.pop_back();
    restoreEditSnapshot(snapshot, true);
    updateUndoRedoActions();
}

void MainWindow::deleteSelectedAtoms() {
    if (m_selectedAtomIds.empty()) {
        statusBar()->showMessage(m_japanese ? QStringLiteral("削除する選択原子がありません。") : QStringLiteral("No selected atoms to delete."), 2500);
        return;
    }

    const int count = static_cast<int>(m_selectedAtomIds.size());
    const auto reply = QMessageBox::question(
        this,
        m_japanese ? QStringLiteral("選択原子を削除") : QStringLiteral("Delete selected atoms"),
        m_japanese
            ? QStringLiteral("黄色で選択中の %1 個の原子を削除しますか？").arg(count)
            : QStringLiteral("Delete %1 atom(s) highlighted in yellow?").arg(count),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (reply != QMessageBox::Yes) {
        return;
    }

    pushUndoState(QStringLiteral("delete"));
    const auto selected = m_selectedAtomIds;
    const auto isSelected = [&selected](int atomId) {
        return std::find(selected.begin(), selected.end(), atomId) != selected.end();
    };
    const auto oldSize = m_structure.atoms.size();
    m_structure.atoms.erase(
        std::remove_if(
            m_structure.atoms.begin(),
            m_structure.atoms.end(),
            [&](const NativeAtom& atom) { return isSelected(atom.atomId); }),
        m_structure.atoms.end());

    const int deleted = static_cast<int>(oldSize - m_structure.atoms.size());
    m_structure.dirty = true;
    applyStructureState(m_structure);
    statusBar()->showMessage(
        m_japanese
            ? QStringLiteral("%1 個の原子を削除しました。").arg(deleted)
            : QStringLiteral("Deleted %1 atom(s).").arg(deleted),
        3000);
}

QString MainWindow::structureDiagnosticsText(bool includeMeasurements) const {
    QStringList lines;
    lines << (m_japanese ? QStringLiteral("構造診断") : QStringLiteral("Structure check"));
    lines << QStringLiteral("Atoms: %1").arg(m_structure.atoms.size());
    if (m_structure.atoms.empty()) {
        return lines.join(QStringLiteral("\n"));
    }

    const QVector3D cAxis = m_structure.cellVectors[2];
    const QVector3D cUnit = cAxis.lengthSquared() > 1.0e-8f ? cAxis.normalized() : QVector3D(0.0f, 0.0f, 1.0f);
    float minC = std::numeric_limits<float>::max();
    float maxC = std::numeric_limits<float>::lowest();
    int fracOut = 0;
    int fixedCount = 0;
    for (const auto& atom : m_structure.atoms) {
        const float p = QVector3D::dotProduct(atom.cartesian, cUnit);
        minC = std::min(minC, p);
        maxC = std::max(maxC, p);
        if (atom.fractional.x() < -1.0e-5f || atom.fractional.x() > 1.0f + 1.0e-5f
            || atom.fractional.y() < -1.0e-5f || atom.fractional.y() > 1.0f + 1.0e-5f
            || atom.fractional.z() < -1.0e-5f || atom.fractional.z() > 1.0f + 1.0e-5f) {
            ++fracOut;
        }
        if (!atom.movable[0] || !atom.movable[1] || !atom.movable[2]) {
            ++fixedCount;
        }
    }
    const double slabC = static_cast<double>(maxC - minC);
    const double cellC = static_cast<double>(cAxis.length());
    lines << QStringLiteral("Cell c: %1 Å").arg(cellC, 0, 'f', 3);
    lines << QStringLiteral("Slab thickness along c: %1 Å").arg(slabC, 0, 'f', 3);
    lines << QStringLiteral("Estimated vacuum along c: %1 Å").arg(std::max(0.0, cellC - slabC), 0, 'f', 3);
    lines << QStringLiteral("Fractional out of [0,1]: %1").arg(fracOut);
    lines << QStringLiteral("Fixed/selective atoms: %1").arg(fixedCount);

    double minDistance = std::numeric_limits<double>::infinity();
    int minA = -1;
    int minB = -1;
    int collisionCount = 0;
    for (std::size_t i = 0; i < m_structure.atoms.size(); ++i) {
        for (std::size_t j = i + 1; j < m_structure.atoms.size(); ++j) {
            const double d = static_cast<double>((m_structure.atoms[j].cartesian - m_structure.atoms[i].cartesian).length());
            if (d < minDistance) {
                minDistance = d;
                minA = m_structure.atoms[i].atomId;
                minB = m_structure.atoms[j].atomId;
            }
            if (d < 0.70) {
                ++collisionCount;
            }
        }
    }
    if (std::isfinite(minDistance)) {
        lines << QStringLiteral("Minimum distance: %1 Å (#%2-#%3)").arg(minDistance, 0, 'f', 3).arg(minA).arg(minB);
    }
    lines << QStringLiteral("Close-contact warnings (<0.70 Å): %1").arg(collisionCount);

    if (includeMeasurements && !m_selectedAtomIds.empty()) {
        std::vector<const NativeAtom*> refs;
        for (int id : m_selectedAtomIds) {
            auto it = std::find_if(m_structure.atoms.begin(), m_structure.atoms.end(), [id](const NativeAtom& atom) { return atom.atomId == id; });
            if (it != m_structure.atoms.end()) {
                refs.push_back(&(*it));
            }
        }
        lines << QString();
        lines << (m_japanese ? QStringLiteral("選択原子の測定") : QStringLiteral("Selected atom measurements"));
        if (refs.size() >= 2) {
            lines << QStringLiteral("Distance #%1-#%2: %3 Å")
                .arg(refs[0]->atomId)
                .arg(refs[1]->atomId)
                .arg(static_cast<double>((refs[1]->cartesian - refs[0]->cartesian).length()), 0, 'f', 4);
        }
        if (refs.size() >= 3) {
            const QVector3D v1 = refs[0]->cartesian - refs[1]->cartesian;
            const QVector3D v2 = refs[2]->cartesian - refs[1]->cartesian;
            const double denom = static_cast<double>(v1.length() * v2.length());
            if (denom > 1.0e-12) {
                const double cosValue = std::clamp(static_cast<double>(QVector3D::dotProduct(v1, v2)) / denom, -1.0, 1.0);
                lines << QStringLiteral("Angle #%1-#%2-#%3: %4°")
                    .arg(refs[0]->atomId)
                    .arg(refs[1]->atomId)
                    .arg(refs[2]->atomId)
                    .arg(std::acos(cosValue) * 180.0 / 3.14159265358979323846, 0, 'f', 3);
            }
        }
        if (!refs.empty()) {
            const double height = static_cast<double>(QVector3D::dotProduct(refs.front()->cartesian, cUnit) - minC);
            lines << QStringLiteral("Height of first selected atom from bottom along c: %1 Å").arg(height, 0, 'f', 3);
        }
    }

    return lines.join(QStringLiteral("\n"));
}

void MainWindow::showMeasurementReport() {
    QMessageBox::information(this, uiText(QStringLiteral("measure")), structureDiagnosticsText(true));
}

void MainWindow::showStructureCheckReport() {
    QMessageBox::information(this, uiText(QStringLiteral("check")), structureDiagnosticsText(false));
}

void MainWindow::loadCustomBondRanges() {
    m_customBondRanges.clear();
    QSettings settings(QStringLiteral("ASEapp"), QStringLiteral("ASEappSurfaceBuilder"));
    const int count = settings.beginReadArray(QStringLiteral("display/customBondRanges"));
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        const QString elementA = settings.value(QStringLiteral("elementA")).toString();
        const QString elementB = settings.value(QStringLiteral("elementB")).toString();
        const double minDistance = settings.value(QStringLiteral("minDistance"), 0.0).toDouble();
        const double maxDistance = settings.value(QStringLiteral("maxDistance"), 0.0).toDouble();
        if (maxDistance > 0.0 && maxDistance >= minDistance) {
            m_customBondRanges.insert(vestaBondKey(elementA, elementB), BondDistanceRange{std::max(0.0, minDistance), maxDistance});
        }
    }
    settings.endArray();
}

void MainWindow::saveCustomBondRanges() const {
    QSettings settings(QStringLiteral("ASEapp"), QStringLiteral("ASEappSurfaceBuilder"));
    settings.remove(QStringLiteral("display/customBondRanges"));
    settings.beginWriteArray(QStringLiteral("display/customBondRanges"));
    int row = 0;
    QStringList keys = m_customBondRanges.keys();
    keys.sort(Qt::CaseInsensitive);
    for (const QString& key : keys) {
        const BondDistanceRange range = m_customBondRanges.value(key);
        if (range.maxDistance <= 0.0 || range.maxDistance < range.minDistance) {
            continue;
        }
        const QStringList elements = key.split(QLatin1Char('|'));
        if (elements.size() != 2) {
            continue;
        }
        settings.setArrayIndex(row++);
        settings.setValue(QStringLiteral("elementA"), elements.at(0));
        settings.setValue(QStringLiteral("elementB"), elements.at(1));
        settings.setValue(QStringLiteral("minDistance"), range.minDistance);
        settings.setValue(QStringLiteral("maxDistance"), range.maxDistance);
    }
    settings.endArray();
}

void MainWindow::editBondDistances() {
    QStringList elements;
    for (const auto& atom : m_structure.atoms) {
        const QString element = vestaNormalizeElement(atom.element);
        if (!element.isEmpty() && !elements.contains(element)) {
            elements << element;
        }
    }

    QString initialA;
    QString initialB;
    double selectedDistance = 0.0;
    if (m_selectedAtomIds.size() >= 2) {
        const NativeAtom* first = findAtomByIdInStructure(m_structure, m_selectedAtomIds[0]);
        const NativeAtom* second = findAtomByIdInStructure(m_structure, m_selectedAtomIds[1]);
        if (first != nullptr && second != nullptr) {
            initialA = first->element;
            initialB = second->element;
            selectedDistance = static_cast<double>((second->cartesian - first->cartesian).length());
        }
    }

    BondDistanceDialog dialog(m_japanese, elements, m_customBondRanges, initialA, initialB, selectedDistance, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString key = vestaBondKey(dialog.elementA(), dialog.elementB());
    if (dialog.resetCustom()) {
        m_customBondRanges.remove(key);
    } else {
        const BondDistanceRange range = dialog.range();
        if (range.maxDistance <= 0.0 || range.maxDistance < range.minDistance) {
            QMessageBox::warning(this,
                m_japanese ? QStringLiteral("ボンド距離設定") : QStringLiteral("Bond distance settings"),
                m_japanese ? QStringLiteral("最大距離は最小距離以上にしてください。") : QStringLiteral("Maximum distance must be greater than or equal to minimum distance."));
            return;
        }
        m_customBondRanges.insert(key, range);
    }
    saveCustomBondRanges();
    syncCanvasDisplayOptions();
    statusBar()->showMessage(m_japanese
        ? QStringLiteral("ボンド距離設定を更新しました: %1").arg(key)
        : QStringLiteral("Updated bond distance settings: %1").arg(key), 4000);
}

void MainWindow::syncCanvasDisplayOptions() {
    StructureCanvas::DisplayOptions options = m_canvas->displayOptions();
    options.showCell = m_showCellCheck->isChecked();
    options.showBonds = m_showBondsCheck->isChecked();
    options.showAxes = m_showAxesCheck->isChecked();
    options.showLabels = m_showLabelsCheck->isChecked();
    options.perspective = m_perspectiveCheck->isChecked();
    options.depthCue = m_depthCueCheck->isChecked();
    options.atomScale = m_atomScaleSpin->value() / 100.0;
    options.customBondRanges = m_customBondRanges;
    m_canvas->setDisplayOptions(options);
}

void MainWindow::refreshPresetUi() {
    if (m_placementModeCombo == nullptr || m_presetDetailsLabel == nullptr) return;
    if (m_placementModeCombo->currentIndex() < 0) {
        m_presetDetailsLabel->setText(m_japanese ? QStringLiteral("配置位置を選択してください。") : QStringLiteral("Select a placement option."));
        if (m_applyPresetButton != nullptr) m_applyPresetButton->setEnabled(false);
        if (m_canvas != nullptr) m_canvas->setPreviewAtoms({});
        return;
    }
    const auto rule = currentPlacementRule();
    if (m_placementFractionSpin != nullptr) {
        m_placementFractionSpin->setEnabled(rule.mode.trimmed().toLower() == QStringLiteral("pair_fraction"));
    }
    const int required = rule.selectionCount > 0 ? rule.selectionCount : defaultSelectionCountForMode(rule.mode);
    m_presetDetailsLabel->setText(uiText(QStringLiteral("placement_hint"))
        .arg(required)
        .arg(static_cast<int>(m_selectedAtomIds.size())));
    updatePreviewAtoms();
    if (m_applyPresetButton != nullptr) {
        m_applyPresetButton->setEnabled(static_cast<int>(m_selectedAtomIds.size()) >= required);
    }
    refreshPrecursorUi();
}

void MainWindow::refreshSelectionUi() {
    if (m_selectionLabel != nullptr) m_selectionLabel->setText(selectionText(m_selectedAtomIds, m_japanese));
    if (m_deleteSelectedButton != nullptr) m_deleteSelectedButton->setEnabled(!m_selectedAtomIds.empty());
    refreshPrecursorUi();
    refreshPoseUi();
}

const MainWindow::PrecursorTemplate* MainWindow::currentPrecursorTemplate() const {
    if (m_loadedPrecursors.empty()) {
        return nullptr;
    }
    if (m_precursorCombo != nullptr) {
        bool ok = false;
        const int storedIndex = m_precursorCombo->currentData().toInt(&ok);
        if (ok && storedIndex >= 0 && storedIndex < static_cast<int>(m_loadedPrecursors.size())) {
            return &m_loadedPrecursors[static_cast<std::size_t>(storedIndex)];
        }
    }
    return &m_loadedPrecursors.front();
}

void MainWindow::setLoadedPrecursors(std::vector<PrecursorTemplate> precursors, const QString& preferredName) {
    for (std::size_t i = 0; i < precursors.size(); ++i) {
        if (precursors[i].name.trimmed().isEmpty()) {
            precursors[i].name = QStringLiteral("Precursor %1").arg(static_cast<int>(i) + 1);
        }
    }
    m_loadedPrecursors = std::move(precursors);

    if (m_precursorCombo != nullptr) {
        m_precursorCombo->blockSignals(true);
        m_precursorCombo->clear();
        if (m_loadedPrecursors.empty()) {
            m_precursorCombo->addItem(uiText(QStringLiteral("precursor_unloaded")), -1);
            m_precursorCombo->setEnabled(false);
        } else {
            m_precursorCombo->setEnabled(true);
            int preferredIndex = 0;
            for (std::size_t i = 0; i < m_loadedPrecursors.size(); ++i) {
                const auto& precursor = m_loadedPrecursors[i];
                const int atomCount = static_cast<int>(precursor.atoms.size());
                const QString label = QStringLiteral("%1 (%2)").arg(precursor.name).arg(atomCount);
                m_precursorCombo->addItem(label, static_cast<int>(i));
                m_precursorCombo->setItemData(m_precursorCombo->count() - 1,
                    m_japanese
                        ? QStringLiteral("%1 原子").arg(atomCount)
                        : QStringLiteral("%1 atom(s)").arg(atomCount),
                    Qt::ToolTipRole);
                if (!preferredName.trimmed().isEmpty()
                    && precursor.name.compare(preferredName.trimmed(), Qt::CaseInsensitive) == 0) {
                    preferredIndex = static_cast<int>(i);
                }
            }
            m_precursorCombo->setCurrentIndex(preferredIndex);
        }
        m_precursorCombo->blockSignals(false);
    }

    refreshPrecursorUi();
}

void MainWindow::refreshPrecursorUi() {
    if (m_savePrecursorButton != nullptr) {
        m_savePrecursorButton->setEnabled(!m_selectedAtomIds.empty());
    }
    const SurfacePlacementRule rule = currentPlacementRule();
    const int required = rule.selectionCount > 0 ? rule.selectionCount : defaultSelectionCountForMode(rule.mode);
    const bool hasEnoughSelection = static_cast<int>(m_selectedAtomIds.size()) >= required;
    const PrecursorTemplate* precursor = currentPrecursorTemplate();
    if (m_placePrecursorButton != nullptr) {
        m_placePrecursorButton->setEnabled(precursor != nullptr && !precursor->atoms.empty() && hasEnoughSelection);
        m_placePrecursorButton->setToolTip(hasEnoughSelection
            ? uiText(QStringLiteral("precursor_tip"))
            : (m_japanese
                ? QStringLiteral("現在の配置位置には %1 個以上の原子選択が必要です。").arg(required)
                : QStringLiteral("The current placement option requires %1 selected atom(s).").arg(required)));
    }
    if (m_precursorLabel == nullptr) {
        return;
    }
    if (precursor == nullptr || precursor->atoms.empty()) {
        m_precursorLabel->setText(uiText(QStringLiteral("precursor_none")));
        return;
    }
    m_precursorLabel->setText(m_japanese
        ? QStringLiteral("前駆体: %1（%2 原子）").arg(precursor->name).arg(static_cast<int>(precursor->atoms.size()))
        : QStringLiteral("Precursor: %1 (%2 atom(s))").arg(precursor->name).arg(static_cast<int>(precursor->atoms.size())));
}

MainWindow::PoseGroup* MainWindow::currentPoseGroup() {
    if (m_poseGroups.empty() || m_poseGroupCombo == nullptr) {
        return nullptr;
    }
    bool ok = false;
    const int index = m_poseGroupCombo->currentData().toInt(&ok);
    if (!ok || index < 0 || index >= static_cast<int>(m_poseGroups.size())) {
        return nullptr;
    }
    return &m_poseGroups[static_cast<std::size_t>(index)];
}

const MainWindow::PoseGroup* MainWindow::currentPoseGroup() const {
    if (m_poseGroups.empty() || m_poseGroupCombo == nullptr) {
        return nullptr;
    }
    bool ok = false;
    const int index = m_poseGroupCombo->currentData().toInt(&ok);
    if (!ok || index < 0 || index >= static_cast<int>(m_poseGroups.size())) {
        return nullptr;
    }
    return &m_poseGroups[static_cast<std::size_t>(index)];
}

std::vector<const NativeAtom*> MainWindow::poseAtoms(const PoseGroup& group) const {
    std::vector<const NativeAtom*> atoms;
    atoms.reserve(group.atomIds.size());
    for (int atomId : group.atomIds) {
        if (const NativeAtom* atom = findAtomByIdInStructure(m_structure, atomId)) {
            atoms.push_back(atom);
        }
    }
    return atoms;
}

std::array<const NativeAtom*, 2> MainWindow::selectedPoseBondAtoms(const PoseGroup& group) const {
    std::array<const NativeAtom*, 2> atoms{nullptr, nullptr};
    int found = 0;
    for (int atomId : m_selectedAtomIds) {
        if (std::find(group.atomIds.begin(), group.atomIds.end(), atomId) == group.atomIds.end()) {
            continue;
        }
        const NativeAtom* atom = findAtomByIdInStructure(m_structure, atomId);
        if (atom == nullptr) {
            continue;
        }
        atoms[static_cast<std::size_t>(found)] = atom;
        ++found;
        if (found >= 2) {
            break;
        }
    }
    return atoms;
}

QVector3D MainWindow::posePivotPosition(const PoseGroup& group) const {
    const QString pivotKey = m_posePivotCombo != nullptr
        ? m_posePivotCombo->currentData().toString()
        : QString();
    if (pivotKey.startsWith(QStringLiteral("atom:"))) {
        bool ok = false;
        const int atomId = pivotKey.mid(5).toInt(&ok);
        if (ok) {
            if (const NativeAtom* atom = findAtomByIdInStructure(m_structure, atomId)) {
                return atom->cartesian;
            }
        }
    }

    const auto atoms = poseAtoms(group);
    if (atoms.empty()) {
        return {};
    }
    QVector3D weighted;
    double totalWeight = 0.0;
    const bool massCenter = pivotKey == QStringLiteral("mass_center");
    for (const NativeAtom* atom : atoms) {
        double weight = 1.0;
        if (massCenter) {
            if (const auto* details = periodicElementDetails(atom->element.trimmed())) {
                bool ok = false;
                const double parsed = QString::fromLatin1(details->atomicMass).toDouble(&ok);
                if (ok && parsed > 0.0) {
                    weight = parsed;
                }
            }
        }
        weighted += atom->cartesian * static_cast<float>(weight);
        totalWeight += weight;
    }
    return totalWeight > 0.0 ? weighted / static_cast<float>(totalWeight) : atoms.front()->cartesian;
}

QVector3D MainWindow::poseRotationAxis(const PoseGroup& group) const {
    const QString axisKey = m_poseAxisCombo != nullptr
        ? m_poseAxisCombo->currentData().toString()
        : QStringLiteral("slab_normal");
    if (axisKey == QStringLiteral("global_x")) return QVector3D(1.0f, 0.0f, 0.0f);
    if (axisKey == QStringLiteral("global_y")) return QVector3D(0.0f, 1.0f, 0.0f);
    if (axisKey == QStringLiteral("global_z")) return QVector3D(0.0f, 0.0f, 1.0f);
    if (axisKey == QStringLiteral("cell_a")) return cellAxisDirection(m_structure, 0);
    if (axisKey == QStringLiteral("cell_b")) return cellAxisDirection(m_structure, 1);
    if (axisKey == QStringLiteral("cell_c")) return cellAxisDirection(m_structure, 2);
    if (axisKey == QStringLiteral("camera") && m_canvas != nullptr) return normalizedOrFallback(m_canvas->viewForward());
    if (isSelectedBondAxisKey(axisKey)) {
        const auto atoms = selectedPoseBondAtoms(group);
        if (atoms[0] != nullptr && atoms[1] != nullptr) {
            const QVector3D axis = atoms[1]->cartesian - atoms[0]->cartesian;
            return axis.lengthSquared() > 1.0e-12f ? axis.normalized() : QVector3D();
        }
        return {};
    }
    return slabNormalDirection(m_structure);
}

QVector3D MainWindow::currentPoseTranslationDelta() const {
    const QVector3D cartesianDelta(
        static_cast<float>(m_poseDxSpin != nullptr ? m_poseDxSpin->value() : 0.0),
        static_cast<float>(m_poseDySpin != nullptr ? m_poseDySpin->value() : 0.0),
        static_cast<float>(m_poseDzSpin != nullptr ? m_poseDzSpin->value() : 0.0));
    QVector3D cellDelta;
    if (m_poseCellASpin != nullptr) cellDelta += cellAxisDirection(m_structure, 0) * static_cast<float>(m_poseCellASpin->value());
    if (m_poseCellBSpin != nullptr) cellDelta += cellAxisDirection(m_structure, 1) * static_cast<float>(m_poseCellBSpin->value());
    if (m_poseCellCSpin != nullptr) cellDelta += cellAxisDirection(m_structure, 2) * static_cast<float>(m_poseCellCSpin->value());
    const QVector3D normalDelta = slabNormalDirection(m_structure) * static_cast<float>(m_poseNormalSpin != nullptr ? m_poseNormalSpin->value() : 0.0);
    return cartesianDelta + cellDelta + normalDelta;
}

bool MainWindow::buildPoseBondLengthPlan(const PoseGroup& group, double targetLength, PoseBondLengthPlan* plan, QString* errorMessage) const {
    if (plan == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Internal error: missing bond-length plan output.");
        }
        return false;
    }
    *plan = {};
    plan->targetLength = targetLength;

    for (int atomId : m_selectedAtomIds) {
        if (std::find(group.atomIds.begin(), group.atomIds.end(), atomId) != group.atomIds.end()
            && findAtomByIdInStructure(m_structure, atomId) != nullptr) {
            plan->selectedAtomIds.push_back(atomId);
        }
    }
    if (plan->selectedAtomIds.size() < 2) {
        if (errorMessage != nullptr) {
            *errorMessage = m_japanese
                ? QStringLiteral("同じ吸着分子グループ内で、結合の両端2原子を選択してください。")
                : QStringLiteral("Select two bond-end atoms in the same adsorbate group.");
        }
        return false;
    }

    const int anchorId = plan->selectedAtomIds[0];
    const int movingSeedId = plan->selectedAtomIds[1];
    const NativeAtom* anchorAtom = findAtomByIdInStructure(m_structure, anchorId);
    const NativeAtom* seedAtom = findAtomByIdInStructure(m_structure, movingSeedId);
    if (anchorAtom == nullptr || seedAtom == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = m_japanese
                ? QStringLiteral("選択した結合端原子を現在の構造から見つけられません。")
                : QStringLiteral("The selected bond-end atoms are missing from the current structure.");
        }
        return false;
    }
    const QVector3D bond = seedAtom->cartesian - anchorAtom->cartesian;
    const double currentLength = bond.length();
    if (targetLength <= 0.0 || currentLength <= 1.0e-8) {
        if (errorMessage != nullptr) {
            *errorMessage = m_japanese
                ? QStringLiteral("現在の結合長または目標結合長が無効です。")
                : QStringLiteral("The current or target bond length is invalid.");
        }
        return false;
    }

    const auto atoms = poseAtoms(group);
    const int nodeCount = static_cast<int>(atoms.size());
    auto indexOfId = [&atoms](int atomId) {
        for (int i = 0; i < static_cast<int>(atoms.size()); ++i) {
            if (atoms[static_cast<std::size_t>(i)]->atomId == atomId) {
                return i;
            }
        }
        return -1;
    };
    const int anchorIndex = indexOfId(anchorId);
    const int seedIndex = indexOfId(movingSeedId);
    if (anchorIndex < 0 || seedIndex < 0) {
        if (errorMessage != nullptr) {
            *errorMessage = m_japanese
                ? QStringLiteral("選択した結合端原子がポーズグループ内にありません。")
                : QStringLiteral("The selected bond-end atoms are not in the pose group.");
        }
        return false;
    }

    std::vector<std::vector<int>> adjacency(static_cast<std::size_t>(nodeCount));
    for (int i = 0; i < nodeCount; ++i) {
        for (int j = i + 1; j < nodeCount; ++j) {
            if ((i == anchorIndex && j == seedIndex) || (i == seedIndex && j == anchorIndex)) {
                continue;
            }
            double cutoff = 0.0;
            if (!vestaBondCutoff(atoms[static_cast<std::size_t>(i)]->element, atoms[static_cast<std::size_t>(j)]->element, &cutoff) || cutoff <= 0.0) {
                cutoff = (vestaElementRadius(atoms[static_cast<std::size_t>(i)]->element)
                    + vestaElementRadius(atoms[static_cast<std::size_t>(j)]->element)) * 0.85;
            }
            const double distance = (atoms[static_cast<std::size_t>(i)]->cartesian - atoms[static_cast<std::size_t>(j)]->cartesian).length();
            if (distance <= cutoff + 0.15) {
                adjacency[static_cast<std::size_t>(i)].push_back(j);
                adjacency[static_cast<std::size_t>(j)].push_back(i);
            }
        }
    }

    std::vector<bool> movableMask(static_cast<std::size_t>(nodeCount), false);
    std::deque<int> queue;
    movableMask[static_cast<std::size_t>(seedIndex)] = true;
    queue.push_back(seedIndex);
    while (!queue.empty()) {
        const int node = queue.front();
        queue.pop_front();
        for (int next : adjacency[static_cast<std::size_t>(node)]) {
            if (!movableMask[static_cast<std::size_t>(next)]) {
                movableMask[static_cast<std::size_t>(next)] = true;
                queue.push_back(next);
            }
        }
    }

    if (movableMask[static_cast<std::size_t>(anchorIndex)]) {
        if (plan->selectedAtomIds.size() <= 2) {
            if (errorMessage != nullptr) {
                *errorMessage = m_japanese
                    ? QStringLiteral("対象結合を除いても分子グラフが分離しません。環状分子などでは、可動側原子群も追加選択してください。")
                    : QStringLiteral("Removing the selected bond does not split the molecular graph. For rings, also select the movable-side atoms explicitly.");
            }
            return false;
        }
        std::fill(movableMask.begin(), movableMask.end(), false);
        for (std::size_t i = 2; i < plan->selectedAtomIds.size(); ++i) {
            const int index = indexOfId(plan->selectedAtomIds[i]);
            if (index >= 0 && index != anchorIndex) {
                movableMask[static_cast<std::size_t>(index)] = true;
            }
        }
        movableMask[static_cast<std::size_t>(seedIndex)] = true;
    }

    plan->delta = bond.normalized() * static_cast<float>(targetLength - currentLength);
    for (int i = 0; i < nodeCount; ++i) {
        if (movableMask[static_cast<std::size_t>(i)]) {
            plan->movableAtomIds.push_back(atoms[static_cast<std::size_t>(i)]->atomId);
        }
    }
    return true;
}

std::vector<NativeAtom> MainWindow::placementPreviewAtoms() const {
    std::vector<NativeAtom> previewAtoms;
    if (m_canvas == nullptr || m_previewPlacementCheck == nullptr || !m_previewPlacementCheck->isChecked()) {
        return previewAtoms;
    }
    const SurfacePlacementRule rule = currentPlacementRule();
    const int required = rule.selectionCount > 0 ? rule.selectionCount : defaultSelectionCountForMode(rule.mode);
    if (static_cast<int>(m_selectedAtomIds.size()) < required) {
        return previewAtoms;
    }

    QString errorMessage;
    const auto preview = addPlacementAtom(m_structure, m_selectedAtomIds, rule, &errorMessage);
    if (!errorMessage.isEmpty() || preview.atoms.size() <= m_structure.atoms.size()) {
        return previewAtoms;
    }

    previewAtoms.insert(previewAtoms.end(),
        preview.atoms.begin() + static_cast<std::ptrdiff_t>(m_structure.atoms.size()),
        preview.atoms.end());
    for (std::size_t i = 0; i < previewAtoms.size(); ++i) {
        previewAtoms[i].atomId = -100000 - static_cast<int>(i);
        previewAtoms[i].tag = (i == 0)
            ? (m_japanese ? QStringLiteral("配置プレビュー") : QStringLiteral("placement preview"))
            : QString();
    }
    return previewAtoms;
}

std::vector<NativeAtom> MainWindow::posePreviewAtoms(QString* errorMessage) const {
    std::vector<NativeAtom> previewAtoms;
    if (m_canvas == nullptr || m_previewPoseCheck == nullptr || !m_previewPoseCheck->isChecked()) {
        return previewAtoms;
    }
    const PoseGroup* group = currentPoseGroup();
    if (group == nullptr || group->atomIds.empty()) {
        return previewAtoms;
    }

    const QString mode = m_posePreviewModeCombo != nullptr
        ? m_posePreviewModeCombo->currentData().toString()
        : QStringLiteral("translate");
    const auto atoms = poseAtoms(*group);
    if (atoms.empty()) {
        return previewAtoms;
    }

    auto appendPreviewAtom = [&](const NativeAtom& source, const QVector3D& cartesian) {
        NativeAtom atom = source;
        atom.atomId = -200000 - static_cast<int>(previewAtoms.size());
        atom.cartesian = cartesian;
        atom.fractional = solveFractionalForCell(m_structure.cellVectors, atom.cartesian);
        atom.tag = previewAtoms.empty()
            ? (m_japanese ? QStringLiteral("ポーズプレビュー") : QStringLiteral("pose preview"))
            : QString();
        previewAtoms.push_back(atom);
    };

    if (mode == QStringLiteral("rotate")) {
        const double angle = m_poseAngleSpin != nullptr ? m_poseAngleSpin->value() : 0.0;
        if (std::abs(angle) <= 1.0e-10) {
            return previewAtoms;
        }
        const QString axisKey = m_poseAxisCombo != nullptr
            ? m_poseAxisCombo->currentData().toString()
            : QStringLiteral("slab_normal");
        std::array<const NativeAtom*, 2> selectedBond{nullptr, nullptr};
        if (isSelectedBondAxisKey(axisKey)) {
            selectedBond = selectedPoseBondAtoms(*group);
            if (selectedBond[0] == nullptr || selectedBond[1] == nullptr) {
                if (errorMessage != nullptr) {
                    *errorMessage = m_japanese
                        ? QStringLiteral("結合軸回転プレビューには、同じポーズグループ内で軸にする2原子を選択してください。")
                        : QStringLiteral("Select the two axis atoms within the same pose group to preview bond-axis rotation.");
                }
                return previewAtoms;
            }
        }
        const QVector3D axis = poseRotationAxis(*group);
        if (axis.lengthSquared() <= 1.0e-12f) {
            if (errorMessage != nullptr) {
                *errorMessage = m_japanese ? QStringLiteral("回転軸を決定できません。") : QStringLiteral("Could not determine a rotation axis.");
            }
            return previewAtoms;
        }
        const QVector3D pivot = isSelectedBondAxisKey(axisKey) && selectedBond[0] != nullptr
            ? selectedBond[0]->cartesian
            : posePivotPosition(*group);
        const QQuaternion rotation = QQuaternion::fromAxisAndAngle(axis.normalized(), static_cast<float>(angle));
        for (const NativeAtom* atom : atoms) {
            appendPreviewAtom(*atom, pivot + rotation.rotatedVector(atom->cartesian - pivot));
        }
        return previewAtoms;
    }

    if (mode == QStringLiteral("bond_length")) {
        PoseBondLengthPlan plan;
        const double targetLength = m_poseBondLengthSpin != nullptr ? m_poseBondLengthSpin->value() : 0.0;
        if (!buildPoseBondLengthPlan(*group, targetLength, &plan, errorMessage)
            || plan.delta.lengthSquared() <= 1.0e-12f) {
            return previewAtoms;
        }
        for (const NativeAtom* atom : atoms) {
            const bool movable = std::find(plan.movableAtomIds.begin(), plan.movableAtomIds.end(), atom->atomId) != plan.movableAtomIds.end();
            appendPreviewAtom(*atom, atom->cartesian + (movable ? plan.delta : QVector3D()));
        }
        return previewAtoms;
    }

    const QVector3D totalDelta = currentPoseTranslationDelta();
    if (totalDelta.lengthSquared() <= 1.0e-12f) {
        return previewAtoms;
    }
    for (const NativeAtom* atom : atoms) {
        appendPreviewAtom(*atom, atom->cartesian + totalDelta);
    }
    return previewAtoms;
}

void MainWindow::updatePreviewAtoms() {
    if (m_canvas == nullptr) {
        return;
    }
    std::vector<NativeAtom> previewAtoms = placementPreviewAtoms();
    std::vector<NativeAtom> poseAtoms = posePreviewAtoms();
    previewAtoms.insert(previewAtoms.end(), poseAtoms.begin(), poseAtoms.end());
    m_canvas->setPreviewAtoms(previewAtoms);
}

void MainWindow::refreshPoseUi() {
    if (m_poseGroupCombo == nullptr) {
        return;
    }

    const int previousIndex = m_poseGroupCombo->currentData().toInt();
    const bool needsGroupComboRebuild = m_poseGroupCombo->count() == 0
        || m_poseGroupCombo->count() != (m_poseGroups.empty() ? 1 : static_cast<int>(m_poseGroups.size()))
        || (!m_poseGroups.empty() && previousIndex < 0);
    if (needsGroupComboRebuild) {
        m_poseGroupCombo->blockSignals(true);
        m_poseGroupCombo->clear();
        if (m_poseGroups.empty()) {
            m_poseGroupCombo->addItem(m_japanese ? QStringLiteral("未作成") : QStringLiteral("None"), -1);
            m_poseGroupCombo->setEnabled(false);
        } else {
            m_poseGroupCombo->setEnabled(true);
            for (std::size_t i = 0; i < m_poseGroups.size(); ++i) {
                const auto& group = m_poseGroups[i];
                m_poseGroupCombo->addItem(QStringLiteral("%1 (%2)")
                    .arg(group.name)
                    .arg(static_cast<int>(group.atomIds.size())), static_cast<int>(i));
            }
            const int row = m_poseGroupCombo->findData(previousIndex >= 0 ? previousIndex : static_cast<int>(m_poseGroups.size()) - 1);
            m_poseGroupCombo->setCurrentIndex(row >= 0 ? row : 0);
        }
        m_poseGroupCombo->blockSignals(false);
    }

    PoseGroup* group = currentPoseGroup();
    const bool hasGroup = group != nullptr && !group->atomIds.empty();
    if (m_posePivotCombo != nullptr) {
        const QString previousPivot = m_posePivotCombo->currentData().toString();
        m_posePivotCombo->blockSignals(true);
        m_posePivotCombo->clear();
        if (hasGroup) {
            m_posePivotCombo->addItem(m_japanese ? QStringLiteral("グループ重心") : QStringLiteral("Group centroid"), QStringLiteral("centroid"));
            m_posePivotCombo->addItem(m_japanese ? QStringLiteral("質量中心") : QStringLiteral("Mass center"), QStringLiteral("mass_center"));
            for (int atomId : group->atomIds) {
                if (const NativeAtom* atom = findAtomByIdInStructure(m_structure, atomId)) {
                    m_posePivotCombo->addItem(QStringLiteral("#%1 %2").arg(atom->atomId).arg(atom->element), QStringLiteral("atom:%1").arg(atom->atomId));
                }
            }
            QString preferred = previousPivot;
            if (preferred.isEmpty() && group->pivotAtomId > 0) {
                preferred = QStringLiteral("atom:%1").arg(group->pivotAtomId);
            }
            const int row = m_posePivotCombo->findData(preferred);
            m_posePivotCombo->setCurrentIndex(row >= 0 ? row : 0);
        } else {
            m_posePivotCombo->addItem(QStringLiteral("-"), QString());
        }
        m_posePivotCombo->setEnabled(hasGroup);
        m_posePivotCombo->blockSignals(false);
    }

    const int liveAtomCount = hasGroup ? static_cast<int>(poseAtoms(*group).size()) : 0;
    if (m_poseStatusLabel != nullptr) {
        if (!hasGroup) {
            m_poseStatusLabel->setText(m_japanese
                ? QStringLiteral("吸着分子グループ: 未作成\n選択原子を2個以上選び、グループ化してください。")
                : QStringLiteral("Adsorbate pose group: none\nSelect 2+ atoms and create a group."));
        } else {
            m_poseStatusLabel->setText(m_japanese
                ? QStringLiteral("吸着分子グループ: %1\n登録 %2 原子 / 現存 %3 原子。並進はXYZ/cell/法線方向を明示指定、回転はpivot固定です。")
                    .arg(group->name)
                    .arg(static_cast<int>(group->atomIds.size()))
                    .arg(liveAtomCount)
                : QStringLiteral("Adsorbate pose group: %1\nRegistered %2 atom(s) / live %3 atom(s). Translation is explicit XYZ/cell/normal; rotation keeps the pivot fixed.")
                    .arg(group->name)
                    .arg(static_cast<int>(group->atomIds.size()))
                    .arg(liveAtomCount));
        }
    }
    for (auto* button : {m_applyPoseTranslationButton, m_applyPoseRotationButton, m_resetPoseButton,
             m_exportPoseXyzButton, m_exportPoseJsonButton, m_exportPoseSnippetButton}) {
        if (button != nullptr) {
            button->setEnabled(hasGroup && liveAtomCount > 0);
        }
    }
    bool hasSelectedBond = false;
    if (hasGroup) {
        const auto selectedInGroup = selectedPoseBondAtoms(*group);
        hasSelectedBond = selectedInGroup[0] != nullptr && selectedInGroup[1] != nullptr;
        if (hasSelectedBond && m_poseBondLengthSpin != nullptr && !m_poseBondLengthSpin->hasFocus()) {
            m_poseBondLengthSpin->setValue(static_cast<double>((selectedInGroup[1]->cartesian - selectedInGroup[0]->cartesian).length()));
        }
    }
    const QString activeAxisKey = m_poseAxisCombo != nullptr ? m_poseAxisCombo->currentData().toString() : QString();
    const bool selectedBondAxis = isSelectedBondAxisKey(activeAxisKey);
    if (m_applyPoseRotationButton != nullptr) {
        m_applyPoseRotationButton->setEnabled(hasGroup && liveAtomCount > 0 && (!selectedBondAxis || hasSelectedBond));
        m_applyPoseRotationButton->setToolTip(!selectedBondAxis || hasSelectedBond
            ? uiText(QStringLiteral("pose_tip"))
            : (m_japanese
                ? QStringLiteral("結合軸回転には、同じ吸着分子グループ内で軸にする2原子を選択してください。")
                : QStringLiteral("Select the two axis atoms within the same pose group to rotate around that bond axis.")));
    }
    if (m_applyPoseBondLengthButton != nullptr) {
        m_applyPoseBondLengthButton->setEnabled(hasGroup && hasSelectedBond);
        m_applyPoseBondLengthButton->setToolTip(hasSelectedBond
            ? uiText(QStringLiteral("pose_tip"))
            : (m_japanese
                ? QStringLiteral("同じ吸着分子グループ内で結合の両端2原子を選択してください。")
                : QStringLiteral("Select the two bond-end atoms within the same pose group.")));
    }
    if (m_createPoseGroupButton != nullptr) {
        m_createPoseGroupButton->setEnabled(m_selectedAtomIds.size() >= 2);
    }
    if (m_previewPoseCheck != nullptr) {
        m_previewPoseCheck->setEnabled(hasGroup && liveAtomCount > 0);
    }
    if (m_posePreviewModeCombo != nullptr) {
        m_posePreviewModeCombo->setEnabled(hasGroup && liveAtomCount > 0
            && m_previewPoseCheck != nullptr && m_previewPoseCheck->isChecked());
    }
    updatePreviewAtoms();
}

bool MainWindow::maybeSaveChanges() {
    if (!m_structure.dirty) return true;
    const auto reply = QMessageBox::question(this, "Unsaved changes", "The current structure has unsaved changes. Discard them?", QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    return reply == QMessageBox::Yes;
}

QString MainWindow::defaultOpenDirectory() const {
    return m_structure.sourcePath.isEmpty() ? QDir::homePath() : QFileInfo(m_structure.sourcePath).absolutePath();
}

QString MainWindow::formatVector(const QVector3D& vec) const {
    return QString("(%1, %2, %3)").arg(vec.x(), 0, 'f', 2).arg(vec.y(), 0, 'f', 2).arg(vec.z(), 0, 'f', 2);
}

QString MainWindow::describePlacementRule(const SurfacePlacementRule& rule) const {
    const QString mode = rule.mode.trimmed().toLower();
    QStringList lines;
    lines << QStringLiteral("Name: %1").arg(rule.name.isEmpty() ? rule.mode : rule.name);
    if (!rule.description.isEmpty()) lines << QStringLiteral("Info: %1").arg(rule.description);
    lines << QStringLiteral("Element: %1").arg(rule.element.isEmpty() ? QStringLiteral("H") : rule.element);
    lines << QStringLiteral("Height offset: %1 Å").arg(rule.height, 0, 'f', 3);
    const int selectionCount = rule.selectionCount > 0 ? rule.selectionCount : defaultSelectionCountForMode(rule.mode);
    lines << QStringLiteral("Selection count: %1 atom(s)").arg(selectionCount);
    if (mode == "pair_fraction") {
        lines << QStringLiteral("Interpolation fraction: %1").arg(rule.fraction, 0, 'f', 3);
    } else if (mode == "triple_weighted") {
        lines << QStringLiteral("Selection: 3 atoms, weights = [%1, %2, %3]")
            .arg(rule.weights[0], 0, 'f', 2)
            .arg(rule.weights[1], 0, 'f', 2)
            .arg(rule.weights[2], 0, 'f', 2);
    } else if (mode == "multi_weighted") {
        lines << QStringLiteral("Selection: all selected atoms, first weights = [%1, %2, %3], later atoms use 1.0")
            .arg(rule.weights[0], 0, 'f', 2)
            .arg(rule.weights[1], 0, 'f', 2)
            .arg(rule.weights[2], 0, 'f', 2);
    }
    if (mode == "single_above") lines << QStringLiteral("Placement: every selected atom + surface normal × height");
    else if (mode == "single_below") lines << QStringLiteral("Placement: every selected atom - surface normal × height");
    else if (mode == "selection_centroid") lines << QStringLiteral("Placement: center of all selected atoms");
    else if (mode == "pair_midpoint") lines << QStringLiteral("Placement: center of all selected atoms (legacy pair mode)");
    else if (mode == "pair_fraction") lines << QStringLiteral("Placement: center of all selected atoms (legacy pair-fraction alias)");
    else if (mode == "triple_centroid") lines << QStringLiteral("Placement: center of all selected atoms (legacy 3-atom mode)");
    else if (mode == "triple_weighted") lines << QStringLiteral("Placement: center of all selected atoms (legacy weighted alias)");
    else if (mode == "multi_centroid") lines << QStringLiteral("Placement: centroid of all selected atoms");
    else if (mode == "multi_weighted") lines << QStringLiteral("Placement: center of all selected atoms (legacy weighted alias)");
    else if (mode == "multi_plane_normal") lines << QStringLiteral("Placement: centroid of selected atoms + selected-plane normal × height");
    else lines << QStringLiteral("Placement: custom");
    lines << QStringLiteral("Note: height can be negative to place the atom on the opposite side.");
    return lines.join(QStringLiteral("\n"));
}

QString MainWindow::supercellStatusText() const {
    const qint64 baseAtoms = m_hasSupercellBaseStructure
        ? static_cast<qint64>(m_supercellBaseStructure.atoms.size())
        : static_cast<qint64>(m_structure.atoms.size());
    const qint64 currentAtoms = static_cast<qint64>(m_structure.atoms.size());
    if (hasNonIdentitySupercellFactors(m_supercellFactors)) {
        if (!m_lastEditWasSupercell) {
            return m_japanese
                ? QStringLiteral("現在: %1 × %2 × %3（編集後も倍率を保持）\n現在原子数: %4\n※次のスーパーセル作成では、現在の編集済み構造を新しい基準にします。")
                    .arg(m_supercellFactors[0])
                    .arg(m_supercellFactors[1])
                    .arg(m_supercellFactors[2])
                    .arg(currentAtoms)
                : QStringLiteral("Current: %1 × %2 × %3 (factors kept after edits)\nCurrent atoms: %4\nThe next supercell operation will use the current edited structure as the new base.")
                    .arg(m_supercellFactors[0])
                    .arg(m_supercellFactors[1])
                    .arg(m_supercellFactors[2])
                    .arg(currentAtoms);
        }
        return m_japanese
            ? QStringLiteral("現在: 初期/基準構造に対して %1 × %2 × %3\n基準原子数: %4 → 現在原子数: %5\n※真空層操作やセル軸傾き後も倍率を保持します。再実行時はこの基準から作り直せます。")
                .arg(m_supercellFactors[0])
                .arg(m_supercellFactors[1])
                .arg(m_supercellFactors[2])
                .arg(baseAtoms)
                .arg(currentAtoms)
            : QStringLiteral("Current: %1 × %2 × %3 relative to the initial/base structure\nBase atoms: %4 → current atoms: %5\nVacuum operations and cell-axis tilt keep these factors. Re-running regenerates from this base.")
                .arg(m_supercellFactors[0])
                .arg(m_supercellFactors[1])
                .arg(m_supercellFactors[2])
                .arg(baseAtoms)
                .arg(currentAtoms);
    }
    return m_japanese
        ? QStringLiteral("現在: 初期/基準構造に対して 1 × 1 × 1\n次のスーパーセル作成では、現在の編集済み構造を新しい基準にします。")
        : QStringLiteral("Current: 1 × 1 × 1 relative to the initial/base structure\nThe next supercell operation will use the current edited structure as the new base.");
}

void MainWindow::setSelectedAtomIds(const std::vector<int>& atomIds) {
    m_selectedAtomIds = atomIds;
    m_canvas->setSelectedAtomIds(m_selectedAtomIds);
    m_canvas->focusAtom(m_selectedAtomIds.empty() ? -1 : m_selectedAtomIds.back());
}

bool MainWindow::runAdsorbatePoseSelfTest(const QString& outputDirectory, QString* errorMessage) {
    QDir outputDir(outputDirectory);
    if (!outputDir.mkpath(QStringLiteral("."))) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Failed to create self-test output directory: %1").arg(outputDirectory);
        }
        return false;
    }

    QStringList steps;
    QStringList artifacts;
    auto writeReport = [&](bool ok, const QString& failure) {
        QJsonObject root;
        root["ok"] = ok;
        root["failure"] = failure;
        root["sample"] = QStringLiteral("methanol_on_cu_slab");
        QJsonArray stepArray;
        for (const QString& step : steps) {
            stepArray.append(step);
        }
        root["steps"] = stepArray;
        QJsonArray artifactArray;
        for (const QString& artifact : artifacts) {
            artifactArray.append(artifact);
        }
        root["artifacts"] = artifactArray;
        QFile reportFile(outputDir.filePath(QStringLiteral("adsorbate_pose_self_test_report.json")));
        if (reportFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            reportFile.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        }
    };
    auto fail = [&](const QString& message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        steps << QStringLiteral("FAIL: %1").arg(message);
        writeReport(false, message);
        return false;
    };
    auto require = [&](bool condition, const QString& message) {
        if (condition) {
            steps << QStringLiteral("PASS: %1").arg(message);
            return true;
        }
        return false;
    };
    auto close = [](double actual, double expected, double tolerance = 1.0e-5) {
        return std::abs(actual - expected) <= tolerance;
    };
    auto closeVector = [](const QVector3D& actual, const QVector3D& expected, double tolerance = 1.0e-5) {
        return static_cast<double>((actual - expected).length()) <= tolerance;
    };
    auto atomPosition = [](const StructureData& structure, int atomId) {
        if (const NativeAtom* atom = findAtomByIdInStructure(structure, atomId)) {
            return atom->cartesian;
        }
        return QVector3D();
    };

    m_structure = adsorbatePoseSelfTestStructure();
    m_structure.sourcePath = outputDir.filePath(QStringLiteral("adsorbate_pose_self_test_input.extxyz"));
    m_structure.dirty = false;
    m_supercellBaseStructure = m_structure;
    m_hasSupercellBaseStructure = true;
    m_lastEditWasSupercell = false;
    m_supercellFactors = {1, 1, 1};
    m_undoStack.clear();
    m_redoStack.clear();
    m_poseGroups.clear();
    m_customBondRanges.clear();
    applyStructureState(m_structure);

    const StructureData initial = m_structure;
    const int baselineBondCount = m_canvas != nullptr ? m_canvas->bondCount() : 0;
    m_customBondRanges.insert(vestaBondKey(QStringLiteral("Cu"), QStringLiteral("C")), BondDistanceRange{0.0, 5.0});
    syncCanvasDisplayOptions();
    const int expandedBondCount = m_canvas != nullptr ? m_canvas->bondCount() : 0;
    if (!require(expandedBondCount > baselineBondCount, QStringLiteral("custom bond distance range can add VESTA-style element-pair bonds"))) {
        return fail(QStringLiteral("Custom Cu-C bond range did not add visible bonds."));
    }
    m_customBondRanges.insert(vestaBondKey(QStringLiteral("Cu"), QStringLiteral("C")), BondDistanceRange{4.5, 5.0});
    syncCanvasDisplayOptions();
    const int filteredBondCount = m_canvas != nullptr ? m_canvas->bondCount() : 0;
    if (!require(filteredBondCount < expandedBondCount, QStringLiteral("custom bond minimum distance can filter short pair bonds"))) {
        return fail(QStringLiteral("Custom Cu-C minimum bond distance did not filter bonds."));
    }
    m_customBondRanges.clear();
    syncCanvasDisplayOptions();

    PoseGroup group;
    group.name = QStringLiteral("methanol");
    group.atomIds = {5, 6, 7, 8, 9, 10};
    for (int atomId : group.atomIds) {
        const NativeAtom* atom = findAtomByIdInStructure(m_structure, atomId);
        if (atom == nullptr) {
            return fail(QStringLiteral("Self-test sample is missing atom #%1").arg(atomId));
        }
        group.initialCartesian.push_back(atom->cartesian);
    }
    group.pivotAtomId = 5;
    m_poseGroups.push_back(group);
    refreshPoseUi();
    if (m_poseGroupCombo != nullptr) {
        const int row = m_poseGroupCombo->findData(0);
        if (row >= 0) {
            m_poseGroupCombo->setCurrentIndex(row);
        }
    }
    setSelectedAtomIds(group.atomIds);
    refreshPoseUi();
    if (!require(currentPoseGroup() != nullptr, QStringLiteral("pose group is available through GUI state"))) {
        return fail(QStringLiteral("Pose group combo did not select the methanol group."));
    }

    const double coInitial = atomDistance(m_structure, 5, 6);
    const double chInitial = atomDistance(m_structure, 5, 7);
    const double ohInitial = atomDistance(m_structure, 6, 10);
    const QVector3D translationDelta(1.25f, -0.50f, 0.75f);
    if (m_poseDxSpin != nullptr) m_poseDxSpin->setValue(translationDelta.x());
    if (m_poseDySpin != nullptr) m_poseDySpin->setValue(translationDelta.y());
    if (m_poseDzSpin != nullptr) m_poseDzSpin->setValue(translationDelta.z());
    if (m_previewPoseCheck != nullptr) {
        m_previewPoseCheck->setChecked(true);
    }
    if (m_posePreviewModeCombo != nullptr) {
        const int row = m_posePreviewModeCombo->findData(QStringLiteral("translate"));
        if (row >= 0) {
            m_posePreviewModeCombo->setCurrentIndex(row);
        }
    }
    QString previewError;
    const auto translationPreview = posePreviewAtoms(&previewError);
    if (!require(previewError.isEmpty(), QStringLiteral("pose translation preview reports no error"))) {
        return fail(previewError);
    }
    if (!require(translationPreview.size() == group.atomIds.size(), QStringLiteral("pose translation preview covers the whole adsorbate group"))) {
        return fail(QStringLiteral("Translation preview atom count did not match the pose group."));
    }
    if (!require(closeVector(translationPreview.front().cartesian, atomPosition(m_structure, 5) + translationDelta),
            QStringLiteral("pose translation preview shows the post-apply pivot position"))) {
        return fail(QStringLiteral("Translation preview did not show the requested position."));
    }
    if (!require(closeVector(atomPosition(m_structure, 5), atomPosition(initial, 5)), QStringLiteral("pose preview does not mutate coordinates before Apply"))) {
        return fail(QStringLiteral("Pose preview changed coordinates before Apply."));
    }
    if (m_posePreviewModeCombo != nullptr) {
        const int row = m_posePreviewModeCombo->findData(QStringLiteral("rotate"));
        if (row >= 0) {
            m_posePreviewModeCombo->setCurrentIndex(row);
        }
    }
    if (m_poseAxisCombo != nullptr) {
        const int row = m_poseAxisCombo->findData(QStringLiteral("global_z"));
        if (row >= 0) {
            m_poseAxisCombo->setCurrentIndex(row);
        }
    }
    if (m_poseAngleSpin != nullptr) {
        m_poseAngleSpin->setValue(45.0);
    }
    previewError.clear();
    const auto rotationPreview = posePreviewAtoms(&previewError);
    if (!require(previewError.isEmpty(), QStringLiteral("pose rotation preview reports no error"))) {
        return fail(previewError);
    }
    if (!require(rotationPreview.size() == group.atomIds.size(), QStringLiteral("pose rotation preview covers the whole adsorbate group"))) {
        return fail(QStringLiteral("Rotation preview atom count did not match the pose group."));
    }
    if (!require((rotationPreview[1].cartesian - atomPosition(m_structure, 6)).length() > 1.0e-4f,
            QStringLiteral("pose rotation preview moves off-pivot atoms without Apply"))) {
        return fail(QStringLiteral("Rotation preview did not move an off-pivot atom."));
    }
    if (!require(closeVector(atomPosition(m_structure, 6), atomPosition(initial, 6)), QStringLiteral("rotation preview also keeps actual coordinates unchanged"))) {
        return fail(QStringLiteral("Rotation preview changed actual coordinates."));
    }
    if (m_poseAngleSpin != nullptr) {
        m_poseAngleSpin->setValue(0.0);
    }
    if (m_posePreviewModeCombo != nullptr) {
        const int row = m_posePreviewModeCombo->findData(QStringLiteral("translate"));
        if (row >= 0) {
            m_posePreviewModeCombo->setCurrentIndex(row);
        }
    }
    applyPoseTranslation();
    const StructureData translated = m_structure;
    if (!require(closeVector(atomPosition(translated, 5), atomPosition(initial, 5) + translationDelta), QStringLiteral("rigid translation moves adsorbate atoms by the requested XYZ delta"))) {
        return fail(QStringLiteral("Rigid translation did not move the pivot atom by the requested delta."));
    }
    if (!require(closeVector(atomPosition(translated, 1), atomPosition(initial, 1)), QStringLiteral("rigid translation leaves slab atoms fixed"))) {
        return fail(QStringLiteral("Slab atom moved during adsorbate translation."));
    }
    if (!require(!m_poseGroups.empty() && closeVector(m_poseGroups.front().translation, translationDelta), QStringLiteral("pose translation metadata is updated"))) {
        return fail(QStringLiteral("Pose translation metadata was not updated."));
    }
    if (!require(close(atomDistance(translated, 5, 6), coInitial) && close(atomDistance(translated, 5, 7), chInitial), QStringLiteral("translation preserves internal methanol distances"))) {
        return fail(QStringLiteral("Internal distances changed during rigid translation."));
    }

    if (m_posePivotCombo != nullptr) {
        const int row = m_posePivotCombo->findData(QStringLiteral("atom:5"));
        if (row >= 0) {
            m_posePivotCombo->setCurrentIndex(row);
        }
    }
    if (m_poseAxisCombo != nullptr) {
        const int row = m_poseAxisCombo->findData(QStringLiteral("global_z"));
        if (row >= 0) {
            m_poseAxisCombo->setCurrentIndex(row);
        }
    }
    if (m_poseAngleSpin != nullptr) {
        m_poseAngleSpin->setValue(90.0);
    }
    const QVector3D pivotBeforeRotation = atomPosition(m_structure, 5);
    applyPoseRotation();
    const StructureData rotated = m_structure;
    const QVector4D rotatedQuaternion = m_poseGroups.empty() ? QVector4D() : m_poseGroups.front().rotationQuaternion;
    if (!require(closeVector(atomPosition(rotated, 5), pivotBeforeRotation), QStringLiteral("pivot atom remains fixed during rotation"))) {
        return fail(QStringLiteral("Pivot atom moved during pose rotation."));
    }
    if (!require(close(atomDistance(rotated, 5, 6), coInitial) && close(atomDistance(rotated, 5, 7), chInitial), QStringLiteral("rotation preserves internal methanol distances"))) {
        return fail(QStringLiteral("Internal distances changed during rigid rotation."));
    }
    if (!require(!m_poseGroups.empty() && !close(m_poseGroups.front().rotationQuaternion.x(), 1.0, 1.0e-4), QStringLiteral("pose rotation quaternion metadata is updated"))) {
        return fail(QStringLiteral("Pose rotation quaternion did not update."));
    }

    undoEdit();
    if (!require(closeVector(atomPosition(m_structure, 6), atomPosition(translated, 6)), QStringLiteral("Undo restores coordinates before rotation"))) {
        return fail(QStringLiteral("Undo did not restore the pre-rotation coordinates."));
    }
    if (!require(!m_poseGroups.empty() && close(m_poseGroups.front().rotationQuaternion.x(), 1.0, 1.0e-5), QStringLiteral("Undo restores pose rotation metadata"))) {
        return fail(QStringLiteral("Undo did not restore pose rotation metadata."));
    }
    redoEdit();
    if (!require(closeVector(atomPosition(m_structure, 6), atomPosition(rotated, 6)), QStringLiteral("Redo restores rotated coordinates"))) {
        return fail(QStringLiteral("Redo did not restore rotated coordinates."));
    }
    if (!require(!m_poseGroups.empty() && close(m_poseGroups.front().rotationQuaternion.x(), rotatedQuaternion.x(), 1.0e-5), QStringLiteral("Redo restores pose rotation metadata"))) {
        return fail(QStringLiteral("Redo did not restore pose rotation metadata."));
    }

    setSelectedAtomIds({5, 6});
    refreshPoseUi();
    if (m_posePivotCombo != nullptr) {
        const int row = m_posePivotCombo->findData(QStringLiteral("centroid"));
        if (row >= 0) {
            m_posePivotCombo->setCurrentIndex(row);
        }
    }
    if (m_poseAxisCombo != nullptr) {
        const int row = m_poseAxisCombo->findData(QStringLiteral("bond_selected_two"));
        if (row >= 0) {
            m_poseAxisCombo->setCurrentIndex(row);
        }
    }
    if (m_poseAngleSpin != nullptr) {
        m_poseAngleSpin->setValue(120.0);
    }
    const QVector3D cBeforeBondAxis = atomPosition(m_structure, 5);
    const QVector3D oBeforeBondAxis = atomPosition(m_structure, 6);
    const QVector3D hBeforeBondAxis = atomPosition(m_structure, 7);
    applyPoseRotation();
    const StructureData bondAxisRotated = m_structure;
    if (!require(closeVector(atomPosition(bondAxisRotated, 5), cBeforeBondAxis)
            && closeVector(atomPosition(bondAxisRotated, 6), oBeforeBondAxis),
            QStringLiteral("selected-bond-axis rotation keeps both selected axis atoms fixed"))) {
        return fail(QStringLiteral("Selected bond-axis rotation moved an axis atom."));
    }
    if (!require((atomPosition(bondAxisRotated, 7) - hBeforeBondAxis).length() > 1.0e-4f,
            QStringLiteral("selected-bond-axis rotation moves off-axis adsorbate atoms"))) {
        return fail(QStringLiteral("Selected bond-axis rotation did not move an off-axis atom."));
    }
    if (!require(close(atomDistance(bondAxisRotated, 5, 6), coInitial)
            && close(atomDistance(bondAxisRotated, 5, 7), chInitial),
            QStringLiteral("selected-bond-axis rotation preserves internal distances"))) {
        return fail(QStringLiteral("Internal distances changed during selected bond-axis rotation."));
    }

    const double coBeforeBondEdit = atomDistance(m_structure, 5, 6);
    const double ohBeforeBondEdit = atomDistance(m_structure, 6, 10);
    const double targetCoLength = coBeforeBondEdit + 0.25;
    setSelectedAtomIds({5, 6, 10});
    refreshPoseUi();
    if (m_poseBondLengthSpin != nullptr) {
        m_poseBondLengthSpin->setValue(targetCoLength);
    }
    if (m_previewPoseCheck != nullptr) {
        m_previewPoseCheck->setChecked(true);
    }
    if (m_posePreviewModeCombo != nullptr) {
        const int row = m_posePreviewModeCombo->findData(QStringLiteral("bond_length"));
        if (row >= 0) {
            m_posePreviewModeCombo->setCurrentIndex(row);
        }
    }
    previewError.clear();
    const auto bondLengthPreview = posePreviewAtoms(&previewError);
    if (!require(previewError.isEmpty(), QStringLiteral("pose bond-length preview reports no error"))) {
        return fail(previewError);
    }
    if (!require(bondLengthPreview.size() == group.atomIds.size(), QStringLiteral("pose bond-length preview covers the whole adsorbate group"))) {
        return fail(QStringLiteral("Bond-length preview atom count did not match the pose group."));
    }
    if (!require(close(static_cast<double>((bondLengthPreview[0].cartesian - bondLengthPreview[1].cartesian).length()), targetCoLength, 1.0e-4),
            QStringLiteral("pose bond-length preview reaches the requested C-O length"))) {
        return fail(QStringLiteral("Bond-length preview did not reach the requested target."));
    }
    if (!require(close(static_cast<double>((bondLengthPreview[1].cartesian - bondLengthPreview[5].cartesian).length()), ohBeforeBondEdit, 1.0e-5),
            QStringLiteral("pose bond-length preview keeps the moving side rigid"))) {
        return fail(QStringLiteral("Bond-length preview did not keep the O-H side rigid."));
    }
    if (!require(close(atomDistance(m_structure, 5, 6), coBeforeBondEdit, 1.0e-4),
            QStringLiteral("bond-length preview does not mutate actual coordinates before Apply"))) {
        return fail(QStringLiteral("Bond-length preview changed actual coordinates before Apply."));
    }
    applyPoseBondLength();
    const StructureData bondAdjusted = m_structure;
    if (!require(close(atomDistance(bondAdjusted, 5, 6), targetCoLength, 1.0e-4), QStringLiteral("bond length adjustment reaches the requested C-O length"))) {
        return fail(QStringLiteral("C-O bond length adjustment did not reach the requested target."));
    }
    if (!require(close(atomDistance(bondAdjusted, 6, 10), ohBeforeBondEdit, 1.0e-5), QStringLiteral("bond length adjustment slides the O-H side rigidly"))) {
        return fail(QStringLiteral("O-H side did not remain rigid during bond length adjustment."));
    }
    undoEdit();
    if (!require(close(atomDistance(m_structure, 5, 6), coBeforeBondEdit, 1.0e-4), QStringLiteral("Undo restores pre-bond-edit length"))) {
        return fail(QStringLiteral("Undo did not restore pre-bond-edit C-O length."));
    }
    redoEdit();
    if (!require(close(atomDistance(m_structure, 5, 6), targetCoLength, 1.0e-4), QStringLiteral("Redo restores adjusted bond length"))) {
        return fail(QStringLiteral("Redo did not restore adjusted C-O length."));
    }

    auto verifySelectedCenterPlacement = [&](const std::vector<int>& selectedIds, const QString& label) {
        const StructureData beforePlacementCenter = m_structure;
        setSelectedAtomIds(selectedIds);
        if (m_placementModeCombo != nullptr) {
            const int row = m_placementModeCombo->findData(QStringLiteral("selection_centroid"));
            if (row >= 0) {
                m_placementModeCombo->setCurrentIndex(row);
            }
        }
        if (m_placementHeightSpin != nullptr) {
            m_placementHeightSpin->setValue(0.0);
        }
        applySelectedPreset();
        QVector3D expectedSelectedCenter;
        for (int atomId : selectedIds) {
            expectedSelectedCenter += atomPosition(beforePlacementCenter, atomId);
        }
        expectedSelectedCenter /= static_cast<float>(selectedIds.size());
        if (!require(m_structure.atoms.size() == beforePlacementCenter.atoms.size() + 1,
                QStringLiteral("%1 selected-atom center placement adds one atom").arg(label))) {
            return fail(QStringLiteral("%1 selected-atom center placement did not add exactly one atom.").arg(label));
        }
        if (!require(closeVector(m_structure.atoms.back().cartesian, expectedSelectedCenter, 1.0e-5),
                QStringLiteral("%1 selected-atom center placement uses the center of all selected atoms").arg(label))) {
            return fail(QStringLiteral("%1 selected-atom center placement did not use the selected-atom center.").arg(label));
        }
        undoEdit();
        if (!require(m_structure.atoms.size() == beforePlacementCenter.atoms.size(),
                QStringLiteral("%1 Undo restores atom count after selected-center placement").arg(label))) {
            return fail(QStringLiteral("%1 Undo did not restore atom count after selected-center placement.").arg(label));
        }
        return true;
    };
    if (!verifySelectedCenterPlacement({1, 2}, QStringLiteral("2-atom"))) {
        return false;
    }
    if (!verifySelectedCenterPlacement({1, 2, 3}, QStringLiteral("3-atom"))) {
        return false;
    }
    if (!verifySelectedCenterPlacement({1, 2, 3, 4}, QStringLiteral("4-atom"))) {
        return false;
    }

    const QString extxyzPath = outputDir.filePath(QStringLiteral("adsorbate_pose_self_test.extxyz"));
    QString writeError;
    if (!writeXyzFile(m_structure, extxyzPath, true, &writeError)) {
        return fail(writeError);
    }
    artifacts << extxyzPath;
    StructureFileLoader loader;
    QString loadError;
    const auto reloaded = loader.load(extxyzPath, &loadError);
    if (!reloaded.has_value()) {
        return fail(loadError.isEmpty() ? QStringLiteral("Failed to reload written extxyz.") : loadError);
    }
    if (!require(reloaded->atoms.size() == m_structure.atoms.size(), QStringLiteral("native extxyz reload preserves atom count"))) {
        return fail(QStringLiteral("Reloaded extxyz atom count mismatch."));
    }
    if (!require(closeVector(reloaded->cellVectors[0], m_structure.cellVectors[0]) && closeVector(reloaded->cellVectors[2], m_structure.cellVectors[2]), QStringLiteral("native extxyz reload preserves cell vectors"))) {
        return fail(QStringLiteral("Reloaded extxyz cell vector mismatch."));
    }
    if (!require(reloaded->atoms.back().atomId == 10 && reloaded->atoms.back().tag == QStringLiteral("methanol-Ho"), QStringLiteral("native extxyz reload preserves atom_id and tag properties"))) {
        return fail(QStringLiteral("Reloaded extxyz lost atom_id/tag properties."));
    }
    if (!require(closeVector(reloaded->atoms.back().cartesian, m_structure.atoms.back().cartesian, 1.0e-5), QStringLiteral("native extxyz reload preserves coordinates"))) {
        return fail(QStringLiteral("Reloaded extxyz coordinate mismatch."));
    }

    writeReport(true, QString());
    if (errorMessage != nullptr) {
        *errorMessage = QString();
    }
    return true;
}
