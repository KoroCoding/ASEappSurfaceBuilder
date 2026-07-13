#pragma once

#include <QDialog>
#include <QMap>
#include <QStringList>
#include <QVector>

#include "DftInputTypes.h"
#include "StructureData.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QTableWidget;
class QTextEdit;
class QTabWidget;
class QVBoxLayout;

class DftInputGeneratorDialog : public QDialog {
    Q_OBJECT
public:
    explicit DftInputGeneratorDialog(const StructureData& structure, QWidget* parent = nullptr);
    void setBatchStructurePaths(const QStringList& paths);

private:
    void buildUi();
    void rebuildCodeDependentUi();
    void syncControlsFromSettings();
    void updateContextLabels();
    void refreshStructureTab();
    void refreshOverviewTab();
    void refreshHydrogenTab();
    void refreshSpeciesTable();
    void refreshParameterTable();
    void refreshRawTable();
    void refreshSourceSummary();
    void collectUiToSettings();
    void updatePreview();
    void saveCurrentDefaults();
    void loadDefaultsFile();
    void exportDefaultsFile();
    void importSettingsFile();
    void exportGeneratedFiles();
    void selectStructureFile();
    void selectBatchStructureFiles();
    void selectBatchStructureFolder();
    void applyHydrogenRoleToSelected(DftHydrogenRole role);
    void setBottomHydrogenRole(DftHydrogenRole role);
    void setSelectedAtomsMovable(bool movable);
    void replaceSelectedAtomsElement();
    void syncTargetNameParameters();
    void syncVisibleTargetNameParameterWidgets();
    bool loadStructurePath(const QString& path, StructureData* structure, QString* errorMessage = nullptr) const;
    void setActiveStructure(const StructureData& structure, const QString& targetName, bool resetHydrogen);
    DftSettings settingsForStructure(const StructureData& structure, const QString& targetName) const;
    QString targetNameForStructureSource(const QString& path, const StructureData& structure) const;
    bool exportSingleGeneratedFile(const QString& outputDirectory, const StructureData& structure,
                                   const DftSettings& settings, QString* errorMessage,
                                   DftGeneratedInput* generatedOut = nullptr) const;

    StructureData m_initialStructure;
    StructureData m_structure;
    DftSettings m_settings;
    DftGeneratedInput m_generated;

    QTabWidget* m_tabs = nullptr;
    QComboBox* m_codeCombo = nullptr;
    QComboBox* m_versionCombo = nullptr;
    QComboBox* m_profileCombo = nullptr;
    QComboBox* m_generationModeCombo = nullptr;
    QComboBox* m_calculationModeCombo = nullptr;
    QComboBox* m_structureSourceCombo = nullptr;
    QLineEdit* m_sourcePathEdit = nullptr;
    QLabel* m_sourceSummaryLabel = nullptr;
    QPushButton* m_selectSourceButton = nullptr;
    QPushButton* m_selectBatchFilesButton = nullptr;
    QPushButton* m_selectBatchFolderButton = nullptr;
    QLineEdit* m_targetEdit = nullptr;
    QLineEdit* m_xcFdfEdit = nullptr;
    QLineEdit* m_pseudoDirEdit = nullptr;
    QCheckBox* m_includeXcCheck = nullptr;
    QCheckBox* m_standaloneCheck = nullptr;
    QCheckBox* m_allowUnknownHydrogenCheck = nullptr;
    QCheckBox* m_assumeIsolatedCheck = nullptr;
    QCheckBox* m_projectStyleFlagsCheck = nullptr;
    QComboBox* m_fixedAtomModeCombo = nullptr;
    QComboBox* m_trailingFlagInterpretationCombo = nullptr;
    QLabel* m_setupContextLabel = nullptr;
    QLabel* m_parameterContextLabel = nullptr;
    QTableWidget* m_overviewTable = nullptr;
    QTextEdit* m_checklistEdit = nullptr;
    QLineEdit* m_parameterSearchEdit = nullptr;
    QComboBox* m_parameterCatalogModeCombo = nullptr;
    QComboBox* m_parameterSectionFilter = nullptr;
    QComboBox* m_parameterScopeFilter = nullptr;
    QTableWidget* m_structureTable = nullptr;
    QTableWidget* m_hydrogenTable = nullptr;
    QTableWidget* m_speciesTable = nullptr;
    QScrollArea* m_parameterScrollArea = nullptr;
    QWidget* m_parameterListWidget = nullptr;
    QVBoxLayout* m_parameterListLayout = nullptr;
    QMap<QString, QWidget*> m_parameterValueWidgets;
    QMap<QString, QCheckBox*> m_parameterEnabledChecks;
    QTableWidget* m_parameterTable = nullptr;
    QTableWidget* m_rawTable = nullptr;
    QTextEdit* m_previewEdit = nullptr;
    QTextEdit* m_warningEdit = nullptr;
    QPushButton* m_exportButton = nullptr;
    QTableWidget* m_batchResultTable = nullptr;
    QStringList m_batchSourcePaths;
};
