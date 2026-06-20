#include "DftParameterRegistry.h"

namespace {

DftParameterSpec spec(DftCode code, const QString& section, const QString& key,
                      const QString& label, const QString& type, const QString& unit,
                      const QString& value, const QString& tip, int order,
                      bool required = false, bool advanced = false,
                      const QStringList& allowed = QStringList()) {
    DftParameterSpec s;
    s.code = code;
    s.section = section;
    s.key = key;
    s.id = dftCodeKey(code) + QStringLiteral(".") + section + QStringLiteral(".") + key;
    s.label = label;
    s.type = type;
    s.unit = unit;
    s.projectDefault = value;
    s.allowedValues = allowed;
    s.required = required;
    s.advanced = advanced;
    s.tooltipShort = tip;
    s.tooltipLong = tip;
    s.outputFormat = QStringLiteral("scalar");
    s.order = order;
    return s;
}

DftParameterEntry entry(const DftParameterSpec& s, DftParameterSource source) {
    DftParameterEntry e;
    e.spec = s;
    e.value = s.projectDefault;
    e.source = e.value.isEmpty() ? DftParameterSource::Unknown : source;
    e.enabled = !e.value.isEmpty() || s.required;
    return e;
}

void addSpecs(DftSettings* settings) {
    const auto specs = DftParameterRegistry::specsForCode(settings->code, settings->version);
    for (const auto& s : specs) settings->parameters.insert(s.id, entry(s, DftParameterSource::ProjectProfile));
}

} // namespace

QVector<DftParameterSpec> DftParameterRegistry::specsForCode(DftCode code, const QString& version) {
    Q_UNUSED(version);
    QVector<DftParameterSpec> out;
    int o = 0;
    const QStringList precisionValues = {QStringLiteral("6"), QStringLiteral("8"), QStringLiteral("10"), QStringLiteral("12"), QStringLiteral("16")};
    if (code == DftCode::Siesta) {
        out << spec(code, "general", "SystemName", "SystemName", "string", "", "ideal", "SIESTAのSystemName。", o++, true);
        out << spec(code, "general", "SystemLabel", "SystemLabel", "string", "", "ideal", "SIESTAのSystemLabel。target nameと同期します。", o++, true);
        out << spec(code, "charge_spin", "NetCharge", "NetCharge", "double", "", "0.0", "SIESTA計算セル全体の電荷。0が中性。負値は電子追加、正値は電子除去。", o++);
        out << spec(code, "charge_spin", "Spin", "Spin", "choice", "", "none", "中性slabではnone、荷電slabやGa atomではpolarized。", o++, false, false, {"none", "polarized"});
        out << spec(code, "charge_spin", "Spin.Fix", "Spin.Fix", "bool", "", "F", "Spin.Totalを固定するかどうか。", o++, false, true, {"T", "F"});
        out << spec(code, "charge_spin", "Spin.Total", "Spin.Total", "double", "", "", "Spin.Fix=Tの場合の全スピン。Ga atom参照では1.0。", o++, false, true);
        out << spec(code, "scf", "MaxSCFIterations", "MaxSCFIterations", "int", "", "300", "SCF最大反復数。公式既定値は未登録の場合unknown扱いです。", o++, false, true);
        out << spec(code, "scf", "DM.Tolerance", "DM.Tolerance", "string", "", "", "密度行列収束閾値。必要に応じてimport値または手入力してください。", o++, false, true);
        out << spec(code, "relaxation", "MD.TypeOfRun", "MD.TypeOfRun", "choice", "", "CG", "構造緩和方式。slab緩和ではCG。", o++, false, false, {"CG", "Broyden", "Verlet"});
        out << spec(code, "relaxation", "MD.VariableCell", "MD.VariableCell", "bool", "", "F", "セルも動かすか。slabでは通常F、bulkではT。", o++, false, false, {"T", "F"});
        out << spec(code, "relaxation", "MD.Steps", "MD.Steps", "int", "", "500", "イオン緩和ステップ数。", o++);
        out << spec(code, "relaxation", "MD.MaxForceTol", "MD.MaxForceTol", "double", "eV/Ang", "0.01", "最大力の収束閾値。", o++);
        out << spec(code, "relaxation", "MD.MaxDispl", "MD.MaxDispl", "double", "Ang", "0.05", "1ステップあたり最大変位。", o++);
        out << spec(code, "relaxation", "MD.MaxStressTol", "MD.MaxStressTol", "double", "GPa", "", "bulk variable cell時の応力閾値。", o++, false, true);
        out << spec(code, "relaxation", "GeometryMustConverge", "GeometryMustConverge", "bool", "", "T", "緩和が収束しない場合の終了扱い。", o++, false, false, {"T", "F"});
        out << spec(code, "kpoints", "kgrid", "kgrid", "string", "", "3 3 1", "Monkhorst-Pack k点。slabでは3 3 1。", o++, true);
        out << spec(code, "species", "MeshCutoff", "MeshCutoff", "double", "Ry", "410", "Kangawa profileではxc.fdf由来の期待値410 Ry。", o++, false, true);
        out << spec(code, "species", "xc.functional", "xc.functional", "string", "", "GGA", "xc.fdfから抽出される交換相関functional。", o++, false, true);
        out << spec(code, "species", "xc.authors", "xc.authors", "string", "", "PBEJsJrLO", "Kangawa profileのxc.authors期待値。", o++, false, true);
        out << spec(code, "output", "coordinate_precision", "coordinate_precision", "choice", "digits", "10", "原子座標出力の小数桁数。既定10桁。", o++, true, false, precisionValues);
        out << spec(code, "output", "cell_precision", "cell_precision", "choice", "digits", "10", "セルベクトル出力の小数桁数。既定10桁。", o++, true, false, precisionValues);
        return out;
    }
    out << spec(code, "CONTROL", "calculation", "calculation", "choice", "", "relax", "pw.xの計算モード。slabではrelax、Ga atom参照ではscf。", o++, true, false, {"relax", "scf", "vc-relax"});
    out << spec(code, "CONTROL", "restart_mode", "restart_mode", "string", "", "from_scratch", "QE再開方法。", o++);
    out << spec(code, "CONTROL", "prefix", "prefix", "string", "", "GaN", "QE出力prefix。", o++, true);
    out << spec(code, "CONTROL", "pseudo_dir", "pseudo_dir", "path", "", "./qe_pp/", "UPF擬ポテンシャル格納ディレクトリ。", o++, true);
    out << spec(code, "CONTROL", "outdir", "outdir", "path", "", "./out/<target>", "QE一時出力ディレクトリ。<target>はtarget nameで置換。", o++, true);
    out << spec(code, "CONTROL", "wf_collect", "wf_collect", "bool", "", ".true.", "波動関数収集設定。", o++, false, true, {".true.", ".false."});
    out << spec(code, "CONTROL", "etot_conv_thr", "etot_conv_thr", "string", "", "1.0D-4", "全エネルギー収束閾値。", o++);
    out << spec(code, "CONTROL", "forc_conv_thr", "forc_conv_thr", "string", "", "1.0D-3", "力収束閾値。", o++);
    out << spec(code, "CONTROL", "nstep", "nstep", "int", "", "500", "イオンステップ数。", o++);
    out << spec(code, "SYSTEM", "ibrav", "ibrav", "int", "", "0", "セルをCELL_PARAMETERSで与えるため0。", o++, true);
    out << spec(code, "SYSTEM", "ecutwfc", "ecutwfc", "double", "Ry", "80", "波動関数平面波カットオフ。宮口スラブ計算では80 Ry。SIESTAのMeshCutoffとは意味が違います。", o++, true);
    out << spec(code, "SYSTEM", "ecutrho", "ecutrho", "double", "Ry", "320", "電荷密度カットオフ。", o++, true);
    out << spec(code, "SYSTEM", "occupations", "occupations", "string", "", "smearing", "占有数扱い。Ga atomではfixed。", o++);
    out << spec(code, "SYSTEM", "smearing", "smearing", "string", "", "cold", "smearing方式。", o++);
    out << spec(code, "SYSTEM", "degauss", "degauss", "double", "", "0.015", "smearing幅。荷電legacyでは0.03、neutral legacyでは0.015例があります。", o++);
    out << spec(code, "SYSTEM", "nosym", "nosym", "bool", "", ".true.", "対称性を無効化。", o++);
    out << spec(code, "SYSTEM", "nspin", "nspin", "int", "", "", "スピン分極。荷電slab/Ga atomでは2。", o++);
    out << spec(code, "SYSTEM", "tot_charge", "tot_charge", "double", "", "", "QE計算セルの総電荷。", o++);
    out << spec(code, "SYSTEM", "assume_isolated", "assume_isolated", "string", "", "", "QEの孤立系補正。2Dはユーザー選択。", o++, false, true);
    out << spec(code, "SYSTEM", "starting_magnetization(1)", "starting_magnetization(1)", "double", "", "", "species 1の初期磁化。Ga atom参照では1.0。", o++, false, true);
    out << spec(code, "SYSTEM", "tot_magnetization", "tot_magnetization", "double", "", "", "全磁化。Ga atom参照では1.0。", o++, false, true);
    out << spec(code, "ELECTRONS", "conv_thr", "conv_thr", "string", "", "1.0d-5", "電子SCF収束閾値。", o++);
    out << spec(code, "ELECTRONS", "mixing_mode", "mixing_mode", "string", "", "plain", "mixing方式。", o++, false, true);
    out << spec(code, "ELECTRONS", "mixing_beta", "mixing_beta", "double", "", "0.03", "mixing beta。荷電legacyでは0.08例があります。", o++);
    out << spec(code, "ELECTRONS", "mixing_ndim", "mixing_ndim", "int", "", "20", "mixing履歴数。", o++);
    out << spec(code, "ELECTRONS", "diagonalization", "diagonalization", "string", "", "david", "対角化方式。荷電legacyではcg例があります。", o++, false, true);
    out << spec(code, "ELECTRONS", "electron_maxstep", "electron_maxstep", "int", "", "1000", "電子SCF最大ステップ数。", o++, false, true);
    out << spec(code, "ELECTRONS", "startingpot", "startingpot", "string", "", "", "必要な場合のみ出力。", o++, false, true);
    out << spec(code, "ELECTRONS", "startingwfc", "startingwfc", "string", "", "", "必要な場合のみ出力。", o++, false, true);
    out << spec(code, "IONS", "ion_dynamics", "ion_dynamics", "string", "", "bfgs", "イオン緩和アルゴリズム。", o++);
    out << spec(code, "IONS", "trust_radius_ini", "trust_radius_ini", "double", "", "0.2", "BFGS初期trust radius。", o++, false, true);
    out << spec(code, "K_POINTS", "automatic", "K_POINTS", "string", "", "3 3 1 0 0 0", "自動k点。slabでは3 3 1 0 0 0。", o++, true);
    out << spec(code, "output", "coordinate_precision", "coordinate_precision", "choice", "digits", "10", "原子座標出力の小数桁数。既定10桁。", o++, true, false, precisionValues);
    out << spec(code, "output", "cell_precision", "cell_precision", "choice", "digits", "10", "セルベクトル出力の小数桁数。既定10桁。", o++, true, false, precisionValues);
    return out;
}

DftSettings DftParameterRegistry::defaultSettings(DftCode code, const QString& version, const QString& targetName) {
    DftSettings s;
    s.code = code;
    s.version = version.isEmpty() ? (code == DftCode::Siesta ? QStringLiteral("4.1.5") : QStringLiteral("7.3.1")) : version;
    s.targetName = targetName.isEmpty() ? QStringLiteral("ideal") : targetName;
    s.profileName = QStringLiteral("Manual");
    s.calculationMode = QStringLiteral("neutral_slab");
    if (code == DftCode::Siesta) {
        s.executable = QStringLiteral("siesta");
        s.schema = s.version == QStringLiteral("5.4.2") ? QStringLiteral("siesta_fdf_5_4") : QStringLiteral("siesta_fdf_4_1");
        s.moduleName = s.version == QStringLiteral("5.4.2") ? QStringLiteral("siesta/5.4.2-mpi") : QStringLiteral("siesta/4.1.5-mpi");
        s.includeXcFdf = true;
        s.xcFdfPath = QStringLiteral("xc.fdf");
        s.siestaSpecies = kangawaSiestaSpecies();
    } else {
        s.executable = QStringLiteral("pw.x");
        s.schema = QStringLiteral("pw_7_3_1");
        s.includeXcFdf = false;
        s.pseudoDir = QStringLiteral("./qe_pp/");
        s.outDirPattern = QStringLiteral("./out/<target>");
        s.qeSpecies = defaultQeSpecies();
    }
    addSpecs(&s);
    applyCalculationModeDefaults(&s, s.calculationMode);
    return s;
}
void DftParameterRegistry::applyCalculationModeDefaults(DftSettings* s, const QString& mode) {
    if (s == nullptr) return;
    s->calculationMode = mode;
    if (s->code == DftCode::Siesta) {
        setParameterValue(s, "siesta.general.SystemName", s->targetName, DftParameterSource::ProjectProfile);
        setParameterValue(s, "siesta.general.SystemLabel", s->targetName, DftParameterSource::ProjectProfile);
        setParameterValue(s, "siesta.relaxation.MD.TypeOfRun", "CG", DftParameterSource::ProjectProfile);
        setParameterValue(s, "siesta.relaxation.MD.Steps", "500", DftParameterSource::ProjectProfile);
        setParameterValue(s, "siesta.relaxation.MD.MaxDispl", "0.05", DftParameterSource::ProjectProfile);
        setParameterValue(s, "siesta.relaxation.GeometryMustConverge", "T", DftParameterSource::ProjectProfile);
        if (mode == "charged_slab_electron_added" || mode == "charged_slab_electron_removed") {
            setParameterValue(s, "siesta.charge_spin.NetCharge", mode.endsWith("added") ? "-0.25" : "0.25", DftParameterSource::ProjectProfile);
            setParameterValue(s, "siesta.charge_spin.Spin", "polarized", DftParameterSource::ProjectProfile);
            setParameterValue(s, "siesta.charge_spin.Spin.Fix", "F", DftParameterSource::ProjectProfile);
            setParameterValue(s, "siesta.charge_spin.Spin.Total", "", DftParameterSource::ProjectProfile);
            setParameterValue(s, "siesta.relaxation.MD.VariableCell", "F", DftParameterSource::ProjectProfile);
            setParameterValue(s, "siesta.relaxation.MD.MaxForceTol", "0.01", DftParameterSource::ProjectProfile);
            setParameterValue(s, "siesta.kpoints.kgrid", "3 3 1", DftParameterSource::ProjectProfile);
        } else if (mode == "bulk_variable_cell") {
            setParameterValue(s, "siesta.charge_spin.NetCharge", "0.0", DftParameterSource::ProjectProfile);
            setParameterValue(s, "siesta.charge_spin.Spin", "none", DftParameterSource::ProjectProfile);
            setParameterValue(s, "siesta.relaxation.MD.VariableCell", "T", DftParameterSource::ProjectProfile);
            setParameterValue(s, "siesta.relaxation.MD.MaxForceTol", "0.001", DftParameterSource::ProjectProfile);
            setParameterValue(s, "siesta.relaxation.MD.MaxStressTol", "0.1", DftParameterSource::ProjectProfile);
            setParameterValue(s, "siesta.kpoints.kgrid", "3 3 3", DftParameterSource::ProjectProfile);
        } else if (mode == "ga_atom_reference") {
            setParameterValue(s, "siesta.charge_spin.NetCharge", "0.0", DftParameterSource::ProjectProfile);
            setParameterValue(s, "siesta.charge_spin.Spin", "polarized", DftParameterSource::ProjectProfile);
            setParameterValue(s, "siesta.charge_spin.Spin.Fix", "T", DftParameterSource::ProjectProfile);
            setParameterValue(s, "siesta.charge_spin.Spin.Total", "1.0", DftParameterSource::ProjectProfile);
            setParameterValue(s, "siesta.relaxation.MD.VariableCell", "F", DftParameterSource::ProjectProfile);
            setParameterValue(s, "siesta.kpoints.kgrid", "1 1 1", DftParameterSource::ProjectProfile);
        } else if (mode == "n2_reference" || mode == "h2_reference") {
            setParameterValue(s, "siesta.charge_spin.NetCharge", "0.0", DftParameterSource::ProjectProfile);
            setParameterValue(s, "siesta.charge_spin.Spin", "none", DftParameterSource::ProjectProfile);
            setParameterValue(s, "siesta.relaxation.MD.VariableCell", "F", DftParameterSource::ProjectProfile);
            setParameterValue(s, "siesta.kpoints.kgrid", "1 1 1", DftParameterSource::ProjectProfile);
        } else {
            setParameterValue(s, "siesta.charge_spin.NetCharge", "0.0", DftParameterSource::ProjectProfile);
            setParameterValue(s, "siesta.charge_spin.Spin", "none", DftParameterSource::ProjectProfile);
            setParameterValue(s, "siesta.relaxation.MD.VariableCell", "F", DftParameterSource::ProjectProfile);
            setParameterValue(s, "siesta.relaxation.MD.MaxForceTol", "0.01", DftParameterSource::ProjectProfile);
            setParameterValue(s, "siesta.kpoints.kgrid", "3 3 1", DftParameterSource::ProjectProfile);
        }
        return;
    }

    setParameterValue(s, "qe.CONTROL.pseudo_dir", s->pseudoDir, DftParameterSource::ProjectProfile);
    setParameterValue(s, "qe.CONTROL.outdir", s->outDirPattern, DftParameterSource::ProjectProfile);
    setParameterValue(s, "qe.CONTROL.wf_collect", ".true.", DftParameterSource::ProjectProfile);
    setParameterValue(s, "qe.SYSTEM.ibrav", "0", DftParameterSource::ProjectProfile);
    setParameterValue(s, "qe.SYSTEM.ecutwfc", "80", DftParameterSource::ProjectProfile);
    setParameterValue(s, "qe.SYSTEM.ecutrho", "320", DftParameterSource::ProjectProfile);
    setParameterValue(s, "qe.SYSTEM.nosym", ".true.", DftParameterSource::ProjectProfile);
    setParameterValue(s, "qe.CONTROL.nstep", "500", DftParameterSource::ProjectProfile);
    setParameterValue(s, "qe.ELECTRONS.conv_thr", "1.0d-5", DftParameterSource::ProjectProfile);
    setParameterValue(s, "qe.IONS.ion_dynamics", "bfgs", DftParameterSource::ProjectProfile);
    setParameterValue(s, "qe.IONS.trust_radius_ini", "0.2", DftParameterSource::ProjectProfile);
    if (mode == "charged_slab_electron_added" || mode == "charged_slab_electron_removed") {
        setParameterValue(s, "qe.CONTROL.calculation", "relax", DftParameterSource::ProjectProfile);
        setParameterValue(s, "qe.SYSTEM.tot_charge", mode.endsWith("added") ? "-0.25" : "0.25", DftParameterSource::ProjectProfile);
        setParameterValue(s, "qe.SYSTEM.nspin", "2", DftParameterSource::ProjectProfile);
        setParameterValue(s, "qe.SYSTEM.occupations", "smearing", DftParameterSource::ProjectProfile);
        setParameterValue(s, "qe.SYSTEM.smearing", "cold", DftParameterSource::ProjectProfile);
        setParameterValue(s, "qe.SYSTEM.degauss", "0.03", DftParameterSource::ProjectProfile);
        setParameterValue(s, "qe.SYSTEM.assume_isolated", s->qeAssumeIsolated ? "2D" : "", DftParameterSource::ProjectProfile);
        setParameterValue(s, "qe.ELECTRONS.mixing_beta", "0.08", DftParameterSource::ProjectProfile);
        setParameterValue(s, "qe.ELECTRONS.mixing_ndim", "8", DftParameterSource::ProjectProfile);
        setParameterValue(s, "qe.ELECTRONS.diagonalization", "cg", DftParameterSource::ProjectProfile);
        setParameterValue(s, "qe.K_POINTS.automatic", "3 3 1 0 0 0", DftParameterSource::ProjectProfile);
    } else if (mode == "ga_atom_reference") {
        setParameterValue(s, "qe.CONTROL.calculation", "scf", DftParameterSource::ProjectProfile);
        setParameterValue(s, "qe.CONTROL.prefix", "Ga", DftParameterSource::ProjectProfile);
        setParameterValue(s, "qe.SYSTEM.occupations", "fixed", DftParameterSource::ProjectProfile);
        setParameterValue(s, "qe.SYSTEM.smearing", "", DftParameterSource::ProjectProfile);
        setParameterValue(s, "qe.SYSTEM.degauss", "", DftParameterSource::ProjectProfile);
        setParameterValue(s, "qe.SYSTEM.nspin", "2", DftParameterSource::ProjectProfile);
        setParameterValue(s, "qe.SYSTEM.starting_magnetization(1)", "1.0", DftParameterSource::ProjectProfile);
        setParameterValue(s, "qe.SYSTEM.tot_magnetization", "1.0", DftParameterSource::ProjectProfile);
        setParameterValue(s, "qe.ELECTRONS.conv_thr", "1.0d-8", DftParameterSource::ProjectProfile);
        setParameterValue(s, "qe.K_POINTS.automatic", "1 1 1 0 0 0", DftParameterSource::ProjectProfile);
    } else if (mode == "n2_reference" || mode == "h2_reference") {
        setParameterValue(s, "qe.CONTROL.calculation", "relax", DftParameterSource::ProjectProfile);
        setParameterValue(s, "qe.SYSTEM.nspin", "", DftParameterSource::ProjectProfile);
        setParameterValue(s, "qe.SYSTEM.tot_charge", "", DftParameterSource::ProjectProfile);
        setParameterValue(s, "qe.K_POINTS.automatic", "1 1 1 0 0 0", DftParameterSource::ProjectProfile);
    } else {
        setParameterValue(s, "qe.CONTROL.calculation", "relax", DftParameterSource::ProjectProfile);
        setParameterValue(s, "qe.CONTROL.prefix", "GaN", DftParameterSource::ProjectProfile);
        setParameterValue(s, "qe.SYSTEM.occupations", "smearing", DftParameterSource::ProjectProfile);
        setParameterValue(s, "qe.SYSTEM.smearing", "cold", DftParameterSource::ProjectProfile);
        setParameterValue(s, "qe.SYSTEM.degauss", "0.015", DftParameterSource::ProjectProfile);
        setParameterValue(s, "qe.SYSTEM.nspin", "", DftParameterSource::ProjectProfile);
        setParameterValue(s, "qe.SYSTEM.tot_charge", "", DftParameterSource::ProjectProfile);
        setParameterValue(s, "qe.SYSTEM.assume_isolated", "", DftParameterSource::ProjectProfile);
        setParameterValue(s, "qe.ELECTRONS.mixing_beta", "0.03", DftParameterSource::ProjectProfile);
        setParameterValue(s, "qe.ELECTRONS.mixing_ndim", "20", DftParameterSource::ProjectProfile);
        setParameterValue(s, "qe.ELECTRONS.diagonalization", "david", DftParameterSource::ProjectProfile);
        setParameterValue(s, "qe.K_POINTS.automatic", "3 3 1 0 0 0", DftParameterSource::ProjectProfile);
    }
}
QStringList DftParameterRegistry::calculationModes(DftCode code) {
    Q_UNUSED(code);
    return {"neutral_slab", "charged_slab_electron_added", "charged_slab_electron_removed",
            "bulk_variable_cell", "ga_atom_reference", "n2_reference", "h2_reference", "custom"};
}

QStringList DftParameterRegistry::versionsForCode(DftCode code) {
    return code == DftCode::Siesta
        ? QStringList{"4.1.5", "5.4.2", "custom"}
        : QStringList{"7.3.1", "custom"};
}

QStringList DftParameterRegistry::builtInProfiles(DftCode code, const QString& version) {
    Q_UNUSED(version);
    if (code == DftCode::Siesta) return {"Manual", "Kangawa_GaN_surface"};
    return {"Manual", "Miyaguchi_Ga_reference", "Miyaguchi_GaN_neutral_slab",
            "Miyaguchi_GaN_charged_slab", "Miyaguchi_GaN_neutral_3GaH"};
}

bool DftParameterRegistry::applyBuiltInProfile(const QString& profileName, DftSettings* s, QStringList* messages) {
    if (s == nullptr || profileName.isEmpty() || profileName == "Manual") return true;
    s->generationMode = DftGenerationMode::Profile;
    s->profileName = profileName;
    if (profileName == "Kangawa_GaN_surface") {
        s->code = DftCode::Siesta;
        s->version = "4.1.5";
        s->moduleName = "siesta/4.1.5-mpi";
        s->schema = "siesta_fdf_4_1";
        s->includeXcFdf = true;
        s->standaloneInline = false;
        s->xcFdfPath = "xc.fdf";
        s->fixedAtomMode = DftFixedAtomMode::FixBottomPseudoHPlusBottomMolecularLayer;
        s->trailingFlagInterpretation = DftTrailingFlagInterpretation::PreserveOrIgnoreUnknown;
        s->siestaSpecies = kangawaSiestaSpecies();
        applyCalculationModeDefaults(s, "neutral_slab");
        if (messages) *messages << "Kangawa_GaN_surface profile loaded.";
        return true;
    }
    if (!profileName.startsWith("Miyaguchi_")) return false;
    s->code = DftCode::QuantumEspresso;
    s->version = "7.3.1";
    s->executable = "pw.x";
    s->schema = "pw_7_3_1";
    s->qeSpecies = defaultQeSpecies();
    if (profileName == "Miyaguchi_Ga_reference") {
        s->targetName = "Ga_atom_total";
        applyCalculationModeDefaults(s, "ga_atom_reference");
    } else if (profileName == "Miyaguchi_GaN_charged_slab") {
        s->qeAssumeIsolated = true;
        applyCalculationModeDefaults(s, "charged_slab_electron_added");
        setParameterValue(s, "qe.SYSTEM.assume_isolated", "2D", DftParameterSource::ProjectProfile);
    } else if (profileName == "Miyaguchi_GaN_neutral_3GaH") {
        applyCalculationModeDefaults(s, "neutral_slab");
        setParameterValue(s, "qe.SYSTEM.degauss", "0.015", DftParameterSource::ProjectProfile);
        setParameterValue(s, "qe.SYSTEM.assume_isolated", "2D", DftParameterSource::ProjectProfile);
        setParameterValue(s, "qe.ELECTRONS.startingpot", "file", DftParameterSource::ProjectProfile);
        setParameterValue(s, "qe.ELECTRONS.startingwfc", "file", DftParameterSource::ProjectProfile);
    } else {
        applyCalculationModeDefaults(s, "neutral_slab");
    }
    if (messages) *messages << QStringLiteral("%1 profile loaded.").arg(profileName);
    return true;
}

QVector<DftSiestaSpecies> DftParameterRegistry::kangawaSiestaSpecies() {
    return {
        {1, 201, "H-0.750", "H", "bottom_pseudo_h_n_terminated_0p75"},
        {2, 7, "N", "N", "anion"},
        {3, 31, "Ga", "Ga", "cation"},
        {4, 1, "H", "H", "ordinary_hydrogen"},
        {5, 201, "H-1.250", "H", "bottom_pseudo_h_iii_terminated_1p25"},
    };
}

double DftParameterRegistry::defaultAtomicMass(const QString& element) {
    if (element.compare(QStringLiteral("H"), Qt::CaseInsensitive) == 0) return 1.00784;
    if (element.compare(QStringLiteral("N"), Qt::CaseInsensitive) == 0) return 14.0067;
    if (element.compare(QStringLiteral("Al"), Qt::CaseInsensitive) == 0) return 26.9815385;
    if (element.compare(QStringLiteral("Ga"), Qt::CaseInsensitive) == 0) return 69.723;
    if (element.compare(QStringLiteral("In"), Qt::CaseInsensitive) == 0) return 114.818;
    return 1.0;
}

QVector<DftQeSpecies> DftParameterRegistry::defaultQeSpecies() {
    return {
        {"Ga", "Ga", defaultAtomicMass(QStringLiteral("Ga")), "Ga.pbe-mt_fhi.UPF", "cation", DftParameterSource::ProjectProfile},
        {"N", "N", defaultAtomicMass(QStringLiteral("N")), "N.pbe-mt_fhi.UPF", "anion", DftParameterSource::ProjectProfile},
        {"H", "H", defaultAtomicMass(QStringLiteral("H")), "H.pbe-MT.075.UPF", "bottom_pseudo_h_n_terminated_0p75", DftParameterSource::ProjectProfile},
        {"H2", "H", defaultAtomicMass(QStringLiteral("H")), "H.pbe-mt_fhi.UPF", "surface_adsorbed_hydrogen", DftParameterSource::ProjectProfile},
        {"Hp125", "H", defaultAtomicMass(QStringLiteral("H")), "H.pbe-MT.125.UPF", "bottom_pseudo_h_iii_terminated_1p25", DftParameterSource::ProjectProfile},
    };
}

void DftParameterRegistry::resetQeSpeciesToProjectDefaults(DftSettings* settings) {
    if (settings == nullptr) return;
    settings->qeSpecies = defaultQeSpecies();
}

QString DftParameterRegistry::parameterValue(const DftSettings& s, const QString& id, const QString& fallback) {
    const auto it = s.parameters.constFind(id);
    if (it == s.parameters.constEnd() || !it.value().enabled) return fallback;
    return it.value().value;
}

void DftParameterRegistry::setParameterValue(DftSettings* s, const QString& id, const QString& value, DftParameterSource source) {
    if (s == nullptr) return;
    auto it = s->parameters.find(id);
    if (it == s->parameters.end()) {
        DftParameterEntry e;
        e.spec.id = id;
        e.spec.key = id.section('.', -1);
        e.spec.section = id.section('.', 1, -2);
        e.spec.code = s->code;
        e.value = value;
        e.source = source;
        e.enabled = !value.isEmpty();
        s->parameters.insert(id, e);
        return;
    }
    it->value = value;
    it->source = source;
    it->enabled = !value.isEmpty() || it->spec.required;
}
