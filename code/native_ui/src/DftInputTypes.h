#pragma once

#include <QJsonObject>
#include <QMap>
#include <QMetaType>
#include <QVector>
#include <QString>
#include <QStringList>
#include <QVector3D>

#include <array>

// DFT入力生成では「既知パラメータ」と「未知/rawパラメータ」を同じUIで扱うため、
// sourceを明示してユーザーが値の由来を確認できるようにする。
enum class DftCode {
    Siesta,
    QuantumEspresso,
};

enum class DftGenerationMode {
    Manual,
    Profile,
    ImportEdit,
};

enum class DftParameterSource {
    VersionDefault,
    ProjectProfile,
    ImportedFile,
    ImportedFdfLog,
    ImportedQeIn,
    UserOverride,
    Unknown,
};

enum class DftHydrogenRole {
    OrdinaryHydrogen,
    BottomPseudoHNTerminated075,
    BottomPseudoHIIITerminated125,
    SurfaceAdsorbedHydrogen,
    MoleculeH2Hydrogen,
    UnknownHydrogen,
};

enum class DftFixedAtomMode {
    PreserveImportedFlags,
    FixBottomPseudoHOnly,
    FixBottomPseudoHPlusBottomMolecularLayer,
    ManualOnly,
};

enum class DftTrailingFlagInterpretation {
    PreserveOrIgnoreUnknown,
    IgnoreTrailingFlags,
    NumericOneMeansFixed,
    NumericOneMeansMovable,
    VaspSelectiveDynamics,
    CustomMapping,
};

struct DftParameterSpec {
    QString id;
    DftCode code = DftCode::Siesta;
    QStringList versionSupport;
    QString executableOrSchema;
    QString section;
    QString key;
    QString label;
    QString type;
    QString unit;
    QString defaultByVersion;
    QString projectDefault;
    QStringList allowedValues;
    QString minValue;
    QString maxValue;
    bool required = false;
    bool advanced = false;
    QString tooltipShort;
    QString tooltipLong;
    QString outputFormat;
    QString validationRule;
    int order = 0;
};

struct DftParameterEntry {
    DftParameterSpec spec;
    QString value;
    QString importedValue;
    DftParameterSource source = DftParameterSource::Unknown;
    bool enabled = true;
};

struct DftRawParameter {
    DftCode code = DftCode::Siesta;
    QString namelistOrBlock;
    QString key;
    QString value;
    QString unit;
    QString outputPosition;
    bool blockOrCard = false;
    bool enabled = true;
};

struct DftSiestaSpecies {
    int index = 0;
    int atomicNumber = 0;
    QString label;
    QString element;
    QString role;
    QString pseudopotential;
};

struct DftQeSpecies {
    QString label;
    QString element;
    double mass = 1.0;
    QString pseudoFile;
    QString role;
    DftParameterSource source = DftParameterSource::ProjectProfile;
};

struct DftHydrogenAssignment {
    int atomIndex = -1; // 0-based; output files use the current atom order.
    QVector3D cartesian;
    QVector3D fractional;
    int nearestNonHAtomIndex = -1;
    QString nearestNonHElement;
    double nearestNonHDistanceAng = 0.0;
    DftHydrogenRole inferredRole = DftHydrogenRole::UnknownHydrogen;
    DftHydrogenRole selectedRole = DftHydrogenRole::UnknownHydrogen;
    QString inferenceSource;
    QString confidence;
    QString siestaSpecies;
    int siestaSpeciesIndex = 0;
    QString qeLabel;
    QString qePseudoFile;
    bool fixedByRole = false;
    bool userOverrodeInference = false;
    QString warning;
};

struct DftSettings {
    DftCode code = DftCode::Siesta;
    QString version = QStringLiteral("4.1.5");
    QString executable = QStringLiteral("siesta");
    QString schema = QStringLiteral("siesta_fdf_4_1");
    DftGenerationMode generationMode = DftGenerationMode::Manual;
    QString profileName = QStringLiteral("Manual");
    QString targetName = QStringLiteral("ideal");
    QString calculationMode = QStringLiteral("neutral_slab");
    QString moduleName = QStringLiteral("siesta/4.1.5-mpi");
    bool includeXcFdf = true;
    bool standaloneInline = false;
    QString xcFdfPath = QStringLiteral("xc_500.fdf");
    QString pseudoDir = QStringLiteral("./qe_pp/");
    QString outDirPattern = QStringLiteral("./out/<target>");
    bool qeProjectStyleFixedFlags = true;
    bool qeAssumeIsolated = false;
    bool allowUnknownHydrogen = false;
    DftFixedAtomMode fixedAtomMode = DftFixedAtomMode::PreserveImportedFlags;
    DftTrailingFlagInterpretation trailingFlagInterpretation = DftTrailingFlagInterpretation::PreserveOrIgnoreUnknown;
    QString sourceStructurePath;
    QMap<QString, DftParameterEntry> parameters;
    QVector<DftRawParameter> rawParameters;
    QVector<DftSiestaSpecies> siestaSpecies;
    QVector<DftQeSpecies> qeSpecies;
    QVector<DftHydrogenAssignment> hydrogenAssignments;
};

struct DftGeneratedInput {
    bool ok = false;
    QString fileExtension;
    QString primaryText;
    QString jsonSummary;
    QString markdownSummary;
    QJsonObject summaryObject;
    QStringList warnings;
    QStringList errors;
    QStringList requiredCompanionFiles;
};

struct DftScientificChecklistItem {
    QString group;
    QString item;
    QString status;
    QString detail;
};

inline QString dftCodeToString(DftCode code) {
    return code == DftCode::Siesta ? QStringLiteral("SIESTA") : QStringLiteral("Quantum ESPRESSO");
}

inline QString dftCodeKey(DftCode code) {
    return code == DftCode::Siesta ? QStringLiteral("siesta") : QStringLiteral("qe");
}

inline DftCode dftCodeFromIndex(int index) {
    return index == 1 ? DftCode::QuantumEspresso : DftCode::Siesta;
}

inline QString dftGenerationModeKey(DftGenerationMode mode) {
    switch (mode) {
    case DftGenerationMode::Manual: return QStringLiteral("manual");
    case DftGenerationMode::Profile: return QStringLiteral("profile");
    case DftGenerationMode::ImportEdit: return QStringLiteral("import_edit");
    }
    return QStringLiteral("manual");
}

inline QString dftParameterSourceKey(DftParameterSource source) {
    switch (source) {
    case DftParameterSource::VersionDefault: return QStringLiteral("version_default");
    case DftParameterSource::ProjectProfile: return QStringLiteral("project_profile");
    case DftParameterSource::ImportedFile: return QStringLiteral("imported_file");
    case DftParameterSource::ImportedFdfLog: return QStringLiteral("imported_fdf_log");
    case DftParameterSource::ImportedQeIn: return QStringLiteral("imported_qe_in");
    case DftParameterSource::UserOverride: return QStringLiteral("user_override");
    case DftParameterSource::Unknown: return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

inline DftParameterSource dftParameterSourceFromKey(const QString& key) {
    const QString normalized = key.trimmed().toLower();
    if (normalized == QStringLiteral("version_default")) return DftParameterSource::VersionDefault;
    if (normalized == QStringLiteral("project_profile")) return DftParameterSource::ProjectProfile;
    if (normalized == QStringLiteral("imported_file")) return DftParameterSource::ImportedFile;
    if (normalized == QStringLiteral("imported_fdf_log")) return DftParameterSource::ImportedFdfLog;
    if (normalized == QStringLiteral("imported_qe_in")) return DftParameterSource::ImportedQeIn;
    if (normalized == QStringLiteral("user_override")) return DftParameterSource::UserOverride;
    return DftParameterSource::Unknown;
}

inline QString dftHydrogenRoleKey(DftHydrogenRole role) {
    switch (role) {
    case DftHydrogenRole::OrdinaryHydrogen: return QStringLiteral("ordinary_hydrogen");
    case DftHydrogenRole::BottomPseudoHNTerminated075: return QStringLiteral("bottom_pseudo_h_n_terminated_0p75");
    case DftHydrogenRole::BottomPseudoHIIITerminated125: return QStringLiteral("bottom_pseudo_h_iii_terminated_1p25");
    case DftHydrogenRole::SurfaceAdsorbedHydrogen: return QStringLiteral("surface_adsorbed_hydrogen");
    case DftHydrogenRole::MoleculeH2Hydrogen: return QStringLiteral("molecule_h2_hydrogen");
    case DftHydrogenRole::UnknownHydrogen: return QStringLiteral("unknown_hydrogen");
    }
    return QStringLiteral("unknown_hydrogen");
}

inline QString dftFixedAtomModeKey(DftFixedAtomMode mode) {
    switch (mode) {
    case DftFixedAtomMode::PreserveImportedFlags: return QStringLiteral("preserve_imported_flags");
    case DftFixedAtomMode::FixBottomPseudoHOnly: return QStringLiteral("bottom_pseudo_h_only");
    case DftFixedAtomMode::FixBottomPseudoHPlusBottomMolecularLayer: return QStringLiteral("bottom_pseudo_h_plus_bottom_molecular_layer");
    case DftFixedAtomMode::ManualOnly: return QStringLiteral("manual_only");
    }
    return QStringLiteral("preserve_imported_flags");
}

inline DftFixedAtomMode dftFixedAtomModeFromKey(const QString& key) {
    const QString normalized = key.trimmed().toLower();
    if (normalized == QStringLiteral("bottom_pseudo_h_only")) return DftFixedAtomMode::FixBottomPseudoHOnly;
    if (normalized == QStringLiteral("bottom_pseudo_h_plus_bottom_molecular_layer")) return DftFixedAtomMode::FixBottomPseudoHPlusBottomMolecularLayer;
    if (normalized == QStringLiteral("manual_only")) return DftFixedAtomMode::ManualOnly;
    return DftFixedAtomMode::PreserveImportedFlags;
}

inline QStringList dftFixedAtomModeKeys() {
    return {
        QStringLiteral("preserve_imported_flags"),
        QStringLiteral("bottom_pseudo_h_only"),
        QStringLiteral("bottom_pseudo_h_plus_bottom_molecular_layer"),
        QStringLiteral("manual_only"),
    };
}

inline QString dftTrailingFlagInterpretationKey(DftTrailingFlagInterpretation interpretation) {
    switch (interpretation) {
    case DftTrailingFlagInterpretation::PreserveOrIgnoreUnknown: return QStringLiteral("preserve_or_ignore_unknown");
    case DftTrailingFlagInterpretation::IgnoreTrailingFlags: return QStringLiteral("ignore_trailing_flags");
    case DftTrailingFlagInterpretation::NumericOneMeansFixed: return QStringLiteral("numeric_one_means_fixed");
    case DftTrailingFlagInterpretation::NumericOneMeansMovable: return QStringLiteral("numeric_one_means_movable");
    case DftTrailingFlagInterpretation::VaspSelectiveDynamics: return QStringLiteral("vasp_selective_dynamics");
    case DftTrailingFlagInterpretation::CustomMapping: return QStringLiteral("custom_mapping");
    }
    return QStringLiteral("preserve_or_ignore_unknown");
}

inline DftTrailingFlagInterpretation dftTrailingFlagInterpretationFromKey(const QString& key) {
    const QString normalized = key.trimmed().toLower();
    if (normalized == QStringLiteral("ignore_trailing_flags")) return DftTrailingFlagInterpretation::IgnoreTrailingFlags;
    if (normalized == QStringLiteral("numeric_one_means_fixed") || normalized == QStringLiteral("1_1_1_means_fixed")) return DftTrailingFlagInterpretation::NumericOneMeansFixed;
    if (normalized == QStringLiteral("numeric_one_means_movable") || normalized == QStringLiteral("1_1_1_means_movable")) return DftTrailingFlagInterpretation::NumericOneMeansMovable;
    if (normalized == QStringLiteral("vasp_selective_dynamics")) return DftTrailingFlagInterpretation::VaspSelectiveDynamics;
    if (normalized == QStringLiteral("custom_mapping")) return DftTrailingFlagInterpretation::CustomMapping;
    return DftTrailingFlagInterpretation::PreserveOrIgnoreUnknown;
}

inline QStringList dftTrailingFlagInterpretationKeys() {
    return {
        QStringLiteral("preserve_or_ignore_unknown"),
        QStringLiteral("ignore_trailing_flags"),
        QStringLiteral("numeric_one_means_fixed"),
        QStringLiteral("numeric_one_means_movable"),
        QStringLiteral("vasp_selective_dynamics"),
        QStringLiteral("custom_mapping"),
    };
}

inline DftHydrogenRole dftHydrogenRoleFromKey(const QString& key) {
    const QString normalized = key.trimmed().toLower();
    if (normalized == QStringLiteral("ordinary_hydrogen")) return DftHydrogenRole::OrdinaryHydrogen;
    if (normalized == QStringLiteral("bottom_pseudo_h_n_terminated_0p75")) return DftHydrogenRole::BottomPseudoHNTerminated075;
    if (normalized == QStringLiteral("bottom_pseudo_h_iii_terminated_1p25")) return DftHydrogenRole::BottomPseudoHIIITerminated125;
    if (normalized == QStringLiteral("surface_adsorbed_hydrogen")) return DftHydrogenRole::SurfaceAdsorbedHydrogen;
    if (normalized == QStringLiteral("molecule_h2_hydrogen")) return DftHydrogenRole::MoleculeH2Hydrogen;
    return DftHydrogenRole::UnknownHydrogen;
}

inline QStringList dftHydrogenRoleKeys() {
    return {
        QStringLiteral("ordinary_hydrogen"),
        QStringLiteral("bottom_pseudo_h_n_terminated_0p75"),
        QStringLiteral("bottom_pseudo_h_iii_terminated_1p25"),
        QStringLiteral("surface_adsorbed_hydrogen"),
        QStringLiteral("molecule_h2_hydrogen"),
        QStringLiteral("unknown_hydrogen"),
    };
}
