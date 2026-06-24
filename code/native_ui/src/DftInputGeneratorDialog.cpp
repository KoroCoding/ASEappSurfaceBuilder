#include "DftInputGeneratorDialog.h"

#include "DftInputGenerator.h"
#include "DftInputParser.h"
#include "DftParameterRegistry.h"
#include "StructureFileLoader.h"

#include <QApplication>
#include <QBrush>
#include <QCheckBox>
#include <QClipboard>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGuiApplication>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScrollArea>
#include <QScrollBar>
#include <QScreen>
#include <QSet>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QTableWidget>
#include <QTextEdit>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>
#include <cmath>
#include <functional>
#include <memory>
#include <map>
#include <utility>

namespace {

QTableWidgetItem* item(const QString& text) {
    auto* it = new QTableWidgetItem(text);
    it->setFlags(it->flags() & ~Qt::ItemIsEditable);
    return it;
}

QTableWidgetItem* editableItem(const QString& text) {
    return new QTableWidgetItem(text);
}

QStringList standardSiestaPsfFiles() {
    return {
        QStringLiteral("Ga.psf"),
        QStringLiteral("N.psf"),
        QStringLiteral("H.psf"),
        QStringLiteral("H-0.750.psf"),
        QStringLiteral("H-1.250.psf"),
    };
}

QString yesNo(bool value) { return value ? QStringLiteral("true") : QStringLiteral("false"); }

QString vectorText(const QVector3D& v) {
    return QStringLiteral("%1 %2 %3").arg(v.x(), 0, 'f', 6).arg(v.y(), 0, 'f', 6).arg(v.z(), 0, 'f', 6);
}

QString objectNameSuffix(QString text) {
    for (QChar& ch : text) {
        if (!ch.isLetterOrNumber()) ch = QLatin1Char('_');
    }
    return text;
}

StructureTrailingFlagInterpretation toStructureTrailingFlagInterpretation(DftTrailingFlagInterpretation interpretation) {
    switch (interpretation) {
    case DftTrailingFlagInterpretation::IgnoreTrailingFlags:
        return StructureTrailingFlagInterpretation::IgnoreTrailingFlags;
    case DftTrailingFlagInterpretation::NumericOneMeansFixed:
        return StructureTrailingFlagInterpretation::NumericOneMeansFixed;
    case DftTrailingFlagInterpretation::NumericOneMeansMovable:
        return StructureTrailingFlagInterpretation::NumericOneMeansMovable;
    case DftTrailingFlagInterpretation::VaspSelectiveDynamics:
        return StructureTrailingFlagInterpretation::VaspSelectiveDynamics;
    case DftTrailingFlagInterpretation::CustomMapping:
        return StructureTrailingFlagInterpretation::CustomMapping;
    case DftTrailingFlagInterpretation::PreserveOrIgnoreUnknown:
    default:
        return StructureTrailingFlagInterpretation::PreserveOrIgnoreUnknown;
    }
}

bool supportedStructureCandidate(const QFileInfo& info) {
    if (!info.isFile()) return false;
    const QString suffix = info.suffix().toLower();
    const QString fileName = info.fileName().toLower();
    return suffix == QStringLiteral("vasp") ||
           suffix == QStringLiteral("poscar") ||
           suffix == QStringLiteral("contcar") ||
           suffix == QStringLiteral("cif") ||
           suffix == QStringLiteral("xyz") ||
           suffix == QStringLiteral("out") ||
           fileName == QStringLiteral("poscar") ||
           fileName == QStringLiteral("contcar") ||
           fileName == QStringLiteral("final_structure.txt");
}

QString calculationTargetSuffix(const DftSettings& settings) {
    const QString mode = settings.calculationMode.trimmed();
    if (settings.code == DftCode::Siesta) {
        if (mode == QStringLiteral("charged_slab_electron_added")) return QStringLiteral("_qm0p25_profile");
        if (mode == QStringLiteral("charged_slab_electron_removed")) return QStringLiteral("_qp0p25_profile");
        if (mode == QStringLiteral("neutral_slab")) return QStringLiteral("_neutral_first_profile");
        if (mode == QStringLiteral("ga_atom_reference")) return QStringLiteral("_ga_atom_profile");
        if (mode == QStringLiteral("n2_reference")) return QStringLiteral("_n2_profile");
        if (mode == QStringLiteral("h2_reference")) return QStringLiteral("_h2_profile");
    }
    if (!mode.isEmpty() && mode != QStringLiteral("custom")) return QStringLiteral("_") + mode + QStringLiteral("_profile");
    return QStringLiteral("_custom_profile");
}

} // namespace

DftInputGeneratorDialog::DftInputGeneratorDialog(const StructureData& structure, QWidget* parent)
    : QDialog(parent), m_initialStructure(structure), m_structure(structure) {
    const QString baseTarget = structure.title.isEmpty() ? QStringLiteral("ideal") : structure.title;
    m_settings = DftParameterRegistry::defaultSettings(DftCode::Siesta, QStringLiteral("4.1.5"), DftInputGenerator::sanitizeTargetName(baseTarget));
    m_settings.sourceStructurePath = structure.sourcePath;
    m_settings.trailingFlagInterpretation = dftTrailingFlagInterpretationFromKey(structure.trailingFlagInterpretation);
    m_settings.hydrogenAssignments = DftInputGenerator::inferHydrogenRoles(structure, m_settings);
    setWindowTitle(QStringLiteral("DFT入力生成"));
    setMinimumSize(820, 540);
    QSize initialSize(1040, 720);
    if (const QScreen* screen = QGuiApplication::primaryScreen()) {
        const QRect available = screen->availableGeometry();
        initialSize.setWidth(std::max(minimumWidth(), std::min(initialSize.width(), available.width() - 80)));
        initialSize.setHeight(std::max(minimumHeight(), std::min(initialSize.height(), available.height() - 80)));
    }
    resize(initialSize);
    setSizeGripEnabled(true);
    buildUi();
    updatePreview();
}

void DftInputGeneratorDialog::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);
    m_tabs = new QTabWidget(this);
    root->addWidget(m_tabs, 1);

    auto* detailsPage = new QWidget(this);
    auto* detailsPageLayout = new QVBoxLayout(detailsPage);
    auto* detailsScroll = new QScrollArea(detailsPage);
    detailsScroll->setWidgetResizable(true);
    auto* detailsContent = new QWidget(detailsScroll);
    auto* detailsLayout = new QVBoxLayout(detailsContent);
    detailsLayout->setContentsMargins(8, 8, 8, 8);
    detailsLayout->setSpacing(10);
    detailsScroll->setWidget(detailsContent);
    detailsPageLayout->addWidget(detailsScroll, 1);

    auto* overviewPage = new QWidget(this);
    auto* overviewLayout = new QVBoxLayout(overviewPage);
    auto* overviewNote = new QLabel(QStringLiteral("Basic overview: target固有名、構造、電荷/スピン、固定原子、数値条件、Kempisty/Kangawa checklistを一画面で確認します。"), overviewPage);
    overviewNote->setWordWrap(true);
    overviewLayout->addWidget(overviewNote);
    m_overviewTable = new QTableWidget(overviewPage);
    m_overviewTable->setObjectName(QStringLiteral("dftOverviewTable"));
    m_overviewTable->setColumnCount(4);
    m_overviewTable->setHorizontalHeaderLabels({"section", "item", "value", "status"});
    m_overviewTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    overviewLayout->addWidget(m_overviewTable, 2);
    m_checklistEdit = new QTextEdit(overviewPage);
    m_checklistEdit->setObjectName(QStringLiteral("scientificChecklistText"));
    m_checklistEdit->setReadOnly(true);
    m_checklistEdit->setMaximumHeight(220);
    overviewLayout->addWidget(m_checklistEdit, 1);
    detailsLayout->addWidget(overviewPage);

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
    detailsLayout->addWidget(structurePage);
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
    detailsLayout->addWidget(hydrogenPage);
    connect(inferButton, &QPushButton::clicked, this, [this]() { m_settings.hydrogenAssignments = DftInputGenerator::inferHydrogenRoles(m_structure, m_settings); refreshHydrogenTab(); updatePreview(); });
    connect(setH075Button, &QPushButton::clicked, this, [this]() { applyHydrogenRoleToSelected(DftHydrogenRole::BottomPseudoHNTerminated075); });
    connect(setH125Button, &QPushButton::clicked, this, [this]() { applyHydrogenRoleToSelected(DftHydrogenRole::BottomPseudoHIIITerminated125); });
    connect(setOrdinaryButton, &QPushButton::clicked, this, [this]() { applyHydrogenRoleToSelected(DftHydrogenRole::OrdinaryHydrogen); });
    connect(setBottomButton, &QPushButton::clicked, this, [this]() { setBottomHydrogenRole(DftHydrogenRole::BottomPseudoHNTerminated075); });

    auto* speciesPage = new QWidget(this);
    auto* speciesLayout = new QVBoxLayout(speciesPage);
    auto* speciesNote = new QLabel(QStringLiteral("SIESTA species / QE pseudopotential mapping. SIESTAではPSF file列にスパコン側の実ファイル名（Ga.psf, N.psf, H.psf, H-0.750.psf, H-1.250.psf）を割り当てます。PSFは既定でFDF配置先から見て../potentialに置く想定です。"), speciesPage);
    speciesNote->setWordWrap(true);
    speciesLayout->addWidget(speciesNote);
    m_speciesTable = new QTableWidget(speciesPage);
    m_speciesTable->setObjectName(QStringLiteral("speciesTable"));
    m_speciesTable->setColumnCount(6);
    m_speciesTable->setHorizontalHeaderLabels({"label/index", "element", "mass/Z", "pseudo/species", "role", "PSF file"});
    m_speciesTable->setToolTip(QStringLiteral("SIESTA: species labelごとに対応するPSFファイル名を編集します。ファイル名はGa.psf等のままにし、PSF directoryで../potentialを指定します。"));
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
    detailsLayout->addWidget(speciesPage);
    detailsLayout->addStretch(1);

    auto* constraintsPage = new QWidget(this);
    auto* constraintsLayout = new QVBoxLayout(constraintsPage);
    auto* constraintsNote = new QLabel(QStringLiteral("固定原子: POSCAR/手動フラグ + 必要に応じて底面pseudo-H/底面分子層を追加固定します。"), constraintsPage);
    constraintsNote->setWordWrap(true);
    constraintsLayout->setContentsMargins(0, 0, 0, 0);
    constraintsLayout->setSpacing(4);
    constraintsNote->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    constraintsLayout->addWidget(constraintsNote);
    auto* constraintsForm = new QFormLayout();
    constraintsForm->setContentsMargins(0, 0, 0, 0);
    constraintsForm->setVerticalSpacing(4);
    m_fixedAtomModeCombo = new QComboBox(constraintsPage);
    m_fixedAtomModeCombo->setObjectName(QStringLiteral("fixedAtomModeCombo"));
    m_fixedAtomModeCombo->setToolTip(QStringLiteral("Fixed atom mode: POSCAR/手動のfixedフラグは常に反映し、その上でbottom pseudo-Hやbottom molecular layerを自動追加固定するかを選びます。Kangawa GaN slabではbottom pseudo-H+bottom molecular layerが推奨既定です。"));
    m_fixedAtomModeCombo->addItem(QStringLiteral("Imported/manual fixed flags only"), dftFixedAtomModeKey(DftFixedAtomMode::PreserveImportedFlags));
    m_fixedAtomModeCombo->addItem(QStringLiteral("Imported flags + bottom pseudo-H"), dftFixedAtomModeKey(DftFixedAtomMode::FixBottomPseudoHOnly));
    m_fixedAtomModeCombo->addItem(QStringLiteral("Imported flags + bottom pseudo-H + bottom layer"), dftFixedAtomModeKey(DftFixedAtomMode::FixBottomPseudoHPlusBottomMolecularLayer));
    m_fixedAtomModeCombo->addItem(QStringLiteral("Manual/import flags only"), dftFixedAtomModeKey(DftFixedAtomMode::ManualOnly));
    m_trailingFlagInterpretationCombo = new QComboBox(constraintsPage);
    m_trailingFlagInterpretationCombo->setObjectName(QStringLiteral("trailingFlagInterpretationCombo"));
    m_trailingFlagInterpretationCombo->setToolTip(QStringLiteral("VASP/POSCAR行末フラグ解釈: 既定ではT/Fと数値1 1 1を自動認識し、数値1はfixed、0はmovableとして扱い、行末列も保持します。"));
    m_trailingFlagInterpretationCombo->addItem(QStringLiteral("Auto-detect/preserve POSCAR flags"), dftTrailingFlagInterpretationKey(DftTrailingFlagInterpretation::PreserveOrIgnoreUnknown));
    m_trailingFlagInterpretationCombo->addItem(QStringLiteral("Ignore trailing flags"), dftTrailingFlagInterpretationKey(DftTrailingFlagInterpretation::IgnoreTrailingFlags));
    m_trailingFlagInterpretationCombo->addItem(QStringLiteral("Numeric 1 1 1 means fixed"), dftTrailingFlagInterpretationKey(DftTrailingFlagInterpretation::NumericOneMeansFixed));
    m_trailingFlagInterpretationCombo->addItem(QStringLiteral("Numeric 1 1 1 means movable"), dftTrailingFlagInterpretationKey(DftTrailingFlagInterpretation::NumericOneMeansMovable));
    m_trailingFlagInterpretationCombo->addItem(QStringLiteral("VASP T/F selective dynamics"), dftTrailingFlagInterpretationKey(DftTrailingFlagInterpretation::VaspSelectiveDynamics));
    m_trailingFlagInterpretationCombo->addItem(QStringLiteral("Custom mapping (preserve only)"), dftTrailingFlagInterpretationKey(DftTrailingFlagInterpretation::CustomMapping));
    constraintsForm->addRow(QStringLiteral("Fixed atom mode"), m_fixedAtomModeCombo);
    constraintsForm->addRow(QStringLiteral("Trailing flag interpretation"), m_trailingFlagInterpretationCombo);
    constraintsLayout->addLayout(constraintsForm);
    auto* constraintsHint = new QLabel(QStringLiteral("理由は 3. Structure / Checks の fixed_reason 列で確認できます。"), constraintsPage);
    constraintsHint->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    constraintsLayout->addWidget(constraintsHint);
    constraintsPage->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto* codePage = new QWidget(this);
    auto* codePageLayout = new QVBoxLayout(codePage);
    codePageLayout->setContentsMargins(0, 0, 0, 0);
    auto* codeScroll = new QScrollArea(codePage);
    codeScroll->setWidgetResizable(true);
    codeScroll->setFrameShape(QFrame::NoFrame);
    codeScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    codePageLayout->addWidget(codeScroll, 1);
    auto* codeContent = new QWidget(codeScroll);
    auto* codeLayout = new QVBoxLayout(codeContent);
    codeLayout->setContentsMargins(8, 8, 8, 8);
    codeLayout->setSpacing(6);
    auto* setupTitle = new QLabel(QStringLiteral("<b>1. Code / Version / Profile を最初に選択</b>"), codePage);
    setupTitle->setTextFormat(Qt::RichText);
    auto* setupHelp = new QLabel(QStringLiteral("SIESTA / Quantum ESPRESSO を切り替えると、そのコードで使えるVersion、Profile、Parameter一覧へ自動で組み替えます。まずここを決めてから各タブを調整してください。"), codePage);
    setupHelp->setWordWrap(true);
    setupHelp->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    m_setupContextLabel = new QLabel(codePage);
    m_setupContextLabel->setObjectName(QStringLiteral("dftSetupContextLabel"));
    m_setupContextLabel->setWordWrap(true);
    m_setupContextLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    m_setupContextLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* sourceGroup = new QGroupBox(QStringLiteral("0. Structure Source"), codePage);
    auto* sourceLayout = new QGridLayout(sourceGroup);
    sourceLayout->setContentsMargins(8, 6, 8, 6);
    sourceLayout->setHorizontalSpacing(8);
    sourceLayout->setVerticalSpacing(4);
    m_structureSourceCombo = new QComboBox(sourceGroup);
    m_structureSourceCombo->setObjectName(QStringLiteral("structureSourceCombo"));
    m_structureSourceCombo->addItem(QStringLiteral("Current open structure"), QStringLiteral("current"));
    m_structureSourceCombo->addItem(QStringLiteral("Select file"), QStringLiteral("file"));
    m_structureSourceCombo->addItem(QStringLiteral("Select multiple files / folder"), QStringLiteral("batch"));
    m_structureSourceCombo->setToolTip(QStringLiteral("FDF生成に使う構造の供給元です。Currentは現在ASEAppで開いている構造、Select fileは単一ファイル、Batchは複数構造を同じpresetで一括生成します。"));
    m_sourcePathEdit = new QLineEdit(sourceGroup);
    m_sourcePathEdit->setObjectName(QStringLiteral("structureSourcePathEdit"));
    m_sourcePathEdit->setReadOnly(true);
    m_sourcePathEdit->setToolTip(QStringLiteral("選択された構造ファイルまたはbatch対象の概要です。読み込み専用で、変更はSelectボタンから行います。"));
    m_sourceSummaryLabel = new QLabel(sourceGroup);
    m_sourceSummaryLabel->setObjectName(QStringLiteral("structureSourceSummaryLabel"));
    m_sourceSummaryLabel->setWordWrap(true);
    m_sourceSummaryLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_selectSourceButton = new QPushButton(QStringLiteral("Select file..."), sourceGroup);
    m_selectBatchFilesButton = new QPushButton(QStringLiteral("Select multiple files..."), sourceGroup);
    m_selectBatchFolderButton = new QPushButton(QStringLiteral("Select folder..."), sourceGroup);
    m_selectSourceButton->setObjectName(QStringLiteral("selectStructureFileButton"));
    m_selectBatchFilesButton->setObjectName(QStringLiteral("selectBatchStructureFilesButton"));
    m_selectBatchFolderButton->setObjectName(QStringLiteral("selectBatchStructureFolderButton"));
    m_selectSourceButton->setToolTip(QStringLiteral("POSCAR/CONTCAR/VASP/CIF/XYZ、SIESTA final_structure.txt/siesta.outなど、ASEAppで読み込める構造を1つ選びます。"));
    m_selectBatchFilesButton->setToolTip(QStringLiteral("複数の構造ファイルを選び、同じCode/Version/Profile/Calculation presetでFDFを一括生成します。"));
    m_selectBatchFolderButton->setToolTip(QStringLiteral("フォルダ内の*.vasp, POSCAR, CONTCAR, *.cif, *.xyz, *.out, final_structure.txtを一括対象にします。不要なファイルを除く場合はSelect multiple filesを使ってください。"));
    auto* sourceButtonRow = new QWidget(sourceGroup);
    auto* sourceButtonLayout = new QHBoxLayout(sourceButtonRow);
    sourceButtonLayout->setContentsMargins(0, 0, 0, 0);
    sourceButtonLayout->addWidget(m_selectSourceButton);
    sourceButtonLayout->addWidget(m_selectBatchFilesButton);
    sourceButtonLayout->addWidget(m_selectBatchFolderButton);
    sourceButtonLayout->addStretch(1);
    sourceLayout->addWidget(new QLabel(QStringLiteral("Source mode"), sourceGroup), 0, 0);
    sourceLayout->addWidget(m_structureSourceCombo, 0, 1);
    sourceLayout->addWidget(new QLabel(QStringLiteral("Selected"), sourceGroup), 1, 0);
    sourceLayout->addWidget(m_sourcePathEdit, 1, 1);
    sourceLayout->addWidget(sourceButtonRow, 2, 1);
    sourceLayout->addWidget(m_sourceSummaryLabel, 3, 0, 1, 2);
    sourceLayout->setColumnStretch(1, 1);
    auto* engineGroup = new QGroupBox(QStringLiteral("1. Engine preset"), codePage);
    auto* codeForm = new QFormLayout(engineGroup);
    codeForm->setContentsMargins(8, 6, 8, 6);
    codeForm->setVerticalSpacing(4);
    codeForm->setHorizontalSpacing(8);
    m_codeCombo = new QComboBox(codePage);
    m_codeCombo->setObjectName(QStringLiteral("codeCombo"));
    m_codeCombo->addItem(QStringLiteral("SIESTA"));
    m_codeCombo->addItem(QStringLiteral("Quantum ESPRESSO pw.x"));
    m_codeCombo->setToolTip(QStringLiteral("最初にSIESTAかQuantum ESPRESSOを選択します。選択に応じてVersion、Profile、Parameter一覧を切り替えます。"));
    m_versionCombo = new QComboBox(codePage);
    m_versionCombo->setObjectName(QStringLiteral("versionCombo"));
    m_versionCombo->setToolTip(QStringLiteral("選択したコードのバージョンです。対応していない古いバージョン値は、コード切替時に既定バージョンへ戻します。"));
    m_profileCombo = new QComboBox(codePage);
    m_profileCombo->setObjectName(QStringLiteral("profileCombo"));
    m_profileCombo->setToolTip(QStringLiteral("選択中のコードとVersionに対応するProfile/Presetです。Profileを選ぶと既定パラメータが適用されます。"));
    m_generationModeCombo = new QComboBox(codePage);
    m_generationModeCombo->setObjectName(QStringLiteral("generationModeCombo"));
    m_generationModeCombo->addItem(QStringLiteral("Manual"), static_cast<int>(DftGenerationMode::Manual));
    m_generationModeCombo->addItem(QStringLiteral("Profile"), static_cast<int>(DftGenerationMode::Profile));
    m_generationModeCombo->addItem(QStringLiteral("Import-Edit"), static_cast<int>(DftGenerationMode::ImportEdit));
    m_generationModeCombo->setToolTip(QStringLiteral("Manualは手動編集、Profileは選択Preset適用、Import-Editは既存入力を読み込んで編集するモードです。"));
    m_targetEdit = new QLineEdit(codePage);
    m_targetEdit->setObjectName(QStringLiteral("targetEdit"));
    m_targetEdit->setToolTip(QStringLiteral("出力ファイル名、SystemName、SystemLabelの基になる安全なtarget名です。"));
    m_xcFdfEdit = new QLineEdit(codePage);
    m_pseudoDirEdit = new QLineEdit(codePage);
    m_includeXcCheck = new QCheckBox(QStringLiteral("SIESTA: %include xc.fdf (separate companion)"), codePage);
    m_standaloneCheck = new QCheckBox(QStringLiteral("SIESTA: single FDF (inline xc.fdf blocks)"), codePage);
    m_includeXcCheck->setObjectName(QStringLiteral("includeXcFdfCheck"));
    m_standaloneCheck->setObjectName(QStringLiteral("standaloneInlineCheck"));
    m_allowUnknownHydrogenCheck = new QCheckBox(QStringLiteral("Allow unknown_hydrogen"), codePage);
    m_assumeIsolatedCheck = new QCheckBox(QStringLiteral("QE: assume_isolated = '2D'"), codePage);
    m_projectStyleFlagsCheck = new QCheckBox(QStringLiteral("QE: omit 1 1 1 for fully movable atoms"), codePage);
    m_assumeIsolatedCheck->setObjectName(QStringLiteral("assumeIsolatedCheck"));
    m_projectStyleFlagsCheck->setObjectName(QStringLiteral("projectStyleFixedFlagsCheck"));
    m_xcFdfEdit->setToolTip(QStringLiteral("SIESTA include modeではmain FDFから%includeするxc.fdf名/外部pathです。Standalone時はこのxc.fdfからPAO.Basis/PS.lmax/SyntheticAtoms等を読み取り、FDF本体へインライン展開します。"));
    m_pseudoDirEdit->setToolTip(QStringLiteral("SIESTAではFDF配置先から見たPSFディレクトリです。スパコン構成がGaN/とpotential/の兄弟なら../potentialを指定します。QEではCONTROL.pseudo_dirです。"));
    m_includeXcCheck->setToolTip(QStringLiteral("ON推奨。main FDFには%include xc.fdfだけを書き、Kempisty共通設定は同じ実行ディレクトリにある既存xc.fdfから読み込ませます。ASEAppはxc.fdf本体を出力しません。NetCharge/Spin/kgrid/Constraintsはmain FDF側だけに出します。"));
    m_standaloneCheck->setToolTip(QStringLiteral("ONにするとxc.fdf相当の基底・SyntheticAtoms・PS.lmaxをFDFへ埋め込み、各計算ケースを単一FDFにします。"));
    m_assumeIsolatedCheck->setToolTip(QStringLiteral("assume_isolated: QE charged slabで2D Coulomb補正を明示するときだけ出力します。OFFでは.inから除外します。"));
    m_projectStyleFlagsCheck->setToolTip(QStringLiteral("QE ATOMIC_POSITIONSのproject style: fully movable原子の1 1 1 flagsを省略します。固定原子は0 0 0です。"));
    m_allowUnknownHydrogenCheck->setToolTip(QStringLiteral("HydrogenRoleがunknown_hydrogenのHを許可する場合のみONにしてください。通常は手動でH-0.750/H-1.250/ordinaryへ修正します。"));
    auto* importButton = new QPushButton(QStringLiteral("Import existing input/profile..."), codePage);
    importButton->setToolTip(QStringLiteral("既存のFDF / QE .in / profile json等を読み込み、選択中の構造に対して編集します。"));
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
    codeForm->addRow(QStringLiteral("Profile / preset"), m_profileCombo);
    codeForm->addRow(QStringLiteral("Generation mode"), m_generationModeCombo);
    auto* outputGroup = new QGroupBox(QStringLiteral("2. Output basics"), codePage);
    auto* outputForm = new QFormLayout(outputGroup);
    outputForm->setContentsMargins(8, 6, 8, 6);
    outputForm->setVerticalSpacing(4);
    outputForm->setHorizontalSpacing(8);
    outputForm->addRow(QStringLiteral("Target name"), m_targetEdit);
    outputForm->addRow(QStringLiteral("xc.fdf source"), xcRow);
    outputForm->addRow(QStringLiteral("PSF / pseudo dir"), pseudoRow);
    auto* optionGroup = new QGroupBox(QStringLiteral("3. Code-specific switches"), codePage);
    auto* optionLayout = new QGridLayout(optionGroup);
    optionLayout->setContentsMargins(8, 6, 8, 6);
    optionLayout->setHorizontalSpacing(14);
    optionLayout->setVerticalSpacing(4);
    optionLayout->addWidget(m_includeXcCheck, 0, 0);
    optionLayout->addWidget(m_standaloneCheck, 1, 0);
    optionLayout->addWidget(m_allowUnknownHydrogenCheck, 2, 0);
    optionLayout->addWidget(m_assumeIsolatedCheck, 3, 0);
    optionLayout->addWidget(m_projectStyleFlagsCheck, 4, 0);
    optionLayout->addWidget(constraintsPage, 0, 1, 5, 1);
    optionLayout->setColumnStretch(0, 1);
    optionLayout->setColumnStretch(1, 2);
    auto* setupTitleRow = new QWidget(codeContent);
    auto* setupTitleLayout = new QHBoxLayout(setupTitleRow);
    setupTitleLayout->setContentsMargins(0, 0, 0, 0);
    setupTitleLayout->setSpacing(8);
    setupTitleLayout->addWidget(setupTitle);
    setupTitleLayout->addStretch(1);
    setupTitleLayout->addWidget(importButton);
    auto* setupGrid = new QGridLayout();
    setupGrid->setContentsMargins(0, 0, 0, 0);
    setupGrid->setHorizontalSpacing(8);
    setupGrid->setVerticalSpacing(6);
    setupGrid->addWidget(sourceGroup, 0, 0, 1, 2);
    setupGrid->addWidget(engineGroup, 1, 0);
    setupGrid->addWidget(outputGroup, 1, 1);
    setupGrid->addWidget(optionGroup, 2, 0, 1, 2);
    setupGrid->setColumnStretch(0, 1);
    setupGrid->setColumnStretch(1, 1);
    codeLayout->addWidget(setupTitleRow);
    codeLayout->addWidget(setupHelp);
    codeLayout->addWidget(m_setupContextLabel);
    codeLayout->addLayout(setupGrid);
    codeScroll->setWidget(codeContent);
    m_tabs->insertTab(0, codePage, QStringLiteral("1. Setup"));

    m_calculationModeCombo = new QComboBox(engineGroup);
    m_calculationModeCombo->setObjectName(QStringLiteral("calculationModeCombo"));
    m_calculationModeCombo->setToolTip(QStringLiteral("neutral slab、charged slab、H2 referenceなどの計算目的を選び、電荷・スピン・緩和・k点の推奨既定値を適用します。"));
    codeForm->addRow(QStringLiteral("Calculation mode"), m_calculationModeCombo);

    auto* parameterPage = new QWidget(this);
    auto* parameterLayout = new QVBoxLayout(parameterPage);
    parameterLayout->setContentsMargins(6, 6, 6, 6);
    parameterLayout->setSpacing(4);
    parameterLayout->setAlignment(Qt::AlignTop);
    auto* parameterNote = new QLabel(QStringLiteral("Parametersは1. SetupのCode/Version/Profileから作られます。各入力欄・ドロップダウン・出力チェックにカーソルを合わせると、値/選択肢/出力条件の説明を直接確認できます。"), parameterPage);
    parameterNote->setWordWrap(true);
    parameterNote->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    parameterLayout->addWidget(parameterNote);
    m_parameterContextLabel = new QLabel(parameterPage);
    m_parameterContextLabel->setObjectName(QStringLiteral("parameterContextLabel"));
    m_parameterContextLabel->setWordWrap(true);
    m_parameterContextLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    m_parameterContextLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    parameterLayout->addWidget(m_parameterContextLabel);
    auto* parameterFilterGrid = new QGridLayout();
    parameterFilterGrid->setContentsMargins(0, 0, 0, 0);
    parameterFilterGrid->setHorizontalSpacing(6);
    parameterFilterGrid->setVerticalSpacing(4);
    m_parameterSearchEdit = new QLineEdit(parameterPage);
    m_parameterSearchEdit->setObjectName(QStringLiteral("parameterSearchEdit"));
    m_parameterSearchEdit->setPlaceholderText(QStringLiteral("Search id / label / section / value"));
    m_parameterSearchEdit->setToolTip(QStringLiteral("Parameter ID、表示名、section、現在値、sourceから絞り込みます。"));
    m_parameterCatalogModeCombo = new QComboBox(parameterPage);
    m_parameterCatalogModeCombo->setObjectName(QStringLiteral("parameterCatalogModeCombo"));
    m_parameterCatalogModeCombo->addItem(QStringLiteral("Main workflow"), QStringLiteral("main"));
    m_parameterCatalogModeCombo->addItem(QStringLiteral("SIESTA full FDF catalog"), QStringLiteral("siesta_full"));
    m_parameterCatalogModeCombo->setToolTip(QStringLiteral("Main workflowはGaN slab生成で通常使う主要項目だけを表示します。SIESTA full FDF catalogは公式マニュアル索引ベースのSIESTA FDFラベルを全て表示し、タブ内スクロールで確認・編集できます。"));
    m_parameterSectionFilter = new QComboBox(parameterPage);
    m_parameterSectionFilter->setObjectName(QStringLiteral("parameterSectionFilter"));
    m_parameterSectionFilter->setToolTip(QStringLiteral("表示するParameter sectionを選びます。Allでは全sectionを表示します。"));
    m_parameterScopeFilter = new QComboBox(parameterPage);
    m_parameterScopeFilter->setObjectName(QStringLiteral("parameterScopeFilter"));
    m_parameterScopeFilter->setToolTip(QStringLiteral("Basic-visibleは通常確認すべき有効parameterのみ、Advanced onlyは上級者向け項目のみを表示します。"));
    m_parameterScopeFilter->addItem(QStringLiteral("All"));
    m_parameterScopeFilter->addItem(QStringLiteral("Basic-visible"));
    m_parameterScopeFilter->addItem(QStringLiteral("Enabled only"));
    m_parameterScopeFilter->addItem(QStringLiteral("Advanced only"));
    m_parameterScopeFilter->setCurrentText(QStringLiteral("Basic-visible"));
    auto* searchLabel = new QLabel(QStringLiteral("Search:"), parameterPage);
    auto* catalogLabel = new QLabel(QStringLiteral("Mode:"), parameterPage);
    auto* sectionLabel = new QLabel(QStringLiteral("Section:"), parameterPage);
    auto* scopeLabel = new QLabel(QStringLiteral("Scope:"), parameterPage);
    searchLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    catalogLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    sectionLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    scopeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    parameterFilterGrid->addWidget(searchLabel, 0, 0);
    parameterFilterGrid->addWidget(m_parameterSearchEdit, 0, 1);
    parameterFilterGrid->addWidget(catalogLabel, 0, 2);
    parameterFilterGrid->addWidget(m_parameterCatalogModeCombo, 0, 3);
    parameterFilterGrid->addWidget(sectionLabel, 0, 4);
    parameterFilterGrid->addWidget(m_parameterSectionFilter, 0, 5);
    parameterFilterGrid->addWidget(scopeLabel, 0, 6);
    parameterFilterGrid->addWidget(m_parameterScopeFilter, 0, 7);
    parameterFilterGrid->setColumnStretch(1, 3);
    parameterFilterGrid->setColumnStretch(3, 1);
    parameterFilterGrid->setColumnStretch(5, 1);
    parameterFilterGrid->setColumnStretch(7, 1);
    parameterLayout->addLayout(parameterFilterGrid);
    m_parameterScrollArea = new QScrollArea(parameterPage);
    m_parameterScrollArea->setObjectName(QStringLiteral("parameterScrollArea"));
    m_parameterScrollArea->setWidgetResizable(true);
    m_parameterScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_parameterScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_parameterScrollArea->setMinimumHeight(160);
    m_parameterScrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_parameterListWidget = new QWidget(m_parameterScrollArea);
    m_parameterListWidget->setObjectName(QStringLiteral("parameterListWidget"));
    m_parameterListWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_parameterListLayout = new QVBoxLayout(m_parameterListWidget);
    m_parameterListLayout->setContentsMargins(2, 2, 2, 2);
    m_parameterListLayout->setSpacing(3);
    m_parameterListLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);
    m_parameterScrollArea->setWidget(m_parameterListWidget);
    parameterLayout->addWidget(m_parameterScrollArea, 1);
    m_parameterTable = new QTableWidget(parameterPage);
    m_parameterTable->setObjectName(QStringLiteral("parameterTable"));
    m_parameterTable->setColumnCount(7);
    m_parameterTable->setHorizontalHeaderLabels({"on", "id", "label", "section", "value", "source", "unit"});
    m_parameterTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_parameterTable->hide();
    m_tabs->addTab(parameterPage, QStringLiteral("2. Parameters"));
    m_tabs->addTab(detailsPage, QStringLiteral("3. Structure / Checks"));

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
    m_tabs->addTab(rawPage, QStringLiteral("4. Raw"));

    auto* previewPage = new QWidget(this);
    auto* previewLayout = new QVBoxLayout(previewPage);
    m_warningEdit = new QTextEdit(previewPage);
    m_warningEdit->setObjectName(QStringLiteral("dftWarningEdit"));
    m_warningEdit->setReadOnly(true);
    m_warningEdit->setMaximumHeight(150);
    m_previewEdit = new QTextEdit(previewPage);
    m_previewEdit->setObjectName(QStringLiteral("dftPreviewEdit"));
    m_previewEdit->setReadOnly(true);
    m_previewEdit->setLineWrapMode(QTextEdit::NoWrap);
    m_batchResultTable = new QTableWidget(previewPage);
    m_batchResultTable->setObjectName(QStringLiteral("batchGenerationResultTable"));
    m_batchResultTable->setColumnCount(4);
    m_batchResultTable->setHorizontalHeaderLabels({"source", "target", "status", "message"});
    m_batchResultTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_batchResultTable->setMaximumHeight(150);
    m_batchResultTable->setToolTip(QStringLiteral("Batch生成後に、各入力構造の出力target、PASS/WARN/FAIL、エラー/警告を表示します。"));
    auto* previewButtons = new QHBoxLayout();
    auto* copyButton = new QPushButton(QStringLiteral("Copy primary input"), previewPage);
    m_exportButton = new QPushButton(QStringLiteral("Save primary input..."), previewPage);
    m_exportButton->setObjectName(QStringLiteral("exportGeneratedFilesButton"));
    m_exportButton->setToolTip(QStringLiteral("検証FAILがない場合に、対象のprimary FDF/inputだけを書き出します。xc.fdf、summary、parameters、required_files、job scriptは作成しません。Batch modeでは選択済みの全構造を一括出力します。"));
    previewButtons->addWidget(copyButton);
    previewButtons->addWidget(m_exportButton);
    previewButtons->addStretch(1);
    previewLayout->addWidget(m_warningEdit);
    previewLayout->addWidget(m_previewEdit, 1);
    previewLayout->addWidget(m_batchResultTable);
    previewLayout->addLayout(previewButtons);
    m_tabs->addTab(previewPage, QStringLiteral("5. Preview / Export"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(m_structureSourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        const QString mode = m_structureSourceCombo != nullptr ? m_structureSourceCombo->currentData().toString() : QStringLiteral("current");
        if (mode == QStringLiteral("current")) {
            m_batchSourcePaths.clear();
            const QString currentTarget = m_targetEdit != nullptr && !m_targetEdit->text().trimmed().isEmpty()
                ? m_targetEdit->text().trimmed()
                : (m_initialStructure.title.isEmpty() ? QStringLiteral("ideal") : m_initialStructure.title);
            setActiveStructure(m_initialStructure, DftInputGenerator::sanitizeTargetName(currentTarget), true);
        }
        refreshSourceSummary();
        updatePreview();
    });
    connect(m_selectSourceButton, &QPushButton::clicked, this, &DftInputGeneratorDialog::selectStructureFile);
    connect(m_selectBatchFilesButton, &QPushButton::clicked, this, &DftInputGeneratorDialog::selectBatchStructureFiles);
    connect(m_selectBatchFolderButton, &QPushButton::clicked, this, &DftInputGeneratorDialog::selectBatchStructureFolder);
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
        const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("PSF / pseudo directoryを選択"), m_pseudoDirEdit->text());
        if (!path.isEmpty()) m_pseudoDirEdit->setText(path);
    });
    connect(m_calculationModeCombo, &QComboBox::currentTextChanged, this, [this](const QString& mode) {
        if (mode.isEmpty()) return;
        collectUiToSettings();
        DftParameterRegistry::applyCalculationModeDefaults(&m_settings, mode);
        refreshParameterTable();
        updatePreview();
    });
    connect(m_parameterSearchEdit, &QLineEdit::textChanged, this, [this]() {
        collectUiToSettings();
        refreshParameterTable();
    });
    connect(m_parameterCatalogModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        collectUiToSettings();
        const bool fullCatalog = m_parameterCatalogModeCombo != nullptr &&
            m_parameterCatalogModeCombo->currentData().toString() == QStringLiteral("siesta_full");
        if (fullCatalog) {
            if (m_parameterScopeFilter != nullptr) {
                m_parameterScopeFilter->setProperty("aseappLastMainScope", m_parameterScopeFilter->currentText());
                const QSignalBlocker blocker(m_parameterScopeFilter);
                m_parameterScopeFilter->setCurrentText(QStringLiteral("All"));
            }
            if (m_parameterSectionFilter != nullptr) {
                m_parameterSectionFilter->setProperty("aseappLastMainSection", m_parameterSectionFilter->currentText());
                const QSignalBlocker blocker(m_parameterSectionFilter);
                m_parameterSectionFilter->setCurrentText(QStringLiteral("All"));
            }
        } else {
            if (m_parameterScopeFilter != nullptr) {
                const QString restoreScope = m_parameterScopeFilter->property("aseappLastMainScope").toString();
                const int restoreScopeIndex = m_parameterScopeFilter->findText(restoreScope);
                if (restoreScopeIndex >= 0) {
                    const QSignalBlocker blocker(m_parameterScopeFilter);
                    m_parameterScopeFilter->setCurrentIndex(restoreScopeIndex);
                }
            }
            if (m_parameterSectionFilter != nullptr) {
                const QString restoreSection = m_parameterSectionFilter->property("aseappLastMainSection").toString();
                if (!restoreSection.trimmed().isEmpty()) {
                    m_parameterSectionFilter->setProperty("aseappPendingSectionRestore", restoreSection);
                }
            }
        }
        refreshParameterTable();
        updatePreview();
    });
    connect(m_parameterSectionFilter, &QComboBox::currentTextChanged, this, [this](const QString&) {
        collectUiToSettings();
        refreshParameterTable();
    });
    connect(m_parameterScopeFilter, &QComboBox::currentTextChanged, this, [this](const QString&) {
        collectUiToSettings();
        refreshParameterTable();
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
    connect(m_exportButton, &QPushButton::clicked, this, &DftInputGeneratorDialog::exportGeneratedFiles);

    syncControlsFromSettings();
    refreshSourceSummary();
    refreshStructureTab();
    refreshHydrogenTab();
    refreshSpeciesTable();
    refreshParameterTable();
    refreshRawTable();
    m_tabs->setCurrentIndex(0);
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
            const QString fileName = QFileInfo(path).fileName().toLower();
            if (!fileName.endsWith(QStringLiteral(".profile.json")) && fileName != QStringLiteral("profile.json")) continue;
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
    QFile file(path);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        const QJsonObject root = doc.object();
        const QStringList keys = {
            QStringLiteral("display_name"),
            QStringLiteral("profile_name"),
            QStringLiteral("profile"),
            QStringLiteral("selected_profile"),
            QStringLiteral("profile_id"),
        };
        for (const QString& key : keys) {
            const QString label = root.value(key).toString().trimmed();
            if (!label.isEmpty()) return label;
        }
    }
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

QString shortSourceText(DftParameterSource source) {
    switch (source) {
    case DftParameterSource::ProjectProfile: return QStringLiteral("preset");
    case DftParameterSource::UserOverride: return QStringLiteral("user");
    case DftParameterSource::ImportedFile: return QStringLiteral("import");
    case DftParameterSource::ImportedFdfLog: return QStringLiteral("fdf-log");
    case DftParameterSource::ImportedQeIn: return QStringLiteral("qe-in");
    case DftParameterSource::VersionDefault: return QStringLiteral("default");
    case DftParameterSource::Unknown: return QStringLiteral("unset");
    }
    return QStringLiteral("unset");
}

QString parameterTooltip(const DftParameterEntry& entry) {
    const auto& spec = entry.spec;
    const QString title = spec.label.trimmed().isEmpty()
        ? (spec.key.trimmed().isEmpty() ? spec.id : spec.key)
        : spec.label.trimmed();
    QStringList lines;
    lines << QStringLiteral("%1 / %2").arg(title, spec.id);
    if (!spec.tooltipLong.trimmed().isEmpty()) {
        lines << spec.tooltipLong.trimmed();
    } else if (!spec.tooltipShort.trimmed().isEmpty()) {
        lines << spec.tooltipShort.trimmed();
    }
    lines << QStringLiteral("FDF/QE key: %1").arg(spec.key.trimmed().isEmpty() ? spec.id : spec.key);
    if (!spec.section.trimmed().isEmpty()) lines << QStringLiteral("Section: %1").arg(spec.section.trimmed());
    if (!spec.type.trimmed().isEmpty()) lines << QStringLiteral("Type: %1").arg(spec.type.trimmed());
    if (!spec.unit.trimmed().isEmpty()) lines << QStringLiteral("Unit: %1").arg(spec.unit.trimmed());
    if (!spec.allowedValues.isEmpty()) lines << QStringLiteral("Choices: %1").arg(spec.allowedValues.join(QStringLiteral(", ")));
    if (!spec.minValue.trimmed().isEmpty() || !spec.maxValue.trimmed().isEmpty()) {
        lines << QStringLiteral("Range: %1 - %2").arg(
            spec.minValue.trimmed().isEmpty() ? QStringLiteral("(none)") : spec.minValue.trimmed(),
            spec.maxValue.trimmed().isEmpty() ? QStringLiteral("(none)") : spec.maxValue.trimmed());
    }
    lines << QStringLiteral("Current value: %1").arg(entry.value.trimmed().isEmpty() ? QStringLiteral("(empty)") : entry.value);
    lines << QStringLiteral("Output: %1").arg(entry.enabled ? QStringLiteral("ON") : QStringLiteral("OFF"));
    lines << QStringLiteral("Source: %1").arg(sourceText(entry.source));
    if (spec.required) lines << QStringLiteral("Required parameter");
    if (spec.advanced) lines << QStringLiteral("Advanced parameter");
    return lines.join(QLatin1Char('\n'));
}

QString comboOptionTooltip(const DftParameterEntry& entry, const QString& option) {
    return QStringLiteral("%1\n\nChoice value: %2\n選択するとこの値が生成Previewへ即時反映されます。")
        .arg(parameterTooltip(entry), option);
}

QString statusFromBool(bool ok, const QString& warningValue = QString()) {
    if (ok) return QStringLiteral("PASS");
    return warningValue.isEmpty() ? QStringLiteral("ERROR") : warningValue;
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

QBrush statusBrush(const QString& status) {
    if (status.compare(QStringLiteral("ERROR"), Qt::CaseInsensitive) == 0) return QBrush(QColor(180, 35, 35));
    if (status.compare(QStringLiteral("WARNING"), Qt::CaseInsensitive) == 0) return QBrush(QColor(170, 105, 0));
    if (status.compare(QStringLiteral("PASS"), Qt::CaseInsensitive) == 0) return QBrush(QColor(35, 120, 65));
    return QBrush();
}

QTableWidgetItem* statusItem(const QString& status) {
    auto* it = item(status);
    it->setForeground(statusBrush(status));
    return it;
}

QString idsText(const QVector<int>& ids) {
    QStringList parts;
    for (int id : ids) parts << QString::number(id);
    return parts.join(QLatin1Char(' '));
}

QVector<int> sortedIds(QVector<int> ids) {
    std::sort(ids.begin(), ids.end());
    return ids;
}

QVector<int> expectedSevenLayerFixedIds() {
    return QVector<int>({1, 5, 9, 13, 42, 46, 50, 54, 58, 59, 60, 61});
}

bool containsAllIds(const QVector<int>& actual, const QVector<int>& expected) {
    for (int id : expected) {
        if (!actual.contains(id)) return false;
    }
    return true;
}

QString orderedCompanionFiles(QStringList files) {
    const QStringList preferred = {
        QStringLiteral("xc.fdf"),
        QStringLiteral("Ga.psf"),
        QStringLiteral("N.psf"),
        QStringLiteral("H.psf"),
        QStringLiteral("H-0.750.psf"),
        QStringLiteral("H-1.250.psf"),
    };
    QStringList ordered;
    for (const QString& wanted : preferred) {
        for (const QString& file : std::as_const(files)) {
            if (QFileInfo(file).fileName().compare(wanted, Qt::CaseInsensitive) == 0 && !ordered.contains(file)) {
                ordered << file;
            }
        }
    }
    for (const QString& file : std::as_const(files)) {
        if (!ordered.contains(file)) ordered << file;
    }
    return ordered.isEmpty() ? QStringLiteral("-") : ordered.join(QStringLiteral(", "));
}

QString generatedFileName(const DftSettings& settings, const DftGeneratedInput& generated) {
    return DftInputGenerator::sanitizeTargetName(settings.targetName) + generated.fileExtension;
}

QString parameterValueOrUnset(const DftSettings& settings, const QString& id) {
    const QString value = DftParameterRegistry::parameterValue(settings, id).trimmed();
    return value.isEmpty() ? QStringLiteral("unset") : value;
}

bool neutralSiestaUi(const DftSettings& settings) {
    return settings.code == DftCode::Siesta &&
        DftParameterRegistry::parameterValue(settings, QStringLiteral("siesta.charge_spin.NetCharge")).trimmed() == QStringLiteral("0.0") &&
        DftParameterRegistry::parameterValue(settings, QStringLiteral("siesta.charge_spin.Spin")).trimmed().compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0;
}

bool neutralUnneededParameter(const DftParameterEntry& entry, const DftSettings& settings) {
    if (!neutralSiestaUi(settings)) return false;
    return entry.spec.id == QStringLiteral("siesta.charge_spin.Spin.Fix") ||
           entry.spec.id == QStringLiteral("siesta.charge_spin.Spin.Total");
}

bool basicVisibleParameter(const DftParameterEntry& entry, const DftSettings& settings) {
    if (!entry.enabled) return false;
    if (entry.source == DftParameterSource::Unknown) return false;
    if (entry.value.trimmed().isEmpty()) return false;
    if (entry.spec.advanced) return false;
    if (neutralUnneededParameter(entry, settings)) return false;
    return true;
}

bool siestaCatalogParameter(const DftParameterEntry& entry) {
    return entry.spec.validationRule == QStringLiteral("siesta_fdf_catalog");
}

bool activeCatalogParameter(const DftParameterEntry& entry) {
    return siestaCatalogParameter(entry) &&
           entry.enabled &&
           entry.source != DftParameterSource::Unknown &&
           !entry.value.trimmed().isEmpty();
}

} // namespace

void DftInputGeneratorDialog::rebuildCodeDependentUi() {
    if (m_codeCombo == nullptr || m_versionCombo == nullptr) return;
    const DftCode code = dftCodeFromIndex(m_codeCombo->currentIndex());
    const QStringList supportedVersions = DftParameterRegistry::versionsForCode(code);
    QString version = m_versionCombo->currentText().trimmed();
    if (version.isEmpty() || !supportedVersions.contains(version)) version = supportedVersions.value(0);
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

void DftInputGeneratorDialog::updateContextLabels() {
    const QString profile = m_settings.profileName.trimmed().isEmpty() ? QStringLiteral("Manual") : m_settings.profileName;
    const QString mode = dftGenerationModeKey(m_settings.generationMode);
    const QString sourceMode = m_structureSourceCombo != nullptr ? m_structureSourceCombo->currentData().toString() : QStringLiteral("current");
    const QString sourceDetail = sourceMode == QStringLiteral("batch")
        ? QStringLiteral("%1 batch files").arg(m_batchSourcePaths.size())
        : (m_structure.sourcePath.isEmpty() ? QStringLiteral("current document") : QFileInfo(m_structure.sourcePath).fileName());
    if (m_setupContextLabel != nullptr) {
        m_setupContextLabel->setText(QStringLiteral("Current: %1 %2 / profile=%3 / mode=%4 / source=%5 (%6) / target=%7")
            .arg(dftCodeToString(m_settings.code), m_settings.version, profile, mode, sourceMode, sourceDetail, m_settings.targetName));
    }
    if (m_parameterContextLabel != nullptr) {
        const QStringList versions = DftParameterRegistry::versionsForCode(m_settings.code);
        const bool fullCatalog = m_parameterCatalogModeCombo != nullptr &&
            m_parameterCatalogModeCombo->currentData().toString() == QStringLiteral("siesta_full");
        const QString catalogLabel = fullCatalog ? QStringLiteral("SIESTA full FDF catalog") : QStringLiteral("main workflow");
        m_parameterContextLabel->setText(QStringLiteral("%1 %2 parameters (%3) / supported versions: %4 / Setup change rebuilds this list")
            .arg(dftCodeToString(m_settings.code), m_settings.version, catalogLabel, versions.join(QStringLiteral(", "))));
    }
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
    const QSignalBlocker b7(m_targetEdit);
    const QSignalBlocker b8(m_xcFdfEdit);
    const QSignalBlocker b9(m_pseudoDirEdit);
    const QSignalBlocker b10(m_includeXcCheck);
    const QSignalBlocker b11(m_standaloneCheck);
    const QSignalBlocker b12(m_allowUnknownHydrogenCheck);
    const QSignalBlocker b13(m_assumeIsolatedCheck);
    const QSignalBlocker b14(m_projectStyleFlagsCheck);
    const QSignalBlocker b15(m_parameterCatalogModeCombo);
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
    if (m_parameterCatalogModeCombo) {
        m_parameterCatalogModeCombo->setEnabled(m_settings.code == DftCode::Siesta);
        if (m_settings.code != DftCode::Siesta) {
            m_parameterCatalogModeCombo->setCurrentIndex(0);
            if (m_parameterScopeFilter) m_parameterScopeFilter->setEnabled(true);
            if (m_parameterSectionFilter) m_parameterSectionFilter->setProperty("aseappPendingSectionRestore", QString());
        }
    }
    updateContextLabels();
    refreshSpeciesTable();
    refreshParameterTable();
    refreshRawTable();
}

void DftInputGeneratorDialog::refreshSourceSummary() {
    QString mode = QStringLiteral("current");
    if (m_structureSourceCombo != nullptr) mode = m_structureSourceCombo->currentData().toString();
    if (m_sourcePathEdit != nullptr) {
        QString pathText;
        if (mode == QStringLiteral("batch")) {
            pathText = m_batchSourcePaths.isEmpty()
                ? QStringLiteral("No batch files selected")
                : QStringLiteral("%1 files; first=%2").arg(m_batchSourcePaths.size()).arg(QDir::toNativeSeparators(m_batchSourcePaths.first()));
        } else if (!m_structure.sourcePath.trimmed().isEmpty()) {
            pathText = QDir::toNativeSeparators(m_structure.sourcePath);
        } else {
            pathText = QStringLiteral("Current open structure");
        }
        m_sourcePathEdit->setText(pathText);
    }
    if (m_selectSourceButton != nullptr) m_selectSourceButton->setEnabled(mode != QStringLiteral("batch"));
    if (m_selectBatchFilesButton != nullptr) m_selectBatchFilesButton->setEnabled(mode != QStringLiteral("file"));
    if (m_selectBatchFolderButton != nullptr) m_selectBatchFolderButton->setEnabled(mode != QStringLiteral("file"));
    if (m_sourceSummaryLabel != nullptr) {
        std::map<QString, int> counts;
        for (const auto& atom : m_structure.atoms) counts[atom.element]++;
        QStringList species;
        for (const auto& kv : counts) species << QStringLiteral("%1:%2").arg(kv.first).arg(kv.second);
        const QString cellText = QStringLiteral("a=%1 Å, b=%2 Å, c=%3 Å")
            .arg(m_structure.cellVectors[0].length(), 0, 'f', 3)
            .arg(m_structure.cellVectors[1].length(), 0, 'f', 3)
            .arg(m_structure.cellVectors[2].length(), 0, 'f', 3);
        m_sourceSummaryLabel->setText(QStringLiteral("Preview: atoms=%1 / species=%2 / cell=%3 / source mode=%4")
            .arg(m_structure.atoms.size())
            .arg(species.isEmpty() ? QStringLiteral("-") : species.join(QStringLiteral(", ")))
            .arg(cellText, mode));
    }
}

bool DftInputGeneratorDialog::loadStructurePath(const QString& path, StructureData* structure, QString* errorMessage) const {
    if (structure == nullptr) return false;
    StructureFileLoader loader;
    StructureImportOptions options;
    options.trailingFlagInterpretation = toStructureTrailingFlagInterpretation(m_settings.trailingFlagInterpretation);
    const auto loaded = loader.load(path, errorMessage, options);
    if (!loaded.has_value()) return false;
    *structure = *loaded;
    if (structure->title.trimmed().isEmpty()) structure->title = QFileInfo(path).completeBaseName();
    structure->sourcePath = path;
    return true;
}

QString DftInputGeneratorDialog::targetNameForStructureSource(const QString& path, const StructureData& structure) const {
    QString base = !path.trimmed().isEmpty()
        ? QFileInfo(path).completeBaseName()
        : (structure.title.trimmed().isEmpty() ? QStringLiteral("ideal") : structure.title.trimmed());
    base = DftInputGenerator::sanitizeTargetName(base);
    const QStringList knownSuffixes = {
        QStringLiteral("_neutral_first_profile"),
        QStringLiteral("_qm0p25_profile"),
        QStringLiteral("_qp0p25_profile"),
        QStringLiteral("_ga_atom_profile"),
        QStringLiteral("_n2_profile"),
        QStringLiteral("_h2_profile"),
        QStringLiteral("_custom_profile"),
    };
    for (const QString& suffix : knownSuffixes) {
        if (base.endsWith(suffix, Qt::CaseInsensitive)) {
            base.chop(suffix.size());
            break;
        }
    }
    return DftInputGenerator::sanitizeTargetName(base + calculationTargetSuffix(m_settings));
}

void DftInputGeneratorDialog::setActiveStructure(const StructureData& structure, const QString& targetName, bool resetHydrogen) {
    m_structure = structure;
    m_settings.sourceStructurePath = structure.sourcePath;
    if (!structure.trailingFlagInterpretation.trimmed().isEmpty()) {
        m_settings.trailingFlagInterpretation = dftTrailingFlagInterpretationFromKey(structure.trailingFlagInterpretation);
    }
    if (!targetName.trimmed().isEmpty()) {
        m_settings.targetName = DftInputGenerator::sanitizeTargetName(targetName);
        DftParameterRegistry::setParameterValue(&m_settings, QStringLiteral("siesta.general.SystemName"), m_settings.targetName, DftParameterSource::UserOverride);
        DftParameterRegistry::setParameterValue(&m_settings, QStringLiteral("siesta.general.SystemLabel"), m_settings.targetName, DftParameterSource::UserOverride);
        DftParameterRegistry::setParameterValue(&m_settings, QStringLiteral("qe.CONTROL.prefix"), m_settings.targetName, DftParameterSource::UserOverride);
    }
    if (resetHydrogen || m_settings.hydrogenAssignments.isEmpty()) {
        m_settings.hydrogenAssignments = DftInputGenerator::inferHydrogenRoles(m_structure, m_settings);
    }
    syncControlsFromSettings();
    refreshSourceSummary();
    refreshStructureTab();
    refreshHydrogenTab();
    updatePreview();
}

DftSettings DftInputGeneratorDialog::settingsForStructure(const StructureData& structure, const QString& targetName) const {
    DftSettings settings = m_settings;
    settings.sourceStructurePath = structure.sourcePath;
    if (!structure.trailingFlagInterpretation.trimmed().isEmpty()) {
        settings.trailingFlagInterpretation = dftTrailingFlagInterpretationFromKey(structure.trailingFlagInterpretation);
    }
    settings.targetName = DftInputGenerator::sanitizeTargetName(targetName);
    if (settings.code == DftCode::Siesta) {
        DftParameterRegistry::setParameterValue(&settings, QStringLiteral("siesta.general.SystemName"), settings.targetName, DftParameterSource::UserOverride);
        DftParameterRegistry::setParameterValue(&settings, QStringLiteral("siesta.general.SystemLabel"), settings.targetName, DftParameterSource::UserOverride);
    } else {
        DftParameterRegistry::setParameterValue(&settings, QStringLiteral("qe.CONTROL.prefix"), settings.targetName, DftParameterSource::UserOverride);
    }
    settings.hydrogenAssignments = DftInputGenerator::inferHydrogenRoles(structure, settings);
    return settings;
}

bool DftInputGeneratorDialog::exportSingleGeneratedFile(const QString& outputDirectory, const StructureData& structure,
                                                        const DftSettings& settings, QString* errorMessage,
                                                        DftGeneratedInput* generatedOut) const {
    const DftGeneratedInput generated = DftInputGenerator::generate(structure, settings);
    if (generatedOut != nullptr) *generatedOut = generated;
    if (!generated.ok) {
        if (errorMessage) *errorMessage = generated.errors.join(QLatin1Char('\n'));
        return false;
    }
    QStringList checklistErrors;
    const auto checklist = DftInputGenerator::scientificChecklist(structure, settings, generated);
    for (const auto& item : checklist) {
        if (item.status == QStringLiteral("ERROR")) {
            checklistErrors << QStringLiteral("%1 / %2: %3").arg(item.group, item.item, item.detail);
        }
    }
    if (!checklistErrors.isEmpty()) {
        if (errorMessage) *errorMessage = checklistErrors.join(QLatin1Char('\n'));
        return false;
    }
    return DftInputGenerator::writeGeneratedFiles(outputDirectory, settings, generated, errorMessage);
}

void DftInputGeneratorDialog::selectStructureFile() {
    collectUiToSettings();
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("構造ファイルを選択"), QString(),
        QStringLiteral("Structure files (*.vasp *.poscar *.contcar *.cif *.xyz *.txt *.out POSCAR CONTCAR);;All files (*.*)"));
    if (path.isEmpty()) return;
    StructureData loaded;
    QString error;
    if (!loadStructurePath(path, &loaded, &error)) {
        QMessageBox::warning(this, QStringLiteral("Structure Source"), error.isEmpty() ? QStringLiteral("構造ファイルを読み込めませんでした。") : error);
        return;
    }
    m_batchSourcePaths.clear();
    if (m_structureSourceCombo != nullptr) {
        const QSignalBlocker blocker(m_structureSourceCombo);
        const int index = m_structureSourceCombo->findData(QStringLiteral("file"));
        if (index >= 0) m_structureSourceCombo->setCurrentIndex(index);
    }
    setActiveStructure(loaded, targetNameForStructureSource(path, loaded), true);
}

void DftInputGeneratorDialog::selectBatchStructureFiles() {
    collectUiToSettings();
    const QStringList paths = QFileDialog::getOpenFileNames(this, QStringLiteral("一括生成する構造ファイルを選択"), QString(),
        QStringLiteral("Structure files (*.vasp *.poscar *.contcar *.cif *.xyz *.txt *.out POSCAR CONTCAR);;All files (*.*)"));
    if (paths.isEmpty()) return;
    m_batchSourcePaths = paths;
    m_batchSourcePaths.removeDuplicates();
    std::sort(m_batchSourcePaths.begin(), m_batchSourcePaths.end(), [](const QString& a, const QString& b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    if (m_structureSourceCombo != nullptr) {
        const QSignalBlocker blocker(m_structureSourceCombo);
        const int index = m_structureSourceCombo->findData(QStringLiteral("batch"));
        if (index >= 0) m_structureSourceCombo->setCurrentIndex(index);
    }
    StructureData preview;
    QString error;
    if (loadStructurePath(m_batchSourcePaths.first(), &preview, &error)) {
        setActiveStructure(preview, targetNameForStructureSource(m_batchSourcePaths.first(), preview), true);
    } else {
        refreshSourceSummary();
        QMessageBox::warning(this, QStringLiteral("Structure Source"), error);
    }
}

void DftInputGeneratorDialog::selectBatchStructureFolder() {
    collectUiToSettings();
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("一括生成する構造フォルダを選択"));
    if (dir.isEmpty()) return;
    QStringList paths;
    QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (supportedStructureCandidate(QFileInfo(path))) paths << path;
    }
    paths.removeDuplicates();
    std::sort(paths.begin(), paths.end(), [](const QString& a, const QString& b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    if (paths.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Structure Source"), QStringLiteral("対象フォルダに対応構造ファイルが見つかりませんでした。"));
        return;
    }
    m_batchSourcePaths = paths;
    if (m_structureSourceCombo != nullptr) {
        const QSignalBlocker blocker(m_structureSourceCombo);
        const int index = m_structureSourceCombo->findData(QStringLiteral("batch"));
        if (index >= 0) m_structureSourceCombo->setCurrentIndex(index);
    }
    StructureData preview;
    QString error;
    if (loadStructurePath(m_batchSourcePaths.first(), &preview, &error)) {
        setActiveStructure(preview, targetNameForStructureSource(m_batchSourcePaths.first(), preview), true);
    } else {
        refreshSourceSummary();
        QMessageBox::warning(this, QStringLiteral("Structure Source"), error);
    }
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

void DftInputGeneratorDialog::refreshOverviewTab() {
    if (m_overviewTable == nullptr) return;
    const QSignalBlocker blocker(m_overviewTable);
    const DftGeneratedInput generated = m_generated.primaryText.isEmpty()
        ? DftInputGenerator::generate(m_structure, m_settings)
        : m_generated;
    const QString text = generated.primaryText;

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
    const double aLen = m_structure.cellVectors[0].length();
    const double bLen = m_structure.cellVectors[1].length();
    const double cLen = m_structure.cellVectors[2].length();
    const double slabThickness = first ? 0.0 : zMax - zMin;
    const double vacuum = std::max(0.0, cLen - slabThickness);

    QVector<int> fixedIds;
    QStringList fixedReasons;
    const QJsonArray fixedAtoms = generated.summaryObject.value(QStringLiteral("fixed_atoms")).toArray();
    for (const auto& value : fixedAtoms) {
        const QJsonObject obj = value.toObject();
        const int atomIndex = obj.value(QStringLiteral("atom_index")).toInt();
        if (atomIndex > 0) fixedIds << atomIndex;
        const QString reason = obj.value(QStringLiteral("reason")).toString();
        if (!reason.isEmpty() && !fixedReasons.contains(reason)) fixedReasons << reason;
    }
    fixedIds = sortedIds(fixedIds);

    QVector<int> bottomH;
    QVector<int> ordinaryH;
    QVector<int> unknownH;
    for (const auto& h : m_settings.hydrogenAssignments) {
        const int id = h.atomIndex + 1;
        if (h.selectedRole == DftHydrogenRole::BottomPseudoHNTerminated075) bottomH << id;
        else if (h.selectedRole == DftHydrogenRole::OrdinaryHydrogen ||
                 h.selectedRole == DftHydrogenRole::SurfaceAdsorbedHydrogen ||
                 h.selectedRole == DftHydrogenRole::MoleculeH2Hydrogen) {
            ordinaryH << id;
        } else if (h.selectedRole == DftHydrogenRole::UnknownHydrogen) {
            unknownH << id;
        }
    }
    bottomH = sortedIds(bottomH);
    ordinaryH = sortedIds(ordinaryH);
    unknownH = sortedIds(unknownH);

    const bool spinFixEmitted = text.contains(QStringLiteral("Spin.Fix"), Qt::CaseInsensitive);
    const bool spinTotalEmitted = text.contains(QStringLiteral("Spin.Total"), Qt::CaseInsensitive);
    const bool initSpinEmitted = text.contains(QStringLiteral("DM.InitSpin"), Qt::CaseInsensitive);
    const QString netCharge = parameterValueOrUnset(m_settings, QStringLiteral("siesta.charge_spin.NetCharge"));
    const QString spin = parameterValueOrUnset(m_settings, QStringLiteral("siesta.charge_spin.Spin"));
    const bool isNeutral = neutralSiestaUi(m_settings);
    const QString overallStatus = !generated.errors.isEmpty()
        ? QStringLiteral("ERROR")
        : (!generated.warnings.isEmpty() ? QStringLiteral("WARNING") : QStringLiteral("PASS"));
    const QVector<int> expected = expectedSevenLayerFixedIds();
    const bool expectedFixedOk = static_cast<int>(m_structure.atoms.size()) == 61
        ? containsAllIds(fixedIds, expected)
        : !fixedIds.isEmpty();

    struct Row {
        QString section;
        QString label;
        QString value;
        QString status;
    };
    QVector<Row> rows;
    auto addRow = [&rows](const QString& section, const QString& label, const QString& value, const QString& status) {
        rows << Row{section, label, value, status};
    };

    addRow(QStringLiteral("Project"), QStringLiteral("Code"), dftCodeToString(m_settings.code), QStringLiteral("PASS"));
    addRow(QStringLiteral("Project"), QStringLiteral("Version"), m_settings.version, QStringLiteral("PASS"));
    addRow(QStringLiteral("Project"), QStringLiteral("Profile"), m_settings.profileName, m_settings.profileName.isEmpty() ? QStringLiteral("WARNING") : QStringLiteral("PASS"));
    addRow(QStringLiteral("Project"), QStringLiteral("Target name"), m_settings.targetName, m_settings.targetName.compare(QStringLiteral("GaN"), Qt::CaseInsensitive) == 0 ? QStringLiteral("WARNING") : QStringLiteral("PASS"));
    addRow(QStringLiteral("Project"), QStringLiteral("SystemName"), parameterValueOrUnset(m_settings, QStringLiteral("siesta.general.SystemName")), QStringLiteral("PASS"));
    addRow(QStringLiteral("Project"), QStringLiteral("SystemLabel"), parameterValueOrUnset(m_settings, QStringLiteral("siesta.general.SystemLabel")), parameterValueOrUnset(m_settings, QStringLiteral("siesta.general.SystemLabel")).compare(QStringLiteral("GaN"), Qt::CaseInsensitive) == 0 ? QStringLiteral("WARNING") : QStringLiteral("PASS"));

    addRow(QStringLiteral("Structure"), QStringLiteral("Number of atoms"), QString::number(m_structure.atoms.size()), text.contains(QStringLiteral("NumberOfAtoms %1").arg(m_structure.atoms.size())) || m_settings.code != DftCode::Siesta ? QStringLiteral("PASS") : QStringLiteral("ERROR"));
    addRow(QStringLiteral("Structure"), QStringLiteral("Element counts"), composition.join(QStringLiteral(", ")), QStringLiteral("PASS"));
    addRow(QStringLiteral("Structure"), QStringLiteral("Cell lengths"), QStringLiteral("a=%1 Å, b=%2 Å, c=%3 Å").arg(aLen, 0, 'f', 3).arg(bLen, 0, 'f', 3).arg(cLen, 0, 'f', 3), QStringLiteral("PASS"));
    addRow(QStringLiteral("Structure"), QStringLiteral("Top vacuum thickness"), QStringLiteral("%1 Å (estimated)").arg(vacuum, 0, 'f', 3), vacuum >= 19.5 ? QStringLiteral("PASS") : QStringLiteral("WARNING"));
    addRow(QStringLiteral("Structure"), QStringLiteral("Atom order preserved"), yesNo(generated.summaryObject.value(QStringLiteral("atom_order_preserved")).toBool(true)), generated.summaryObject.value(QStringLiteral("atom_order_preserved")).toBool(true) ? QStringLiteral("PASS") : QStringLiteral("ERROR"));

    addRow(QStringLiteral("Charge / Spin"), QStringLiteral("NetCharge"), netCharge, isNeutral && netCharge != QStringLiteral("0.0") ? QStringLiteral("ERROR") : QStringLiteral("PASS"));
    addRow(QStringLiteral("Charge / Spin"), QStringLiteral("Spin"), spin, isNeutral && spin.compare(QStringLiteral("none"), Qt::CaseInsensitive) != 0 ? QStringLiteral("ERROR") : QStringLiteral("PASS"));
    addRow(QStringLiteral("Charge / Spin"), QStringLiteral("Spin.Fix emitted?"), yesNo(spinFixEmitted), isNeutral && spinFixEmitted ? QStringLiteral("ERROR") : QStringLiteral("PASS"));
    addRow(QStringLiteral("Charge / Spin"), QStringLiteral("Spin.Total emitted?"), yesNo(spinTotalEmitted), isNeutral && spinTotalEmitted ? QStringLiteral("ERROR") : QStringLiteral("PASS"));
    addRow(QStringLiteral("Charge / Spin"), QStringLiteral("DM.InitSpin emitted?"), yesNo(initSpinEmitted), isNeutral && initSpinEmitted ? QStringLiteral("ERROR") : QStringLiteral("PASS"));
    if (isNeutral) {
        addRow(QStringLiteral("Neutral preset"), QStringLiteral("Neutral expectations"), QStringLiteral("NetCharge=0.0, Spin=none, no Spin.Fix/Spin.Total/DM.InitSpin"),
               (!spinFixEmitted && !spinTotalEmitted && !initSpinEmitted && netCharge == QStringLiteral("0.0") && spin.compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0) ? QStringLiteral("PASS") : QStringLiteral("ERROR"));
    }

    addRow(QStringLiteral("Hydrogen roles"), QStringLiteral("bottom pseudo-H list"), bottomH.isEmpty() ? QStringLiteral("-") : idsText(bottomH), bottomH.isEmpty() && m_structure.atoms.size() >= 10 ? QStringLiteral("ERROR") : QStringLiteral("PASS"));
    addRow(QStringLiteral("Hydrogen roles"), QStringLiteral("ordinary H list"), ordinaryH.isEmpty() ? QStringLiteral("-") : idsText(ordinaryH), QStringLiteral("PASS"));
    addRow(QStringLiteral("Hydrogen roles"), QStringLiteral("unknown H list"), unknownH.isEmpty() ? QStringLiteral("-") : idsText(unknownH), unknownH.isEmpty() ? QStringLiteral("PASS") : QStringLiteral("WARNING"));

    addRow(QStringLiteral("Fixed atoms"), QStringLiteral("fixed mode"), dftFixedAtomModeKey(m_settings.fixedAtomMode), QStringLiteral("PASS"));
    addRow(QStringLiteral("Fixed atoms"), QStringLiteral("fixed atom IDs"), fixedIds.isEmpty() ? QStringLiteral("-") : idsText(fixedIds), fixedIds.isEmpty() && m_structure.atoms.size() >= 10 ? QStringLiteral("ERROR") : QStringLiteral("PASS"));
    addRow(QStringLiteral("Fixed atoms"), QStringLiteral("fixed reasons"), fixedReasons.isEmpty() ? QStringLiteral("-") : fixedReasons.join(QStringLiteral(", ")), fixedReasons.isEmpty() && m_structure.atoms.size() >= 10 ? QStringLiteral("ERROR") : QStringLiteral("PASS"));
    addRow(QStringLiteral("Fixed atoms"), QStringLiteral("expected 7layer fixed atoms"), idsText(expected), expectedFixedOk ? QStringLiteral("PASS") : QStringLiteral("ERROR"));

    addRow(QStringLiteral("Numerical"), QStringLiteral("kgrid"), parameterValueOrUnset(m_settings, QStringLiteral("siesta.kpoints.kgrid")), parameterValueOrUnset(m_settings, QStringLiteral("siesta.kpoints.kgrid")) == QStringLiteral("3 3 1") ? QStringLiteral("PASS") : QStringLiteral("WARNING"));
    addRow(QStringLiteral("Numerical"), QStringLiteral("MD.VariableCell"), parameterValueOrUnset(m_settings, QStringLiteral("siesta.relaxation.MD.VariableCell")), parameterValueOrUnset(m_settings, QStringLiteral("siesta.relaxation.MD.VariableCell")).compare(QStringLiteral("F"), Qt::CaseInsensitive) == 0 ? QStringLiteral("PASS") : QStringLiteral("WARNING"));
    addRow(QStringLiteral("Numerical"), QStringLiteral("MD.Steps"), parameterValueOrUnset(m_settings, QStringLiteral("siesta.relaxation.MD.Steps")), QStringLiteral("PASS"));
    addRow(QStringLiteral("Numerical"), QStringLiteral("MD.MaxForceTol"), parameterValueOrUnset(m_settings, QStringLiteral("siesta.relaxation.MD.MaxForceTol")), QStringLiteral("PASS"));
    addRow(QStringLiteral("Numerical"), QStringLiteral("MaxSCFIterations"), parameterValueOrUnset(m_settings, QStringLiteral("siesta.scf.MaxSCFIterations")), parameterValueOrUnset(m_settings, QStringLiteral("siesta.scf.MaxSCFIterations")).toInt() >= 1000 ? QStringLiteral("PASS") : QStringLiteral("WARNING"));
    addRow(QStringLiteral("Numerical"), QStringLiteral("DM.Tolerance"), parameterValueOrUnset(m_settings, QStringLiteral("siesta.scf.DM.Tolerance")), parameterValueOrUnset(m_settings, QStringLiteral("siesta.scf.DM.Tolerance")) == QStringLiteral("unset") ? QStringLiteral("WARNING") : QStringLiteral("PASS"));
    addRow(QStringLiteral("Numerical"), QStringLiteral("DM.MixingWeight"), parameterValueOrUnset(m_settings, QStringLiteral("siesta.scf.DM.MixingWeight")), QStringLiteral("PASS"));
    addRow(QStringLiteral("Numerical"), QStringLiteral("DM.NumberPulay"), parameterValueOrUnset(m_settings, QStringLiteral("siesta.scf.DM.NumberPulay")), QStringLiteral("PASS"));
    const auto meshIt = m_settings.parameters.constFind(QStringLiteral("siesta.species.MeshCutoff"));
    const QString meshSource = meshIt == m_settings.parameters.constEnd()
        ? QStringLiteral("unset")
        : QStringLiteral("%1 (%2)").arg(meshIt->value.trimmed().isEmpty() ? QStringLiteral("unset") : meshIt->value, shortSourceText(meshIt->source));
    addRow(QStringLiteral("Numerical"), QStringLiteral("MeshCutoff source"), m_settings.includeXcFdf ? QStringLiteral("xc.fdf include; registry=%1").arg(meshSource) : meshSource, m_settings.includeXcFdf || meshSource != QStringLiteral("unset") ? QStringLiteral("PASS") : QStringLiteral("WARNING"));
    addRow(QStringLiteral("Numerical"), QStringLiteral("xc.fdf include status"), m_settings.includeXcFdf ? QStringLiteral("%include %1").arg(m_settings.xcFdfPath.trimmed().isEmpty() ? QStringLiteral("xc.fdf") : m_settings.xcFdfPath.trimmed()) : QStringLiteral("not included"), m_settings.includeXcFdf ? (m_settings.xcFdfPath.trimmed().isEmpty() ? QStringLiteral("ERROR") : QStringLiteral("PASS")) : (m_settings.standaloneInline ? QStringLiteral("PASS") : QStringLiteral("ERROR")));

    addRow(QStringLiteral("Status"), QStringLiteral("PASS / WARNING / ERROR"), QStringLiteral("%1 errors, %2 warnings").arg(generated.errors.size()).arg(generated.warnings.size()), overallStatus);

    m_overviewTable->setRowCount(rows.size());
    for (int row = 0; row < rows.size(); ++row) {
        const auto& r = rows.at(row);
        m_overviewTable->setItem(row, 0, item(r.section));
        m_overviewTable->setItem(row, 1, item(r.label));
        m_overviewTable->setItem(row, 2, item(r.value));
        m_overviewTable->setItem(row, 3, statusItem(r.status));
        if (r.status == QStringLiteral("ERROR") || r.status == QStringLiteral("WARNING")) {
            for (int col = 0; col < 3; ++col) {
                if (auto* it = m_overviewTable->item(row, col)) it->setForeground(statusBrush(r.status));
            }
        }
    }
    m_overviewTable->resizeColumnsToContents();

    if (m_checklistEdit != nullptr) {
        const auto checklist = DftInputGenerator::scientificChecklist(m_structure, m_settings, generated);
        m_checklistEdit->setPlainText(DftInputGenerator::scientificChecklistText(checklist));
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
        m_speciesTable->setColumnCount(6);
        m_speciesTable->setHorizontalHeaderLabels({"index", "element", "atomic number/Z", "label", "role", "PSF file"});
        m_speciesTable->setRowCount(m_settings.siestaSpecies.size());
        for (int row = 0; row < m_settings.siestaSpecies.size(); ++row) {
            const auto& sp = m_settings.siestaSpecies.at(row);
            m_speciesTable->setItem(row, 0, editableItem(QString::number(sp.index)));
            m_speciesTable->setItem(row, 1, editableItem(sp.element));
            m_speciesTable->setItem(row, 2, editableItem(QString::number(sp.atomicNumber)));
            m_speciesTable->setItem(row, 3, editableItem(sp.label));
            m_speciesTable->setItem(row, 4, editableItem(sp.role));
            auto* psfCombo = new QComboBox(m_speciesTable);
            psfCombo->setEditable(true);
            psfCombo->addItems(standardSiestaPsfFiles());
            if (!sp.pseudopotential.trimmed().isEmpty() && psfCombo->findText(sp.pseudopotential) < 0) {
                psfCombo->addItem(sp.pseudopotential);
            }
            psfCombo->setCurrentText(sp.pseudopotential);
            psfCombo->setToolTip(QStringLiteral("このspeciesで使うSIESTA PSFファイル名を選択します。スパコン側の実ファイル名と完全一致させてください。"));
            connect(psfCombo, &QComboBox::currentTextChanged, this, [this]() { updatePreview(); });
            m_speciesTable->setCellWidget(row, 5, psfCombo);
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
    if (m_parameterTable == nullptr && m_parameterListLayout == nullptr) return;
    updateContextLabels();
    std::unique_ptr<QSignalBlocker> tableBlocker;
    if (m_parameterTable != nullptr) tableBlocker = std::make_unique<QSignalBlocker>(m_parameterTable);
    QVector<DftParameterEntry> entries;
    for (auto it = m_settings.parameters.constBegin(); it != m_settings.parameters.constEnd(); ++it) entries << it.value();
    std::sort(entries.begin(), entries.end(), [](const DftParameterEntry& a, const DftParameterEntry& b) {
        if (a.spec.order == b.spec.order) return a.spec.id < b.spec.id;
        return a.spec.order < b.spec.order;
    });

    const bool showFullSiestaCatalog = m_settings.code == DftCode::Siesta &&
        m_parameterCatalogModeCombo != nullptr &&
        m_parameterCatalogModeCombo->currentData().toString() == QStringLiteral("siesta_full");

    if (m_parameterSectionFilter != nullptr) {
        const QSignalBlocker sectionBlocker(m_parameterSectionFilter);
        const QString pendingSectionRestore = m_parameterSectionFilter->property("aseappPendingSectionRestore").toString();
        if (!pendingSectionRestore.isEmpty()) {
            m_parameterSectionFilter->setProperty("aseappPendingSectionRestore", QString());
        }
        const QString current = !pendingSectionRestore.isEmpty()
            ? pendingSectionRestore
            : (m_parameterSectionFilter->currentText().isEmpty() ? QStringLiteral("All") : m_parameterSectionFilter->currentText());
        QSet<QString> sections;
        for (const auto& entry : std::as_const(entries)) {
            if (siestaCatalogParameter(entry) && !showFullSiestaCatalog && !activeCatalogParameter(entry)) continue;
            sections.insert(entry.spec.section);
        }
        QStringList sectionList = sections.values();
        std::sort(sectionList.begin(), sectionList.end());
        m_parameterSectionFilter->clear();
        m_parameterSectionFilter->addItem(QStringLiteral("All"));
        m_parameterSectionFilter->addItems(sectionList);
        const int sectionIndex = m_parameterSectionFilter->findText(current);
        m_parameterSectionFilter->setCurrentIndex(sectionIndex >= 0 ? sectionIndex : 0);
    }

    const QString search = m_parameterSearchEdit != nullptr ? m_parameterSearchEdit->text().trimmed() : QString();
    const QString sectionFilter = m_parameterSectionFilter != nullptr ? m_parameterSectionFilter->currentText() : QStringLiteral("All");
    if (m_parameterScopeFilter != nullptr) {
        const QSignalBlocker scopeBlocker(m_parameterScopeFilter);
        if (showFullSiestaCatalog) {
            m_parameterScopeFilter->setCurrentText(QStringLiteral("All"));
            m_parameterScopeFilter->setEnabled(false);
        } else {
            m_parameterScopeFilter->setEnabled(true);
        }
    }
    QString scopeFilter = m_parameterScopeFilter != nullptr ? m_parameterScopeFilter->currentText() : QStringLiteral("All");
    if (showFullSiestaCatalog) scopeFilter = QStringLiteral("All");
    QVector<DftParameterEntry> visibleEntries;
    for (const auto& entry : std::as_const(entries)) {
        if (siestaCatalogParameter(entry) && !showFullSiestaCatalog && !activeCatalogParameter(entry)) continue;
        if (!sectionFilter.isEmpty() && sectionFilter != QStringLiteral("All") && entry.spec.section != sectionFilter) continue;
        if (!search.isEmpty()) {
            const QString haystack = QStringList({entry.spec.id, entry.spec.label, entry.spec.section, entry.spec.key, entry.value, entry.spec.unit, entry.spec.validationRule, shortSourceText(entry.source)}).join(QLatin1Char(' '));
            if (!haystack.contains(search, Qt::CaseInsensitive)) continue;
        }
        if (scopeFilter == QStringLiteral("Basic-visible") && !basicVisibleParameter(entry, m_settings) && !activeCatalogParameter(entry)) continue;
        if (scopeFilter == QStringLiteral("Enabled only") && !entry.enabled) continue;
        if (scopeFilter == QStringLiteral("Advanced only") && !entry.spec.advanced) continue;
        visibleEntries << entry;
    }

    m_parameterValueWidgets.clear();
    m_parameterEnabledChecks.clear();
    if (m_parameterListLayout != nullptr) {
        while (QLayoutItem* child = m_parameterListLayout->takeAt(0)) {
            if (QWidget* widget = child->widget()) {
                widget->setParent(nullptr);
                delete widget;
            }
            delete child;
        }
    }

    if (m_parameterTable != nullptr) {
        m_parameterTable->clearContents();
        m_parameterTable->setRowCount(visibleEntries.size());
    }

    QString lastParameterSection;
    QGroupBox* currentSectionGroup = nullptr;
    QGridLayout* currentSectionGrid = nullptr;
    int currentSectionItem = 0;

    auto ensureSection = [&](const QString& section) {
        if (m_parameterListLayout == nullptr || m_parameterListWidget == nullptr) return;
        if (currentSectionGroup != nullptr && section == lastParameterSection) return;
        lastParameterSection = section;
        currentSectionItem = 0;
        const QString sectionTitle = section.trimmed().isEmpty() ? QStringLiteral("General") : section.trimmed();
        currentSectionGroup = new QGroupBox(sectionTitle, m_parameterListWidget);
        currentSectionGroup->setObjectName(QStringLiteral("parameterSectionGroup_") + objectNameSuffix(sectionTitle));
        currentSectionGroup->setToolTip(QStringLiteral("%1 section のParameterです。各入力欄へカーソルを合わせると詳細説明を表示します。").arg(sectionTitle));
        currentSectionGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        currentSectionGroup->setStyleSheet(QStringLiteral(
            "QGroupBox { color: #d7dde6; font-weight: 600; border: 1px solid #3f4752; border-radius: 6px; margin-top: 7px; padding-top: 5px; }"
            "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }"));
        currentSectionGrid = new QGridLayout(currentSectionGroup);
        currentSectionGrid->setContentsMargins(8, 7, 8, 6);
        currentSectionGrid->setHorizontalSpacing(10);
        currentSectionGrid->setVerticalSpacing(1);
        currentSectionGrid->setColumnStretch(0, 1);
        currentSectionGrid->setColumnStretch(1, 1);
        m_parameterListLayout->addWidget(currentSectionGroup);
    };

    for (int row = 0; row < visibleEntries.size(); ++row) {
        const auto& entry = visibleEntries.at(row);
        const QString tooltip = parameterTooltip(entry);
        const QString title = entry.spec.label.trimmed().isEmpty() ? entry.spec.id : entry.spec.label.trimmed();
        const QString suffix = objectNameSuffix(entry.spec.id);

        if (m_parameterListLayout != nullptr && m_parameterListWidget != nullptr) {
            ensureSection(entry.spec.section);

            auto* pairWidget = new QWidget(currentSectionGroup);
            pairWidget->setObjectName(QStringLiteral("parameterPair_") + suffix);
            pairWidget->setToolTip(tooltip);
            pairWidget->setMinimumHeight(26);
            pairWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            pairWidget->setStyleSheet(QStringLiteral(
                "QWidget#%1 { border-bottom: 1px solid #313840; }"
            ).arg(pairWidget->objectName()));
            auto* pairLayout = new QHBoxLayout(pairWidget);
            pairLayout->setContentsMargins(3, 0, 3, 0);
            pairLayout->setSpacing(5);

            auto* enabled = new QCheckBox(pairWidget);
            enabled->setObjectName(QStringLiteral("parameterEnabled_") + suffix);
            enabled->setChecked(entry.enabled);
            enabled->setFixedWidth(20);
            enabled->setAccessibleName(QStringLiteral("Output %1").arg(title));
            enabled->setToolTip(QStringLiteral("%1\n\nチェックON: 生成ファイルへこのParameterを出力します。\nチェックOFF: このParameterを出力しません。").arg(tooltip));
            connect(enabled, &QCheckBox::toggled, this, [this]() { updatePreview(); });

            auto* label = new QLabel(title, pairWidget);
            label->setObjectName(QStringLiteral("parameterLabel_") + suffix);
            label->setTextInteractionFlags(Qt::TextSelectableByMouse);
            label->setToolTip(tooltip);
            label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            label->setMinimumWidth(112);
            label->setMaximumWidth(178);
            label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

            QWidget* valueWidget = nullptr;
            if (entry.spec.outputFormat == QStringLiteral("block")) {
                auto* value = new QTextEdit(pairWidget);
                value->setPlainText(entry.value);
                value->setAcceptRichText(false);
                value->setLineWrapMode(QTextEdit::NoWrap);
                value->setFixedHeight(58);
                value->setToolTip(tooltip);
                value->setPlaceholderText(QStringLiteral("block rows"));
                connect(value, &QTextEdit::textChanged, this, [this]() { updatePreview(); });
                valueWidget = value;
            } else if (!entry.spec.allowedValues.isEmpty()) {
                auto* value = new QComboBox(pairWidget);
                value->addItems(entry.spec.allowedValues);
                if (value->findText(entry.value) < 0) value->addItem(entry.value);
                for (int i = 0; i < value->count(); ++i) {
                    value->setItemData(i, comboOptionTooltip(entry, value->itemText(i)), Qt::ToolTipRole);
                }
                value->setCurrentText(entry.value);
                value->setToolTip(tooltip);
                connect(value, &QComboBox::currentTextChanged, this, [this](const QString&) { updatePreview(); });
                valueWidget = value;
            } else {
                auto* value = new QLineEdit(entry.value, pairWidget);
                value->setToolTip(tooltip);
                value->setPlaceholderText(entry.spec.defaultByVersion.trimmed().isEmpty() ? entry.spec.type : entry.spec.defaultByVersion);
                connect(value, &QLineEdit::textChanged, this, [this](const QString&) { updatePreview(); });
                valueWidget = value;
            }
            valueWidget->setObjectName(QStringLiteral("parameterValue_") + suffix);
            valueWidget->setMinimumWidth(150);
            valueWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

            auto* sourceLabel = new QLabel(shortSourceText(entry.source), pairWidget);
            sourceLabel->setObjectName(QStringLiteral("parameterSource_") + suffix);
            sourceLabel->setToolTip(QStringLiteral("%1\n\nsource=%2").arg(tooltip, sourceText(entry.source)));
            sourceLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            sourceLabel->setFixedWidth(54);
            sourceLabel->setStyleSheet(QStringLiteral("color: #7f8fa3; font-size: 11px;"));

            const bool subdued = !entry.enabled || entry.source == DftParameterSource::Unknown;
            if (subdued) {
                label->setStyleSheet(QStringLiteral("color: #8a8f98;"));
                sourceLabel->setStyleSheet(QStringLiteral("color: #6f7782; font-size: 11px;"));
            }

            m_parameterEnabledChecks.insert(entry.spec.id, enabled);
            m_parameterValueWidgets.insert(entry.spec.id, valueWidget);
            pairLayout->addWidget(enabled);
            pairLayout->addWidget(label);
            pairLayout->addWidget(valueWidget, 1);
            pairLayout->addWidget(sourceLabel);

            const int gridRow = currentSectionItem / 2;
            const int gridCol = currentSectionItem % 2;
            currentSectionGrid->addWidget(pairWidget, gridRow, gridCol);
            ++currentSectionItem;
        }

        if (m_parameterTable == nullptr) continue;
        auto* enabled = new QCheckBox(m_parameterTable);
        enabled->setText(entry.enabled ? QStringLiteral("on") : QStringLiteral("off"));
        enabled->setChecked(entry.enabled);
        enabled->setToolTip(tooltip);
        connect(enabled, &QCheckBox::toggled, this, [this]() { updatePreview(); });
        m_parameterTable->setCellWidget(row, 0, enabled);
        m_parameterTable->setItem(row, 1, item(entry.spec.id));
        m_parameterTable->item(row, 1)->setData(Qt::UserRole, entry.spec.id);
        m_parameterTable->setItem(row, 2, item(entry.spec.label));
        m_parameterTable->setItem(row, 3, item(entry.spec.section));
        if (entry.spec.outputFormat == QStringLiteral("block")) {
            auto* value = new QTextEdit(m_parameterTable);
            value->setPlainText(entry.value);
            value->setAcceptRichText(false);
            value->setLineWrapMode(QTextEdit::NoWrap);
            value->setFixedHeight(58);
            value->setToolTip(tooltip);
            connect(value, &QTextEdit::textChanged, this, [this]() { updatePreview(); });
            m_parameterTable->setCellWidget(row, 4, value);
        } else if (!entry.spec.allowedValues.isEmpty()) {
            auto* value = new QComboBox(m_parameterTable);
            value->addItems(entry.spec.allowedValues);
            if (value->findText(entry.value) < 0) value->addItem(entry.value);
            for (int i = 0; i < value->count(); ++i) value->setItemData(i, comboOptionTooltip(entry, value->itemText(i)), Qt::ToolTipRole);
            value->setCurrentText(entry.value);
            value->setToolTip(tooltip);
            connect(value, &QComboBox::currentTextChanged, this, [this](const QString&) { updatePreview(); });
            m_parameterTable->setCellWidget(row, 4, value);
        } else {
            auto* value = new QLineEdit(entry.value, m_parameterTable);
            value->setToolTip(tooltip);
            connect(value, &QLineEdit::textChanged, this, [this](const QString&) { updatePreview(); });
            m_parameterTable->setCellWidget(row, 4, value);
        }
        m_parameterTable->setItem(row, 5, item(shortSourceText(entry.source)));
        m_parameterTable->setItem(row, 6, item(entry.spec.unit));
        const bool subdued = !entry.enabled || entry.source == DftParameterSource::Unknown;
        for (int col = 1; col < m_parameterTable->columnCount(); ++col) {
            if (auto* tableItem = m_parameterTable->item(row, col)) {
                tableItem->setToolTip(tooltip);
                if (subdued) tableItem->setForeground(QBrush(QColor(120, 120, 120)));
            }
        }
    }

    if (m_parameterListLayout != nullptr) {
        if (visibleEntries.isEmpty()) {
            auto* empty = new QLabel(QStringLiteral("No parameters match the current filters."), m_parameterListWidget);
            empty->setAlignment(Qt::AlignCenter);
            empty->setToolTip(QStringLiteral("Search、Section、Scopeの条件を緩めるとParameterが表示されます。"));
            m_parameterListLayout->addWidget(empty);
        }
        m_parameterListLayout->activate();
        if (m_parameterListWidget != nullptr && m_parameterScrollArea != nullptr) {
            m_parameterListWidget->adjustSize();
            if (showFullSiestaCatalog) {
                m_parameterScrollArea->setMinimumHeight(160);
                m_parameterScrollArea->setMaximumHeight(QWIDGETSIZE_MAX);
            } else {
                const int contentHeight = std::max(72, m_parameterListWidget->sizeHint().height());
                const int frameHeight = (m_parameterScrollArea->frameWidth() * 2) + 2;
                const int compactMaximumHeight = contentHeight + frameHeight;
                m_parameterScrollArea->setMinimumHeight(std::min(compactMaximumHeight, 96));
                m_parameterScrollArea->setMaximumHeight(compactMaximumHeight);
            }
            if (auto* bar = m_parameterScrollArea->verticalScrollBar()) {
                bar->setValue(0);
            }
            if (auto* bar = m_parameterScrollArea->horizontalScrollBar()) {
                bar->setValue(0);
            }
            m_parameterScrollArea->updateGeometry();
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
                if (auto* psfCombo = qobject_cast<QComboBox*>(m_speciesTable->cellWidget(row, 5))) {
                    sp.pseudopotential = psfCombo->currentText().trimmed();
                } else {
                    sp.pseudopotential = m_speciesTable->item(row, 5) != nullptr ? m_speciesTable->item(row, 5)->text().trimmed() : QString();
                }
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

    bool collectedParameterCards = false;
    for (auto it = m_settings.parameters.begin(); it != m_settings.parameters.end(); ++it) {
        const QString id = it.key();
        bool touched = false;
        if (auto* enabled = m_parameterEnabledChecks.value(id, nullptr)) {
            it->enabled = enabled->isChecked();
            touched = true;
        }
        if (auto* value = qobject_cast<QLineEdit*>(m_parameterValueWidgets.value(id, nullptr))) {
            if (it->value != value->text()) {
                it->value = value->text();
                it->source = DftParameterSource::UserOverride;
                it->enabled = it->enabled || !it->value.trimmed().isEmpty();
            }
            touched = true;
        } else if (auto* value = qobject_cast<QTextEdit*>(m_parameterValueWidgets.value(id, nullptr))) {
            if (it->value != value->toPlainText()) {
                it->value = value->toPlainText();
                it->source = DftParameterSource::UserOverride;
                it->enabled = it->enabled || !it->value.trimmed().isEmpty();
            }
            touched = true;
        } else if (auto* value = qobject_cast<QComboBox*>(m_parameterValueWidgets.value(id, nullptr))) {
            if (it->value != value->currentText()) {
                it->value = value->currentText();
                it->source = DftParameterSource::UserOverride;
                it->enabled = it->enabled || !it->value.trimmed().isEmpty();
            }
            touched = true;
        }
        collectedParameterCards = collectedParameterCards || touched;
    }

    if (!collectedParameterCards && m_parameterTable != nullptr) {
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
            } else if (auto* value = qobject_cast<QTextEdit*>(m_parameterTable->cellWidget(row, 4))) {
                if (it->value != value->toPlainText()) {
                    it->value = value->toPlainText();
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
    updateContextLabels();
    refreshSourceSummary();
    m_generated = DftInputGenerator::generate(m_structure, m_settings);
    const auto checklist = DftInputGenerator::scientificChecklist(m_structure, m_settings, m_generated);
    bool checklistHasError = false;
    bool checklistHasWarning = false;
    QStringList checklistErrors;
    QStringList checklistWarnings;
    for (const auto& item : checklist) {
        const QString line = QStringLiteral("%1 / %2: %3").arg(item.group, item.item, item.detail);
        if (item.status == QStringLiteral("ERROR")) {
            checklistHasError = true;
            checklistErrors << line;
        } else if (item.status == QStringLiteral("WARNING")) {
            checklistHasWarning = true;
            checklistWarnings << line;
        }
    }

    const QString overallStatus = (!m_generated.errors.isEmpty() || checklistHasError)
        ? QStringLiteral("ERROR")
        : ((!m_generated.warnings.isEmpty() || checklistHasWarning) ? QStringLiteral("WARNING") : QStringLiteral("PASS"));
    const QString inputKind = m_settings.code == DftCode::Siesta ? QStringLiteral("FDF") : QStringLiteral("Input");
    QStringList preview;
    preview << QStringLiteral("Summary:")
            << QStringLiteral("  status: %1").arg(overallStatus)
            << QStringLiteral("  target: %1").arg(m_settings.targetName)
            << QStringLiteral("  generated file name: %1").arg(generatedFileName(m_settings, m_generated))
            << QStringLiteral("  companion files required: %1").arg(orderedCompanionFiles(m_generated.requiredCompanionFiles))
            << QString();
    preview << QStringLiteral("Scientific Checklist:");
    const QString checklistText = DftInputGenerator::scientificChecklistText(checklist);
    preview << (checklistText.isEmpty() ? QStringLiteral("-") : checklistText)
            << QString()
            << QStringLiteral("--- %1 ---").arg(inputKind)
            << m_generated.primaryText;
    m_previewEdit->setPlainText(preview.join('\n'));

    QStringList status;
    if (!m_generated.ok || checklistHasError) {
        status << QStringLiteral("ERROR: export is blocked until generator errors are fixed; checklist errors are shown below.");
    } else if (checklistHasWarning || !m_generated.warnings.isEmpty()) {
        status << QStringLiteral("WARNING: export is possible, but review warnings before running DFT.");
    } else {
        status << QStringLiteral("OK: primary input has no explanatory comments and checklist is PASS.");
    }
    if (!m_generated.errors.isEmpty()) {
        status << QStringLiteral("Errors:");
        for (const auto& e : m_generated.errors) status << QStringLiteral("- %1").arg(e);
    }
    if (!checklistErrors.isEmpty()) {
        status << QStringLiteral("Checklist errors:");
        for (const auto& e : checklistErrors) status << QStringLiteral("- %1").arg(e);
    }
    if (!m_generated.warnings.isEmpty()) {
        status << QStringLiteral("Warnings:");
        for (const auto& w : m_generated.warnings) status << QStringLiteral("- %1").arg(w);
    }
    if (!checklistWarnings.isEmpty()) {
        status << QStringLiteral("Checklist warnings:");
        for (const auto& w : checklistWarnings) status << QStringLiteral("- %1").arg(w);
    }
    if (!m_generated.requiredCompanionFiles.isEmpty()) {
        status << QStringLiteral("Required companion files: %1").arg(orderedCompanionFiles(m_generated.requiredCompanionFiles));
        if (m_settings.code == DftCode::Siesta) {
            status << QStringLiteral("Run-ready check: include mode exports only the main FDF; an existing xc.fdf must already be in the same run directory. PSF files are checked relative to the export directory (default: %1). Standalone mode embeds xc.fdf blocks in the primary FDF.").arg(m_settings.pseudoDir.isEmpty() ? QStringLiteral("../potential") : m_settings.pseudoDir);
        }
    }
    m_warningEdit->setPlainText(status.join('\n'));
    if (m_exportButton != nullptr) {
        const bool canExport = m_generated.ok && !checklistHasError;
        m_exportButton->setEnabled(canExport);
        m_exportButton->setToolTip(canExport
            ? QStringLiteral("対象のprimary FDF/inputだけを書き出します。include modeではxc.fdfは出力しないため、実行時は同じディレクトリに既存xc.fdfが必要です。Batch modeでは選択した全構造を一括生成します。")
            : QStringLiteral("検証FAILがあるため書き出しを無効化しています。上のErrors/Checklist errorsを修正してください。"));
    }
    refreshOverviewTab();
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
    const QString sourceMode = m_structureSourceCombo != nullptr ? m_structureSourceCombo->currentData().toString() : QStringLiteral("current");
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("DFT入力ファイルの出力先"));
    if (dir.isEmpty()) return;

    auto confirmOverwrite = [this](const QStringList& paths) -> bool {
        if (paths.isEmpty()) return true;
        QStringList preview;
        for (int i = 0; i < paths.size() && i < 8; ++i) preview << QDir::toNativeSeparators(paths.at(i));
        if (paths.size() > preview.size()) preview << QStringLiteral("... and %1 more").arg(paths.size() - preview.size());
        return QMessageBox::question(this, QStringLiteral("Overwrite confirmation"),
            QStringLiteral("以下の既存ファイルを上書きします。続行しますか？\n%1").arg(preview.join(QLatin1Char('\n'))),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
    };

    if (sourceMode == QStringLiteral("batch") && !m_batchSourcePaths.isEmpty()) {
        if (m_batchResultTable != nullptr) m_batchResultTable->setRowCount(0);

        QStringList overwritePaths;
        for (const QString& path : std::as_const(m_batchSourcePaths)) {
            StructureData structure;
            QString loadError;
            if (!loadStructurePath(path, &structure, &loadError)) continue;
            const QString target = targetNameForStructureSource(path, structure);
            const DftSettings settings = settingsForStructure(structure, target);
            const DftGeneratedInput generated = DftInputGenerator::generate(structure, settings);
            const QString primaryPath = QDir(dir).filePath(DftInputGenerator::sanitizeTargetName(settings.targetName) + generated.fileExtension);
            if (QFileInfo::exists(primaryPath)) overwritePaths << primaryPath;
        }
        if (!confirmOverwrite(overwritePaths)) return;

        int passCount = 0;
        int warnCount = 0;
        int failCount = 0;
        for (const QString& path : std::as_const(m_batchSourcePaths)) {
            StructureData structure;
            QString message;
            QString status = QStringLiteral("FAIL");
            QString target = QFileInfo(path).completeBaseName();
            DftGeneratedInput generated;
            if (!loadStructurePath(path, &structure, &message)) {
                ++failCount;
            } else {
                target = targetNameForStructureSource(path, structure);
                const DftSettings settings = settingsForStructure(structure, target);
                if (exportSingleGeneratedFile(dir, structure, settings, &message, &generated)) {
                    const auto checklist = DftInputGenerator::scientificChecklist(structure, settings, generated);
                    bool hasWarning = !generated.warnings.isEmpty();
                    for (const auto& item : checklist) hasWarning = hasWarning || item.status == QStringLiteral("WARNING");
                    status = hasWarning ? QStringLiteral("WARN") : QStringLiteral("PASS");
                    message = hasWarning ? QStringLiteral("generated with warnings; review preview/checks") : QStringLiteral("generated");
                    if (hasWarning) ++warnCount; else ++passCount;
                } else {
                    ++failCount;
                }
            }
            if (m_batchResultTable != nullptr) {
                const int row = m_batchResultTable->rowCount();
                m_batchResultTable->insertRow(row);
                m_batchResultTable->setItem(row, 0, item(QDir::toNativeSeparators(path)));
                m_batchResultTable->setItem(row, 1, item(target));
                m_batchResultTable->setItem(row, 2, statusItem(status == QStringLiteral("FAIL") ? QStringLiteral("ERROR") : (status == QStringLiteral("WARN") ? QStringLiteral("WARNING") : QStringLiteral("PASS"))));
                m_batchResultTable->setItem(row, 3, item(message));
            }
        }
        QMessageBox::information(this, QStringLiteral("DFT入力生成"),
            QStringLiteral("Batch出力が完了しました。\n出力先: %1\nPASS=%2 / WARN=%3 / FAIL=%4")
                .arg(QDir::toNativeSeparators(dir)).arg(passCount).arg(warnCount).arg(failCount));
        refreshSourceSummary();
        updatePreview();
        return;
    }

    m_generated = DftInputGenerator::generate(m_structure, m_settings);
    const QString primaryPath = QDir(dir).filePath(DftInputGenerator::sanitizeTargetName(m_settings.targetName) + m_generated.fileExtension);
    if (!confirmOverwrite(QFileInfo::exists(primaryPath) ? QStringList{primaryPath} : QStringList{})) return;

    QString error;
    if (!exportSingleGeneratedFile(dir, m_structure, m_settings, &error, &m_generated)) {
        QMessageBox::warning(this, QStringLiteral("DFT入力生成"), QStringLiteral("エラーがあるため出力しません。\n%1").arg(error));
        updatePreview();
        return;
    }
    QStringList message;
    message << QStringLiteral("出力しました:")
            << dir;
    if (m_settings.code == DftCode::Siesta && !m_generated.requiredCompanionFiles.isEmpty()) {
        QStringList missing;
        for (const QString& file : std::as_const(m_generated.requiredCompanionFiles)) {
            const QString normalized = file.trimmed();
            const QString path = QFileInfo(normalized).isAbsolute()
                ? normalized
                : QDir(dir).filePath(normalized);
            if (!normalized.isEmpty() && !QFileInfo::exists(path) && !missing.contains(normalized)) missing << normalized;
        }
        if (missing.isEmpty()) {
            message << QStringLiteral("Run-ready: OK（必要なcompanion fileが揃っています）");
        } else {
            message << QStringLiteral("Run-ready: 未完了（不足: %1）").arg(missing.join(QStringLiteral(", ")))
                    << QStringLiteral("include modeでは対象FDFだけを出力します。既存xc.fdfをmain FDFと同じ実行ディレクトリへ置き、psfファイルはSIESTA実行前に指定PSF directory（通常 ../potential）へ置いてください。");
        }
    }
    QMessageBox::information(this, QStringLiteral("DFT入力生成"), message.join('\n'));
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
