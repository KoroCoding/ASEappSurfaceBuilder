#pragma once

#include <QMainWindow>

#include <array>
#include <vector>

#include "StructureData.h"
#include "SurfaceCustomizationRegistry.h"

class QLabel;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QPushButton;
class StructureCanvas;
class StructureFileLoader;
class QDragEnterEvent;
class QDropEvent;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    bool loadStructureFile(const QString& path);

protected:
    void showEvent(QShowEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void openStructure();
    void saveStructureAs();
    void exportElementLegendImage();
    void showStartupGuide();
    void showUsageHelp();
    void showAboutDialog();
    void fitStructure();
    void resetView();
    void toggleLanguage();
    void createSupercell();
    void terminateHydrogen();
    void addVacuumLayer();
    void removeVacuumLayer();
    void tiltCellAxis();
    void choosePlacementElement();
    void applySelectedPreset();
    void reloadPresetRegistry();
    void openPresetFile();
    void toggleAtomSelection(int atomId);
    void translateSelectedAtoms(const QVector3D& delta);
    void finishSelectedAtomTranslation();
    void clearSelection();
    void deleteSelectedAtoms();
    void undoEdit();
    void redoEdit();
    void showMeasurementReport();
    void showStructureCheckReport();
    void saveSelectedPrecursorCsv();
    void loadPrecursorCsv();
    void placeLoadedPrecursor();
    void syncCanvasDisplayOptions();

private:
    struct PrecursorTemplate {
        QString name;
        std::vector<NativeAtom> atoms;
        QString sourcePath;
    };

    void buildUi();
    void applyTheme();
    void applyStructureState(const StructureData& structure);
    void setCView(bool resetPan = true);
    QString uiText(const QString& key) const;
    SurfacePlacementRule currentPlacementRule() const;
    void refreshPresetUi();
    void refreshSelectionUi();
    void refreshPrecursorUi();
    const PrecursorTemplate* currentPrecursorTemplate() const;
    void setLoadedPrecursors(std::vector<PrecursorTemplate> precursors, const QString& preferredName = QString());
    bool maybeSaveChanges();
    bool loadFromPath(const QString& path);
    QString defaultOpenDirectory() const;
    QString formatVector(const QVector3D& vec) const;
    QString describePlacementRule(const SurfacePlacementRule& rule) const;
    QString supercellStatusText() const;
    void setSelectedAtomIds(const std::vector<int>& atomIds);
    void pushUndoState(const QString& label);
    void replaceStructureFromEdit(const StructureData& structure, const QString& label);
    void updateUndoRedoActions();
    QString structureDiagnosticsText(bool includeMeasurements) const;

    StructureCanvas* m_canvas = nullptr;
    StructureFileLoader* m_loader = nullptr;
    SurfaceCustomizationRegistry m_customizationRegistry;
    StructureData m_structure;
    StructureData m_supercellBaseStructure;
    bool m_hasSupercellBaseStructure = false;
    bool m_lastEditWasSupercell = false;
    std::array<int, 3> m_supercellFactors{1, 1, 1};
    QLabel* m_fileLabel = nullptr;
    QLabel* m_summaryLabel = nullptr;
    QLabel* m_presetPathLabel = nullptr;
    QLabel* m_presetDetailsLabel = nullptr;
    QLabel* m_selectionLabel = nullptr;
    QLabel* m_precursorLabel = nullptr;
    QLabel* m_supercellStatusLabel = nullptr;
    QComboBox* m_presetCombo = nullptr;
    QComboBox* m_placementModeCombo = nullptr;
    QComboBox* m_elementCombo = nullptr;
    QComboBox* m_precursorCombo = nullptr;
    QAction* m_openAction = nullptr;
    QAction* m_saveAction = nullptr;
    QAction* m_exportLegendAction = nullptr;
    QAction* m_quickStartAction = nullptr;
    QAction* m_fitAction = nullptr;
    QAction* m_resetAction = nullptr;
    QAction* m_supercellAction = nullptr;
    QAction* m_terminateAction = nullptr;
    QAction* m_vacuumAction = nullptr;
    QAction* m_removeVacuumAction = nullptr;
    QAction* m_axisTiltAction = nullptr;
    QAction* m_showCellAction = nullptr;
    QAction* m_showBondsAction = nullptr;
    QAction* m_showAxesAction = nullptr;
    QAction* m_showLabelsAction = nullptr;
    QAction* m_perspectiveAction = nullptr;
    QAction* m_depthCueAction = nullptr;
    QAction* m_helpAction = nullptr;
    QAction* m_aboutAction = nullptr;
    QAction* m_languageAction = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QCheckBox* m_showCellCheck = nullptr;
    QCheckBox* m_showBondsCheck = nullptr;
    QCheckBox* m_showAxesCheck = nullptr;
    QCheckBox* m_showLabelsCheck = nullptr;
    QCheckBox* m_perspectiveCheck = nullptr;
    QCheckBox* m_depthCueCheck = nullptr;
    QCheckBox* m_previewPlacementCheck = nullptr;
    QDoubleSpinBox* m_atomScaleSpin = nullptr;
    QDoubleSpinBox* m_placementHeightSpin = nullptr;
    QDoubleSpinBox* m_placementTiltSpin = nullptr;
    QDoubleSpinBox* m_placementFractionSpin = nullptr;
    QPushButton* m_periodicTableButton = nullptr;
    QPushButton* m_applyPresetButton = nullptr;
    QPushButton* m_reloadPresetButton = nullptr;
    QPushButton* m_openPresetButton = nullptr;
    QPushButton* m_clearSelectionButton = nullptr;
    QPushButton* m_deleteSelectedButton = nullptr;
    QPushButton* m_savePrecursorButton = nullptr;
    QPushButton* m_loadPrecursorButton = nullptr;
    QPushButton* m_placePrecursorButton = nullptr;
    std::vector<int> m_selectedAtomIds;
    std::vector<PrecursorTemplate> m_loadedPrecursors;
    std::vector<StructureData> m_undoStack;
    std::vector<StructureData> m_redoStack;
    bool m_translationUndoActive = false;
    bool m_japanese = true;
    bool m_initialCViewAppliedAfterShow = false;
    bool m_moveAtomsMode = false;
    bool m_moveModelMode = false;
};
