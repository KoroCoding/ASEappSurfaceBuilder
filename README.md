# ASEapp Surface Builder

ASEapp Surface Builder は、第一原理計算や表面モデル作成の前処理を支援する Qt アプリです。VESTA に近い操作感で slab / supercell / adsorption / precursor 配置を確認しながら編集できます。

## インストール

最新版は GitHub Releases から入手できます。

| OS | 直接ダウンロード | 推奨形式 |
| --- | --- | --- |
| Windows | [ASEappSurfaceBuilder-1.0.0-Windows.exe](https://github.com/ic191226/ASEappSurfaceBuilder/releases/latest/download/ASEappSurfaceBuilder-1.0.0-Windows.exe) | 単体 `.exe` |
| Windows ZIP | [ASEappSurfaceBuilder-1.0.0-Windows.zip](https://github.com/ic191226/ASEappSurfaceBuilder/releases/latest/download/ASEappSurfaceBuilder-1.0.0-Windows.zip) | フォルダ展開版 |
| Linux | [ASEappSurfaceBuilder-1.0.0-Linux.tar.gz](https://github.com/ic191226/ASEappSurfaceBuilder/releases/latest/download/ASEappSurfaceBuilder-1.0.0-Linux.tar.gz) | `.tar.gz` |
| macOS | [ASEappSurfaceBuilder-1.0.0-macOS.dmg](https://github.com/ic191226/ASEappSurfaceBuilder/releases/latest/download/ASEappSurfaceBuilder-1.0.0-macOS.dmg) | `.dmg` |

> macOS 版は `code/native_ui/package_macos.sh` でローカルビルドできます。GitHub Release から直接入手する場合は、Release assets に `.dmg` が追加済みか確認してください。

### Windows

PowerShell で直接ダウンロードして起動できます。

```powershell
$url = "https://github.com/ic191226/ASEappSurfaceBuilder/releases/latest/download/ASEappSurfaceBuilder-1.0.0-Windows.exe"
$out = "$env:USERPROFILE\Downloads\ASEappSurfaceBuilder-1.0.0-Windows.exe"
Invoke-WebRequest -Uri $url -OutFile $out
Start-Process $out
```

ZIP 版を使う場合は、展開後の `bin` / `plugins` / `translations` を含むフォルダ構成を崩さず、`bin\ASEappNativeUI.exe` を起動してください。`bin\ASEappNativeUI.exe` だけを単独で移動すると Qt DLL 不足で起動できません。

### Linux

```bash
curl -L -o ASEappSurfaceBuilder-1.0.0-Linux.tar.gz \
  https://github.com/ic191226/ASEappSurfaceBuilder/releases/latest/download/ASEappSurfaceBuilder-1.0.0-Linux.tar.gz

mkdir -p ~/.local/opt/aseapp-surface-builder ~/.local/bin
tar -xzf ASEappSurfaceBuilder-1.0.0-Linux.tar.gz -C ~/.local/opt/aseapp-surface-builder
cat > ~/.local/bin/aseapp-surface-builder <<'EOF'
#!/usr/bin/env bash
exec "$HOME/.local/opt/aseapp-surface-builder/bin/ASEappNativeUI" "$@"
EOF
chmod +x ~/.local/bin/aseapp-surface-builder

aseapp-surface-builder
```

`~/.local/bin` に PATH が通っていない場合は、次を `~/.bashrc` や `~/.zshrc` に追加してください。

```bash
export PATH="$HOME/.local/bin:$PATH"
```

### macOS

macOS 版を Release assets に追加後、次でダウンロードして開けます。

```bash
curl -L -o ASEappSurfaceBuilder-1.0.0-macOS.dmg \
  https://github.com/ic191226/ASEappSurfaceBuilder/releases/latest/download/ASEappSurfaceBuilder-1.0.0-macOS.dmg
open ASEappSurfaceBuilder-1.0.0-macOS.dmg
```

DMG を開いたら、同梱されているアプリを `Applications` にドラッグしてください。

Developer ID 署名を使わない自己署名版では、別の Mac で初回起動時に Gatekeeper の警告が出ることがあります。その場合は、右クリックから「開く」を選ぶか、DMG 内の `ASEapp-macOS-Allow-This-App.command` を実行して、同梱の自己署名証明書を信頼登録してください。

## フォルダ構成

GitHub 公開時に迷わないように、ソースコードと単体EXEを分けています。

```text
code/native_ui/                          # C++ / Qt ソースコード
standalone_exe/windows/                  # Windows 単体配布EXE
standalone_exe/windows/ASEappSurfaceBuilder-1.0.0-Windows.exe
requirements.txt                         # Python補助ツール用ライブラリ
environment.yml                          # Conda環境構築用
PACKAGING.md                             # 配布物の再生成手順
```

`standalone_exe/windows/ASEappSurfaceBuilder-1.0.0-Windows.exe` は1ファイルで配布できます。ソースから使う場合は次の手順で環境を作ってください。

## 単体EXEを使わない場合

推奨は Conda です。Qt 6 / CMake / Python補助ツールをまとめて入れます。

```bash
conda env create -f environment.yml
conda activate aseapp-surface-builder
pip install -r requirements.txt
```

Windows でビルドして起動する例です。

```powershell
$env:CONDA_PREFIX = (conda info --base) + "\envs\aseapp-surface-builder"
cmake -S code/native_ui -B code/native_ui/build -DCMAKE_PREFIX_PATH="$env:CONDA_PREFIX\Library"
cmake --build code/native_ui/build --config Release --parallel 2
.\code\native_ui\build\Release\ASEappNativeUI.exe
```

Linux / macOS では次を基本にします。

```bash
cmake -S code/native_ui -B code/native_ui/build -DCMAKE_PREFIX_PATH="$CONDA_PREFIX"
cmake --build code/native_ui/build --config Release --parallel 2
./code/native_ui/build/ASEappNativeUI
```

## 主な機能
- POSCAR / CONTCAR / `.vasp` / `.xyz` / `.cif` / `.pdb` / `.xsf` / `.json` / `.aseproj` / `.vesta` の読み込み
- VESTA 風の原子色・球表示・視点操作
- 左クリック選択、Ctrl 追加選択、Ctrl + 左ドラッグで重なった奥の原子も選択、Esc で解除
- 選択原子の直上 / 直下 / 原子間 / 多点中心への原子配置
- 複数選択時の一括直上 / 直下配置
- 周期表から生成元素を選択し、詳細情報はホバー時だけ表示
- 前駆体 CSV 保存 / 読込 / 現在の配置位置への一括配置
- Supercell 作成、真空層追加、真空層除去、セル軸傾き（真空層操作後も Supercell 倍率を保持）
- slab 全体の a/b/c 方向移動
- 論文・学会用の原子一覧 PNG 出力
- POSCAR 形式で保存

## 基本操作

1. 構造ファイルをドラッグ&ドロップ、または `Open` で読み込みます。
2. 原子をクリックして選択します。
3. 必要なら周期表から生成元素を選びます。
4. 配置位置を選び、`Apply` で配置します。
5. `Save` で POSCAR 形式として保存します。

詳しい手順は `code/native_ui/QUICKSTART.txt` または配布フォルダ内の `QUICKSTART.txt` を参照してください。

## 重要なショートカット

| 操作 | 内容 |
| --- | --- |
| 左ドラッグ | 視点回転 |
| 右 / 中ドラッグ | パン |
| ホイール | ズーム |
| 左クリック | 原子選択 |
| Ctrl + 左クリック | 選択追加 / 解除 |
| Ctrl + 左ドラッグ | 重なった奥の原子も追加選択 |
| Shift + 左ドラッグ | 選択原子を画面平面内で移動 |
| Esc | 選択解除 |
| F / ダブルクリック | フィット |
| A / B / C | direct a/b/c 方向表示 |
| Ctrl+Alt+A/B/C | reciprocal a*/b*/c* 方向表示 |

通常の slab のように c 軸が ab 面法線と平行なセルでは、direct c と reciprocal c* は同じ向きに見えます。c 軸を傾けるなどして非直交になった場合は、c と c* が異なる視点になります。

## 前駆体 CSV

保存時に前駆体名を入力します。保存 CSV は前駆体名と相対座標だけです。

```csv
name,element,dx_angstrom,dy_angstrom,dz_angstrom
GaNH,Ga,0.000000,0.000000,0.000000
GaNH,N,0.000000,0.000000,1.950000
GaNH,H,0.000000,0.000000,2.950000
```

`dx/dy/dz` は、前駆体内で最も低い原子（cartesian z 最小）を基準にした Å 単位の相対座標です。
読み込んだ前駆体名は、生成元素とは別の「前駆体」ドロップダウンに表示されます。前駆体を選び、通常の原子配置と同じ「配置位置」を指定して「前駆体配置」を押すと、前駆体内で最も低い原子がその位置へ来るように配置できます。

## トラブルシュート

- `freetype.dll` などが見つからない場合: ZIP 展開フォルダ全体、または単体 launcher 版 `ASEappSurfaceBuilder-1.0.0-Windows.exe` を使ってください。
- Windows の Application Control / Smart App Control により起動が止められる場合: これは DLL 不足ではなく Windows 側の実行制御です。GitHub Releases で他の人へ配布する正式版は、自己署名ではなく Microsoft Trusted Signing や OV/EV などの信頼済みコード署名で署名してください。自己署名は検証用で、Smart App Control 環境では許可されない場合があります。
- 画面が重い場合: ボンド表示やプレビューを減らし、必要な時だけ Supercell を大きくしてください。
- 周期表が見切れる場合: 現行版では説明文・凡例・ボタンを出さず、周期表だけを固定サイズ表示します。古い配布物を起動していないか確認してください。

## 開発・再ビルド

ビルドと配布物作成は `PACKAGING.md` を参照してください。
