#include "StructureFileLoader.h"
#include "ElementStyle.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>
#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>

namespace {
QColor elementColor(const QString& element) {
    return vestaElementColor(element);
}

double elementRadius(const QString& element) {
    return vestaElementRadius(element);
}

QString readUtf8NoBom(const QString& path, QString* errorMessage) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to open %1").arg(path);
        }
        return {};
    }
    QByteArray bytes = file.readAll();
    if (bytes.startsWith("\xEF\xBB\xBF")) {
        bytes.remove(0, 3);
    }
    return QString::fromUtf8(bytes);
}

QString normalizeElement(QString text) {
    text = text.trimmed();
    if (text.isEmpty() || text == "?" || text == ".") {
        return QStringLiteral("X");
    }
    const QRegularExpression rx(QStringLiteral("([A-Za-z]{1,2})"));
    const auto match = rx.match(text);
    if (match.hasMatch()) {
        text = match.captured(1);
    }
    if (text.isEmpty()) {
        return QStringLiteral("X");
    }
    text = text.left(2);
    return text.left(1).toUpper() + text.mid(1).toLower();
}

double parseNumber(QString token, double fallback = 0.0) {
    token = token.trimmed();
    if (token.isEmpty() || token == "?" || token == ".") {
        return fallback;
    }
    token.remove(QRegularExpression(QStringLiteral("\\([^\\)]*\\)$")));
    bool ok = false;
    const double value = token.toDouble(&ok);
    return ok ? value : fallback;
}

QVector3D jsonVector(const QJsonArray& array) {
    return QVector3D(
        static_cast<float>(array.at(0).toDouble()),
        static_cast<float>(array.at(1).toDouble()),
        static_cast<float>(array.at(2).toDouble()));
}

QVector3D solveFractional(const std::array<QVector3D, 3>& cell, const QVector3D& cart) {
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

QVector3D toCartesian(const std::array<QVector3D, 3>& cell, const QVector3D& frac) {
    return cell[0] * frac.x() + cell[1] * frac.y() + cell[2] * frac.z();
}

struct CifSymmetryExpression {
    std::array<double, 3> coefficients{0.0, 0.0, 0.0};
    double offset = 0.0;
};

struct CifSymmetryOperation {
    std::array<CifSymmetryExpression, 3> expressions;
};

double parseCifLinearNumber(QString token, bool* ok) {
    token = token.trimmed();
    token.remove(QRegularExpression(QStringLiteral("\\([^\\)]*\\)$")));
    if (token.isEmpty()) {
        if (ok != nullptr) {
            *ok = false;
        }
        return 0.0;
    }

    const int slashIndex = token.indexOf(QLatin1Char('/'));
    if (slashIndex > 0) {
        bool numeratorOk = false;
        bool denominatorOk = false;
        const double numerator = token.left(slashIndex).toDouble(&numeratorOk);
        const double denominator = token.mid(slashIndex + 1).toDouble(&denominatorOk);
        const bool valid = numeratorOk && denominatorOk && std::abs(denominator) > 1.0e-12;
        if (ok != nullptr) {
            *ok = valid;
        }
        return valid ? numerator / denominator : 0.0;
    }

    bool localOk = false;
    const double value = token.toDouble(&localOk);
    if (ok != nullptr) {
        *ok = localOk;
    }
    return localOk ? value : 0.0;
}

std::optional<CifSymmetryExpression> parseCifSymmetryExpression(QString expression) {
    expression = expression.trimmed().toLower();
    expression.replace(QChar(0x2212), QLatin1Char('-'));
    expression.remove(QRegularExpression(QStringLiteral("\\s+")));
    if (expression.isEmpty()) {
        return std::nullopt;
    }
    if (!expression.startsWith(QLatin1Char('+')) && !expression.startsWith(QLatin1Char('-'))) {
        expression.prepend(QLatin1Char('+'));
    }

    CifSymmetryExpression parsed;
    int position = 0;
    while (position < expression.size()) {
        const QChar signChar = expression.at(position);
        if (signChar != QLatin1Char('+') && signChar != QLatin1Char('-')) {
            return std::nullopt;
        }
        const double sign = signChar == QLatin1Char('-') ? -1.0 : 1.0;
        ++position;
        const int termStart = position;
        while (position < expression.size()
               && expression.at(position) != QLatin1Char('+')
               && expression.at(position) != QLatin1Char('-')) {
            ++position;
        }
        QString term = expression.mid(termStart, position - termStart).trimmed();
        if (term.isEmpty()) {
            continue;
        }

        int variableIndex = -1;
        int variablePosition = -1;
        for (int axis = 0; axis < 3; ++axis) {
            const QChar variable = QStringLiteral("xyz").at(axis);
            const int found = term.indexOf(variable);
            if (found >= 0) {
                if (variableIndex >= 0) {
                    return std::nullopt;
                }
                variableIndex = axis;
                variablePosition = found;
            }
        }

        if (variableIndex >= 0) {
            QString prefix = term.left(variablePosition).trimmed();
            QString suffix = term.mid(variablePosition + 1).trimmed();
            if (prefix.endsWith(QLatin1Char('*'))) {
                prefix.chop(1);
            }
            double coefficient = 1.0;
            if (!prefix.isEmpty()) {
                bool coefficientOk = false;
                coefficient = parseCifLinearNumber(prefix, &coefficientOk);
                if (!coefficientOk) {
                    return std::nullopt;
                }
            }
            if (!suffix.isEmpty()) {
                if (suffix.startsWith(QLatin1Char('*'))) {
                    suffix.remove(0, 1);
                    bool multiplierOk = false;
                    const double multiplier = parseCifLinearNumber(suffix, &multiplierOk);
                    if (!multiplierOk) {
                        return std::nullopt;
                    }
                    coefficient *= multiplier;
                } else if (suffix.startsWith(QLatin1Char('/'))) {
                    suffix.remove(0, 1);
                    bool divisorOk = false;
                    const double divisor = parseCifLinearNumber(suffix, &divisorOk);
                    if (!divisorOk || std::abs(divisor) < 1.0e-12) {
                        return std::nullopt;
                    }
                    coefficient /= divisor;
                } else {
                    return std::nullopt;
                }
            }
            parsed.coefficients[static_cast<std::size_t>(variableIndex)] += sign * coefficient;
        } else {
            bool offsetOk = false;
            const double offset = parseCifLinearNumber(term, &offsetOk);
            if (!offsetOk) {
                return std::nullopt;
            }
            parsed.offset += sign * offset;
        }
    }
    return parsed;
}

std::optional<CifSymmetryOperation> parseCifSymmetryOperation(QString operation) {
    operation = operation.trimmed();
    if ((operation.startsWith('\'') && operation.endsWith('\'')) || (operation.startsWith('"') && operation.endsWith('"'))) {
        operation = operation.mid(1, operation.size() - 2);
    }

    const QStringList pieces = operation.split(QLatin1Char(','), Qt::KeepEmptyParts);
    if (pieces.size() != 3) {
        return std::nullopt;
    }

    CifSymmetryOperation parsed;
    for (int axis = 0; axis < 3; ++axis) {
        const auto expression = parseCifSymmetryExpression(pieces.at(axis));
        if (!expression.has_value()) {
            return std::nullopt;
        }
        parsed.expressions[static_cast<std::size_t>(axis)] = *expression;
    }
    return parsed;
}

double wrapFractionalComponent(double value) {
    value = value - std::floor(value);
    if (value < 0.0) {
        value += 1.0;
    }
    if (std::abs(value) < 1.0e-7 || std::abs(value - 1.0) < 1.0e-7) {
        return 0.0;
    }
    return value;
}

QVector3D applyCifSymmetryOperation(const CifSymmetryOperation& operation, const QVector3D& fractional) {
    const std::array<double, 3> source{
        static_cast<double>(fractional.x()),
        static_cast<double>(fractional.y()),
        static_cast<double>(fractional.z())
    };
    std::array<double, 3> transformed{};
    for (int axis = 0; axis < 3; ++axis) {
        const CifSymmetryExpression& expression = operation.expressions[static_cast<std::size_t>(axis)];
        double value = expression.offset;
        for (int component = 0; component < 3; ++component) {
            value += expression.coefficients[static_cast<std::size_t>(component)] * source[static_cast<std::size_t>(component)];
        }
        transformed[static_cast<std::size_t>(axis)] = wrapFractionalComponent(value);
    }
    return QVector3D(
        static_cast<float>(transformed[0]),
        static_cast<float>(transformed[1]),
        static_cast<float>(transformed[2]));
}

bool sameFractionalSite(const QVector3D& lhs, const QVector3D& rhs, double tolerance = 1.0e-4) {
    auto componentDistance = [](double a, double b) {
        const double direct = std::abs(a - b);
        return std::min(direct, std::abs(1.0 - direct));
    };
    return componentDistance(lhs.x(), rhs.x()) <= tolerance
        && componentDistance(lhs.y(), rhs.y()) <= tolerance
        && componentDistance(lhs.z(), rhs.z()) <= tolerance;
}

bool containsEquivalentFractionalAtom(const std::vector<NativeAtom>& atoms, const QString& element, const QVector3D& fractional) {
    const QString normalizedElement = normalizeElement(element);
    return std::any_of(atoms.begin(), atoms.end(), [&](const NativeAtom& atom) {
        return normalizeElement(atom.element) == normalizedElement
            && sameFractionalSite(atom.fractional, fractional);
    });
}

std::vector<NativeAtom> expandAtomsByCifSymmetry(const std::vector<NativeAtom>& asymmetricAtoms,
                                                 const std::array<QVector3D, 3>& cell,
                                                 const QStringList& operationTexts) {
    std::vector<CifSymmetryOperation> operations;
    operations.reserve(static_cast<std::size_t>(operationTexts.size()));
    for (const QString& operationText : operationTexts) {
        const auto operation = parseCifSymmetryOperation(operationText);
        if (operation.has_value()) {
            operations.push_back(*operation);
        }
    }
    if (operations.empty()) {
        return asymmetricAtoms;
    }

    std::vector<NativeAtom> expandedAtoms;
    expandedAtoms.reserve(asymmetricAtoms.size() * operations.size());
    for (const NativeAtom& sourceAtom : asymmetricAtoms) {
        const QString baseTag = sourceAtom.tag.trimmed().isEmpty()
            ? normalizeElement(sourceAtom.element)
            : sourceAtom.tag.trimmed();
        int generatedForThisSite = 0;
        for (const CifSymmetryOperation& operation : operations) {
            const QVector3D fractional = applyCifSymmetryOperation(operation, sourceAtom.fractional);
            if (containsEquivalentFractionalAtom(expandedAtoms, sourceAtom.element, fractional)) {
                continue;
            }
            NativeAtom atom = sourceAtom;
            atom.atomId = static_cast<int>(expandedAtoms.size()) + 1;
            atom.fractional = fractional;
            atom.cartesian = toCartesian(cell, fractional);
            atom.tag = generatedForThisSite == 0
                ? baseTag
                : QStringLiteral("%1_sym%2").arg(baseTag).arg(generatedForThisSite + 1, 2, 10, QChar('0'));
            expandedAtoms.push_back(atom);
            ++generatedForThisSite;
        }
    }
    return expandedAtoms.empty() ? asymmetricAtoms : expandedAtoms;
}

std::array<QVector3D, 3> buildCell(double a, double b, double c, double alphaDeg, double betaDeg, double gammaDeg) {
    const double alpha = qDegreesToRadians(alphaDeg);
    const double beta = qDegreesToRadians(betaDeg);
    const double gamma = qDegreesToRadians(gammaDeg);
    const double sinGamma = std::max(1.0e-8, std::sin(gamma));

    const QVector3D va(static_cast<float>(a), 0.0f, 0.0f);
    const QVector3D vb(static_cast<float>(b * std::cos(gamma)), static_cast<float>(b * sinGamma), 0.0f);
    const double cx = c * std::cos(beta);
    const double cy = c * (std::cos(alpha) - std::cos(beta) * std::cos(gamma)) / sinGamma;
    const double czSq = std::max(0.0, c * c - cx * cx - cy * cy);
    const QVector3D vc(static_cast<float>(cx), static_cast<float>(cy), static_cast<float>(std::sqrt(czSq)));
    return {va, vb, vc};
}

bool hasCell(const std::array<QVector3D, 3>& cell) {
    return cell[0].lengthSquared() > 1.0e-8f && cell[1].lengthSquared() > 1.0e-8f && cell[2].lengthSquared() > 1.0e-8f;
}

void ensureCell(StructureData& data) {
    if (hasCell(data.cellVectors)) {
        return;
    }
    if (data.atoms.empty()) {
        data.cellVectors = {QVector3D(10.0f, 0.0f, 0.0f), QVector3D(0.0f, 10.0f, 0.0f), QVector3D(0.0f, 0.0f, 10.0f)};
        return;
    }

    QVector3D minPoint = data.atoms.front().cartesian;
    QVector3D maxPoint = data.atoms.front().cartesian;
    for (const auto& atom : data.atoms) {
        minPoint.setX(std::min(minPoint.x(), atom.cartesian.x()));
        minPoint.setY(std::min(minPoint.y(), atom.cartesian.y()));
        minPoint.setZ(std::min(minPoint.z(), atom.cartesian.z()));
        maxPoint.setX(std::max(maxPoint.x(), atom.cartesian.x()));
        maxPoint.setY(std::max(maxPoint.y(), atom.cartesian.y()));
        maxPoint.setZ(std::max(maxPoint.z(), atom.cartesian.z()));
    }
    const QVector3D margin(1.5f, 1.5f, 1.5f);
    const QVector3D shift = margin - minPoint;
    const QVector3D extents = (maxPoint - minPoint) + margin * 2.0f;
    for (auto& atom : data.atoms) {
        atom.cartesian += shift;
    }
    data.cellVectors = {
        QVector3D(std::max(6.0f, extents.x()), 0.0f, 0.0f),
        QVector3D(0.0f, std::max(6.0f, extents.y()), 0.0f),
        QVector3D(0.0f, 0.0f, std::max(6.0f, extents.z()))
    };
}

void finalizeAtoms(StructureData& data) {
    ensureCell(data);
    int nextId = 1;
    for (auto& atom : data.atoms) {
        if (atom.atomId <= 0) {
            atom.atomId = nextId;
        }
        nextId = std::max(nextId, atom.atomId + 1);
        atom.element = normalizeElement(atom.element);
        atom.color = elementColor(atom.element);
        atom.radius = elementRadius(atom.element);
        if (atom.tag.trimmed().isEmpty()) {
            atom.tag = QString("%1-%2").arg(atom.element).arg(atom.atomId, 4, 10, QChar('0'));
        }
        const bool fracZero = atom.fractional.lengthSquared() <= 1.0e-8f;
        const bool cartZero = atom.cartesian.lengthSquared() <= 1.0e-8f;
        if (cartZero && !fracZero) {
            atom.cartesian = toCartesian(data.cellVectors, atom.fractional);
        } else {
            atom.fractional = solveFractional(data.cellVectors, atom.cartesian);
        }
    }
    data.dirty = false;
}

QStringList tokenizeRespectingQuotes(const QString& line) {
    QStringList tokens;
    QString current;
    QChar quote;
    for (const QChar ch : line) {
        if (!quote.isNull()) {
            if (ch == quote) {
                quote = QChar();
            } else {
                current += ch;
            }
            continue;
        }
        if (ch == '\'' || ch == '"') {
            quote = ch;
            continue;
        }
        if (ch.isSpace()) {
            if (!current.isEmpty()) {
                tokens.push_back(current);
                current.clear();
            }
            continue;
        }
        current += ch;
    }
    if (!current.isEmpty()) {
        tokens.push_back(current);
    }
    return tokens;
}

QString stripCifInlineComment(const QString& line) {
    QChar quote;
    for (int i = 0; i < line.size(); ++i) {
        const QChar ch = line.at(i);
        if (!quote.isNull()) {
            if (ch == quote) {
                quote = QChar();
            }
            continue;
        }
        if (ch == '\'' || ch == '"') {
            quote = ch;
            continue;
        }
        if (ch == '#') {
            return line.left(i);
        }
    }
    return line;
}

struct ExtXyzPropertyLayout {
    int speciesColumn = 0;
    int positionColumn = 1;
    int atomIdColumn = -1;
    int tagColumn = -1;
    int columnCount = 4;
    bool hasExplicitLayout = false;
};

ExtXyzPropertyLayout parseExtXyzPropertyLayout(const QString& comment) {
    ExtXyzPropertyLayout layout;
    const QRegularExpression propertiesRx(QStringLiteral("Properties=(?:\\\"([^\\\"]+)\\\"|([^ \\t\\r\\n]+))"));
    const auto match = propertiesRx.match(comment);
    if (!match.hasMatch()) {
        return layout;
    }

    const QString definition = !match.captured(1).isEmpty() ? match.captured(1) : match.captured(2);
    const QStringList parts = definition.split(QLatin1Char(':'), Qt::SkipEmptyParts);
    if (parts.size() < 3 || parts.size() % 3 != 0) {
        return layout;
    }

    int column = 0;
    bool hasSpecies = false;
    bool hasPosition = false;
    for (int i = 0; i + 2 < parts.size(); i += 3) {
        const QString name = parts.at(i).trimmed();
        bool countOk = false;
        const int count = parts.at(i + 2).trimmed().toInt(&countOk);
        if (!countOk || count <= 0) {
            return layout;
        }
        if (name == QStringLiteral("species") || name == QStringLiteral("symbol") || name == QStringLiteral("element")) {
            layout.speciesColumn = column;
            hasSpecies = true;
        } else if (name == QStringLiteral("pos") || name == QStringLiteral("position") || name == QStringLiteral("positions")) {
            layout.positionColumn = column;
            hasPosition = count >= 3;
        } else if (name == QStringLiteral("atom_id") || name == QStringLiteral("id")) {
            layout.atomIdColumn = column;
        } else if (name == QStringLiteral("tag") || name == QStringLiteral("label")) {
            layout.tagColumn = column;
        }
        column += count;
    }

    if (hasSpecies && hasPosition && column >= 4) {
        layout.columnCount = column;
        layout.hasExplicitLayout = true;
    }
    return layout;
}

std::optional<StructureData> loadAseprojLike(const QString& path, QString* errorMessage) {
    const QString text = readUtf8NoBom(path, errorMessage);
    if (text.isEmpty()) {
        return std::nullopt;
    }
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(text.toUtf8(), &parseError);
    if (!doc.isObject()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("JSON parse error: %1").arg(parseError.errorString());
        }
        return std::nullopt;
    }
    const auto root = doc.object();
    StructureData data;
    data.sourcePath = path;
    data.title = root.value("name").toString(root.value("title").toString(QFileInfo(path).baseName()));

    const auto cellArray = root.value("cell_vectors").toArray();
    for (int i = 0; i < 3 && i < cellArray.size(); ++i) {
        data.cellVectors[static_cast<std::size_t>(i)] = jsonVector(cellArray.at(i).toArray());
    }

    const auto atoms = root.value("atoms").toArray();
    data.atoms.reserve(static_cast<std::size_t>(atoms.size()));
    for (const auto& atomValue : atoms) {
        const auto obj = atomValue.toObject();
        NativeAtom atom;
        atom.atomId = obj.value("atom_id").toInt();
        atom.element = obj.value("element").toString();
        atom.tag = obj.value("tag").toString();
        if (obj.contains("fractional")) {
            atom.fractional = jsonVector(obj.value("fractional").toArray());
        }
        if (obj.contains("cartesian")) {
            atom.cartesian = jsonVector(obj.value("cartesian").toArray());
        }
        if (obj.contains("radius")) {
            atom.radius = obj.value("radius").toDouble(1.0);
        }
        if (obj.contains("color")) {
            const auto color = obj.value("color").toArray();
            atom.color = QColor(color.at(0).toInt(220), color.at(1).toInt(220), color.at(2).toInt(220));
        }
        if (obj.contains("movable")) {
            const auto movable = obj.value("movable").toArray();
            for (int axis = 0; axis < 3 && axis < movable.size(); ++axis) {
                atom.movable[static_cast<std::size_t>(axis)] = movable.at(axis).toBool(true);
            }
        }
        data.atoms.push_back(atom);
    }
    finalizeAtoms(data);
    return data;
}

std::optional<StructureData> loadXyz(const QString& path, QString* errorMessage) {
    const QString text = readUtf8NoBom(path, errorMessage);
    if (text.isEmpty()) {
        return std::nullopt;
    }
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("\r?\n")), Qt::KeepEmptyParts);
    if (lines.size() < 2) {
        if (errorMessage) *errorMessage = QStringLiteral("XYZ file is too short.");
        return std::nullopt;
    }
    bool countOk = false;
    const int atomCount = lines.at(0).trimmed().toInt(&countOk);
    if (!countOk || atomCount <= 0) {
        if (errorMessage) *errorMessage = QStringLiteral("Invalid XYZ atom count.");
        return std::nullopt;
    }

    StructureData data;
    data.sourcePath = path;
    data.title = QFileInfo(path).baseName();

    const QString comment = lines.at(1);
    const auto propertyLayout = parseExtXyzPropertyLayout(comment);
    const QRegularExpression titleRx(QStringLiteral("Title=\"([^\"]+)\""));
    const auto titleMatch = titleRx.match(comment);
    if (titleMatch.hasMatch() && !titleMatch.captured(1).trimmed().isEmpty()) {
        data.title = titleMatch.captured(1).trimmed();
    }
    const QRegularExpression latticeRx(QStringLiteral("Lattice=\"([^\"]+)\""));
    const auto latticeMatch = latticeRx.match(comment);
    if (latticeMatch.hasMatch()) {
        const auto values = tokenizeRespectingQuotes(latticeMatch.captured(1));
        if (values.size() >= 9) {
            data.cellVectors = {
                QVector3D(static_cast<float>(parseNumber(values.at(0))), static_cast<float>(parseNumber(values.at(1))), static_cast<float>(parseNumber(values.at(2)))),
                QVector3D(static_cast<float>(parseNumber(values.at(3))), static_cast<float>(parseNumber(values.at(4))), static_cast<float>(parseNumber(values.at(5)))),
                QVector3D(static_cast<float>(parseNumber(values.at(6))), static_cast<float>(parseNumber(values.at(7))), static_cast<float>(parseNumber(values.at(8))))
            };
        }
    }

    data.atoms.reserve(static_cast<std::size_t>(atomCount));
    for (int i = 0; i < atomCount && i + 2 < lines.size(); ++i) {
        const auto tokens = tokenizeRespectingQuotes(lines.at(i + 2).trimmed());
        const int requiredColumns = propertyLayout.hasExplicitLayout ? propertyLayout.columnCount : 4;
        if (tokens.size() < requiredColumns) {
            continue;
        }
        NativeAtom atom;
        atom.atomId = i + 1;
        atom.element = tokens.at(propertyLayout.speciesColumn);
        if (propertyLayout.atomIdColumn >= 0 && propertyLayout.atomIdColumn < tokens.size()) {
            bool idOk = false;
            const int parsedId = tokens.at(propertyLayout.atomIdColumn).toInt(&idOk);
            if (idOk && parsedId > 0) {
                atom.atomId = parsedId;
            }
        }
        if (propertyLayout.tagColumn >= 0 && propertyLayout.tagColumn < tokens.size()) {
            atom.tag = tokens.at(propertyLayout.tagColumn);
        }
        if (atom.tag.trimmed().isEmpty()) {
            atom.tag = QString("%1-%2").arg(normalizeElement(atom.element)).arg(atom.atomId, 4, 10, QChar('0'));
        }
        const int pos = propertyLayout.positionColumn;
        atom.cartesian = QVector3D(
            static_cast<float>(parseNumber(tokens.at(pos))),
            static_cast<float>(parseNumber(tokens.at(pos + 1))),
            static_cast<float>(parseNumber(tokens.at(pos + 2))));
        data.atoms.push_back(atom);
    }
    finalizeAtoms(data);
    return data;
}

std::optional<StructureData> loadCif(const QString& path, QString* errorMessage) {
    const QString text = readUtf8NoBom(path, errorMessage);
    if (text.isEmpty()) {
        return std::nullopt;
    }
    const QStringList rawLines = text.split(QRegularExpression(QStringLiteral("\r?\n")));
    StructureData data;
    data.sourcePath = path;
    data.title = QFileInfo(path).baseName();

    double a = 0.0, b = 0.0, c = 0.0, alpha = 90.0, beta = 90.0, gamma = 90.0;
    QStringList symmetryOperations;
    bool sawFractionalAtomSites = false;

    auto unquote = [](QString value) {
        value = value.trimmed();
        if ((value.startsWith('\'') && value.endsWith('\'')) || (value.startsWith('"') && value.endsWith('"'))) {
            value = value.mid(1, value.size() - 2);
        }
        return value;
    };
    auto itemValue = [&](const QString& line) {
        const auto tokens = tokenizeRespectingQuotes(line);
        return tokens.size() >= 2 ? unquote(tokens.at(1)) : QString();
    };

    for (int i = 0; i < rawLines.size(); ++i) {
        const QString trimmed = stripCifInlineComment(rawLines.at(i)).trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith('#')) {
            continue;
        }
        if (trimmed.startsWith("data_")) {
            data.title = trimmed.mid(5).trimmed();
            continue;
        }
        if (trimmed.startsWith("_cell_length_a")) { a = parseNumber(itemValue(trimmed)); continue; }
        if (trimmed.startsWith("_cell_length_b")) { b = parseNumber(itemValue(trimmed)); continue; }
        if (trimmed.startsWith("_cell_length_c")) { c = parseNumber(itemValue(trimmed)); continue; }
        if (trimmed.startsWith("_cell_angle_alpha")) { alpha = parseNumber(itemValue(trimmed), 90.0); continue; }
        if (trimmed.startsWith("_cell_angle_beta")) { beta = parseNumber(itemValue(trimmed), 90.0); continue; }
        if (trimmed.startsWith("_cell_angle_gamma")) { gamma = parseNumber(itemValue(trimmed), 90.0); continue; }

        if (!trimmed.startsWith("loop_")) {
            continue;
        }

        QStringList headers;
        int j = i + 1;
        while (j < rawLines.size()) {
            const QString headerLine = stripCifInlineComment(rawLines.at(j)).trimmed();
            if (headerLine.startsWith('_')) {
                headers.push_back(headerLine);
                ++j;
            } else {
                break;
            }
        }
        if (headers.isEmpty()) {
            i = j - 1;
            continue;
        }

        const int symbolIndex = headers.indexOf(QRegularExpression(QStringLiteral("_atom_site_(type_symbol|symbol)")));
        const int labelIndex = headers.indexOf(QRegularExpression(QStringLiteral("_atom_site_label")));
        const int fxIndex = headers.indexOf(QRegularExpression(QStringLiteral("_atom_site_fract_x")));
        const int fyIndex = headers.indexOf(QRegularExpression(QStringLiteral("_atom_site_fract_y")));
        const int fzIndex = headers.indexOf(QRegularExpression(QStringLiteral("_atom_site_fract_z")));
        const int cxIndex = headers.indexOf(QRegularExpression(QStringLiteral("_atom_site_Cartn_x")));
        const int cyIndex = headers.indexOf(QRegularExpression(QStringLiteral("_atom_site_Cartn_y")));
        const int czIndex = headers.indexOf(QRegularExpression(QStringLiteral("_atom_site_Cartn_z")));
        const int symopIndex = headers.indexOf(QRegularExpression(
            QStringLiteral("^(_space_group_symop_operation_xyz|_symmetry_equiv_pos_as_xyz)$"),
            QRegularExpression::CaseInsensitiveOption));

        QStringList buffer;
        for (; j < rawLines.size(); ++j) {
            const QString rowLine = stripCifInlineComment(rawLines.at(j)).trimmed();
            if (rowLine.isEmpty()) {
                continue;
            }
            if (rowLine.startsWith("loop_") || rowLine.startsWith("data_") || rowLine.startsWith('_')) {
                break;
            }
            const auto parts = tokenizeRespectingQuotes(rowLine);
            for (const auto& part : parts) {
                buffer.push_back(unquote(part));
            }
            while (buffer.size() >= headers.size()) {
                const auto row = buffer.mid(0, headers.size());
                buffer = buffer.mid(headers.size());
                if (symopIndex >= 0) {
                    const QString operation = row.value(symopIndex).trimmed();
                    if (!operation.isEmpty() && operation != QStringLiteral("?") && operation != QStringLiteral(".")) {
                        symmetryOperations.push_back(operation);
                    }
                    continue;
                }
                if ((fxIndex >= 0 && fyIndex >= 0 && fzIndex >= 0) || (cxIndex >= 0 && cyIndex >= 0 && czIndex >= 0)) {
                    NativeAtom atom;
                    atom.atomId = static_cast<int>(data.atoms.size()) + 1;
                    atom.element = symbolIndex >= 0 ? row.value(symbolIndex) : row.value(labelIndex);
                    atom.tag = labelIndex >= 0 ? row.value(labelIndex) : QString();
                    if (fxIndex >= 0 && fyIndex >= 0 && fzIndex >= 0) {
                        sawFractionalAtomSites = true;
                        atom.fractional = QVector3D(
                            static_cast<float>(parseNumber(row.value(fxIndex))),
                            static_cast<float>(parseNumber(row.value(fyIndex))),
                            static_cast<float>(parseNumber(row.value(fzIndex))));
                    }
                    if (cxIndex >= 0 && cyIndex >= 0 && czIndex >= 0) {
                        atom.cartesian = QVector3D(
                            static_cast<float>(parseNumber(row.value(cxIndex))),
                            static_cast<float>(parseNumber(row.value(cyIndex))),
                            static_cast<float>(parseNumber(row.value(czIndex))));
                    }
                    data.atoms.push_back(atom);
                }
            }
        }
        i = j - 1;
    }

    if (a > 0.0 && b > 0.0 && c > 0.0) {
        data.cellVectors = buildCell(a, b, c, alpha, beta, gamma);
    }
    if (data.atoms.empty()) {
        if (errorMessage) *errorMessage = QStringLiteral("No atom sites found in CIF.");
        return std::nullopt;
    }
    if (sawFractionalAtomSites && !symmetryOperations.isEmpty() && hasCell(data.cellVectors)) {
        data.atoms = expandAtomsByCifSymmetry(data.atoms, data.cellVectors, symmetryOperations);
    }
    finalizeAtoms(data);
    return data;
}

std::optional<StructureData> loadPoscar(const QString& path, QString* errorMessage) {
    const QString text = readUtf8NoBom(path, errorMessage);
    if (text.isEmpty()) {
        return std::nullopt;
    }
    QStringList lines = text.split(QRegularExpression(QStringLiteral("\r?\n")), Qt::SkipEmptyParts);
    if (lines.size() < 8) {
        if (errorMessage) *errorMessage = QStringLiteral("POSCAR/CONTCAR file is too short.");
        return std::nullopt;
    }

    StructureData data;
    data.sourcePath = path;
    data.title = lines.at(0).trimmed();
    const double scale = parseNumber(lines.at(1).trimmed(), 1.0);
    for (int i = 0; i < 3; ++i) {
        const auto vec = tokenizeRespectingQuotes(lines.at(2 + i));
        if (vec.size() < 3) {
            if (errorMessage) *errorMessage = QStringLiteral("Invalid POSCAR cell vector.");
            return std::nullopt;
        }
        data.cellVectors[static_cast<std::size_t>(i)] = QVector3D(
            static_cast<float>(parseNumber(vec.at(0)) * scale),
            static_cast<float>(parseNumber(vec.at(1)) * scale),
            static_cast<float>(parseNumber(vec.at(2)) * scale));
    }

    int lineIndex = 5;
    QStringList elementNames = tokenizeRespectingQuotes(lines.at(lineIndex));
    bool hasNames = true;
    for (const auto& token : elementNames) {
        bool ok = false;
        token.toInt(&ok);
        if (ok) {
            hasNames = false;
            break;
        }
    }
    if (!hasNames) {
        elementNames.clear();
    } else {
        ++lineIndex;
    }

    const QStringList countTokens = tokenizeRespectingQuotes(lines.at(lineIndex));
    QList<int> counts;
    int totalCount = 0;
    for (const auto& token : countTokens) {
        bool ok = false;
        const int value = token.toInt(&ok);
        if (!ok) {
            if (errorMessage) *errorMessage = QStringLiteral("Invalid POSCAR atom counts.");
            return std::nullopt;
        }
        counts.push_back(value);
        totalCount += value;
    }
    ++lineIndex;

    const bool selectiveDynamics = lines.at(lineIndex).trimmed().startsWith('S', Qt::CaseInsensitive);
    if (selectiveDynamics) {
        ++lineIndex;
    }
    const QString coordMode = lines.at(lineIndex).trimmed();
    const bool directMode = coordMode.startsWith('D', Qt::CaseInsensitive);
    ++lineIndex;

    if (elementNames.isEmpty()) {
        for (int i = 0; i < counts.size(); ++i) {
            elementNames.push_back(QString("X%1").arg(i + 1));
        }
    }

    int atomId = 1;
    for (int elementIndex = 0; elementIndex < counts.size(); ++elementIndex) {
        const QString element = elementNames.value(elementIndex, QString("X%1").arg(elementIndex + 1));
        for (int n = 0; n < counts.at(elementIndex); ++n) {
            if (lineIndex >= lines.size()) {
                if (errorMessage) *errorMessage = QStringLiteral("POSCAR ended before all atoms were read.");
                return std::nullopt;
            }
            const auto parts = tokenizeRespectingQuotes(lines.at(lineIndex++));
            if (parts.size() < 3) {
                continue;
            }
            NativeAtom atom;
            atom.atomId = atomId++;
            atom.element = element;
            atom.tag = QString("%1-%2").arg(normalizeElement(element)).arg(atom.atomId, 4, 10, QChar('0'));
            const QVector3D vec(
                static_cast<float>(parseNumber(parts.at(0))),
                static_cast<float>(parseNumber(parts.at(1))),
                static_cast<float>(parseNumber(parts.at(2))));
            if (directMode) {
                atom.fractional = vec;
                atom.cartesian = toCartesian(data.cellVectors, atom.fractional);
            } else {
                atom.cartesian = vec * static_cast<float>(scale);
                atom.fractional = solveFractional(data.cellVectors, atom.cartesian);
            }
            if (selectiveDynamics && parts.size() >= 6) {
                for (int axis = 0; axis < 3; ++axis) {
                    atom.movable[static_cast<std::size_t>(axis)] = parts.at(3 + axis).trimmed().startsWith('T', Qt::CaseInsensitive);
                }
            }
            data.atoms.push_back(atom);
        }
    }

    if (totalCount != data.atoms.size()) {
        if (errorMessage) *errorMessage = QStringLiteral("POSCAR atom count mismatch.");
        return std::nullopt;
    }
    finalizeAtoms(data);
    return data;
}

std::optional<StructureData> loadPdb(const QString& path, QString* errorMessage) {
    const QString text = readUtf8NoBom(path, errorMessage);
    if (text.isEmpty()) {
        return std::nullopt;
    }
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("\r?\n")));
    StructureData data;
    data.sourcePath = path;
    data.title = QFileInfo(path).baseName();

    for (const auto& line : lines) {
        if (line.startsWith("HEADER") && line.size() > 10) {
            data.title = line.mid(10).trimmed();
            continue;
        }
        if (line.startsWith("CRYST1")) {
            const double a = parseNumber(line.mid(6, 9));
            const double b = parseNumber(line.mid(15, 9));
            const double c = parseNumber(line.mid(24, 9));
            const double alpha = parseNumber(line.mid(33, 7), 90.0);
            const double beta = parseNumber(line.mid(40, 7), 90.0);
            const double gamma = parseNumber(line.mid(47, 7), 90.0);
            if (a > 0.0 && b > 0.0 && c > 0.0) {
                data.cellVectors = buildCell(a, b, c, alpha, beta, gamma);
            }
            continue;
        }
        if (!(line.startsWith("ATOM  ") || line.startsWith("HETATM"))) {
            continue;
        }

        NativeAtom atom;
        atom.atomId = parseNumber(line.mid(6, 5), static_cast<double>(data.atoms.size() + 1));
        atom.cartesian = QVector3D(
            static_cast<float>(parseNumber(line.mid(30, 8))),
            static_cast<float>(parseNumber(line.mid(38, 8))),
            static_cast<float>(parseNumber(line.mid(46, 8))));
        atom.tag = line.mid(12, 4).trimmed();
        atom.element = line.size() >= 78 ? line.mid(76, 2).trimmed() : atom.tag;
        if (atom.element.isEmpty()) {
            atom.element = atom.tag;
        }
        data.atoms.push_back(atom);
    }

    if (data.atoms.empty()) {
        if (errorMessage) *errorMessage = QStringLiteral("No ATOM/HETATM records found in PDB.");
        return std::nullopt;
    }
    finalizeAtoms(data);
    return data;
}

std::optional<StructureData> loadXsf(const QString& path, QString* errorMessage) {
    const QString text = readUtf8NoBom(path, errorMessage);
    if (text.isEmpty()) {
        return std::nullopt;
    }
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("\r?\n")));
    StructureData data;
    data.sourcePath = path;
    data.title = QFileInfo(path).baseName();

    for (int i = 0; i < lines.size(); ++i) {
        const QString trimmed = lines.at(i).trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith('#')) {
            continue;
        }
        if (trimmed.compare("PRIMVEC", Qt::CaseInsensitive) == 0) {
            if (i + 3 >= lines.size()) {
                break;
            }
            for (int v = 0; v < 3; ++v) {
                const auto parts = tokenizeRespectingQuotes(lines.at(i + 1 + v));
                if (parts.size() >= 3) {
                    data.cellVectors[static_cast<std::size_t>(v)] = QVector3D(
                        static_cast<float>(parseNumber(parts.at(0))),
                        static_cast<float>(parseNumber(parts.at(1))),
                        static_cast<float>(parseNumber(parts.at(2))));
                }
            }
            i += 3;
            continue;
        }
        if (trimmed.compare("PRIMCOORD", Qt::CaseInsensitive) == 0) {
            if (i + 1 >= lines.size()) {
                break;
            }
            const auto header = tokenizeRespectingQuotes(lines.at(i + 1));
            const int atomCount = header.isEmpty() ? 0 : static_cast<int>(parseNumber(header.at(0)));
            i += 2;
            for (int n = 0; n < atomCount && i < lines.size(); ++n, ++i) {
                const auto parts = tokenizeRespectingQuotes(lines.at(i));
                if (parts.size() < 4) {
                    continue;
                }
                NativeAtom atom;
                atom.atomId = static_cast<int>(data.atoms.size()) + 1;
                atom.element = parts.at(0);
                atom.cartesian = QVector3D(
                    static_cast<float>(parseNumber(parts.at(1))),
                    static_cast<float>(parseNumber(parts.at(2))),
                    static_cast<float>(parseNumber(parts.at(3))));
                data.atoms.push_back(atom);
            }
            --i;
            continue;
        }
        if (trimmed.compare("ATOMS", Qt::CaseInsensitive) == 0) {
            ++i;
            for (; i < lines.size(); ++i) {
                const auto row = lines.at(i).trimmed();
                if (row.isEmpty() || row.compare("END", Qt::CaseInsensitive) == 0 || row.startsWith("PRIM")) {
                    --i;
                    break;
                }
                const auto parts = tokenizeRespectingQuotes(row);
                if (parts.size() < 4) {
                    continue;
                }
                NativeAtom atom;
                atom.atomId = static_cast<int>(data.atoms.size()) + 1;
                atom.element = parts.at(0);
                atom.cartesian = QVector3D(
                    static_cast<float>(parseNumber(parts.at(1))),
                    static_cast<float>(parseNumber(parts.at(2))),
                    static_cast<float>(parseNumber(parts.at(3))));
                data.atoms.push_back(atom);
            }
        }
    }

    if (data.atoms.empty()) {
        if (errorMessage) *errorMessage = QStringLiteral("No atoms found in XSF.");
        return std::nullopt;
    }
    finalizeAtoms(data);
    return data;
}

QString suffixForPath(const QString& path) {
    const QFileInfo info(path);
    const QString fileName = info.fileName().toLower();
    if (fileName == "poscar" || fileName == "contcar") {
        return QStringLiteral("vasp");
    }
    if (fileName.endsWith(".pdb")) {
        return QStringLiteral("pdb");
    }
    return info.suffix().toLower();
}
}

StructureFileLoader::StructureFileLoader(QObject* parent) : QObject(parent) {}

std::optional<StructureData> StructureFileLoader::load(const QString& path, QString* errorMessage) const {
    const QString suffix = suffixForPath(path);
    if (suffix == "aseproj" || suffix == "json" || suffix == "vesta") {
        return loadAseprojLike(path, errorMessage);
    }
    if (suffix == "xyz" || suffix == "extxyz") {
        return loadXyz(path, errorMessage);
    }
    if (suffix == "cif") {
        return loadCif(path, errorMessage);
    }
    if (suffix == "vasp" || suffix == "poscar" || suffix == "contcar") {
        return loadPoscar(path, errorMessage);
    }
    if (suffix == "pdb") {
        return loadPdb(path, errorMessage);
    }
    if (suffix == "xsf") {
        return loadXsf(path, errorMessage);
    }
    if (errorMessage) {
        *errorMessage = QStringLiteral("Unsupported file format for native loader: %1").arg(QFileInfo(path).suffix());
    }
    return std::nullopt;
}



