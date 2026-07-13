#include "SiestaResultsAnalyzer.h"

#include <QCheckBox>
#include <QComboBox>
#include <QAbstractItemView>
#include <QDirIterator>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMouseEvent>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QProxyStyle>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QStringConverter>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {

const QString kFloatPattern = QStringLiteral(R"([-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[EeDd][-+]?\d+)?)");

double parsedNumber(QString value) {
    value.replace(QLatin1Char('D'), QLatin1Char('E'));
    value.replace(QLatin1Char('d'), QLatin1Char('e'));
    return value.toDouble();
}

QString displayNumber(double value, bool available, int precision = 6) {
    return available && std::isfinite(value) ? QString::number(value, 'g', precision) : QStringLiteral("-");
}

QTableWidgetItem* tableItem(const QString& value) {
    auto* item = new QTableWidgetItem(value);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    item->setForeground(QColor(17, 17, 17));
    return item;
}

QString csvField(QString value) {
    value.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QStringLiteral("\"%1\"").arg(value);
}

SiestaGeometryRecord* ensureGeometry(SiestaAnalysisResult* result, int step) {
    for (auto& record : result->geometry) {
        if (record.step == step) return &record;
    }
    SiestaGeometryRecord record;
    record.step = step;
    result->geometry.push_back(record);
    return &result->geometry.back();
}

const SiestaGeometryRecord* latestMeaningfulGeometry(const SiestaAnalysisResult& result) {
    for (auto iterator = result.geometry.crbegin(); iterator != result.geometry.crend(); ++iterator) {
        if (iterator->scfConverged || iterator->hasEnergy || iterator->hasForce || iterator->hasDmError) return &*iterator;
    }
    return result.geometry.isEmpty() ? nullptr : &result.geometry.back();
}

class CompactComboBoxStyle final : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;

    int styleHint(StyleHint hint, const QStyleOption* option = nullptr,
                  const QWidget* widget = nullptr, QStyleHintReturn* returnData = nullptr) const override {
        // Windows' native popup ignores QComboBox::maxVisibleItems. Disabling the
        // native-style popup makes Qt honor the requested row limit and scrollbar.
        if (hint == QStyle::SH_ComboBox_Popup) return 0;
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
};

class PlotWidget : public QWidget {
public:
    explicit PlotWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumSize(260, 150);
        setMouseTracking(true);
        setToolTip(QStringLiteral("マウスホイール: カーソル位置を中心に拡大・縮小 / 左ドラッグ: 移動 / ダブルクリックまたは Reset: 全体表示"));
        m_resetButton = new QToolButton(this);
        m_resetButton->setText(QStringLiteral("Reset"));
        m_resetButton->setAutoRaise(true);
        m_resetButton->setToolTip(QStringLiteral("グラフを全体表示に戻す"));
        m_resetButton->setEnabled(false);
        connect(m_resetButton, &QToolButton::clicked, this, [this]() { resetView(); });
    }

    void setSeries(QVector<QPointF> points, QString title, QString yLabel, double threshold = std::numeric_limits<double>::quiet_NaN()) {
        const bool seriesChanged = title != m_title || yLabel != m_yLabel;
        m_points = std::move(points);
        m_title = std::move(title);
        m_yLabel = std::move(yLabel);
        m_threshold = threshold;
        if (seriesChanged || m_points.isEmpty()) resetView();
        update();
    }

protected:
    QRectF plotArea() const {
        return QRectF(75, 45, std::max(20, width() - 100), std::max(20, height() - 105));
    }

    bool dataBounds(double* xMin, double* xMax, double* yMin, double* yMax) const {
        if (m_points.isEmpty()) return false;
        *xMin = m_points.front().x(); *xMax = *xMin;
        *yMin = m_points.front().y(); *yMax = *yMin;
        for (const QPointF& point : m_points) {
            *xMin = std::min(*xMin, point.x()); *xMax = std::max(*xMax, point.x());
            *yMin = std::min(*yMin, point.y()); *yMax = std::max(*yMax, point.y());
        }
        if (std::isfinite(m_threshold)) { *yMin = std::min(*yMin, m_threshold); *yMax = std::max(*yMax, m_threshold); }
        if (qFuzzyCompare(*xMin, *xMax)) *xMax = *xMin + 1.0;
        if (qFuzzyCompare(*yMin, *yMax)) { *yMin -= 0.5; *yMax += 0.5; }
        const double yPadding = (*yMax - *yMin) * 0.08;
        *yMin -= yPadding; *yMax += yPadding;
        return true;
    }

    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        m_resetButton->adjustSize();
        m_resetButton->move(std::max(0, width() - m_resetButton->width() - 7), 5);
    }

    void wheelEvent(QWheelEvent* event) override {
        const QRectF area = plotArea();
        if (m_points.isEmpty() || !area.contains(event->position())) { QWidget::wheelEvent(event); return; }
        if (!m_manualView && !dataBounds(&m_viewXMin, &m_viewXMax, &m_viewYMin, &m_viewYMax)) return;
        const int delta = event->angleDelta().y() != 0 ? event->angleDelta().y() : event->pixelDelta().y();
        if (delta == 0) return;
        const double factor = delta > 0 ? 0.80 : 1.25;
        const double xFraction = std::clamp((event->position().x() - area.left()) / area.width(), 0.0, 1.0);
        const double yFraction = std::clamp((area.bottom() - event->position().y()) / area.height(), 0.0, 1.0);
        const double anchorX = m_viewXMin + xFraction * (m_viewXMax - m_viewXMin);
        const double anchorY = m_viewYMin + yFraction * (m_viewYMax - m_viewYMin);
        m_viewXMin = anchorX - (anchorX - m_viewXMin) * factor;
        m_viewXMax = anchorX + (m_viewXMax - anchorX) * factor;
        m_viewYMin = anchorY - (anchorY - m_viewYMin) * factor;
        m_viewYMax = anchorY + (m_viewYMax - anchorY) * factor;
        m_manualView = true;
        if (clampViewToInitialBounds()) {
            event->accept();
            return;
        }
        m_resetButton->setEnabled(true);
        update();
        event->accept();
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && m_manualView && plotArea().contains(event->position())) {
            m_dragging = true; m_lastDragPosition = event->position(); setCursor(Qt::ClosedHandCursor); event->accept(); return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (m_dragging) {
            const QRectF area = plotArea();
            const QPointF delta = event->position() - m_lastDragPosition;
            const double xShift = -delta.x() / area.width() * (m_viewXMax - m_viewXMin);
            const double yShift = delta.y() / area.height() * (m_viewYMax - m_viewYMin);
            m_viewXMin += xShift; m_viewXMax += xShift; m_viewYMin += yShift; m_viewYMax += yShift;
            clampViewToInitialBounds();
            m_lastDragPosition = event->position(); update(); event->accept(); return;
        }
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && m_dragging) {
            m_dragging = false; unsetCursor(); event->accept(); return;
        }
        QWidget::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && plotArea().contains(event->position())) {
            resetView(); event->accept(); return;
        }
        QWidget::mouseDoubleClickEvent(event);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor(250, 251, 253));
        painter.setPen(QColor(35, 42, 52));
        painter.drawText(QRect(12, 8, width() - 24, 24), Qt::AlignCenter, m_title);
        const QRectF area = plotArea();
        painter.setPen(QColor(75, 82, 92));
        painter.drawRect(area);
        painter.drawText(QRectF(area.left(), area.bottom() + 15, area.width(), 24), Qt::AlignCenter, QStringLiteral("Geometry step / SCF iteration"));
        painter.save();
        painter.translate(20, area.center().y());
        painter.rotate(-90);
        painter.drawText(QRectF(-area.height() / 2, -15, area.height(), 25), Qt::AlignCenter, m_yLabel);
        painter.restore();
        if (m_points.isEmpty()) {
            painter.drawText(area, Qt::AlignCenter, QStringLiteral("解析可能なデータがありません"));
            return;
        }

        double xMin, xMax, yMin, yMax;
        if (m_manualView) { xMin = m_viewXMin; xMax = m_viewXMax; yMin = m_viewYMin; yMax = m_viewYMax; }
        else dataBounds(&xMin, &xMax, &yMin, &yMax);
        auto mapPoint = [&](const QPointF& point) {
            return QPointF(area.left() + (point.x() - xMin) / (xMax - xMin) * area.width(),
                           area.bottom() - (point.y() - yMin) / (yMax - yMin) * area.height());
        };
        painter.setPen(QColor(205, 210, 218));
        for (int i = 1; i < 5; ++i) {
            const double y = area.top() + area.height() * i / 5.0;
            painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        }
        painter.save();
        painter.setClipRect(area.adjusted(-1, -1, 1, 1));
        if (std::isfinite(m_threshold)) {
            const double y = mapPoint(QPointF(xMin, m_threshold)).y();
            painter.setPen(QPen(QColor(214, 70, 70), 1.5, Qt::DashLine));
            painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
            painter.drawText(QPointF(area.left() + 6, y - 5), QStringLiteral("threshold %1").arg(m_threshold, 0, 'g', 5));
        }
        QPainterPath path;
        path.moveTo(mapPoint(m_points.front()));
        for (int i = 1; i < m_points.size(); ++i) path.lineTo(mapPoint(m_points.at(i)));
        painter.setPen(QPen(QColor(35, 112, 190), 2.2));
        painter.drawPath(path);
        painter.setBrush(QColor(35, 112, 190));
        for (const QPointF& point : m_points) painter.drawEllipse(mapPoint(point), 3.2, 3.2);
        painter.restore();
        painter.setPen(QColor(55, 60, 68));
        painter.drawText(QPointF(area.left(), area.bottom() + 14), QString::number(xMin, 'g', 4));
        painter.drawText(QPointF(area.right() - 30, area.bottom() + 14), QString::number(xMax, 'g', 4));
        painter.drawText(QPointF(area.left() - 68, area.top() + 5), QString::number(yMax, 'g', 5));
        painter.drawText(QPointF(area.left() - 68, area.bottom()), QString::number(yMin, 'g', 5));
    }

private:
    bool clampViewToInitialBounds() {
        double fullXMin, fullXMax, fullYMin, fullYMax;
        if (!dataBounds(&fullXMin, &fullXMax, &fullYMin, &fullYMax)) return false;

        auto clampAxis = [](double fullMin, double fullMax, double* viewMin, double* viewMax) {
            const double fullSpan = fullMax - fullMin;
            double span = std::min(*viewMax - *viewMin, fullSpan);
            if (span >= fullSpan * (1.0 - 1.0e-9)) {
                *viewMin = fullMin;
                *viewMax = fullMax;
                return true;
            }
            const double halfSpan = span * 0.5;
            const double center = std::clamp((*viewMin + *viewMax) * 0.5,
                                             fullMin + halfSpan,
                                             fullMax - halfSpan);
            *viewMin = center - halfSpan;
            *viewMax = center + halfSpan;
            return false;
        };

        const bool fullX = clampAxis(fullXMin, fullXMax, &m_viewXMin, &m_viewXMax);
        const bool fullY = clampAxis(fullYMin, fullYMax, &m_viewYMin, &m_viewYMax);
        if (fullX && fullY) {
            resetView();
            return true;
        }
        return false;
    }

    void resetView() {
        m_manualView = false;
        m_dragging = false;
        unsetCursor();
        if (m_resetButton) m_resetButton->setEnabled(false);
        update();
    }

    QVector<QPointF> m_points;
    QString m_title;
    QString m_yLabel;
    double m_threshold = std::numeric_limits<double>::quiet_NaN();
    QToolButton* m_resetButton = nullptr;
    bool m_manualView = false;
    bool m_dragging = false;
    QPointF m_lastDragPosition;
    double m_viewXMin = 0.0;
    double m_viewXMax = 1.0;
    double m_viewYMin = 0.0;
    double m_viewYMax = 1.0;
};

} // namespace

class SiestaConvergencePlot : public PlotWidget {
public:
    using PlotWidget::PlotWidget;
};

SiestaAnalysisResult SiestaResultsParser::parseFile(const QString& path, QString* errorMessage) {
    SiestaAnalysisResult result;
    const QFileInfo sourceInfo(path);
    result.path = sourceInfo.absoluteFilePath();
    result.fileSize = sourceInfo.size();
    result.lastModified = sourceInfo.lastModified();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage) *errorMessage = QStringLiteral("ファイルを開けません: %1").arg(path);
        result.status = QStringLiteral("READ ERROR");
        return result;
    }
    const QString text = QString::fromUtf8(file.readAll());
    const QStringList lines = text.split(QLatin1Char('\n'));
    result.outputTail = lines.mid(std::max<qsizetype>(0, lines.size() - 120)).join(QLatin1Char('\n'));

    const QRegularExpression reVersion(QStringLiteral(R"(Siesta Version\s*:\s*(.+))"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression reSystemName(QStringLiteral(R"(^\s*SystemName\s+(.+?)\s*$)"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression reSystemLabel(QStringLiteral(R"(^\s*SystemLabel\s+(.+?)\s*$)"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression reMaxScf(QStringLiteral(R"(^\s*MaxSCFIterations\s+(\d+))"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression reDmTol(QStringLiteral("^\\s*DM\\.Tolerance\\s+(%1)").arg(kFloatPattern), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression reForceTol(QStringLiteral("^\\s*MD\\.MaxForceTol\\s+(%1)").arg(kFloatPattern), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression reStressTol(QStringLiteral("^\\s*MD\\.MaxStressTol\\s+(%1)").arg(kFloatPattern), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression reGeometry(QStringLiteral(R"(Begin\s+\w+\s+opt\.\s+move\s*=\s*(\d+))"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression reScf(QStringLiteral("^\\s*scf:\\s*(\\d+)\\s+(%1)\\s+(%1)\\s+(%1)\\s+(%1)\\s+(%1)(?:\\s+(%1))?").arg(kFloatPattern), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression reScfConverged(QStringLiteral(R"(SCF cycle converged after\s+(\d+)\s+iterations)"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression reDmError(QStringLiteral("max \\|DM_out - DM_in\\|\\s*:\\s*(%1)").arg(kFloatPattern), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression reHError(QStringLiteral("max \\|H_out - H_in\\|\\s*\\(eV\\)\\s*:\\s*(%1)").arg(kFloatPattern), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression reEnergy(QStringLiteral("siesta:\\s+E_KS\\(eV\\)\\s*=\\s*(%1)").arg(kFloatPattern), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression reFreeEnergy(QStringLiteral("siesta:\\s+FreeEng\\s*=\\s*(%1)").arg(kFloatPattern), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression reForce(QStringLiteral("^\\s*Max\\s+(%1)(?:\\s+constrained)?\\s*$").arg(kFloatPattern), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression reResidualForce(QStringLiteral("^\\s*Res\\s+(%1)").arg(kFloatPattern), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression reStress(QStringLiteral("Stress tensor Voigt.*:\\s+(%1)\\s+(%1)\\s+(%1)\\s+(%1)\\s+(%1)\\s+(%1)").arg(kFloatPattern), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression reCellLengths(QStringLiteral("outcell:\\s+Cell vector modules \\(Ang\\)\\s*:\\s+(%1)\\s+(%1)\\s+(%1)").arg(kFloatPattern), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression reCellAngles(QStringLiteral("outcell:\\s+Cell angles \\(23,13,12\\) \\(deg\\)\\s*:\\s+(%1)\\s+(%1)\\s+(%1)").arg(kFloatPattern), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression reCellVolume(QStringLiteral("outcell:\\s+Cell volume \\(Ang\\*\\*3\\)\\s*:\\s+(%1)").arg(kFloatPattern), QRegularExpression::CaseInsensitiveOption);

    int currentStep = 0;
    int globalScfIteration = 0;
    bool forceBlock = false;
    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed().isEmpty() ? rawLine : rawLine;
        auto match = reVersion.match(line); if (result.version.isEmpty() && match.hasMatch()) result.version = match.captured(1).trimmed();
        match = reSystemName.match(line); if (result.systemName.isEmpty() && match.hasMatch()) result.systemName = match.captured(1).trimmed();
        match = reSystemLabel.match(line); if (result.systemLabel.isEmpty() && match.hasMatch()) result.systemLabel = match.captured(1).trimmed();
        match = reMaxScf.match(line); if (result.maxScfIterations == 0 && match.hasMatch()) result.maxScfIterations = match.captured(1).toInt();
        match = reDmTol.match(line); if (result.dmTolerance <= 0.0 && match.hasMatch()) result.dmTolerance = parsedNumber(match.captured(1));
        match = reForceTol.match(line); if (result.forceToleranceEvAng <= 0.0 && match.hasMatch()) result.forceToleranceEvAng = parsedNumber(match.captured(1));
        match = reStressTol.match(line); if (result.stressToleranceGpa <= 0.0 && match.hasMatch()) result.stressToleranceGpa = parsedNumber(match.captured(1));
        match = reGeometry.match(line);
        if (match.hasMatch()) { currentStep = match.captured(1).toInt(); ensureGeometry(&result, currentStep); forceBlock = false; continue; }
        match = reScf.match(line);
        if (match.hasMatch()) {
            SiestaGeometryRecord* geometry = ensureGeometry(&result, currentStep);
            SiestaScfRecord scf;
            scf.geometryStep = currentStep;
            scf.iteration = match.captured(1).toInt();
            scf.globalIteration = ++globalScfIteration;
            scf.eharrisEv = parsedNumber(match.captured(2));
            scf.energyEv = parsedNumber(match.captured(3));
            scf.freeEnergyEv = parsedNumber(match.captured(4));
            scf.dmError = parsedNumber(match.captured(5));
            scf.fermiEnergyEv = parsedNumber(match.captured(6));
            if (!match.captured(7).isEmpty()) { scf.hErrorEv = parsedNumber(match.captured(7)); scf.hasHError = true; }
            result.scf.push_back(scf);
            geometry->scfIterations = std::max(geometry->scfIterations, scf.iteration);
            geometry->dmError = scf.dmError; geometry->hasDmError = true;
            geometry->freeEnergyEv = scf.freeEnergyEv; geometry->hasFreeEnergy = true;
            if (scf.hasHError) { geometry->hErrorEv = scf.hErrorEv; geometry->hasHError = true; }
            continue;
        }
        SiestaGeometryRecord* geometry = result.geometry.isEmpty() ? nullptr : ensureGeometry(&result, currentStep);
        if (geometry == nullptr) continue;
        match = reScfConverged.match(line); if (match.hasMatch()) { geometry->scfConverged = true; geometry->scfIterations = match.captured(1).toInt(); continue; }
        match = reDmError.match(line); if (match.hasMatch()) { geometry->dmError = parsedNumber(match.captured(1)); geometry->hasDmError = true; continue; }
        match = reHError.match(line); if (match.hasMatch()) { geometry->hErrorEv = parsedNumber(match.captured(1)); geometry->hasHError = true; continue; }
        match = reEnergy.match(line); if (match.hasMatch()) { geometry->energyEv = parsedNumber(match.captured(1)); geometry->hasEnergy = true; continue; }
        match = reFreeEnergy.match(line); if (match.hasMatch()) { geometry->freeEnergyEv = parsedNumber(match.captured(1)); geometry->hasFreeEnergy = true; continue; }
        if (line.contains(QStringLiteral("Atomic forces (eV/Ang)"), Qt::CaseInsensitive)) { forceBlock = true; continue; }
        if (forceBlock) {
            match = reResidualForce.match(line);
            if (match.hasMatch()) { geometry->residualForceEvAng = parsedNumber(match.captured(1)); geometry->hasResidualForce = true; continue; }
            match = reForce.match(line);
            if (match.hasMatch()) {
                const double value = parsedNumber(match.captured(1));
                if (line.contains(QStringLiteral("constrained"), Qt::CaseInsensitive) || geometry->hasRawForce) {
                    geometry->constrainedMaxForceEvAng = value; geometry->hasConstrainedForce = true;
                } else {
                    geometry->rawMaxForceEvAng = value; geometry->hasRawForce = true;
                }
                geometry->maxForceEvAng = geometry->hasConstrainedForce ? geometry->constrainedMaxForceEvAng : geometry->rawMaxForceEvAng;
                geometry->hasForce = true;
                continue;
            }
        }
        match = reStress.match(line);
        if (match.hasMatch()) {
            double maximum = 0.0;
            for (int index = 1; index <= 6; ++index) {
                geometry->stressVoigtKbar[static_cast<std::size_t>(index - 1)] = parsedNumber(match.captured(index));
                maximum = std::max(maximum, std::abs(geometry->stressVoigtKbar[static_cast<std::size_t>(index - 1)]));
            }
            geometry->maxStressKbar = maximum; geometry->hasStress = true; forceBlock = false;
            continue;
        }
        match = reCellLengths.match(line); if (match.hasMatch()) { for (int i = 0; i < 3; ++i) geometry->cellLengthsAng[static_cast<std::size_t>(i)] = parsedNumber(match.captured(i + 1)); geometry->hasCellLengths = true; continue; }
        match = reCellAngles.match(line); if (match.hasMatch()) { for (int i = 0; i < 3; ++i) geometry->cellAnglesDeg[static_cast<std::size_t>(i)] = parsedNumber(match.captured(i + 1)); geometry->hasCellAngles = true; continue; }
        match = reCellVolume.match(line); if (match.hasMatch()) { geometry->cellVolumeAng3 = parsedNumber(match.captured(1)); geometry->hasCellVolume = true; continue; }
    }

    for (auto& geometry : result.geometry) {
        if (result.maxScfIterations > 0 && geometry.scfIterations >= result.maxScfIterations) geometry.notes << QStringLiteral("MaxSCFIterationsに到達");
        if (!geometry.scfConverged && std::any_of(result.scf.cbegin(), result.scf.cend(), [&](const SiestaScfRecord& record) { return record.geometryStep == geometry.step; })) geometry.notes << QStringLiteral("SCF未収束または出力途中");
    }

    const QString tail = result.outputTail;
    result.normalEnd = tail.contains(QStringLiteral("End of run"), Qt::CaseInsensitive);
    result.fatalError = text.contains(QRegularExpression(QStringLiteral(R"((?:^|\n)\s*(?:FATAL|ERROR)[:\s])"), QRegularExpression::CaseInsensitiveOption));
    if (result.fatalError) result.status = QStringLiteral("ERROR");
    else if (result.normalEnd) result.status = QStringLiteral("FINISHED");
    else result.status = QStringLiteral("RUNNING / INCOMPLETE");
    if (result.geometry.isEmpty()) result.warnings << QStringLiteral("Geometry/SCFデータを検出できませんでした。出力形式を確認してください。");
    if (result.scf.isEmpty()) result.warnings << QStringLiteral("SCF反復表を検出できませんでした。");
    if (!result.normalEnd) result.warnings << QStringLiteral("End of run がないため、実行中または途中終了の可能性があります。");
    if (!result.geometry.isEmpty() && !result.geometry.back().scfConverged) result.warnings << QStringLiteral("最新ステップのSCF収束を確認できません。");
    return result;
}

SiestaResultsDialog::SiestaResultsDialog(QWidget* parent) : QDialog(parent) {
    setAcceptDrops(true);
    buildUi();
}

void SiestaResultsDialog::buildUi() {
    setWindowTitle(QStringLiteral("SIESTA結果解析・収束モニター"));
    resize(1280, 900);
    auto* root = new QVBoxLayout(this);
    auto* controls = new QHBoxLayout();
    auto* openFiles = new QPushButton(QStringLiteral("出力ファイルを追加..."), this);
    auto* openFolder = new QPushButton(QStringLiteral("フォルダーを追加..."), this);
    auto* reload = new QPushButton(QStringLiteral("再解析"), this);
    auto* remove = new QPushButton(QStringLiteral("選択を削除"), this);
    auto* clear = new QPushButton(QStringLiteral("すべて消去"), this);
    auto* exportCsv = new QPushButton(QStringLiteral("サマリーCSV保存..."), this);
    auto* exportDetails = new QPushButton(QStringLiteral("詳細CSV..."), this);
    auto* exportJson = new QPushButton(QStringLiteral("JSON保存..."), this);
    auto* exportPlot = new QPushButton(QStringLiteral("グラフPNG..."), this);
    m_autoRefreshCheck = new QCheckBox(QStringLiteral("3秒ごとに自動更新"), this);
    controls->addWidget(openFiles); controls->addWidget(openFolder); controls->addWidget(reload); controls->addWidget(remove); controls->addWidget(clear);
    controls->addWidget(exportCsv); controls->addWidget(exportDetails); controls->addWidget(exportJson); controls->addWidget(exportPlot);
    controls->addStretch(1); controls->addWidget(m_autoRefreshCheck);
    root->addLayout(controls);
    connect(openFiles, &QPushButton::clicked, this, &SiestaResultsDialog::chooseFiles);
    connect(openFolder, &QPushButton::clicked, this, &SiestaResultsDialog::chooseFolder);
    connect(reload, &QPushButton::clicked, this, &SiestaResultsDialog::reloadAll);
    connect(remove, &QPushButton::clicked, this, &SiestaResultsDialog::removeSelectedFiles);
    connect(clear, &QPushButton::clicked, this, &SiestaResultsDialog::clearFiles);
    connect(exportCsv, &QPushButton::clicked, this, &SiestaResultsDialog::exportSummaryCsv);
    connect(exportDetails, &QPushButton::clicked, this, &SiestaResultsDialog::exportCurrentDetailsCsv);
    connect(exportJson, &QPushButton::clicked, this, &SiestaResultsDialog::exportAnalysisJson);
    connect(exportPlot, &QPushButton::clicked, this, &SiestaResultsDialog::exportPlotPng);

    m_statusLabel = new QLabel(QStringLiteral("SIESTAの .out/.log を追加するか、ここへドラッグ＆ドロップしてください。"), this);
    m_statusLabel->setWordWrap(true);
    root->addWidget(m_statusLabel);
    auto* splitter = new QSplitter(Qt::Vertical, this);
    m_summaryTable = new QTableWidget(splitter);
    m_summaryTable->setColumnCount(9);
    m_summaryTable->setHorizontalHeaderLabels({QStringLiteral("File"), QStringLiteral("Status"), QStringLiteral("System"), QStringLiteral("Steps"), QStringLiteral("E_KS (eV)"), QStringLiteral("Max force"), QStringLiteral("Force ratio"), QStringLiteral("SCF iter"), QStringLiteral("Warnings")});
    m_summaryTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_summaryTable->horizontalHeader()->setStretchLastSection(true);
    m_summaryTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_summaryTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_summaryTable->setStyleSheet(QStringLiteral(
        "QTableWidget { color: #111111; background: #ffffff; }"
        "QTableWidget::item:selected { color: #000000; background: #b8d7ff; }"
        "QHeaderView::section { color: #111111; background: #f0f2f5; }"));
    splitter->addWidget(m_summaryTable);

    auto* detail = new QWidget(splitter);
    auto* detailLayout = new QVBoxLayout(detail);
    auto* selection = new QHBoxLayout();
    m_resultCombo = new QComboBox(detail);
    m_metricCombo = new QComboBox(detail);
    m_metricCombo->addItem(QStringLiteral("Energy"), QStringLiteral("energy"));
    m_metricCombo->addItem(QStringLiteral("Free energy"), QStringLiteral("free_energy"));
    m_metricCombo->addItem(QStringLiteral("Force / threshold"), QStringLiteral("force_ratio"));
    m_metricCombo->addItem(QStringLiteral("Stress / threshold"), QStringLiteral("stress_ratio"));
    m_metricCombo->addItem(QStringLiteral("SCF iterations"), QStringLiteral("scf"));
    m_metricCombo->addItem(QStringLiteral("DM error / tolerance"), QStringLiteral("dm_ratio"));
    m_metricCombo->addItem(QStringLiteral("Cell volume"), QStringLiteral("cell_volume"));
    m_forceThresholdSpin = new QDoubleSpinBox(detail); m_forceThresholdSpin->setDecimals(8); m_forceThresholdSpin->setRange(0.0, 1.0e6); m_forceThresholdSpin->setValue(0.04); m_forceThresholdSpin->setSuffix(QStringLiteral(" eV/Å"));
    m_dmThresholdSpin = new QDoubleSpinBox(detail); m_dmThresholdSpin->setDecimals(12); m_dmThresholdSpin->setRange(0.0, 1.0); m_dmThresholdSpin->setValue(1.0e-4);
    selection->addWidget(new QLabel(QStringLiteral("対象:"), detail)); selection->addWidget(m_resultCombo, 2);
    selection->addWidget(new QLabel(QStringLiteral("表示:"), detail)); selection->addWidget(m_metricCombo);
    selection->addWidget(new QLabel(QStringLiteral("Force閾値:"), detail)); selection->addWidget(m_forceThresholdSpin);
    selection->addWidget(new QLabel(QStringLiteral("DM閾値:"), detail)); selection->addWidget(m_dmThresholdSpin);
    detailLayout->addLayout(selection);
    auto* tabs = new QTabWidget(detail);
    auto* overview = new QWidget(tabs); auto* overviewLayout = new QVBoxLayout(overview); overviewLayout->setContentsMargins(4, 4, 4, 4); overviewLayout->setSpacing(5);
    auto* dashboardLayout = new QHBoxLayout(); dashboardLayout->setSpacing(6);
    auto addDashboardCard = [&](QLabel** target) {
        auto* card = new QLabel(overview);
        card->setWordWrap(true);
        card->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        card->setMinimumHeight(96);
        card->setMargin(10);
        *target = card;
        dashboardLayout->addWidget(card, 1);
    };
    addDashboardCard(&m_runDashboard); addDashboardCard(&m_energyDashboard); addDashboardCard(&m_forceDashboard);
    addDashboardCard(&m_scfDashboard); addDashboardCard(&m_stressDashboard);
    overviewLayout->addLayout(dashboardLayout);
    m_overviewEnergyPlot = new SiestaConvergencePlot(overview); m_overviewForcePlot = new SiestaConvergencePlot(overview); m_overviewScfPlot = new SiestaConvergencePlot(overview);
    auto* overviewPlots = new QHBoxLayout(); overviewPlots->setSpacing(5);
    overviewPlots->addWidget(m_overviewEnergyPlot, 1); overviewPlots->addWidget(m_overviewForcePlot, 1); overviewPlots->addWidget(m_overviewScfPlot, 1);
    overviewLayout->addLayout(overviewPlots, 1);
    m_overviewDiagnostics = new QLabel(overview); m_overviewDiagnostics->setWordWrap(true); overviewLayout->addWidget(m_overviewDiagnostics);
    tabs->addTab(overview, QStringLiteral("収束概要"));
    m_plot = new SiestaConvergencePlot(tabs); m_plot->setMinimumWidth(700); tabs->addTab(m_plot, QStringLiteral("詳細グラフ"));
    auto* scfConvergence = new QWidget(tabs); auto* scfConvergenceLayout = new QVBoxLayout(scfConvergence); auto* scfControls = new QHBoxLayout();
    scfControls->addWidget(new QLabel(QStringLiteral("Geometry step:"), scfConvergence));
    m_scfStepCombo = new QComboBox(scfConvergence);
    m_scfStepCombo->setFixedWidth(120);
    auto* compactComboStyle = new CompactComboBoxStyle();
    compactComboStyle->setParent(m_scfStepCombo);
    m_scfStepCombo->setStyle(compactComboStyle);
    m_scfStepCombo->setMaxVisibleItems(10);
    m_scfStepCombo->setStyleSheet(QStringLiteral(
        "QComboBox { color: #172033; background: #ffffff; border: 1px solid #aeb8c6; border-radius: 4px; padding: 4px 8px; }"
        "QComboBox QAbstractItemView { color: #172033; background: #ffffff; selection-color: #111111; selection-background-color: #b8d7ff; }"));
    scfControls->addWidget(m_scfStepCombo); scfControls->addStretch(1);
    m_scfDiagnostics = new QLabel(scfConvergence); m_scfDiagnostics->setWordWrap(true); scfConvergenceLayout->addLayout(scfControls); scfConvergenceLayout->addWidget(m_scfDiagnostics);
    m_scfEnergyPlot = new SiestaConvergencePlot(scfConvergence); m_scfResidualPlot = new SiestaConvergencePlot(scfConvergence); scfConvergenceLayout->addWidget(m_scfEnergyPlot, 1); scfConvergenceLayout->addWidget(m_scfResidualPlot, 1);
    tabs->addTab(scfConvergence, QStringLiteral("SCF収束"));
    const QString historyTableStyle = QStringLiteral(
        "QTableWidget { color: #111111; background: #ffffff; alternate-background-color: #f6f8fb; gridline-color: #d5dbe4; border: 1px solid #c7cfda; }"
        "QTableWidget::item { color: #111111; padding: 3px; }"
        "QTableWidget::item:selected { color: #000000; background: #b8d7ff; }"
        "QHeaderView::section { color: #111111; background: #e9edf3; border: 0; border-right: 1px solid #c7cfda; border-bottom: 1px solid #c7cfda; padding: 5px; font-weight: 600; }"
        "QTableCornerButton::section { background: #e9edf3; border: 1px solid #c7cfda; }");
    auto prepareHistoryTable = [&](QTableWidget* table) {
        table->setAlternatingRowColors(true);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setShowGrid(true);
        table->setStyleSheet(historyTableStyle);
    };
    m_geometryTable = new QTableWidget(tabs); prepareHistoryTable(m_geometryTable); tabs->addTab(m_geometryTable, QStringLiteral("Geometry履歴"));
    m_scfTable = new QTableWidget(tabs); prepareHistoryTable(m_scfTable); tabs->addTab(m_scfTable, QStringLiteral("SCF履歴"));
    m_tailEdit = new QPlainTextEdit(tabs); m_tailEdit->setReadOnly(true); m_tailEdit->setLineWrapMode(QPlainTextEdit::NoWrap); tabs->addTab(m_tailEdit, QStringLiteral("出力末尾"));
    detailLayout->addWidget(tabs, 1);
    splitter->addWidget(detail);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({190, 650});
    root->addWidget(splitter, 1);
    connect(m_summaryTable, &QTableWidget::itemSelectionChanged, this, [this]() {
        const int row = m_summaryTable->currentRow(); if (row >= 0 && row < m_resultCombo->count()) m_resultCombo->setCurrentIndex(row);
    });
    connect(m_resultCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { refreshViews(); });
    connect(m_metricCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { refreshViews(); });
    connect(m_forceThresholdSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this]() { refreshViews(); });
    connect(m_dmThresholdSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this]() { refreshViews(); });
    connect(m_scfStepCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { refreshViews(); });
    m_timer = new QTimer(this); m_timer->setInterval(3000);
    connect(m_timer, &QTimer::timeout, this, &SiestaResultsDialog::reloadAll);
    connect(m_autoRefreshCheck, &QCheckBox::toggled, m_timer, [this](bool enabled) { if (enabled) m_timer->start(); else m_timer->stop(); });
}

void SiestaResultsDialog::chooseFiles() {
    addFiles(QFileDialog::getOpenFileNames(this, QStringLiteral("SIESTA出力を選択"), QString(), QStringLiteral("SIESTA outputs (*.out *.log *.txt);;All files (*.*)")));
}

void SiestaResultsDialog::chooseFolder() {
    const QString folder = QFileDialog::getExistingDirectory(this, QStringLiteral("SIESTA結果フォルダーを選択"));
    if (folder.isEmpty()) return;
    QStringList paths;
    QDirIterator iterator(folder, {QStringLiteral("*.out"), QStringLiteral("*.log")}, QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) paths << iterator.next();
    addFiles(paths);
}

void SiestaResultsDialog::addFiles(const QStringList& paths) {
    QStringList expandedPaths;
    for (const QString& path : paths) {
        const QFileInfo info(path);
        if (!info.isDir()) {
            expandedPaths << path;
            continue;
        }
        QDirIterator iterator(info.absoluteFilePath(), {QStringLiteral("*.out"), QStringLiteral("*.log")}, QDir::Files, QDirIterator::Subdirectories);
        while (iterator.hasNext()) expandedPaths << iterator.next();
    }
    QStringList existing;
    for (const auto& result : std::as_const(m_results)) existing << QFileInfo(result.path).absoluteFilePath().toLower();
    QStringList errors;
    for (const QString& path : std::as_const(expandedPaths)) {
        const QString absolute = QFileInfo(path).absoluteFilePath();
        if (!QFileInfo(absolute).isFile() || existing.contains(absolute.toLower())) continue;
        QString error;
        SiestaAnalysisResult parsed = SiestaResultsParser::parseFile(absolute, &error);
        if (!error.isEmpty()) errors << error; else { m_results.push_back(parsed); existing << absolute.toLower(); }
    }
    refreshViews();
    if (!errors.isEmpty()) QMessageBox::warning(this, QStringLiteral("SIESTA解析"), errors.join(QLatin1Char('\n')));
}

void SiestaResultsDialog::reloadAll() {
    const int current = currentResultIndex();
    for (int i = 0; i < m_results.size(); ++i) {
        const QFileInfo info(m_results.at(i).path);
        if (info.exists() && info.size() == m_results.at(i).fileSize && info.lastModified() == m_results.at(i).lastModified) continue;
        QString error;
        const auto parsed = SiestaResultsParser::parseFile(m_results.at(i).path, &error);
        if (error.isEmpty()) m_results[i] = parsed;
    }
    refreshViews();
    if (current >= 0 && current < m_resultCombo->count()) m_resultCombo->setCurrentIndex(current);
}

int SiestaResultsDialog::currentResultIndex() const { return m_resultCombo != nullptr ? m_resultCombo->currentIndex() : -1; }

void SiestaResultsDialog::refreshViews() {
    const int oldIndex = std::max(0, currentResultIndex());
    m_resultCombo->blockSignals(true); m_resultCombo->clear();
    m_summaryTable->setRowCount(m_results.size());
    for (int row = 0; row < m_results.size(); ++row) {
        const auto& result = m_results.at(row);
        m_resultCombo->addItem(QFileInfo(result.path).fileName(), row);
        const auto* last = latestMeaningfulGeometry(result);
        const double forceThreshold = result.forceToleranceEvAng > 0.0 ? result.forceToleranceEvAng : m_forceThresholdSpin->value();
        const double ratio = last && last->hasForce && forceThreshold > 0.0 ? last->maxForceEvAng / forceThreshold : 0.0;
        m_summaryTable->setItem(row, 0, tableItem(QFileInfo(result.path).fileName()));
        m_summaryTable->setItem(row, 1, tableItem(result.status));
        m_summaryTable->setItem(row, 2, tableItem(result.systemName.isEmpty() ? result.systemLabel : result.systemName));
        m_summaryTable->setItem(row, 3, tableItem(QString::number(result.geometry.size())));
        m_summaryTable->setItem(row, 4, tableItem(last ? displayNumber(last->energyEv, last->hasEnergy, 10) : QStringLiteral("-")));
        m_summaryTable->setItem(row, 5, tableItem(last ? displayNumber(last->maxForceEvAng, last->hasForce) : QStringLiteral("-")));
        m_summaryTable->setItem(row, 6, tableItem(last && last->hasForce && forceThreshold > 0.0 ? QString::number(ratio, 'g', 5) : QStringLiteral("-")));
        m_summaryTable->setItem(row, 7, tableItem(last ? QString::number(last->scfIterations) : QStringLiteral("-")));
        m_summaryTable->setItem(row, 8, tableItem(result.warnings.join(QStringLiteral("; "))));
        const QColor color = result.status == QStringLiteral("FINISHED") ? QColor(218, 244, 225) : (result.status == QStringLiteral("ERROR") ? QColor(255, 218, 218) : QColor(255, 242, 201));
        for (int column = 0; column < m_summaryTable->columnCount(); ++column) m_summaryTable->item(row, column)->setBackground(color);
    }
    m_resultCombo->setCurrentIndex(m_results.isEmpty() ? -1 : std::min(oldIndex, static_cast<int>(m_results.size()) - 1));
    m_resultCombo->blockSignals(false);
    const int index = currentResultIndex();
    if (index < 0 || index >= m_results.size()) {
        m_statusLabel->setText(QStringLiteral("SIESTA出力ファイルを追加してください。"));
        m_geometryTable->setRowCount(0); m_scfTable->setRowCount(0); m_tailEdit->clear();
        m_plot->setSeries({}, QStringLiteral("SIESTA convergence"), QStringLiteral("Value"));
        m_overviewEnergyPlot->setSeries({}, QStringLiteral("Energy convergence"), QStringLiteral("E_KS (eV)"));
        m_overviewForcePlot->setSeries({}, QStringLiteral("Force / threshold"), QStringLiteral("Ratio"), 1.0);
        m_overviewScfPlot->setSeries({}, QStringLiteral("Final SCF dDmax / threshold"), QStringLiteral("Ratio"), 1.0);
        m_scfEnergyPlot->setSeries({}, QStringLiteral("SCF energy"), QStringLiteral("E_KS (eV)"));
        m_scfResidualPlot->setSeries({}, QStringLiteral("SCF residual / threshold"), QStringLiteral("Ratio"), 1.0);
        for (QLabel* card : {m_runDashboard, m_energyDashboard, m_forceDashboard, m_scfDashboard, m_stressDashboard}) {
            card->setText(QStringLiteral("<b>NO DATA</b><br>-"));
            card->setStyleSheet(QStringLiteral("QLabel { color: #172033; background: #f5f7fa; border: 1px solid #d8dee8; border-radius: 7px; }"));
        }
        m_overviewDiagnostics->clear(); m_scfDiagnostics->clear(); m_scfStepCombo->clear();
        return;
    }
    const auto& result = m_results.at(index);
    if (result.forceToleranceEvAng > 0.0 && !m_forceThresholdSpin->hasFocus()) m_forceThresholdSpin->setValue(result.forceToleranceEvAng);
    if (result.dmTolerance > 0.0 && !m_dmThresholdSpin->hasFocus()) m_dmThresholdSpin->setValue(result.dmTolerance);
    m_statusLabel->setText(QStringLiteral("%1 | %2 | version=%3 | ForceTol=%4 eV/Å | DMTol=%5")
        .arg(QDir::toNativeSeparators(result.path), result.status, result.version)
        .arg(m_forceThresholdSpin->value(), 0, 'g', 6).arg(m_dmThresholdSpin->value(), 0, 'g', 6));

    m_geometryTable->setColumnCount(15); m_geometryTable->setHorizontalHeaderLabels({QStringLiteral("Step"), QStringLiteral("SCF"), QStringLiteral("Converged"), QStringLiteral("E_KS (eV)"), QStringLiteral("FreeEng (eV)"), QStringLiteral("Optimization force"), QStringLiteral("Raw force"), QStringLiteral("Constrained force"), QStringLiteral("Residual force"), QStringLiteral("Force ratio"), QStringLiteral("Max stress (kbar)"), QStringLiteral("DM error"), QStringLiteral("H error (eV)"), QStringLiteral("Cell volume (Å³)"), QStringLiteral("Notes")});
    m_geometryTable->setRowCount(result.geometry.size());
    for (int row = 0; row < result.geometry.size(); ++row) {
        const auto& record = result.geometry.at(row);
        const double ratio = record.hasForce && m_forceThresholdSpin->value() > 0.0 ? record.maxForceEvAng / m_forceThresholdSpin->value() : 0.0;
        const QStringList values = {QString::number(record.step), QString::number(record.scfIterations), record.scfConverged ? QStringLiteral("Yes") : QStringLiteral("No"), displayNumber(record.energyEv, record.hasEnergy, 10), displayNumber(record.freeEnergyEv, record.hasFreeEnergy, 10), displayNumber(record.maxForceEvAng, record.hasForce), displayNumber(record.rawMaxForceEvAng, record.hasRawForce), displayNumber(record.constrainedMaxForceEvAng, record.hasConstrainedForce), displayNumber(record.residualForceEvAng, record.hasResidualForce), record.hasForce ? QString::number(ratio, 'g', 6) : QStringLiteral("-"), displayNumber(record.maxStressKbar, record.hasStress), displayNumber(record.dmError, record.hasDmError), displayNumber(record.hErrorEv, record.hasHError), displayNumber(record.cellVolumeAng3, record.hasCellVolume), record.notes.join(QStringLiteral("; "))};
        for (int column = 0; column < values.size(); ++column) m_geometryTable->setItem(row, column, tableItem(values.at(column)));
    }
    m_geometryTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_scfTable->setColumnCount(9); m_scfTable->setHorizontalHeaderLabels({QStringLiteral("Geometry"), QStringLiteral("Iteration"), QStringLiteral("Global"), QStringLiteral("E_Harris (eV)"), QStringLiteral("E_KS (eV)"), QStringLiteral("FreeEng (eV)"), QStringLiteral("DM error"), QStringLiteral("DM ratio"), QStringLiteral("dHmax (eV)")});
    m_scfTable->setRowCount(result.scf.size());
    for (int row = 0; row < result.scf.size(); ++row) {
        const auto& record = result.scf.at(row);
        const double ratio = m_dmThresholdSpin->value() > 0.0 ? record.dmError / m_dmThresholdSpin->value() : 0.0;
        const QStringList values = {QString::number(record.geometryStep), QString::number(record.iteration), QString::number(record.globalIteration), QString::number(record.eharrisEv, 'g', 11), QString::number(record.energyEv, 'g', 11), QString::number(record.freeEnergyEv, 'g', 11), QString::number(record.dmError, 'g', 7), m_dmThresholdSpin->value() > 0.0 ? QString::number(ratio, 'g', 6) : QStringLiteral("-"), displayNumber(record.hErrorEv, record.hasHError)};
        for (int column = 0; column < values.size(); ++column) m_scfTable->setItem(row, column, tableItem(values.at(column)));
    }
    m_scfTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_tailEdit->setPlainText(result.outputTail);

    QVector<QPointF> overviewEnergy;
    QVector<QPointF> overviewForce;
    QVector<QPointF> overviewScf;
    for (const auto& record : result.geometry) {
        if (record.hasEnergy) overviewEnergy << QPointF(record.step, record.energyEv);
        if (record.hasForce && m_forceThresholdSpin->value() > 0.0) overviewForce << QPointF(record.step, record.maxForceEvAng / m_forceThresholdSpin->value());
        if (record.hasDmError && m_dmThresholdSpin->value() > 0.0) overviewScf << QPointF(record.step, record.dmError / m_dmThresholdSpin->value());
    }
    m_overviewEnergyPlot->setSeries(overviewEnergy, QStringLiteral("Energy convergence"), QStringLiteral("E_KS (eV)"));
    m_overviewForcePlot->setSeries(overviewForce, QStringLiteral("Force convergence relative to MD.MaxForceTol"), QStringLiteral("Force / threshold"), 1.0);
    m_overviewScfPlot->setSeries(overviewScf, QStringLiteral("Final SCF convergence relative to DM.Tolerance"), QStringLiteral("dDmax / threshold"), 1.0);
    const auto* latest = latestMeaningfulGeometry(result);
    const double forceRatio = latest && latest->hasForce && m_forceThresholdSpin->value() > 0.0 ? latest->maxForceEvAng / m_forceThresholdSpin->value() : std::numeric_limits<double>::quiet_NaN();
    const double dmRatio = latest && latest->hasDmError && m_dmThresholdSpin->value() > 0.0 ? latest->dmError / m_dmThresholdSpin->value() : std::numeric_limits<double>::quiet_NaN();
    const QString forceStatus = std::isfinite(forceRatio) ? (forceRatio <= 1.0 ? QStringLiteral("PASS") : QStringLiteral("NOT converged")) : QStringLiteral("Unknown");
    const QString scfStatus = latest ? (latest->scfConverged && (!std::isfinite(dmRatio) || dmRatio <= 1.0) ? QStringLiteral("PASS") : QStringLiteral("NOT converged / incomplete")) : QStringLiteral("Unknown");
    const double stressThresholdKbar = result.stressToleranceGpa > 0.0 ? result.stressToleranceGpa * 10.0 : std::numeric_limits<double>::quiet_NaN();
    const double stressRatio = latest && latest->hasStress && std::isfinite(stressThresholdKbar) ? latest->maxStressKbar / stressThresholdKbar : std::numeric_limits<double>::quiet_NaN();
    const QString stressStatus = std::isfinite(stressRatio) ? (stressRatio <= 1.0 ? QStringLiteral("PASS") : QStringLiteral("NOT converged")) : QStringLiteral("Unknown");
    int convergedScfSteps = 0;
    double previousEnergy = 0.0;
    double latestEnergy = 0.0;
    int energyCount = 0;
    for (const auto& record : result.geometry) {
        if (record.scfConverged) ++convergedScfSteps;
        if (record.hasEnergy) { previousEnergy = latestEnergy; latestEnergy = record.energyEv; ++energyCount; }
    }
    const double energyDelta = energyCount >= 2 ? latestEnergy - previousEnergy : std::numeric_limits<double>::quiet_NaN();

    auto setDashboardStyle = [](QLabel* card, const QString& background, const QString& border) {
        card->setStyleSheet(QStringLiteral("QLabel { color: #172033; background: %1; border: 1px solid %2; border-radius: 7px; }").arg(background, border));
    };
    const bool runOk = result.status == QStringLiteral("FINISHED");
    const bool runError = result.status == QStringLiteral("ERROR") || result.status == QStringLiteral("READ ERROR");
    m_runDashboard->setText(QStringLiteral("<b>RUN · %1</b><br>Geometry steps: %2<br>SCF converged: %3 / %2")
        .arg(result.status).arg(result.geometry.size()).arg(convergedScfSteps));
    setDashboardStyle(m_runDashboard, runOk ? QStringLiteral("#e8f6ed") : (runError ? QStringLiteral("#fdeaea") : QStringLiteral("#fff5d9")),
                      runOk ? QStringLiteral("#75bf8c") : (runError ? QStringLiteral("#dc8080") : QStringLiteral("#d9b75d")));
    m_energyDashboard->setText(QStringLiteral("<b>ENERGY</b><br>Latest E<sub>KS</sub>: %1 eV<br>ΔE: %2 eV")
        .arg(latest && latest->hasEnergy ? QString::number(latest->energyEv, 'g', 10) : QStringLiteral("-"),
             std::isfinite(energyDelta) ? QString::number(energyDelta, 'g', 6) : QStringLiteral("-")));
    setDashboardStyle(m_energyDashboard, QStringLiteral("#eaf2fc"), QStringLiteral("#83addd"));
    m_forceDashboard->setText(QStringLiteral("<b>FORCE · %1</b><br>Threshold: %2 eV/Å<br>Latest: %3 eV/Å<br>Ratio: %4 ×")
        .arg(forceStatus).arg(m_forceThresholdSpin->value(), 0, 'g', 6)
        .arg(latest && latest->hasForce ? QString::number(latest->maxForceEvAng, 'g', 6) : QStringLiteral("-"),
             std::isfinite(forceRatio) ? QString::number(forceRatio, 'g', 5) : QStringLiteral("-")));
    setDashboardStyle(m_forceDashboard, forceStatus == QStringLiteral("PASS") ? QStringLiteral("#e8f6ed") : (std::isfinite(forceRatio) ? QStringLiteral("#fdeaea") : QStringLiteral("#f5f7fa")),
                      forceStatus == QStringLiteral("PASS") ? QStringLiteral("#75bf8c") : (std::isfinite(forceRatio) ? QStringLiteral("#dc8080") : QStringLiteral("#d8dee8")));
    m_scfDashboard->setText(QStringLiteral("<b>SCF · %1</b><br>DM.Tolerance: %2<br>Latest dDmax: %3<br>Ratio: %4 ×")
        .arg(scfStatus).arg(m_dmThresholdSpin->value(), 0, 'g', 6)
        .arg(latest && latest->hasDmError ? QString::number(latest->dmError, 'g', 6) : QStringLiteral("-"),
             std::isfinite(dmRatio) ? QString::number(dmRatio, 'g', 5) : QStringLiteral("-")));
    setDashboardStyle(m_scfDashboard, scfStatus == QStringLiteral("PASS") ? QStringLiteral("#e8f6ed") : (latest ? QStringLiteral("#fdeaea") : QStringLiteral("#f5f7fa")),
                      scfStatus == QStringLiteral("PASS") ? QStringLiteral("#75bf8c") : (latest ? QStringLiteral("#dc8080") : QStringLiteral("#d8dee8")));
    m_stressDashboard->setText(QStringLiteral("<b>STRESS · %1</b><br>Threshold: %2 kbar<br>Latest max: %3 kbar<br>Ratio: %4 ×")
        .arg(stressStatus,
             std::isfinite(stressThresholdKbar) ? QString::number(stressThresholdKbar, 'g', 6) : QStringLiteral("not set"),
             latest && latest->hasStress ? QString::number(latest->maxStressKbar, 'g', 6) : QStringLiteral("-"),
             std::isfinite(stressRatio) ? QString::number(stressRatio, 'g', 5) : QStringLiteral("-")));
    setDashboardStyle(m_stressDashboard, stressStatus == QStringLiteral("PASS") ? QStringLiteral("#e8f6ed") : (std::isfinite(stressRatio) ? QStringLiteral("#fdeaea") : QStringLiteral("#f5f7fa")),
                      stressStatus == QStringLiteral("PASS") ? QStringLiteral("#75bf8c") : (std::isfinite(stressRatio) ? QStringLiteral("#dc8080") : QStringLiteral("#d8dee8")));
    m_overviewDiagnostics->setText(QStringLiteral("収束判定は比率 ≤ 1.0。Force は拘束後最大力を優先し、SCF は各 Geometry step の最終 dDmax を表示します。グラフ上のホイールで拡大・縮小、左ドラッグで移動、Reset またはダブルクリックで全体表示に戻せます。"));

    const int previousScfStep = m_scfStepCombo->currentData().toInt();
    m_scfStepCombo->blockSignals(true); m_scfStepCombo->clear();
    QSet<int> steps; for (const auto& record : result.scf) steps.insert(record.geometryStep);
    QList<int> sortedSteps = steps.values(); std::sort(sortedSteps.begin(), sortedSteps.end());
    int selectedScfIndex = -1;
    for (int step : sortedSteps) { m_scfStepCombo->addItem(QString::number(step), step); if (step == previousScfStep) selectedScfIndex = m_scfStepCombo->count() - 1; }
    if (selectedScfIndex < 0 && m_scfStepCombo->count() > 0) selectedScfIndex = m_scfStepCombo->count() - 1;
    m_scfStepCombo->setCurrentIndex(selectedScfIndex); m_scfStepCombo->blockSignals(false);
    const int selectedStep = m_scfStepCombo->currentData().toInt();
    QVector<QPointF> scfEnergy; QVector<QPointF> scfResidual;
    const SiestaScfRecord* lastScf = nullptr;
    for (const auto& record : result.scf) if (record.geometryStep == selectedStep) {
        scfEnergy << QPointF(record.iteration, record.energyEv);
        if (m_dmThresholdSpin->value() > 0.0) scfResidual << QPointF(record.iteration, record.dmError / m_dmThresholdSpin->value());
        lastScf = &record;
    }
    m_scfEnergyPlot->setSeries(scfEnergy, QStringLiteral("SCF E_KS at geometry step %1").arg(selectedStep), QStringLiteral("E_KS (eV)"));
    m_scfResidualPlot->setSeries(scfResidual, QStringLiteral("SCF residual path at geometry step %1").arg(selectedStep), QStringLiteral("dDmax / DM.Tolerance"), 1.0);
    const auto geometryIterator = std::find_if(result.geometry.cbegin(), result.geometry.cend(), [selectedStep](const SiestaGeometryRecord& record) { return record.step == selectedStep; });
    const bool scfConverged = geometryIterator != result.geometry.cend() && geometryIterator->scfConverged;
    const double selectedRatio = lastScf && m_dmThresholdSpin->value() > 0.0 ? lastScf->dmError / m_dmThresholdSpin->value() : std::numeric_limits<double>::quiet_NaN();
    m_scfDiagnostics->setText(QStringLiteral("DM.Tolerance=%1 | 最終 dDmax=%2 | 比率=%3 × | SCF反復=%4 / Max=%5 | %6")
        .arg(QString::number(m_dmThresholdSpin->value(), 'g', 6), lastScf ? QString::number(lastScf->dmError, 'g', 6) : QStringLiteral("-"),
             std::isfinite(selectedRatio) ? QString::number(selectedRatio, 'g', 6) : QStringLiteral("-"))
        .arg(lastScf ? lastScf->iteration : 0).arg(result.maxScfIterations).arg(scfConverged && (!std::isfinite(selectedRatio) || selectedRatio <= 1.0) ? QStringLiteral("PASS") : QStringLiteral("NOT converged / incomplete")));

    QVector<QPointF> points;
    const QString metric = m_metricCombo->currentData().toString();
    QString title; QString yLabel; double threshold = std::numeric_limits<double>::quiet_NaN();
    if (metric == QStringLiteral("energy")) {
        for (const auto& record : result.geometry) if (record.hasEnergy) points << QPointF(record.step, record.energyEv);
        title = QStringLiteral("Geometry energy convergence"); yLabel = QStringLiteral("E_KS (eV)");
    } else if (metric == QStringLiteral("free_energy")) {
        for (const auto& record : result.geometry) if (record.hasFreeEnergy) points << QPointF(record.step, record.freeEnergyEv);
        title = QStringLiteral("Geometry free-energy convergence"); yLabel = QStringLiteral("FreeEng (eV)");
    } else if (metric == QStringLiteral("force_ratio")) {
        for (const auto& record : result.geometry) if (record.hasForce && m_forceThresholdSpin->value() > 0.0) points << QPointF(record.step, record.maxForceEvAng / m_forceThresholdSpin->value());
        title = QStringLiteral("Force convergence ratio"); yLabel = QStringLiteral("Max force / threshold"); threshold = 1.0;
    } else if (metric == QStringLiteral("stress_ratio")) {
        const double stressThresholdKbar = result.stressToleranceGpa > 0.0 ? result.stressToleranceGpa * 10.0 : 0.0;
        for (const auto& record : result.geometry) if (record.hasStress && stressThresholdKbar > 0.0) points << QPointF(record.step, record.maxStressKbar / stressThresholdKbar);
        title = QStringLiteral("Stress convergence ratio"); yLabel = QStringLiteral("Max stress / threshold"); threshold = 1.0;
    } else if (metric == QStringLiteral("scf")) {
        for (const auto& record : result.geometry) points << QPointF(record.step, record.scfIterations);
        title = QStringLiteral("SCF iterations per geometry step"); yLabel = QStringLiteral("Iterations"); if (result.maxScfIterations > 0) threshold = result.maxScfIterations;
    } else if (metric == QStringLiteral("dm_ratio")) {
        for (int row = 0; row < result.scf.size(); ++row) if (m_dmThresholdSpin->value() > 0.0) points << QPointF(row + 1, result.scf.at(row).dmError / m_dmThresholdSpin->value());
        title = QStringLiteral("SCF density-matrix convergence ratio"); yLabel = QStringLiteral("DM error / tolerance"); threshold = 1.0;
    } else {
        for (const auto& record : result.geometry) if (record.hasCellVolume) points << QPointF(record.step, record.cellVolumeAng3);
        title = QStringLiteral("Cell-volume history"); yLabel = QStringLiteral("Volume (Å³)");
    }
    m_plot->setSeries(points, title, yLabel, threshold);
}

void SiestaResultsDialog::exportSummaryCsv() {
    if (m_results.isEmpty()) return;
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("SIESTA解析サマリーCSV"), QStringLiteral("siesta_analysis_summary.csv"), QStringLiteral("CSV (*.csv)"));
    if (path.isEmpty()) return;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) { QMessageBox::warning(this, QStringLiteral("CSV保存"), QStringLiteral("保存先を開けません。")); return; }
    QTextStream out(&file); out.setEncoding(QStringConverter::Utf8);
    out << "file,status,system,version,steps,last_energy_ev,last_force_ev_ang,force_tolerance,force_ratio,last_scf_iterations,warnings\n";
    for (const auto& result : std::as_const(m_results)) {
        const auto* last = latestMeaningfulGeometry(result);
        const double threshold = result.forceToleranceEvAng > 0.0 ? result.forceToleranceEvAng : m_forceThresholdSpin->value();
        const double ratio = last && last->hasForce && threshold > 0.0 ? last->maxForceEvAng / threshold : 0.0;
        out << csvField(result.path) << ',' << csvField(result.status) << ',' << csvField(result.systemName) << ',' << csvField(result.version) << ',' << result.geometry.size() << ','
            << (last && last->hasEnergy ? QString::number(last->energyEv, 'g', 12) : QString()) << ',' << (last && last->hasForce ? QString::number(last->maxForceEvAng, 'g', 9) : QString()) << ','
            << QString::number(threshold, 'g', 9) << ',' << (last && last->hasForce ? QString::number(ratio, 'g', 9) : QString()) << ',' << (last ? QString::number(last->scfIterations) : QString()) << ',' << csvField(result.warnings.join(QStringLiteral("; "))) << '\n';
    }
    if (!file.commit()) QMessageBox::warning(this, QStringLiteral("CSV保存"), QStringLiteral("CSVの保存に失敗しました。"));
}

void SiestaResultsDialog::exportAnalysisJson() {
    if (m_results.isEmpty()) return;
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("SIESTA解析JSON"), QStringLiteral("siesta_analysis.json"), QStringLiteral("JSON (*.json)"));
    if (path.isEmpty()) return;
    QJsonArray files;
    for (const auto& result : std::as_const(m_results)) {
        QJsonObject root{{QStringLiteral("file"), result.path}, {QStringLiteral("status"), result.status}, {QStringLiteral("version"), result.version},
                         {QStringLiteral("system_name"), result.systemName}, {QStringLiteral("system_label"), result.systemLabel},
                         {QStringLiteral("normal_end"), result.normalEnd}, {QStringLiteral("fatal_error"), result.fatalError},
                         {QStringLiteral("max_scf_iterations"), result.maxScfIterations}, {QStringLiteral("dm_tolerance"), result.dmTolerance},
                         {QStringLiteral("force_tolerance_ev_ang"), result.forceToleranceEvAng}, {QStringLiteral("stress_tolerance_gpa"), result.stressToleranceGpa}};
        QJsonArray warnings; for (const QString& warning : result.warnings) warnings.append(warning); root.insert(QStringLiteral("warnings"), warnings);
        QJsonArray geometry;
        for (const auto& record : result.geometry) {
            QJsonObject item{{QStringLiteral("step"), record.step}, {QStringLiteral("scf_iterations"), record.scfIterations}, {QStringLiteral("scf_converged"), record.scfConverged}};
            auto optional = [&](const char* key, double value, bool present) { if (present) item.insert(QString::fromLatin1(key), value); };
            optional("energy_ev", record.energyEv, record.hasEnergy); optional("free_energy_ev", record.freeEnergyEv, record.hasFreeEnergy);
            optional("max_force_ev_ang", record.maxForceEvAng, record.hasForce); optional("raw_max_force_ev_ang", record.rawMaxForceEvAng, record.hasRawForce);
            optional("constrained_max_force_ev_ang", record.constrainedMaxForceEvAng, record.hasConstrainedForce); optional("residual_force_ev_ang", record.residualForceEvAng, record.hasResidualForce);
            optional("max_stress_kbar", record.maxStressKbar, record.hasStress); optional("dm_error", record.dmError, record.hasDmError); optional("h_error_ev", record.hErrorEv, record.hasHError);
            optional("cell_volume_ang3", record.cellVolumeAng3, record.hasCellVolume);
            if (record.hasCellLengths) { QJsonArray values; for (double value : record.cellLengthsAng) values.append(value); item.insert(QStringLiteral("cell_lengths_ang"), values); }
            if (record.hasCellAngles) { QJsonArray values; for (double value : record.cellAnglesDeg) values.append(value); item.insert(QStringLiteral("cell_angles_deg"), values); }
            if (record.hasStress) { QJsonArray values; for (double value : record.stressVoigtKbar) values.append(value); item.insert(QStringLiteral("stress_voigt_kbar"), values); }
            QJsonArray notes; for (const QString& note : record.notes) notes.append(note); item.insert(QStringLiteral("notes"), notes);
            geometry.append(item);
        }
        root.insert(QStringLiteral("geometry"), geometry);
        QJsonArray scf;
        for (const auto& record : result.scf) {
            QJsonObject item{{QStringLiteral("geometry_step"), record.geometryStep}, {QStringLiteral("iteration"), record.iteration}, {QStringLiteral("global_iteration"), record.globalIteration},
                             {QStringLiteral("eharris_ev"), record.eharrisEv}, {QStringLiteral("energy_ev"), record.energyEv}, {QStringLiteral("free_energy_ev"), record.freeEnergyEv},
                             {QStringLiteral("dm_error"), record.dmError}, {QStringLiteral("fermi_energy_ev"), record.fermiEnergyEv}};
            if (record.hasHError) item.insert(QStringLiteral("h_error_ev"), record.hErrorEv);
            scf.append(item);
        }
        root.insert(QStringLiteral("scf"), scf); files.append(root);
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) { QMessageBox::warning(this, QStringLiteral("JSON保存"), QStringLiteral("保存先を開けません。")); return; }
    file.write(QJsonDocument(QJsonObject{{QStringLiteral("format"), QStringLiteral("ASEapp SIESTA analysis 1")}, {QStringLiteral("files"), files}}).toJson(QJsonDocument::Indented));
    if (!file.commit()) QMessageBox::warning(this, QStringLiteral("JSON保存"), QStringLiteral("JSONの保存に失敗しました。"));
}

void SiestaResultsDialog::exportCurrentDetailsCsv() {
    const int index = currentResultIndex(); if (index < 0 || index >= m_results.size()) return;
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Geometry詳細CSV"), QFileInfo(m_results.at(index).path).completeBaseName() + QStringLiteral("_geometry.csv"), QStringLiteral("CSV (*.csv)"));
    if (path.isEmpty()) return;
    const auto& result = m_results.at(index);
    QSaveFile geometryFile(path); if (!geometryFile.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream geometryOut(&geometryFile); geometryOut.setEncoding(QStringConverter::Utf8);
    geometryOut << "step,scf_iterations,converged,e_ks_ev,free_energy_ev,max_force_ev_ang,raw_force_ev_ang,constrained_force_ev_ang,residual_force_ev_ang,max_stress_kbar,dm_error,h_error_ev,cell_a,cell_b,cell_c,alpha,beta,gamma,volume_ang3,notes\n";
    for (const auto& r : result.geometry) geometryOut << r.step << ',' << r.scfIterations << ',' << (r.scfConverged ? 1 : 0) << ',' << displayNumber(r.energyEv,r.hasEnergy,12) << ',' << displayNumber(r.freeEnergyEv,r.hasFreeEnergy,12) << ',' << displayNumber(r.maxForceEvAng,r.hasForce,10) << ',' << displayNumber(r.rawMaxForceEvAng,r.hasRawForce,10) << ',' << displayNumber(r.constrainedMaxForceEvAng,r.hasConstrainedForce,10) << ',' << displayNumber(r.residualForceEvAng,r.hasResidualForce,10) << ',' << displayNumber(r.maxStressKbar,r.hasStress,10) << ',' << displayNumber(r.dmError,r.hasDmError,10) << ',' << displayNumber(r.hErrorEv,r.hasHError,10) << ',' << (r.hasCellLengths ? QString::number(r.cellLengthsAng[0],'g',10) : QString()) << ',' << (r.hasCellLengths ? QString::number(r.cellLengthsAng[1],'g',10) : QString()) << ',' << (r.hasCellLengths ? QString::number(r.cellLengthsAng[2],'g',10) : QString()) << ',' << (r.hasCellAngles ? QString::number(r.cellAnglesDeg[0],'g',10) : QString()) << ',' << (r.hasCellAngles ? QString::number(r.cellAnglesDeg[1],'g',10) : QString()) << ',' << (r.hasCellAngles ? QString::number(r.cellAnglesDeg[2],'g',10) : QString()) << ',' << displayNumber(r.cellVolumeAng3,r.hasCellVolume,10) << ',' << csvField(r.notes.join(QStringLiteral("; "))) << '\n';
    if (!geometryFile.commit()) return;
    const QFileInfo info(path); const QString scfPath = info.dir().filePath(info.completeBaseName().replace(QRegularExpression(QStringLiteral("_geometry$")), QString()) + QStringLiteral("_scf.csv"));
    QSaveFile scfFile(scfPath); if (!scfFile.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream scfOut(&scfFile); scfOut.setEncoding(QStringConverter::Utf8); scfOut << "geometry_step,iteration,global_iteration,eharris_ev,e_ks_ev,free_energy_ev,dm_error,fermi_energy_ev,h_error_ev\n";
    for (const auto& r : result.scf) scfOut << r.geometryStep << ',' << r.iteration << ',' << r.globalIteration << ',' << QString::number(r.eharrisEv,'g',12) << ',' << QString::number(r.energyEv,'g',12) << ',' << QString::number(r.freeEnergyEv,'g',12) << ',' << QString::number(r.dmError,'g',10) << ',' << QString::number(r.fermiEnergyEv,'g',10) << ',' << (r.hasHError ? QString::number(r.hErrorEv,'g',10) : QString()) << '\n';
    scfFile.commit();
}

void SiestaResultsDialog::exportPlotPng() {
    if (currentResultIndex() < 0) return;
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("収束グラフPNG"), QStringLiteral("siesta_convergence.png"), QStringLiteral("PNG (*.png)"));
    if (!path.isEmpty() && !m_plot->grab().save(path, "PNG")) QMessageBox::warning(this, QStringLiteral("PNG保存"), QStringLiteral("グラフを保存できませんでした。"));
}

void SiestaResultsDialog::removeSelectedFiles() {
    QSet<int> rows; for (const QModelIndex& index : m_summaryTable->selectionModel()->selectedRows()) rows.insert(index.row());
    QList<int> sorted = rows.values(); std::sort(sorted.begin(), sorted.end(), std::greater<int>());
    for (int row : sorted) if (row >= 0 && row < m_results.size()) m_results.removeAt(row);
    refreshViews();
}

void SiestaResultsDialog::clearFiles() { m_results.clear(); refreshViews(); }

void SiestaResultsDialog::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void SiestaResultsDialog::dropEvent(QDropEvent* event) {
    QStringList paths;
    for (const QUrl& url : event->mimeData()->urls()) if (url.isLocalFile()) paths << url.toLocalFile();
    addFiles(paths); event->acceptProposedAction();
}
