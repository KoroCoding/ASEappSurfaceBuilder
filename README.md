# ASEapp Surface Builder

**ASEapp Surface Builder** は、第一原理計算・表面反応モデル作成の前処理を支援する C++ / Qt デスクトップアプリです。VESTA に近い感覚で構造を見ながら、slab、supercell、真空層、吸着原子、前駆体、吸着分子ポーズを編集できます。

<p align="center">
  <img src="guide_assets/02_loaded_overview.png" alt="ASEapp Surface Builder main window with a loaded structure" width="820">
</p>

## すぐ使う

| やりたいこと | 最短ルート |
| --- | --- |
| アプリを起動したい | 同梱 Windows 版 [`standalone_exe/windows/ASEappSurfaceBuilder-1.1.0-Windows.exe`](standalone_exe/windows/ASEappSurfaceBuilder-1.1.0-Windows.exe) を実行 |
| 画面を見ながら使い方を知りたい | [操作ガイド `Guide.md`](Guide.md) を開く |
| 自分でビルドしたい | [ソースからビルド](#ソースからビルド) を実行 |
| 配布物を作り直したい | [`PACKAGING.md`](PACKAGING.md) を参照 |
| 変更点を確認したい | [`release_notes/`](release_notes/) を参照 |

## どんなアプリか

- 構造ファイルを開き、球・ボンド・セル枠で確認できます。
- 原子を選択して、直上・直下・原子間・多点中心・面法線上に新しい原子を置けます。
- Supercell、真空層、セル軸傾き、slab 全体移動を GUI で調整できます。
- 前駆体 CSV を保存/読み込みし、同じ相対配置を別の表面位置に再利用できます。
- 吸着分子を剛体グループ化し、数値並進、pivot 回転、結合軸回転、結合長調整ができます。
- extended XYZ / pose JSON / ASE snippet を出力し、ASE 側のバッチ処理へ戻せます。

## 30秒でわかる操作イメージ

| 1. 構造を開く | 2. 原子を選択して配置条件を決める |
| --- | --- |
| <img src="guide_assets/01_start_open.png" alt="Start screen" width="390"> | <img src="guide_assets/03_select_atom_and_placement_panel.png" alt="Placement panel" width="390"> |

| 3. プレビューで位置を確認 | 4. 原子追加後に必要なら supercell / vacuum を調整 |
| --- | --- |
| <img src="guide_assets/05_placement_preview.png" alt="Placement preview" width="390"> | <img src="guide_assets/12_right_panel_lower_controls.png" alt="Supercell vacuum and view controls" width="390"> |

| 吸着分子の向きを詰める | 論文・資料用の原子一覧を出す |
| --- | --- |
| <img src="guide_assets/13_adsorbate_pose_editor.png" alt="Adsorbate pose editor" width="390"> | <img src="guide_assets/10_atom_legend_export_dialog.png" alt="Atom legend export" width="390"> |

より詳しい画面説明と手順は、スクリーンショット付きの [Guide.md](Guide.md) にまとめています。

## インストール

確認済みの配布バイナリは `standalone_exe/` に同梱しています。GitHub Releases にも添付できますが、この checkout だけで使う場合は次のファイルを直接使えます。

| OS | 推奨 | 備考 |
| --- | --- | --- |
| Windows | [`standalone_exe/windows/ASEappSurfaceBuilder-1.1.0-Windows.exe`](standalone_exe/windows/ASEappSurfaceBuilder-1.1.0-Windows.exe) | 現在同梱している Windows 版は v1.1.0 です。互換確認用として v1.0.0 も同じフォルダに残しています。 |
| macOS | [`standalone_exe/macos/ASEappSurfaceBuilder-1.1.0-macOS.dmg`](standalone_exe/macos/ASEappSurfaceBuilder-1.1.0-macOS.dmg) | v1.0.0 の DMG も [`standalone_exe/macos/`](standalone_exe/macos/) に残しています。 |
| Linux | ソースからビルド | Qt 6 と CMake が必要です。 |

### Windows: 同梱 EXE を起動

`standalone_exe/windows/ASEappSurfaceBuilder-1.1.0-Windows.exe` を実行してください。

### macOS

最新の macOS 版は `standalone_exe/macos/ASEappSurfaceBuilder-1.1.0-macOS.dmg` です。過去版として `ASEappSurfaceBuilder-1.0.0-macOS.dmg` も同じフォルダに残しています。

macOS の `.dmg` 作成・署名・notarization の考え方は [`PACKAGING.md`](PACKAGING.md) に分けています。

## ソースからビルド

推奨は Conda 環境です。Qt 6 / CMake / Python 補助ツールをまとめて用意できます。

```bash
conda env create -f environment.yml
conda activate aseapp-surface-builder
pip install -r requirements.txt
```

### Windows

```powershell
$env:CONDA_PREFIX = (conda info --base) + "\envs\aseapp-surface-builder"
cmake -S code/native_ui -B code/native_ui/build -DCMAKE_PREFIX_PATH="$env:CONDA_PREFIX\Library"
cmake --build code/native_ui/build --config Release --parallel 2
.\code\native_ui\build\Release\ASEappNativeUI.exe
```

### macOS / Linux

```bash
cmake -S code/native_ui -B code/native_ui/build -DCMAKE_PREFIX_PATH="$CONDA_PREFIX"
cmake --build code/native_ui/build --config Release --parallel 2
./code/native_ui/build/ASEappNativeUI
```

## 対応ファイル形式

| 分類 | 対応形式 | 用途 |
| --- | --- | --- |
| VASP | `POSCAR`, `CONTCAR`, `.vasp`, `.poscar` | 計算投入前後の構造確認・保存 |
| XYZ | `.xyz`, `.extxyz` | ASE とのやり取り。cell / PBC を残したい場合は extended XYZ 推奨 |
| 結晶構造 | `.cif`, `.pdb`, `.xsf` | 外部データの読み込み |
| ASEapp | `.aseproj`, `.json` | ASEapp の編集情報を保持した再編集用形式 |
| VESTA | `.vesta` | VESTA 由来構造の取り込み |

## 主な機能

### 表示・選択

- VESTA 風の原子色、球表示、ボンド表示、セル枠表示
- direct `a/b/c` と reciprocal `a*/b*/c*` 視点
- 透視投影、奥行き表現、原子ラベル、軸表示の切替
- 左クリック選択、Ctrl 追加選択、Ctrl + 左ドラッグで重なった奥の原子も選択
- Shift + 左ドラッグで選択原子を画面平面内に移動

### 原子配置

- 選択原子の直上 / 直下への配置
- 複数選択時の一括直上 / 直下配置
- 選択原子の中心、または選択面の法線上への配置
- 周期表ダイアログから生成元素を選択
- 配置プレビューは明示的にオンにした時だけ表示

### 表面モデル編集

- Supercell 作成
- 真空層の追加・除去
- slab 全体の a/b/c 方向移動
- セル軸傾きによる step-terrace 候補作成
- VESTA `SBOND` 風の元素ペア別ボンド距離 min/max Å 設定

### 前駆体・吸着分子ポーズ

- 前駆体 CSV の保存 / 読込 / 再配置
- 選択原子群を吸着分子 pose group として登録
- XYZ / cell / slab 法線方向の数値並進
- pivot 固定回転、選択中 2 原子の結合軸回転
- 結合長調整
- extended XYZ、pose JSON、ASE snippet 出力

## 基本操作

1. `開く` またはドラッグ&ドロップで構造ファイルを読み込みます。
2. `c` / `c*` 視点で表面方向を確認します。
3. 必要に応じて `スーパーセル`、`真空層`、`セル軸傾き` を調整します。
4. 原子をクリックして基準原子を選択します。
5. `生成元素` と `配置位置` を決め、必要な時だけ `配置プレビューを表示` をオンにします。
6. `配置する` を押して原子を追加します。
7. `保存` で `.aseproj`, POSCAR, extended XYZ などに保存します。

## ショートカット

| 操作 | 内容 |
| --- | --- |
| 左ドラッグ | 視点回転 |
| 右 / 中ドラッグ | パン |
| ホイール / トラックパッドのピンチ | カーソル位置を基準にズーム |
| 左クリック | 原子選択 |
| Ctrl + 左クリック | 選択追加 / 解除 |
| Ctrl + 左ドラッグ | 重なった奥の原子も追加選択 |
| Shift + 左ドラッグ | 選択原子を画面平面内で移動 |
| Delete | 選択原子を削除 |
| Esc | 選択解除 |
| F / ダブルクリック | フィット |
| A / B / C | direct a/b/c 方向表示 |
| Ctrl+Alt+A/B/C | reciprocal a*/b*/c* 方向表示 |
| F1 | ヘルプ |

通常の slab のように c 軸が ab 面法線と平行なセルでは、direct `c` と reciprocal `c*` は同じ向きに見えます。非直交セルやセル軸傾き後は、`c` と `c*` が異なる確認視点になります。

## 前駆体 CSV 例

```csv
name,element,dx_angstrom,dy_angstrom,dz_angstrom
GaNH,Ga,0.000000,0.000000,0.000000
GaNH,N,0.000000,0.000000,1.950000
GaNH,H,0.000000,0.000000,2.950000
```

`dx/dy/dz` は、前駆体内で最も低い原子を基準にした Å 単位の相対座標です。前駆体を読み込むと、通常の原子配置と同じ配置位置にまとめて置けます。

## リポジトリ構成

```text
code/native_ui/       C++ / Qt の本体ソース
Guide.md              スクリーンショット付き操作ガイド
guide_assets/         README / Guide 用スクリーンショットとサンプル構造
release_notes/        バージョン別リリースノート
PACKAGING.md          配布物の再生成手順
environment.yml       Conda 環境定義
requirements.txt      Python 補助ツール用依存関係
standalone_exe/       確認済み配布物（Windows / macOS）
```

このリポジトリには、確認済みの `standalone_exe/` 配布物を含めています。`code/native_ui/build/`、`code/native_ui/dist/`、証明書、秘密鍵、仕様書などのローカル生成物は含めません。

## 開発時の確認

```powershell
cmake -S code/native_ui -B code/native_ui/build
cmake --build code/native_ui/build --config Release --parallel 2
ctest --test-dir code/native_ui/build -C Release --output-on-failure
```

Windows + Conda + MSVC では、Debug ビルドも Conda Qt と同じ CRT 側に揃える設定を入れています。Debug 検証を行う場合も次を推奨します。

```powershell
$env:CONDA_PREFIX = "C:\Users\<you>\anaconda3\envs\aseapp-surface-builder"
$env:PATH = "$env:CONDA_PREFIX\Library\bin;$env:CONDA_PREFIX;$env:CONDA_PREFIX\Scripts;" + $env:PATH
cmake --build code/native_ui/build --config Debug --parallel 2
ctest --test-dir code/native_ui/build -C Debug --output-on-failure
```

## トラブルシュート

| 症状 | 対処 |
| --- | --- |
| `freetype.dll` などが見つからない | ZIP 版は展開フォルダ全体を保ったまま起動してください。単体 EXE 版なら DLL 同梱の launcher を使えます。 |
| Windows Application Control / Smart App Control で止まる | DLL 不足ではなく Windows 側の実行制御です。広く配布する正式版は信頼済みコード署名を推奨します。 |
| 画面が重い | ボンド表示、ラベル、プレビューを必要な時だけ有効にし、Supercell を大きくしすぎないでください。 |
| 原子配置位置がわかりにくい | `配置プレビューを表示` をオンにして、半透明の予定位置を確認してから `配置する` を押してください。 |
| macOS で初回起動警告が出る | 自己署名や未notarizeのビルドでは Gatekeeper 警告が出ます。右クリックの「開く」、署名、notarization を確認してください。 |

## ライセンス・引用

ライセンスファイルを追加する場合は、この節に明記してください。論文・発表で利用する場合は、使用したバージョン、入力構造、出力形式、追加した前駆体/吸着分子条件を記録しておくことを推奨します。
