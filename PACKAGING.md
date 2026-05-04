# ASEapp Surface Builder packaging

この文書は、ASEapp Surface Builder の配布物を再生成するための手順です。

## フォルダ構成

```text
code/native_ui/                          # C++ / Qt ソースコード
standalone_exe/windows/                  # Windows 単体配布EXE
standalone_exe/windows/ASEappSurfaceBuilder-1.0.0-Windows.exe
requirements.txt                         # Python補助ツール用ライブラリ
environment.yml                          # Conda環境構築用
```

## 最終配布物

GitHub Releases には次の名前でアップロードします。

- Windows 単体 launcher: `standalone_exe/windows/ASEappSurfaceBuilder-1.0.0-Windows.exe`
- Windows ZIP: `code/native_ui/dist/ASEappSurfaceBuilder-1.0.0-Windows.zip`
- Linux: `code/native_ui/dist/ASEappSurfaceBuilder-1.0.0-Linux.tar.gz`
- macOS: `code/native_ui/dist/ASEappSurfaceBuilder-1.0.0-macOS.dmg`

Windows では通常、単体 launcher 版だけを配布すれば十分です。

ZIP 展開版を渡す場合は、`bin` / `plugins` / `translations` / `README.txt` / `QUICKSTART.txt` / `CHANGELOG.txt` を含むフォルダ一式で渡してください。`bin/ASEappNativeUI.exe` だけを単独で移動すると DLL 不足になります。

## アイコン管理

アイコンの元ファイルは次の 1 つだけです。

- `code/native_ui/assets/aseapp_surface_builder_icon.svg`

Windows 用 `.ico`、アプリ内表示用 `.png`、macOS 用 `.icns` はビルド時に自動生成されます。迷わないように、`assets` には生成済みアイコンを置かないでください。

## 環境構築

Conda を使う場合は次で必要な Qt / CMake / Python 補助ライブラリを入れます。

```bash
conda env create -f environment.yml
conda activate aseapp-surface-builder
pip install -r requirements.txt
```

`requirements.txt` はアイコン生成などの Python 補助ツール用です。C++/Qt 本体のビルドには `environment.yml` の `qt6-main` と `cmake` が必要です。

## Windows ビルド

```powershell
conda activate aseapp-surface-builder
$env:CONDA_PREFIX = (conda info --base) + "\envs\aseapp-surface-builder"

cmake -S code/native_ui -B code/native_ui/build -DCMAKE_PREFIX_PATH="$env:CONDA_PREFIX\Library"
cmake --build code/native_ui/build --config Release --parallel 2
cmake --build code/native_ui/build --config Release --target package --parallel 2
powershell -ExecutionPolicy Bypass -File code/native_ui/package_windows_launcher.ps1
```

`package_windows_launcher.ps1` は、既定で次を作成します。

```text
standalone_exe/windows/ASEappSurfaceBuilder-1.0.0-Windows.exe
```

### ローカル署名（開発確認用）

開発中に署名状態を確認する場合は、ローカル署名証明書を入れてから再パッケージできます。

```powershell
powershell -ExecutionPolicy Bypass -File code/native_ui/tools/install_local_codesign_cert.ps1
powershell -ExecutionPolicy Bypass -File code/native_ui/package_windows_launcher.ps1
```

`package_windows_launcher.ps1` は、既定で `CN=ASEapp Surface Builder Local Code Signing` の証明書があれば、単体 launcher と同梱される `.exe` / `.dll` を署名します。明示する場合は次を使ってください。

```powershell
$env:ASEAPP_CODESIGN_THUMBPRINT = "<証明書の Thumbprint>"
powershell -ExecutionPolicy Bypass -File code/native_ui/package_windows_launcher.ps1
```

この自己署名はローカル開発確認向けです。Smart App Control / Windows Application Control 環境では、自己署名が Windows の実行制御ポリシーに許可されない場合があります。GitHub Releases で他の人へ配布する正式版は、Microsoft Trusted Signing や OV/EV などの信頼済みコード署名で署名してください。

## 展開版を確認する

```powershell
$zip = (Resolve-Path 'code/native_ui/dist/ASEappSurfaceBuilder-1.0.0-Windows.zip').Path
$target = Join-Path $env:TEMP 'aseapp_surface_builder_verify'
if (Test-Path $target) { Remove-Item -LiteralPath $target -Recurse -Force }
Expand-Archive -LiteralPath $zip -DestinationPath $target -Force
$p = Start-Process -FilePath (Join-Path $target 'bin/ASEappNativeUI.exe') -PassThru
Start-Sleep -Seconds 3
$p.Refresh()
"HAS_EXITED=$($p.HasExited) PID=$($p.Id)"
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
Remove-Item -LiteralPath $target -Recurse -Force -ErrorAction SilentlyContinue
```

`HAS_EXITED=False` なら、少なくとも起動直後に依存 DLL 不足で終了していないことを確認できます。

組織管理PCなどで Windows Application Control / Smart App Control が EXE を止める場合は、この起動確認がポリシーで失敗することがあります。その場合は、アプリの依存DLL不足ではなく端末側ポリシーとして扱い、信頼済みコード署名または許可済み端末で確認してください。

## Linux / macOS ビルド

```bash
conda activate aseapp-surface-builder
cmake -S code/native_ui -B code/native_ui/build -DCMAKE_PREFIX_PATH="$CONDA_PREFIX"
cmake --build code/native_ui/build --config Release --parallel 2
cmake --build code/native_ui/build --target package --config Release
```

macOS 版は後で実機でビルドし、`ASEappSurfaceBuilder-1.0.0-macOS.dmg` として Release assets に追加します。

## 削除してよい生成物

次は再生成可能な一時/中間生成物です。

- `$tmp/`
- `code/native_ui/build/`
- `code/native_ui/install/`
- `code/native_ui/dist/`
