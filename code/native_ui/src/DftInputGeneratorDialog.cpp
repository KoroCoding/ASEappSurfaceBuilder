#include "DftInputGeneratorDialog.h"

#include "DftInputGenerator.h"
#include "DftInputParser.h"
#include "DftParameterRegistry.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTextEdit>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>

namespace {

QTableWidgetItem* item(const QString& text) {
    auto* it = new QTableWidgetItem(text);
    it->setFlags(it->flags() & ~Qt::ItemIsEditable);
    return it;
}

QTableWidgetItem* editableItem(const QString& text) {
    return new QTableWidgetItem(text);
}

QString yesNo(bool value) { return value ? QStringLiteral("true") : QStringLiteral("false"); }

QString vectorText(const QVector3D& v) {
    return QStringLiteral("%1 %2 %3").arg(v.x(), 0, 'f', 6).arg(v.y(), 0, 'f', 6).arg(v.z(), 0, 'f', 6);
}

} // namespace

DftInputGeneratorDialog::DftInputGeneratorDialog(const StructureData& structure, QWidget* parent)
    : QDialog(parent), m_structure(structure) {
    const QString baseTarget = structure.title.isEmpty() ? QStringLiteral("ideal") : structure.title;
    m_settings = DftParameterRegistry::defaultSettings(DftCode::Siesta, QStringLiteral("4.1.5"), DftInputGenerator::sanitizeTargetName(baseTarget));
    m_settings.sourceStructurePath = structure.sourcePath;
    m_settings.trailingFlagInterpretation = dftTrailingFlagInterpretationFromKey(structure.trailingFlagInterpretation);
    m_settings.hydrogenAssignments = DftInputGenerator::inferHydrogenRoles(structure, m_settings);
    setWindowTitle(QStringLiteral("DFT入力生成"));
    resize(1180, 780);
    buildUi();
    updatePreview();
}

void DftInputGeneratorDialog::buildUi() {
    auto* root = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);
    root->addWidget(m_tabs, 1);

    auto* structurePage = new QWidget(this);
    auto* structureLayout = new QVBoxLayout(structurePage);
    auto* structureSummary = new QLabel(structurePage);
    structureSummary->setObjectName(QStringLiteral("structureSummaryLabel"));
    structureLayout->addWidget(structureSummary);
    auto* fixedButtonRow = new QHBoxLayout();
    auto* fixSelectedButton = new QPushButton(QStringLiteral("Fix selected atoms"), structurePage);
    auto* freeSelectedButton = new QPushButton(QStringLiteral("Free selected atoms"), structurePage);
    fixSelectedButton->setObjectName(QStringLiteral("fixSelectedAtomsButton"));
    freeSelectedButton->setObjectName(QStringLiteral("freeSelectedAtomsButton"));
    fixSelectedButton->setToolTip(QStringLiteral("選択原子のNativeAtom::movableをfalseにし、Fixed atom modeがPreserve/Manual系のとき固定候補として扱います。"));
    freeSelectedButton->setToolTip(QStringLiteral("選択原子のNativeAtom::movableをtrueに戻します。Geometry.Constraints/QE flagsへの反映はFixed atom modeに従います。"));
    fixedButtonRow->addWidget(fixSelectedButton);
    fixedButtonRow->addWidget(freeSelectedButton);
    fixedButtonRow->addStretch(1);
    structureLayout->addLayout(fixedButtonRow);
    m_structureTable = new QTableWidget(structurePage);
    m_structureTable->setObjectName(QStringLiteral("structureTable"));
    m_structureTable->setColumnCount(13);
    m_structureTable->setHorizontalHeaderLabels({"index", "element", "x", "y", "z", "frac_x", "frac_y", "frac_z", "species", "HydrogenRole", "imported_movable", "fixed_reason", "warning"});
    m_structureTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    structureLayout->addWidget(m_structureTable, 1);
    m_tabs->addTab(structurePage, QStringLiteral("Structure"));
    connect(fixSelectedButton, &QPushButton::clicked, this, [this]() { setSelectedAtomsMovable(false); });
    connect(freeSelectedButton, &QPushButton::clicked, this, [this]() { setSelectedAtomsMovable(true); });

    auto* hydrogenPage = new QWidget(this);
    auto* hydrogenLayout = new QVBoxLayout(hydrogenPage);
    auto* hButtonRow = new QHBoxLayout();
    auto* inferButton = new QPushButton(QStringLiteral("Infer H roles"), hydrogenPage);
    auto* setH075Button = new QPushButton(QStringLiteral("Set selected as H-0.750"), hydrogenPage);
    auto* setH125Button = new QPushButton(QStringLiteral("Set selected as H-1.250"), hydrogenPage);
    auto* setOrdinaryButton = new QPushButton(QStringLiteral("Set selected as ordinary H"), hydrogenPage);
    auto* setBottomButton = new QPushButton(QStringLiteral("Set bottom H as H-0.750"), hydrogenPage);
    inferButton->setObjectName(QStringLiteral("inferHydrogenRolesButton"));
    setH075Button->setObjectName(QStringLiteral("setH075Button"));
    setH125Button->setObjectName(QStringLiteral("setH125Button"));
    setOrdinaryButton->setObjectName(QStringLiteral("setOrdinaryHydrogenButton"));
    setBottomButton->setObjectName(QStringLiteral("setBottomHydrogen075Button"));
    inferButton->setToolTip(QStringLiteral("HydrogenRoleを構造から再推定します。底面N近傍HはH-0.750、底面Ga/Al近傍HはH-1.250候補です。"));
    setH075Button->setToolTip(QStringLiteral("選択HのHydrogenRoleをH-0.750 pseudo-Hに手動変更します。SIESTA species index 1 / QE H.pbe-MT.075.UPF候補です。"));
    setH125Button->setToolTip(QStringLiteral("選択HのHydrogenRoleをH-1.250 pseudo-Hに手動変更します。"));
    setOrdinaryButton->setToolTip(QStringLiteral("選択Hをordinary/surface Hとして扱います。pseudo-H固定候補から外れます。"));
    setBottomButton->setToolTip(QStringLiteral("最下部HをまとめてH-0.750にします。7layer底面pseudo-Hの手動補正用です。"));
    hButtonRow->addWidget(inferButton);
    hButtonRow->addWidget(setH075Button);
    hButtonRow->addWidget(setH125Button);
    hButtonRow->addWidget(setOrdinaryButton);
    hButtonRow->addWidget(setBottomButton);
    hButtonRow->addStretch(1);
    hydrogenLayout->addLayout(hButtonRow);
    m_hydrogenTable = new QTableWidget(hydrogenPage);
    m_hydrogenTable->setObjectName(QStringLiteral("hydrogenRoleTable"));
    m_hydrogenTable->setColumnCount(13);
    m_hydrogenTable->setHorizontalHeaderLabels({"atom index", "element", "x", "y", "z", "fractional z", "nearest non-H", "distance", "inferred", "selected", "confidence", "SIESTA species", "QE pseudo"});
    m_hydrogenTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    hydrogenLayout->addWidget(m_hydrogenTable, 1);
    m_tabs->addTab(hydrogenPage, QStringLiteral("Hydrogen Roles"));
    connect(inferButton, &QPushButton::clicked, this, [this]() { m_settings.hydrogenAssignments = DftInputGenerator::inferHydrogenRoles(m_structure, m_settings); refreshHydrogenTab(); updatePreview(); });
    connect(setH075Button, &QPushButton::clicked, this, [this]() { applyHydrogenRoleToSelected(DftHydrogenRole::BottomPseudoHNTerminated075); });
    connect(setH125Button, &QPushButton::clicked, this, [this]() { applyHydrogenRoleToSelected(DftHydrogenRole::BottomPseudoHIIITerminated125); });
    connect(setOrdinaryButton, &QPushButton::clicked, this, [this]() { applyHydrogenRoleToSelected(DftHydrogenRole::OrdinaryHydrogen); });
    connect(setBottomButton, &QPushButton::clicked, this, [this]() { setBottomHydrogenRole(DftHydrogenRole::BottomPseudoHNTerminated075); });

    auto* speciesPage = new QWidget(this);
    auto* speciesLayout = new QVBoxLayout(speciesPage);
    speciesLayout->addWidget(new QLabel(QStringLiteral("SIESTA species / QE pseudopotential mapping"), speciesPage));
    m_speciesTable = new QTableWidget(speciesPage);
    m_speciesTable->setObjectName(QStringLiteral("speciesTable"));
    m_speciesTable->setColumnCount(5);
    m_speciesTable->setHorizontalHeaderLabels({"label/index", "element", "mass/Z", "pseudo/species", "role"});
    m_speciesTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    speciesLayout->addWidget(m_speciesTable, 1);
    auto* resetQeSpeciesButton = new QPushButton(QStringLiteral("QE: Reset to project default masses/pseudos"), speciesPage);
    resetQeSpeciesButton->setObjectName(QStringLiteral("resetQeSpeciesProjectDefaultButton"));
    resetQeSpeciesButton->setToolTip(QStringLiteral("QE ATOMIC_SPECIESをプロジェクト既定へ戻します。massはH/N/Al/Ga/Inの実質量既定に戻り、sourceはproject_profileになります。"));
    connect(resetQeSpeciesButton, &QPushButton::clicked, this, [this]() {
        collectUiToSettings();
        if (m_settings.code != DftCode::QuantumEspresso) return;
        DftParameterRegistry::resetQeSpeciesToProjectDefaults(&m_settings);
        refreshSpeciesTable();
        updatePreview();
    });
    speciesLayout->addWidget(resetQeSpeciesButton);
    m_tabs->addTab(speciesPage, QStringLiteral("Species / Pseudopotentials"));

    auto* constraintsPage = new QWidget(this);
    auto* constraintsLayout = new QVBoxLayout(constraintsPage);
    auto* constraintsNote = new QLabel(QStringLiteral("Fixed atom modeでSIESTA Geometry.Constraints / QE fixed flagsの作り方を選びます。pseudo-Hや底面分子層は視覚的にfixed_reason列へ表示されます。"), constraintsPage);
    constraintsNote->setWordWrap(true);
    constraintsLayout->addWidget(constraintsNote);
    auto* constraintsForm = new QFormLayout();
    m_fixedAtomModeCombo = new QComboBox(constraintsPage);
    m_fixedAtomModeCombo->setObjectName(QStringLiteral("fixedAtomModeCombo"));
    m_fixedAtomModeCombo->setToolTip(QStringLiteral("Fixed atom mode: imported/manual flags保持、bottom pseudo-Hのみ固定、bottom pseudo-H+bottom molecular layer固定、manual onlyを切替えます。Kangawa GaN slabではbottom pseudo-H+bottom molecular layerが推奨既定です。"));
    m_fixedAtomModeCombo->addItem(QStringLiteral("Preserve imported flags"), dftFixedAtomModeKey(DftFixedAtomMode::PreserveImportedFlags));
    m_fixedAtomModeCombo->addItem(QStringLiteral("Fix bottom pseudo-H only"), dftFixedAtomModeKey(DftFixedAtomMode::FixBottomPseudoHOnly));
    m_fixedAtomModeCombo->addItem(QStringLiteral("Fix bottom pseudo-H + bottom molecular layer"), dftFixedAtomModeKey(DftFixedAtomMode::FixBottomPseudoHPlusBottomMolecularLayer));
    m_fixedAtomModeCombo->addItem(QStringLiteral("Manual only"), dftFixedAtomModeKey(DftFixedAtomMode::ManualOnly));
    m_trailingFlagInterpretationCombo = new QComboBox(constraintsPage);
    m_trailingFlagInterpretationCombo->setObjectName(QStringLiteral("trailingFlagInterpretationCombo"));
    m_trailingFlagInterpretationCombo->setToolTip(QStringLiteral("VASP/POSCAR行末フラグ解釈: 数値1 1 1は既定では曖昧としてimported_extra_columnsに保持し、固定とはみなしません。"));
    m_trailingFlagInterpretationCombo->addItem(QStringLiteral("Preserve/ignore ambiguous trailing flags"), dftTrailingFlagInterpretationKey(DftTrailingFlagInterpretation::PreserveOrIgnoreUnknown));
    m_trailingFlagInterpretationCombo->addItem(QStringLiteral("Ignore trailing flags"), dftTrailingFlagInterpretationKey(DftTrailingFlagInterpretation::IgnoreTrailingFlags));
    m_trailingFlagInterpretationCombo->addItem(QStringLiteral("Numeric 1 1 1 means fixed"), dftTrailingFlagInterpretationKey(DftTrailingFlagInterpretation::NumericOneMeansFixed));
    m_trailingFlagInterpretationCombo->addItem(QStringLiteral("Numeric 1 1 1 means movable"), dftTrailingFlagInterpretationKey(DftTrailingFlagInterpretation::NumericOneMeansMovable));
    m_trailingFlagInterpretationCombo->addItem(QStringLiteral("VASP T/F selective dynamics"), dftTrailingFlagInterpretationKey(DftTrailingFlagInterpretation::VaspSelectiveDynamics));
    m_trailingFlagInterpretationCombo->addItem(QStringLiteral("Custom mapping (preserve only)"), dftTrailingFlagInterpretationKey(DftTrailingFlagInterpretation::CustomMapping));
    constraintsForm->addRow(QStringLiteral("Fixed atom mode"), m_fixedAtomModeCombo);
    constraintsForm->addRow(QStringLiteral("Trailing flag interpretation"), m_trailingFlagInterpretationCombo);
    constraintsLayout->addLayout(constraintsForm);
    constraintsLayout->addWidget(new QLabel(QStringLiteral("Structureタブのfixed_reason列で、底面pseudo-H / bottom molecular layer / imported flag / manualの理由を確認できます。"), constraintsPage));
    constraintsLayout->addStretch(1);
    m_tabs->addTab(constraintsPage, QStringLiteral("Constraints"));

    auto* codePage = new QWidget(this);
    auto* codeLayout = new QVBoxLayout(codePage);
    auto* codeForm = new QFormLayout();
    m_codeCombo = new QComboBox(codePage);
    m_codeCombo->setObjectName(QStringLiteral("codeCombo"));
    m_codeCombo->addItem(QStringLiteral("SIESTA"));
    m_codeCombo->addItem(QStringLiteral("Quantum ESPRESSO pw.x"));
    m_versionCombo = new QComboBox(codePage);
    m_versionCombo->setObjectName(QStringLiteral("versionCombo"));
    m_profileCombo = new QComboBox(codePage);
    m_profileCombo->setObjectName(QStringLiteral("profileCombo"));
    m_generationModeCombo = new QComboBox(codePage);
    m_generationModeCombo->setObjectName(QStringLiteral("generationModeCombo"));
    m_generationModeCombo->addItem(QStringLiteral("Manual"), static_cast<int>(DftGenerationMode::Manual));
    m_generationModeCombo->addItem(QStringLiteral("Profile"), static_cast<int>(DftGenerationMode::Profile));
    m_generationModeCombo->addItem(QStringLiteral("Import-Edit"), static_cast<int>(DftGenerationMode::ImportEdit));
    m_targetEdit = new QLineEdit(codePage);
    m_targetEdit->setObjectName(QStringLiteral("targetEdit"));
    m_xcFdfEdit = new QLineEdit(codePage);
    m_pseudoDirEdit = new QLineEdit(codePage);
    m_includeXcCheck = new QCheckBox(QStringLiteral("SIESTA: %include xc.fdf"), codePage);
    m_standaloneCheck = new QCheckBox(QStringLiteral("SIESTA: standalone inline blocks"), codePage);
    m_allowUnknownHydrogenCheck = new QCheckBox(QStringLiteral("Allow unknown_hydrogen"), codePage);
    m_assumeIsolatedCheck = new QCheckBox(QStringLiteral("QE: assume_isolated = '2D'"), codePage);
    m_projectStyleFlagsCheck = new QCheckBox(QStringLiteral("QE: omit 1 1 1 for fully movable atoms"), codePage);
    m_assumeIsolatedCheck->setObjectName(QStringLiteral("assumeIsolatedCheck"));
    m_projectStyleFlagsCheck->setObjectName(QStringLiteral("projectStyleFixedFlagsCheck"));
    m_assumeIsolatedCheck->setToolTip(QStringLiteral("assume_isolated: QE charged slabで2D Coulomb補正を明示するときだけ出力します。OFFでは.inから除外します。"));
    m_projectStyleFlagsCheck->setToolTip(QStringLiteral("QE ATOMIC_POSITIONSのproject style: fully movable原子の1 1 1 flagsを省略します。固定原子は0 0 0です。"));
    m_allowUnknownHydrogenCheck->setToolTip(QStringLiteral("HydrogenRoleがunknown_hydrogenのHを許可する場合のみONにしてください。通常は手動でH-0.750/H-1.250/ordinaryへ修正します。"));
    auto* importButton = new QPushButton(QStringLiteral("Import existing input/profile..."), codePage);
    auto* browseXcButton = new QPushButton(QStringLiteral("Browse xc.fdf"), codePage);
    auto* browsePseudoButton = new QPushButton(QStringLiteral("Browse pseudo dir"), codePage);
    auto* xcRow = new QWidget(codePage);
    auto* xcRowLayout = new QHBoxLayout(xcRow);
    xcRowLayout->setContentsMargins(0, 0, 0, 0);
    xcRowLayout->addWidget(m_xcFdfEdit, 1);
    xcRowLayout->addWidget(browseXcButton);
    auto* pseudoRow = new QWidget(codePage);
    auto* pseudoRowLayout = new QHBoxLayout(pseudoRow);
    pseudoRowLayout->setContentsMargins(0, 0, 0, 0);
    pseudoRowLayout->addWidget(m_pseudoDirEdit, 1);
    pseudoRowLayout->addWidget(browsePseudoButton);
    codeForm->addRow(QStringLiteral("Code"), m_codeCombo);
    codeForm->addRow(QStringLiteral("Version"), m_versionCombo);
    codeForm->addRow(QStringLiteral("Profile"), m_profileCombo);
    codeForm->addRow(QStringLiteral("Generation mode"), m_generationModeCombo);
    codeForm->addRow(QStringLiteral("Target name"), m_targetEdit);
    codeForm->addRow(QStringLiteral("xc.fdf path"), xcRow);
    codeForm->addRow(QStringLiteral("QE pseudo_dir"), pseudoRow);
    codeLayout->addLayout(codeForm);
    codeLayout->addWidget(m_includeXcCheck);
    codeLayout->addWidget(m_standaloneCheck);
    codeLayout->addWidget(m_allowUnknownHydrogenCheck);
    codeLayout->addWidget(m_assumeIsolatedCheck);
    codeLayout->addWidget(m_projectStyleFlagsCheck);
    codeLayout->addWidget(importButton);
    codeLayout->addStretch(1);
    m_tabs->addTab(codePage, QStringLiteral("Code / Version / Profile"));

    auto* calcPage = new QWidget(this);
    auto* calcLayout = new QVBoxLayout(calcPage);
    auto* calcNote = new QLabel(QStringLiteral("計算モードを選ぶと電荷・スピン・緩和・k点の既定値を更新します。必要に応じてParametersで上書きしてください。"), calcPage);
    calcNote->setWordWrap(true);
    m_calculationModeCombo = new QComboBox(calcPage);
    calcLayout->addWidget(calcNote);
    calcLayout->addWidget(m_calculationModeCombo);
    calcLayout->addStretch(1);
    m_tabs->addTab(calcPage, QStringLiteral("Calculation Mode"));

    auto* parameterPage = new QWidget(this);
    auto* parameterLayout = new QVBoxLayout(parameterPage);
    auto* parameterNote = new QLabel(QStringLiteral("Known parameters. Source badgeとtooltipで値の由来を確認できます。"), parameterPage);
    parameterNote->setWordWrap(true);
    parameterLayout->addWidget(parameterNote);
    m_parameterTable = new QTableWidget(parameterPage);
    m_parameterTable->setObjectName(QStringLiteral("parameterTable"));
    m_parameterTable->setColumnCount(7);
    m_parameterTable->setHorizontalHeaderLabels({"on", "id", "label", "section", "value", "source", "unit"});
    m_parameterTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    parameterLayout->addWidget(m_parameterTable, 1);
    m_tabs->addTab(parameterPage, QStringLiteral("Parameters"));

    auto* rawPage = new QWidget(this);
    auto* rawLayout = new QVBoxLayout(rawPage);
    auto* rawNote = new QLabel(QStringLiteral("Raw Additional Parameters. Registry未登録のFDF label/block、QE namelist/cardを保持します。"), rawPage);
    rawNote->setWordWrap(true);
    rawLayout->addWidget(rawNote);
    auto* rawButtons = new QHBoxLayout();
    auto* addRawButton = new QPushButton(QStringLiteral("Add raw"), rawPage);
    auto* removeRawButton = new QPushButton(QStringLiteral("Remove selected"), rawPage);
    rawButtons->addWidget(addRawButton);
    rawButtons->addWidget(removeRawButton);
    rawButtons->addStretch(1);
    rawLayout->addLayout(rawButtons);
    m_rawTable = new QTableWidget(rawPage);
    m_rawTable->setColumnCount(7);
    m_rawTable->setHorizontalHeaderLabels({"on", "code", "namelist/block", "key", "value", "unit", "block/card"});
    m_rawTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    rawLayout->addWidget(m_rawTable, 1);
    m_tabs->addTab(rawPage, QStringLiteral("Raw Additional Parameters"));

    auto* previewPage = new QWidget(this);
    auto* previewLayout = new QVBoxLayout(previewPage);
    m_warningEdit = new QTextEdit(previewPage);
    m_warningEdit->setReadOnly(true);
    m_warningEdit->setMaximumHeight(150);
    m_previewEdit = new QTextEdit(previewPage);
    m_previewEdit->setReadOnly(true);
    m_previewEdit->setLineWrapMode(QTextEdit::NoWrap);
    auto* previewButtons = new QHBoxLayout();
    auto* copyButton = new QPushButton(QStringLiteral("Copy primary input"), previewPage);
    auto* exportButton = new QPushButton(QStringLiteral("Export files..."), previewPage);
    previewButtons->addWidget(copyButton);
    previewButtons->addWidget(exportButton);
    previewButtons->addStretch(1);
    previewLayout->addWidget(m_warningEdit);
    previewLayout->addWidget(m_previewEdit, 1);
    previewLayout->addLayout(previewButtons);
    m_tabs->addTab(previewPage, QStringLiteral("Preview / Export"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(m_codeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { rebuildCodeDependentUi(); });
    connect(m_versionCombo, &QComboBox::currentTextChanged, this, [this](const QString&) { rebuildCodeDependentUi(); });
    connect(m_profileCombo, &QComboBox::currentTextChanged, this, [this](const QString& profile) {
        if (profile.isEmpty()) return;
        collectUiToSettings();
        if (profile == QStringLiteral("Manual")) {
            m_settings.profileName = profile;
            m_settings.generationMode = DftGenerationMode::Manual;
        } else {
            const QString source = m_profileCombo->currentData().toString();
            if (source.startsWith(QStringLiteral("file:"))) {
                const auto result = DftInputParser::parseFile(source.mid(5), m_settings);
                if (result.ok) {
                    m_settings = result.settings;
                    m_settings.generationMode = DftGenerationMode::Profile;
                    m_settings.sourceStructurePath = m_structure.sourcePath;
                }
            } else {
                QStringList messages;
                DftParameterRegistry::applyBuiltInProfile(profile, &m_settings, &messages);
                Q_UNUSED(messages);
            }
        }
        if (m_settings.hydrogenAssignments.isEmpty()) {
            m_settings.hydrogenAssignments = DftInputGenerator::inferHydrogenRoles(m_structure, m_settings);
        }
        syncControlsFromSettings();
        refreshStructureTab();
        refreshHydrogenTab();
        updatePreview();
    });
    connect(m_generationModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { updatePreview(); });
    connect(m_targetEdit, &QLineEdit::textChanged, this, [this]() { updatePreview(); });
    connect(m_xcFdfEdit, &QLineEdit::textChanged, this, [this]() { updatePreview(); });
    connect(m_pseudoDirEdit, &QLineEdit::textChanged, this, [this]() { updatePreview(); });
    connect(m_includeXcCheck, &QCheckBox::toggled, this, [this]() { updatePreview(); });
    connect(m_standaloneCheck, &QCheckBox::toggled, this, [this]() { updatePreview(); });
    connect(m_allowUnknownHydrogenCheck, &QCheckBox::toggled, this, [this]() { updatePreview(); });
    connect(m_assumeIsolatedCheck, &QCheckBox::toggled, this, [this]() { updatePreview(); });
    connect(m_projectStyleFlagsCheck, &QCheckBox::toggled, this, [this]() { updatePreview(); });
    connect(m_fixedAtomModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { updatePreview(); refreshStructureTab(); });
    connect(m_trailingFlagInterpretationCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { updatePreview(); refreshStructureTab(); });
    connect(importButton, &QPushButton::clicked, this, &DftInputGeneratorDialog::importSettingsFile);
    connect(browseXcButton, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("xc.fdfを選択"), QString(), QStringLiteral("FDF (*.fdf);;All files (*.*)"));
        if (!path.isEmpty()) m_xcFdfEdit->setText(path);
    });
    connect(browsePseudoButton, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("QE pseudo_dirを選択"), m_pseudoDirEdit->text());
        if (!path.isEmpty()) m_pseudoDirEdit->setText(path);
    });
    connect(m_calculationModeCombo, &QComboBox::currentTextChanged, this, [this](const QString& mode) {
        if (mode.isEmpty()) return;
        collectUiToSettings();
        DftParameterRegistry::applyCalculationModeDefaults(&m_settings, mode);
        refreshParameterTable();
        updatePreview();
    });
    connect(addRawButton, &QPushButton::clicked, this, [this]() {
        collectUiToSettings();
        DftRawParameter raw;
        raw.code = m_settings.code;
        raw.namelistOrBlock = m_settings.code == DftCode::QuantumEspresso ? QStringLiteral("SYSTEM") : QString();
        raw.key = m_settings.code == DftCode::QuantumEspresso ? QStringLiteral("custom_key") : QStringLiteral("CustomFdfLabel");
        raw.enabled = true;
        m_settings.rawParameters << raw;
        refreshRawTable();
        updatePreview();
    });
    connect(removeRawButton, &QPushButton::clicked, this, [this]() {
        collectUiToSettings();
        QList<int> rows;
        for (const auto* selected : m_rawTable->selectedItems()) if (!rows.contains(selected->row())) rows << selected->row();
        std::sort(rows.begin(), rows.end(), std::greater<int>());
        for (int row : rows) if (row >= 0 && row < m_settings.rawParameters.size()) m_settings.rawParameters.removeAt(row);
        refreshRawTable();
        updatePreview();
    });
    connect(copyButton, &QPushButton::clicked, this, [this]() {
        collectUiToSettings();
        m_generated = DftInputGenerator::generate(m_structure, m_settings);
        QApplication::clipboard()->setText(m_generated.primaryText);
    });
    connect(exportButton, &QPushButton::clicked, this, &DftInputGeneratorDialog::exportGeneratedFiles);

    syncControlsFromSettings();
    refreshStructureTab();
    refreshHydrogenTab();
    refreshSpeciesTable();
    refreshParameterTable();
    refreshRawTable();
}

namespace {

QStringList discoverProfileFiles(DftCode code, const QString& version) {
    const QStringList roots = {
        QStringLiteral(":/dft_profiles"),
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("assets/dft_profiles")),
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../assets/dft_profiles")),
        QDir(QDir::homePath()).filePath(QStringLiteral(".aseapp/dft_profiles")),
    };
    QStringList files;
    QSet<QString> seen;
    const DftSettings base = DftParameterRegistry::defaultSettings(code, version, QStringLiteral("profile_probe"));
    for (const QString& root : roots) {
        if (!QDir(root).exists()) continue;
        QDirIterator it(root, {QStringLiteral("*.json")}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            if (seen.contains(path)) continue;
            const auto parsed = DftInputParser::parseFile(path, base);
            if (!parsed.ok || parsed.settings.code != code) continue;
            if (!version.isEmpty() && !parsed.settings.version.isEmpty() && parsed.settings.version != version) continue;
            seen.insert(path);
            files << path;
        }
    }
    return files;
}

QString profileLabelFromPath(const QString& path) {
    QString label = QFileInfo(path).completeBaseName();
    if (label.endsWith(QStringLiteral(".profile"))) label.chop(QStringLiteral(".profile").size());
    return label;
}

QString roleText(DftHydrogenRole role) {
    return dftHydrogenRoleKey(role);
}

DftHydrogenRole roleFromCombo(const QComboBox* combo) {
    return combo == nullptr ? DftHydrogenRole::UnknownHydrogen : static_cast<DftHydrogenRole>(combo->currentData().toInt());
}

void fillRoleCombo(QComboBox* combo, DftHydrogenRole current) {
    const QVector<DftHydrogenRole> roles = {
        DftHydrogenRole::OrdinaryHydrogen,
        DftHydrogenRole::BottomPseudoHNTerminated075,
        DftHydrogenRole::BottomPseudoHIIITerminated125,
        DftHydrogenRole::SurfaceAdsorbedHydrogen,
        DftHydrogenRole::MoleculeH2Hydrogen,
        DftHydrogenRole::UnknownHydrogen,
    };
    for (DftHydrogenRole role : roles) combo->addItem(roleText(role), static_cast<int>(role));
    const int index = combo->findData(static_cast<int>(current));
    combo->setCurrentIndex(index >= 0 ? index : combo->count() - 1);
}

DftGenerationMode modeFromCombo(const QComboBox* combo) {
    return combo == nullptr ? DftGenerationMode::Manual : static_cast<DftGenerationMode>(combo->currentData().toInt());
}

QString sourceText(DftParameterSource source) {
    return dftParameterSourceKey(source);
}

bool atomAllMovable(const NativeAtom& atom) {
    return atom.movable[0] && atom.movable[1] && atom.movable[2];
}

bool atomAllFixed(const NativeAtom& atom) {
    return !atom.movable[0] && !atom.movable[1] && !atom.movable[2];
}

QString movableText(const NativeAtom& atom) {
    return QStringLiteral("%1 %2 %3").arg(atom.movable[0] ? 1 : 0).arg(atom.movable[1] ? 1 : 0).arg(atom.movable[2] ? 1 : 0);
}

const DftHydrogenAssignment* assignmentForAtom(const QVector<DftHydrogenAssignment>& assignments, int atomIndex) {
    for (const auto& assignment : assignments) if (assignment.atomIndex == atomIndex) return &assignment;
    return nullptr;
}

QString speciesForAtom(const NativeAtom& atom, const DftHydrogenAssignment* h, DftCode code) {
    if (code == DftCode::Siesta) {
        if (atom.element.compare(QStringLiteral("Ga"), Qt::CaseInsensitive) == 0) return QStringLiteral("Ga / 3");
        if (atom.element.compare(QStringLiteral("N"), Qt::CaseInsensitive) == 0) return QStringLiteral("N / 2");
        if (atom.element.compare(QStringLiteral("H"), Qt::CaseInsensitive) == 0 && h != nullptr) {
            return QStringLiteral("%1 / %2").arg(h->siestaSpecies).arg(h->siestaSpeciesIndex);
        }
        return QStringLiteral("unmapped");
    }
    if (atom.element.compare(QStringLiteral("H"), Qt::CaseInsensitive) == 0 && h != nullptr) {
        return QStringLiteral("%1 / %2").arg(h->qeLabel, h->qePseudoFile);
    }
    if (atom.element.compare(QStringLiteral("Ga"), Qt::CaseInsensitive) == 0) return QStringLiteral("Ga / Ga.pbe-mt_fhi.UPF");
    if (atom.element.compare(QStringLiteral("N"), Qt::CaseInsensitive) == 0) return QStringLiteral("N / N.pbe-mt_fhi.UPF");
    return atom.element + QStringLiteral(" / ") + atom.element + QStringLiteral(".UPF");
}

} // namespace

void DftInputGeneratorDialog::rebuildCodeDependentUi() {
    if (m_codeCombo == nullptr || m_versionCombo == nullptr) return;
    const DftCode code = dftCodeFromIndex(m_codeCombo->currentIndex());
    QString version = m_versionCombo->currentText();
    if (version.isEmpty()) version = DftParameterRegistry::versionsForCode(code).value(0);
    QString target = m_targetEdit != nullptr ? m_targetEdit->text() : m_settings.targetName;
    if (target.trimmed().isEmpty()) target = m_settings.targetName;
    const auto hydrogen = m_settings.hydrogenAssignments;
    const bool allowUnknown = m_allowUnknownHydrogenCheck != nullptr ? m_allowUnknownHydrogenCheck->isChecked() : m_settings.allowUnknownHydrogen;
    const bool assumeIsolated = m_assumeIsolatedCheck != nullptr ? m_assumeIsolatedCheck->isChecked() : m_settings.qeAssumeIsolated;
    const bool projectStyle = m_projectStyleFlagsCheck != nullptr ? m_projectStyleFlagsCheck->isChecked() : m_settings.qeProjectStyleFixedFlags;
    const DftFixedAtomMode fixedMode = m_fixedAtomModeCombo != nullptr
        ? dftFixedAtomModeFromKey(m_fixedAtomModeCombo->currentData().toString())
        : m_settings.fixedAtomMode;
    const DftTrailingFlagInterpretation trailingInterpretation = m_trailingFlagInterpretationCombo != nullptr
        ? dftTrailingFlagInterpretationFromKey(m_trailingFlagInterpretationCombo->currentData().toString())
        : m_settings.trailingFlagInterpretation;
    m_settings = DftParameterRegistry::defaultSettings(code, version, DftInputGenerator::sanitizeTargetName(target));
    m_settings.sourceStructurePath = m_structure.sourcePath;
    m_settings.allowUnknownHydrogen = allowUnknown;
    m_settings.qeAssumeIsolated = assumeIsolated;
    m_settings.qeProjectStyleFixedFlags = projectStyle;
    m_settings.fixedAtomMode = fixedMode;
    m_settings.trailingFlagInterpretation = trailingInterpretation;
    m_settings.hydrogenAssignments = hydrogen.isEmpty() ? DftInputGenerator::inferHydrogenRoles(m_structure, m_settings) : hydrogen;
    syncControlsFromSettings();
    refreshStructureTab();
    refreshHydrogenTab();
    updatePreview();
}

void DftInputGeneratorDialog::syncControlsFromSettings() {
    if (m_codeCombo == nullptr) return;
    const QSignalBlocker b0(m_codeCombo);
    const QSignalBlocker b1(m_versionCombo);
    const QSignalBlocker b2(m_profileCombo);
    const QSignalBlocker b3(m_generationModeCombo);
    const QSignalBlocker b4(m_calculationModeCombo);
    const QSignalBlocker b5(m_fixedAtomModeCombo);
    const QSignalBlocker b6(m_trailingFlagInterpretationCombo);
    m_codeCombo->setCurrentIndex(m_settings.code == DftCode::QuantumEspresso ? 1 : 0);
    m_versionCombo->clear();
    m_versionCombo->addItems(DftParameterRegistry::versionsForCode(m_settings.code));
    if (m_versionCombo->findText(m_settings.version) < 0) m_versionCombo->addItem(m_settings.version);
    m_versionCombo->setCurrentText(m_settings.version);
    m_profileCombo->clear();
    for (const QString& profile : DftParameterRegistry::builtInProfiles(m_settings.code, m_settings.version)) {
        m_profileCombo->addItem(profile, QStringLiteral("builtin:") + profile);
    }
    for (const QString& path : discoverProfileFiles(m_settings.code, m_settings.version)) {
        const QString label = profileLabelFromPath(path);
        if (m_profileCombo->findText(label) < 0) m_profileCombo->addItem(label, QStringLiteral("file:") + path);
    }
    if (m_profileCombo->findText(m_settings.profileName) < 0) m_profileCombo->addItem(m_settings.profileName);
    m_profileCombo->setCurrentText(m_settings.profileName);
    int modeIndex = m_generationModeCombo->findData(static_cast<int>(m_settings.generationMode));
    m_generationModeCombo->setCurrentIndex(modeIndex >= 0 ? modeIndex : 0);
    m_calculationModeCombo->clear();
    m_calculationModeCombo->addItems(DftParameterRegistry::calculationModes(m_settings.code));
    if (m_calculationModeCombo->findText(m_settings.calculationMode) < 0) m_calculationModeCombo->addItem(m_settings.calculationMode);
    m_calculationModeCombo->setCurrentText(m_settings.calculationMode);
    if (m_targetEdit) m_targetEdit->setText(m_settings.targetName);
    if (m_xcFdfEdit) m_xcFdfEdit->setText(m_settings.xcFdfPath);
    if (m_pseudoDirEdit) m_pseudoDirEdit->setText(m_settings.pseudoDir);
    if (m_includeXcCheck) m_includeXcCheck->setChecked(m_settings.includeXcFdf);
    if (m_standaloneCheck) m_standaloneCheck->setChecked(m_settings.standaloneInline);
    if (m_allowUnknownHydrogenCheck) m_allowUnknownHydrogenCheck->setChecked(m_settings.allowUnknownHydrogen);
    if (m_assumeIsolatedCheck) m_assumeIsolatedCheck->setChecked(m_settings.qeAssumeIsolated);
    if (m_projectStyleFlagsCheck) m_projectStyleFlagsCheck->setChecked(m_settings.qeProjectStyleFixedFlags);
    if (m_fixedAtomModeCombo) {
        const int fixedIndex = m_fixedAtomModeCombo->findData(dftFixedAtomModeKey(m_settings.fixedAtomMode));
        m_fixedAtomModeCombo->setCurrentIndex(fixedIndex >= 0 ? fixedIndex : 0);
    }
    if (m_trailingFlagInterpretationCombo) {
        const int trailingIndex = m_trailingFlagInterpretationCombo->findData(dftTrailingFlagInterpretationKey(m_settings.trailingFlagInterpretation));
        m_trailingFlagInterpretationCombo->setCurrentIndex(trailingIndex >= 0 ? trailingIndex : 0);
    }
    refreshSpeciesTable();
    refreshParameterTable();
    refreshRawTable();
}

void DftInputGeneratorDialog::refreshStructureTab() {
    if (m_structureTable == nullptr) return;
    const QSignalBlocker blocker(m_structureTable);
    std::map<QString, int> counts;
    double zMin = 0.0;
    double zMax = 0.0;
    bool first = true;
    for (const auto& atom : m_structure.atoms) {
        counts[atom.element]++;
        if (first) {
            zMin = zMax = atom.cartesian.z();
            first = false;
        } else {
            zMin = std::min(zMin, static_cast<double>(atom.cartesian.z()));
            zMax = std::max(zMax, static_cast<double>(atom.cartesian.z()));
        }
    }
    QStringList composition;
    for (const auto& kv : counts) composition << QStringLiteral("%1:%2").arg(kv.first).arg(kv.second);
    const double cLength = m_structure.cellVectors[2].length();
    const double vacuumEstimate = std::max(0.0, cLength - (zMax - zMin));
    const DftGeneratedInput preview = DftInputGenerator::generate(m_structure, m_settings);
    QMap<int, QString> fixedReasonByAtom;
    QMap<int, QString> fixedFlagsByAtom;
    const QJsonArray fixedAtoms = preview.summaryObject.value(QStringLiteral("fixed_atoms")).toArray();
    for (const auto& value : fixedAtoms) {
        const QJsonObject obj = value.toObject();
        const int atomIndex = obj.value(QStringLiteral("atom_index")).toInt();
        const QJsonArray flags = obj.value(QStringLiteral("fixed")).toArray();
        QStringList flagText;
        for (int i = 0; i < 3; ++i) flagText << (i < flags.size() && flags.at(i).toBool(false) ? QStringLiteral("0") : QStringLiteral("1"));
        fixedReasonByAtom.insert(atomIndex, QStringLiteral("%1 [%2]").arg(obj.value(QStringLiteral("reason")).toString(), obj.value(QStringLiteral("source")).toString()));
        fixedFlagsByAtom.insert(atomIndex, flagText.join(QLatin1Char(' ')));
    }
    if (auto* summary = findChild<QLabel*>(QStringLiteral("structureSummaryLabel"))) {
        summary->setText(QStringLiteral("Atoms: %1 (%2) | fixed=%3 | mode=%4 | c=%5 Å | z-range=%6 Å | vacuum estimate=%7 Å | source=%8")
            .arg(m_structure.atoms.size())
            .arg(composition.join(QStringLiteral(", ")))
            .arg(fixedAtoms.size())
            .arg(dftFixedAtomModeKey(m_settings.fixedAtomMode))
            .arg(cLength, 0, 'f', 3)
            .arg(zMax - zMin, 0, 'f', 3)
            .arg(vacuumEstimate, 0, 'f', 3)
            .arg(m_structure.sourcePath.isEmpty() ? QStringLiteral("current document") : m_structure.sourcePath));
    }
    m_structureTable->setRowCount(static_cast<int>(m_structure.atoms.size()));
    for (int row = 0; row < static_cast<int>(m_structure.atoms.size()); ++row) {
        const auto& atom = m_structure.atoms[static_cast<std::size_t>(row)];
        const auto* h = assignmentForAtom(m_settings.hydrogenAssignments, row);
        m_structureTable->setItem(row, 0, item(QString::number(row + 1)));
        m_structureTable->setItem(row, 1, item(atom.element));
        m_structureTable->setItem(row, 2, item(QString::number(atom.cartesian.x(), 'f', 8)));
        m_structureTable->setItem(row, 3, item(QString::number(atom.cartesian.y(), 'f', 8)));
        m_structureTable->setItem(row, 4, item(QString::number(atom.cartesian.z(), 'f', 8)));
        m_structureTable->setItem(row, 5, item(QString::number(atom.fractional.x(), 'f', 8)));
        m_structureTable->setItem(row, 6, item(QString::number(atom.fractional.y(), 'f', 8)));
        m_structureTable->setItem(row, 7, item(QString::number(atom.fractional.z(), 'f', 8)));
        m_structureTable->setItem(row, 8, item(speciesForAtom(atom, h, m_settings.code)));
        m_structureTable->setItem(row, 9, item(h != nullptr ? roleText(h->selectedRole) : QStringLiteral("-")));
        m_structureTable->setItem(row, 10, item(movableText(atom)));
        m_structureTable->setItem(row, 11, item(fixedReasonByAtom.value(row + 1, QStringLiteral("-"))));
        QString warning = h != nullptr ? h->warning : QString();
        if (h != nullptr && h->fixedByRole && warning.isEmpty()) warning = QStringLiteral("pseudo-H fixed candidate");
        const bool bottomLayerCandidate = atom.element.compare(QStringLiteral("H"), Qt::CaseInsensitive) != 0 &&
            (atom.cartesian.z() - zMin) <= 1.6;
        if (bottomLayerCandidate && warning.isEmpty()) warning = QStringLiteral("bottom layer candidate");
        if (fixedFlagsByAtom.contains(row + 1)) {
            warning = warning.isEmpty()
                ? QStringLiteral("fixed flags %1").arg(fixedFlagsByAtom.value(row + 1))
                : warning + QStringLiteral("; fixed flags %1").arg(fixedFlagsByAtom.value(row + 1));
        }
        if (m_settings.code == DftCode::Siesta && !atomAllMovable(atom) && !atomAllFixed(atom)) warning = QStringLiteral("partial fixed -> atom constraint");
        m_structureTable->setItem(row, 12, item(warning));
    }
}

void DftInputGeneratorDialog::refreshHydrogenTab() {
    if (m_hydrogenTable == nullptr) return;
    const QSignalBlocker blocker(m_hydrogenTable);
    m_hydrogenTable->setRowCount(m_settings.hydrogenAssignments.size());
    for (int row = 0; row < m_settings.hydrogenAssignments.size(); ++row) {
        const auto& h = m_settings.hydrogenAssignments.at(row);
        m_hydrogenTable->setItem(row, 0, item(QString::number(h.atomIndex + 1)));
        m_hydrogenTable->item(row, 0)->setData(Qt::UserRole, h.atomIndex);
        m_hydrogenTable->setItem(row, 1, item(QStringLiteral("H")));
        m_hydrogenTable->setItem(row, 2, item(QString::number(h.cartesian.x(), 'f', 8)));
        m_hydrogenTable->setItem(row, 3, item(QString::number(h.cartesian.y(), 'f', 8)));
        m_hydrogenTable->setItem(row, 4, item(QString::number(h.cartesian.z(), 'f', 8)));
        m_hydrogenTable->setItem(row, 5, item(QString::number(h.fractional.z(), 'f', 8)));
        m_hydrogenTable->setItem(row, 6, item(h.nearestNonHAtomIndex >= 0 ? QStringLiteral("%1 %2").arg(h.nearestNonHAtomIndex + 1).arg(h.nearestNonHElement) : QStringLiteral("-")));
        m_hydrogenTable->setItem(row, 7, item(QString::number(h.nearestNonHDistanceAng, 'f', 4)));
        m_hydrogenTable->setItem(row, 8, item(roleText(h.inferredRole)));
        auto* combo = new QComboBox(m_hydrogenTable);
        combo->setObjectName(QStringLiteral("hydrogenRoleCombo_%1").arg(h.atomIndex + 1));
        combo->setToolTip(QStringLiteral("HydrogenRole: H-0.750/H-1.250/ordinary/surface/H2を手動選択します。H-0.750は底面N終端pseudo-H、H-1.250は底面III族終端pseudo-Hです。"));
        fillRoleCombo(combo, h.selectedRole);
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, row, combo]() {
            if (row < 0 || row >= m_settings.hydrogenAssignments.size()) return;
            auto& assignment = m_settings.hydrogenAssignments[row];
            assignment.selectedRole = roleFromCombo(combo);
            assignment.userOverrodeInference = assignment.selectedRole != assignment.inferredRole;
            refreshStructureTab();
            updatePreview();
        });
        m_hydrogenTable->setCellWidget(row, 9, combo);
        m_hydrogenTable->setItem(row, 10, item(h.confidence));
        m_hydrogenTable->setItem(row, 11, item(QStringLiteral("%1/%2").arg(h.siestaSpecies).arg(h.siestaSpeciesIndex)));
        m_hydrogenTable->setItem(row, 12, item(QStringLiteral("%1/%2").arg(h.qeLabel, h.qePseudoFile)));
    }
}

void DftInputGeneratorDialog::refreshSpeciesTable() {
    if (m_speciesTable == nullptr) return;
    const QSignalBlocker blocker(m_speciesTable);
    if (m_settings.code == DftCode::Siesta) {
        m_speciesTable->setColumnCount(5);
        m_speciesTable->setHorizontalHeaderLabels({"index", "element", "atomic number/Z", "label", "role"});
        m_speciesTable->setRowCount(m_settings.siestaSpecies.size());
        for (int row = 0; row < m_settings.siestaSpecies.size(); ++row) {
            const auto& sp = m_settings.siestaSpecies.at(row);
            m_speciesTable->setItem(row, 0, editableItem(QString::number(sp.index)));
            m_speciesTable->setItem(row, 1, editableItem(sp.element));
            m_speciesTable->setItem(row, 2, editableItem(QString::number(sp.atomicNumber)));
            m_speciesTable->setItem(row, 3, editableItem(sp.label));
            m_speciesTable->setItem(row, 4, editableItem(sp.role));
        }
    } else {
        m_speciesTable->setColumnCount(6);
        m_speciesTable->setHorizontalHeaderLabels({"label", "element", "mass", "pseudo_file", "role", "source"});
        m_speciesTable->setRowCount(m_settings.qeSpecies.size());
        for (int row = 0; row < m_settings.qeSpecies.size(); ++row) {
            const auto& sp = m_settings.qeSpecies.at(row);
            m_speciesTable->setItem(row, 0, editableItem(sp.label));
            m_speciesTable->setItem(row, 1, editableItem(sp.element));
            m_speciesTable->setItem(row, 2, editableItem(QString::number(sp.mass, 'g', 12)));
            m_speciesTable->setItem(row, 3, editableItem(sp.pseudoFile));
            m_speciesTable->setItem(row, 4, editableItem(sp.role));
            m_speciesTable->setItem(row, 5, item(sourceText(sp.source)));
        }
    }
}

void DftInputGeneratorDialog::refreshParameterTable() {
    if (m_parameterTable == nullptr) return;
    const QSignalBlocker blocker(m_parameterTable);
    QVector<DftParameterEntry> entries;
    for (auto it = m_settings.parameters.constBegin(); it != m_settings.parameters.constEnd(); ++it) entries << it.value();
    std::sort(entries.begin(), entries.end(), [](const DftParameterEntry& a, const DftParameterEntry& b) {
        if (a.spec.order == b.spec.order) return a.spec.id < b.spec.id;
        return a.spec.order < b.spec.order;
    });
    m_parameterTable->setRowCount(entries.size());
    for (int row = 0; row < entries.size(); ++row) {
        const auto& entry = entries.at(row);
        auto* enabled = new QCheckBox(m_parameterTable);
        enabled->setChecked(entry.enabled);
        connect(enabled, &QCheckBox::toggled, this, [this]() { updatePreview(); });
        m_parameterTable->setCellWidget(row, 0, enabled);
        m_parameterTable->setItem(row, 1, item(entry.spec.id));
        m_parameterTable->item(row, 1)->setData(Qt::UserRole, entry.spec.id);
        m_parameterTable->setItem(row, 2, item(entry.spec.label));
        m_parameterTable->setItem(row, 3, item(entry.spec.section));
        const QString tooltip = entry.spec.tooltipLong + QStringLiteral("\nSource: ") + sourceText(entry.source);
        if (!entry.spec.allowedValues.isEmpty()) {
            auto* value = new QComboBox(m_parameterTable);
            value->addItems(entry.spec.allowedValues);
            if (value->findText(entry.value) < 0) value->addItem(entry.value);
            value->setCurrentText(entry.value);
            value->setToolTip(tooltip);
            connect(value, &QComboBox::currentTextChanged, this, [this](const QString&) { updatePreview(); });
            m_parameterTable->setCellWidget(row, 4, value);
        } else {
            auto* value = new QLineEdit(entry.value, m_parameterTable);
            value->setToolTip(tooltip);
            connect(value, &QLineEdit::editingFinished, this, [this]() { updatePreview(); });
            m_parameterTable->setCellWidget(row, 4, value);
        }
        m_parameterTable->setItem(row, 5, item(sourceText(entry.source)));
        m_parameterTable->setItem(row, 6, item(entry.spec.unit));
        for (int col = 1; col < m_parameterTable->columnCount(); ++col) {
            if (auto* tableItem = m_parameterTable->item(row, col)) tableItem->setToolTip(entry.spec.tooltipLong);
        }
    }
}

void DftInputGeneratorDialog::refreshRawTable() {
    if (m_rawTable == nullptr) return;
    const QSignalBlocker blocker(m_rawTable);
    m_rawTable->setRowCount(m_settings.rawParameters.size());
    for (int row = 0; row < m_settings.rawParameters.size(); ++row) {
        const auto& raw = m_settings.rawParameters.at(row);
        auto* enabled = new QCheckBox(m_rawTable);
        enabled->setChecked(raw.enabled);
        m_rawTable->setCellWidget(row, 0, enabled);
        m_rawTable->setItem(row, 1, editableItem(dftCodeKey(raw.code)));
        m_rawTable->setItem(row, 2, editableItem(raw.namelistOrBlock));
        m_rawTable->setItem(row, 3, editableItem(raw.key));
        m_rawTable->setItem(row, 4, editableItem(raw.value));
        m_rawTable->setItem(row, 5, editableItem(raw.unit));
        auto* block = new QCheckBox(m_rawTable);
        block->setChecked(raw.blockOrCard);
        m_rawTable->setCellWidget(row, 6, block);
        connect(enabled, &QCheckBox::toggled, this, [this]() { updatePreview(); });
        connect(block, &QCheckBox::toggled, this, [this]() { updatePreview(); });
    }
}

void DftInputGeneratorDialog::collectUiToSettings() {
    if (m_codeCombo != nullptr) m_settings.code = dftCodeFromIndex(m_codeCombo->currentIndex());
    if (m_versionCombo != nullptr) m_settings.version = m_versionCombo->currentText();
    if (m_profileCombo != nullptr) m_settings.profileName = m_profileCombo->currentText();
    if (m_generationModeCombo != nullptr) m_settings.generationMode = modeFromCombo(m_generationModeCombo);
    if (m_calculationModeCombo != nullptr) m_settings.calculationMode = m_calculationModeCombo->currentText();
    if (m_targetEdit != nullptr) m_settings.targetName = DftInputGenerator::sanitizeTargetName(m_targetEdit->text());
    if (m_xcFdfEdit != nullptr) m_settings.xcFdfPath = m_xcFdfEdit->text().trimmed();
    if (m_pseudoDirEdit != nullptr) m_settings.pseudoDir = m_pseudoDirEdit->text().trimmed();
    if (m_includeXcCheck != nullptr) m_settings.includeXcFdf = m_includeXcCheck->isChecked();
    if (m_standaloneCheck != nullptr) m_settings.standaloneInline = m_standaloneCheck->isChecked();
    if (m_allowUnknownHydrogenCheck != nullptr) m_settings.allowUnknownHydrogen = m_allowUnknownHydrogenCheck->isChecked();
    if (m_assumeIsolatedCheck != nullptr) m_settings.qeAssumeIsolated = m_assumeIsolatedCheck->isChecked();
    if (m_projectStyleFlagsCheck != nullptr) m_settings.qeProjectStyleFixedFlags = m_projectStyleFlagsCheck->isChecked();
    if (m_fixedAtomModeCombo != nullptr) m_settings.fixedAtomMode = dftFixedAtomModeFromKey(m_fixedAtomModeCombo->currentData().toString());
    if (m_trailingFlagInterpretationCombo != nullptr) m_settings.trailingFlagInterpretation = dftTrailingFlagInterpretationFromKey(m_trailingFlagInterpretationCombo->currentData().toString());

    for (int row = 0; m_hydrogenTable != nullptr && row < m_hydrogenTable->rowCount() && row < m_settings.hydrogenAssignments.size(); ++row) {
        if (auto* combo = qobject_cast<QComboBox*>(m_hydrogenTable->cellWidget(row, 9))) {
            auto& h = m_settings.hydrogenAssignments[row];
            h.selectedRole = roleFromCombo(combo);
            h.userOverrodeInference = h.selectedRole != h.inferredRole;
        }
    }

    if (m_speciesTable != nullptr) {
        if (m_settings.code == DftCode::Siesta) {
            QVector<DftSiestaSpecies> species;
            for (int row = 0; row < m_speciesTable->rowCount(); ++row) {
                DftSiestaSpecies sp;
                sp.index = m_speciesTable->item(row, 0) != nullptr ? m_speciesTable->item(row, 0)->text().toInt() : 0;
                sp.element = m_speciesTable->item(row, 1) != nullptr ? m_speciesTable->item(row, 1)->text().trimmed() : QString();
                sp.atomicNumber = m_speciesTable->item(row, 2) != nullptr ? m_speciesTable->item(row, 2)->text().toInt() : 0;
                sp.label = m_speciesTable->item(row, 3) != nullptr ? m_speciesTable->item(row, 3)->text().trimmed() : QString();
                sp.role = m_speciesTable->item(row, 4) != nullptr ? m_speciesTable->item(row, 4)->text().trimmed() : QString();
                if (sp.index > 0 && !sp.label.isEmpty()) species << sp;
            }
            if (!species.isEmpty()) m_settings.siestaSpecies = species;
        } else {
            QVector<DftQeSpecies> species;
            for (int row = 0; row < m_speciesTable->rowCount(); ++row) {
                DftQeSpecies sp;
                sp.label = m_speciesTable->item(row, 0) != nullptr ? m_speciesTable->item(row, 0)->text().trimmed() : QString();
                sp.element = m_speciesTable->item(row, 1) != nullptr ? m_speciesTable->item(row, 1)->text().trimmed() : QString();
                sp.mass = m_speciesTable->item(row, 2) != nullptr ? m_speciesTable->item(row, 2)->text().toDouble() : 1.0;
                sp.pseudoFile = m_speciesTable->item(row, 3) != nullptr ? m_speciesTable->item(row, 3)->text().trimmed() : QString();
                sp.role = m_speciesTable->item(row, 4) != nullptr ? m_speciesTable->item(row, 4)->text().trimmed() : QString();
                sp.source = m_speciesTable->item(row, 5) != nullptr
                    ? dftParameterSourceFromKey(m_speciesTable->item(row, 5)->text())
                    : DftParameterSource::UserOverride;
                if (row >= m_settings.qeSpecies.size()) {
                    sp.source = DftParameterSource::UserOverride;
                } else {
                    const auto& before = m_settings.qeSpecies.at(row);
                    const bool changed = before.label != sp.label ||
                                         before.element != sp.element ||
                                         std::abs(before.mass - sp.mass) > 1.0e-10 ||
                                         before.pseudoFile != sp.pseudoFile ||
                                         before.role != sp.role;
                    if (changed) sp.source = DftParameterSource::UserOverride;
                }
                if (!sp.label.isEmpty()) species << sp;
            }
            if (!species.isEmpty()) m_settings.qeSpecies = species;
        }
    }

    if (m_parameterTable != nullptr) {
        for (int row = 0; row < m_parameterTable->rowCount(); ++row) {
            const auto* idItem = m_parameterTable->item(row, 1);
            if (idItem == nullptr) continue;
            const QString id = idItem->data(Qt::UserRole).toString().isEmpty() ? idItem->text() : idItem->data(Qt::UserRole).toString();
            auto it = m_settings.parameters.find(id);
            if (it == m_settings.parameters.end()) continue;
            if (auto* enabled = qobject_cast<QCheckBox*>(m_parameterTable->cellWidget(row, 0))) it->enabled = enabled->isChecked();
            if (auto* value = qobject_cast<QLineEdit*>(m_parameterTable->cellWidget(row, 4))) {
                if (it->value != value->text()) {
                    it->value = value->text();
                    it->source = DftParameterSource::UserOverride;
                    it->enabled = it->enabled || !it->value.trimmed().isEmpty();
                }
            } else if (auto* value = qobject_cast<QComboBox*>(m_parameterTable->cellWidget(row, 4))) {
                if (it->value != value->currentText()) {
                    it->value = value->currentText();
                    it->source = DftParameterSource::UserOverride;
                    it->enabled = it->enabled || !it->value.trimmed().isEmpty();
                }
            }
        }
    }

    if (m_rawTable != nullptr) {
        QVector<DftRawParameter> rawParameters;
        for (int row = 0; row < m_rawTable->rowCount(); ++row) {
            DftRawParameter raw;
            auto* enabled = qobject_cast<QCheckBox*>(m_rawTable->cellWidget(row, 0));
            auto* block = qobject_cast<QCheckBox*>(m_rawTable->cellWidget(row, 6));
            raw.enabled = enabled == nullptr || enabled->isChecked();
            const QString code = m_rawTable->item(row, 1) != nullptr ? m_rawTable->item(row, 1)->text().trimmed().toLower() : dftCodeKey(m_settings.code);
            raw.code = code == QStringLiteral("qe") || code.contains(QStringLiteral("espresso")) ? DftCode::QuantumEspresso : DftCode::Siesta;
            raw.namelistOrBlock = m_rawTable->item(row, 2) != nullptr ? m_rawTable->item(row, 2)->text().trimmed() : QString();
            raw.key = m_rawTable->item(row, 3) != nullptr ? m_rawTable->item(row, 3)->text().trimmed() : QString();
            raw.value = m_rawTable->item(row, 4) != nullptr ? m_rawTable->item(row, 4)->text() : QString();
            raw.unit = m_rawTable->item(row, 5) != nullptr ? m_rawTable->item(row, 5)->text().trimmed() : QString();
            raw.blockOrCard = block != nullptr && block->isChecked();
            if (!raw.key.isEmpty() || !raw.value.trimmed().isEmpty()) rawParameters << raw;
        }
        m_settings.rawParameters = rawParameters;
    }

    if (m_settings.code == DftCode::QuantumEspresso) {
        DftParameterRegistry::setParameterValue(&m_settings, QStringLiteral("qe.CONTROL.pseudo_dir"), m_settings.pseudoDir, DftParameterSource::UserOverride);
        DftParameterRegistry::setParameterValue(&m_settings, QStringLiteral("qe.CONTROL.outdir"), m_settings.outDirPattern, DftParameterSource::ProjectProfile);
        DftParameterRegistry::setParameterValue(&m_settings, QStringLiteral("qe.SYSTEM.assume_isolated"), m_settings.qeAssumeIsolated ? QStringLiteral("2D") : QString(), DftParameterSource::UserOverride);
    }
    if (m_settings.code == DftCode::Siesta) {
        DftParameterRegistry::setParameterValue(&m_settings, QStringLiteral("siesta.general.SystemName"), m_settings.targetName, DftParameterSource::UserOverride);
        DftParameterRegistry::setParameterValue(&m_settings, QStringLiteral("siesta.general.SystemLabel"), m_settings.targetName, DftParameterSource::UserOverride);
    }
}

void DftInputGeneratorDialog::updatePreview() {
    if (m_previewEdit == nullptr || m_warningEdit == nullptr) return;
    collectUiToSettings();
    m_generated = DftInputGenerator::generate(m_structure, m_settings);
    m_previewEdit->setPlainText(m_generated.primaryText);
    QStringList status;
    status << (m_generated.ok ? QStringLiteral("OK: primary input has no explanatory comments.") : QStringLiteral("ERROR: export is blocked until errors are fixed."));
    if (!m_generated.errors.isEmpty()) {
        status << QStringLiteral("Errors:");
        for (const auto& e : m_generated.errors) status << QStringLiteral("- %1").arg(e);
    }
    if (!m_generated.warnings.isEmpty()) {
        status << QStringLiteral("Warnings:");
        for (const auto& w : m_generated.warnings) status << QStringLiteral("- %1").arg(w);
    }
    if (!m_generated.requiredCompanionFiles.isEmpty()) status << QStringLiteral("Required companion files: %1").arg(m_generated.requiredCompanionFiles.join(QStringLiteral(", ")));
    m_warningEdit->setPlainText(status.join('\n'));
}

void DftInputGeneratorDialog::importSettingsFile() {
    collectUiToSettings();
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("DFT input/profile import"), QString(), QStringLiteral("DFT/Profile files (*.fdf *.in *.log *.json *.yaml *.yml);;All files (*.*)"));
    if (path.isEmpty()) return;
    const auto result = DftInputParser::parseFile(path, m_settings);
    if (!result.ok) {
        QMessageBox::warning(this, QStringLiteral("Import"), result.messages.isEmpty() ? QStringLiteral("Importできませんでした。") : result.messages.join('\n'));
        return;
    }
    m_settings = result.settings;
    m_settings.generationMode = DftGenerationMode::ImportEdit;
    m_settings.sourceStructurePath = m_structure.sourcePath;
    if (m_settings.targetName.trimmed().isEmpty()) m_settings.targetName = DftInputGenerator::sanitizeTargetName(QFileInfo(path).completeBaseName());
    if (m_settings.hydrogenAssignments.isEmpty()) m_settings.hydrogenAssignments = DftInputGenerator::inferHydrogenRoles(m_structure, m_settings);
    syncControlsFromSettings();
    refreshStructureTab();
    refreshHydrogenTab();
    updatePreview();
    QStringList lines;
    lines << QStringLiteral("Imported: %1").arg(path) << QStringLiteral("Kind: %1").arg(result.sourceKind);
    if (!result.diffLines.isEmpty()) lines << QStringLiteral("Diff:") << result.diffLines;
    if (!result.messages.isEmpty()) lines << QStringLiteral("Messages:") << result.messages;
    QMessageBox::information(this, QStringLiteral("Import"), lines.join('\n'));
}

void DftInputGeneratorDialog::exportGeneratedFiles() {
    collectUiToSettings();
    m_generated = DftInputGenerator::generate(m_structure, m_settings);
    if (!m_generated.ok) {
        QMessageBox::warning(this, QStringLiteral("DFT入力生成"), QStringLiteral("エラーがあるため出力しません。\n%1").arg(m_generated.errors.join('\n')));
        updatePreview();
        return;
    }
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("DFT入力ファイルの出力先"));
    if (dir.isEmpty()) return;
    QString error;
    if (!DftInputGenerator::writeGeneratedFiles(dir, m_settings, m_generated, &error)) {
        QMessageBox::critical(this, QStringLiteral("DFT入力生成"), error);
        return;
    }
    QMessageBox::information(this, QStringLiteral("DFT入力生成"), QStringLiteral("出力しました:\n%1").arg(dir));
}

void DftInputGeneratorDialog::applyHydrogenRoleToSelected(DftHydrogenRole role) {
    if (m_hydrogenTable == nullptr) return;
    QList<int> rows;
    for (const auto* tableItem : m_hydrogenTable->selectedItems()) if (!rows.contains(tableItem->row())) rows << tableItem->row();
    for (int row : rows) {
        if (row < 0 || row >= m_settings.hydrogenAssignments.size()) continue;
        m_settings.hydrogenAssignments[row].selectedRole = role;
        m_settings.hydrogenAssignments[row].userOverrodeInference = true;
    }
    refreshHydrogenTab();
    refreshStructureTab();
    updatePreview();
}

void DftInputGeneratorDialog::setBottomHydrogenRole(DftHydrogenRole role) {
    double zMin = 0.0;
    bool first = true;
    for (const auto& h : m_settings.hydrogenAssignments) {
        if (first) {
            zMin = h.cartesian.z();
            first = false;
        } else {
            zMin = std::min(zMin, static_cast<double>(h.cartesian.z()));
        }
    }
    for (auto& h : m_settings.hydrogenAssignments) {
        if (h.cartesian.z() - zMin <= 1.5) {
            h.selectedRole = role;
            h.userOverrodeInference = true;
        }
    }
    refreshHydrogenTab();
    refreshStructureTab();
    updatePreview();
}

void DftInputGeneratorDialog::setSelectedAtomsMovable(bool movable) {
    if (m_structureTable == nullptr) return;
    QSet<int> rows;
    for (const auto* tableItem : m_structureTable->selectedItems()) rows.insert(tableItem->row());
    for (int row : rows) {
        if (row < 0 || row >= static_cast<int>(m_structure.atoms.size())) continue;
        auto& atom = m_structure.atoms[static_cast<std::size_t>(row)];
        atom.movable = {movable, movable, movable};
    }
    refreshStructureTab();
    updatePreview();
}
