#pragma once

#include <QDialog>
#include <QDateTime>
#include <QString>
#include <QVector>

#include <array>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QDragEnterEvent;
class QDropEvent;
class QLabel;
class QPlainTextEdit;
class QTableWidget;
class QTimer;
class SiestaConvergencePlot;

struct SiestaScfRecord {
    int geometryStep = 0;
    int iteration = 0;
    int globalIteration = 0;
    double eharrisEv = 0.0;
    double energyEv = 0.0;
    double freeEnergyEv = 0.0;
    double dmError = 0.0;
    double fermiEnergyEv = 0.0;
    double hErrorEv = 0.0;
    bool hasHError = false;
};

struct SiestaGeometryRecord {
    int step = 0;
    int scfIterations = 0;
    bool scfConverged = false;
    double energyEv = 0.0;
    bool hasEnergy = false;
    double freeEnergyEv = 0.0;
    bool hasFreeEnergy = false;
    double maxForceEvAng = 0.0;
    bool hasForce = false;
    double rawMaxForceEvAng = 0.0;
    bool hasRawForce = false;
    double constrainedMaxForceEvAng = 0.0;
    bool hasConstrainedForce = false;
    double residualForceEvAng = 0.0;
    bool hasResidualForce = false;
    double maxStressKbar = 0.0;
    bool hasStress = false;
    std::array<double, 6> stressVoigtKbar{};
    double dmError = 0.0;
    bool hasDmError = false;
    double hErrorEv = 0.0;
    bool hasHError = false;
    std::array<double, 3> cellLengthsAng{};
    std::array<double, 3> cellAnglesDeg{};
    double cellVolumeAng3 = 0.0;
    bool hasCellLengths = false;
    bool hasCellAngles = false;
    bool hasCellVolume = false;
    QStringList notes;
};

struct SiestaAnalysisResult {
    QString path;
    QString version;
    QString systemName;
    QString systemLabel;
    QString status;
    QStringList warnings;
    QString outputTail;
    int maxScfIterations = 0;
    double dmTolerance = 0.0;
    double forceToleranceEvAng = 0.0;
    double stressToleranceGpa = 0.0;
    bool normalEnd = false;
    bool fatalError = false;
    qint64 fileSize = 0;
    QDateTime lastModified;
    QVector<SiestaScfRecord> scf;
    QVector<SiestaGeometryRecord> geometry;
};

class SiestaResultsParser {
public:
    static SiestaAnalysisResult parseFile(const QString& path, QString* errorMessage = nullptr);
};

class SiestaResultsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SiestaResultsDialog(QWidget* parent = nullptr);
    void addFiles(const QStringList& paths);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void buildUi();
    void chooseFiles();
    void chooseFolder();
    void reloadAll();
    void refreshViews();
    void exportSummaryCsv();
    void exportAnalysisJson();
    void exportCurrentDetailsCsv();
    void exportPlotPng();
    void removeSelectedFiles();
    void clearFiles();
    int currentResultIndex() const;

    QVector<SiestaAnalysisResult> m_results;
    QTableWidget* m_summaryTable = nullptr;
    QComboBox* m_resultCombo = nullptr;
    QComboBox* m_metricCombo = nullptr;
    QDoubleSpinBox* m_forceThresholdSpin = nullptr;
    QDoubleSpinBox* m_dmThresholdSpin = nullptr;
    QCheckBox* m_autoRefreshCheck = nullptr;
    QLabel* m_statusLabel = nullptr;
    SiestaConvergencePlot* m_plot = nullptr;
    SiestaConvergencePlot* m_overviewEnergyPlot = nullptr;
    SiestaConvergencePlot* m_overviewForcePlot = nullptr;
    SiestaConvergencePlot* m_overviewScfPlot = nullptr;
    QLabel* m_overviewDiagnostics = nullptr;
    QLabel* m_runDashboard = nullptr;
    QLabel* m_energyDashboard = nullptr;
    QLabel* m_forceDashboard = nullptr;
    QLabel* m_scfDashboard = nullptr;
    QLabel* m_stressDashboard = nullptr;
    QComboBox* m_scfStepCombo = nullptr;
    SiestaConvergencePlot* m_scfEnergyPlot = nullptr;
    SiestaConvergencePlot* m_scfResidualPlot = nullptr;
    QLabel* m_scfDiagnostics = nullptr;
    QTableWidget* m_geometryTable = nullptr;
    QTableWidget* m_scfTable = nullptr;
    QPlainTextEdit* m_tailEdit = nullptr;
    QTimer* m_timer = nullptr;
};
