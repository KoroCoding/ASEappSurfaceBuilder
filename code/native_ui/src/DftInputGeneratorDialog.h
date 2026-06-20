#pragma once

#include <QDialog>
#include <QVector>

#include "DftInputTypes.h"
#include "StructureData.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QTableWidget;
class QTextEdit;
class QTabWidget;

class DftInputGeneratorDialog : public QDialog {
    Q_OBJECT
public:
    explicit DftInputGeneratorDialog(const StructureData& structure, QWidget* parent = nullptr);

private:
    void buildUi();
    void rebuildCodeDependentUi();
    void syncControlsFromSettings();
    void refreshStructureTab();
    void refreshHydrogenTab();
    void refreshSpeciesTable();
    void refreshParameterTable();
    void refreshRawTable();
    void collectUiToSettings();
    void updatePreview();
    void importSettingsFile();
    void exportGeneratedFiles();
    void applyHydrogenRoleToSelected(DftHydrogenRole role);
    void setBottomHydrogenRole(DftHydrogenRole role);
    void setSelectedAtomsMovable(bool movable);

    StructureData m_structure;
    DftSettings m_settings;
    DftGeneratedInput m_generated;

    QTabWidget* m_tabs = nullptr;
    QComboBox* m_codeCombo = nullptr;
    QComboBox* m_versionCombo = nullptr;
    QComboBox* m_profileCombo = nullptr;
    QComboBox* m_generationModeCombo = nullptr;
    QComboBox* m_calculationModeCombo = nullptr;
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
    QTableWidget* m_structureTable = nullptr;
    QTableWidget* m_hydrogenTable = nullptr;
    QTableWidget* m_speciesTable = nullptr;
    QTableWidget* m_parameterTable = nullptr;
    QTableWidget* m_rawTable = nullptr;
    QTextEdit* m_previewEdit = nullptr;
    QTextEdit* m_warningEdit = nullptr;
};
