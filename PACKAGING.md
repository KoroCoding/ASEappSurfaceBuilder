# ASEapp Surface Builder packaging

この文書は、ASEapp Surface Builder の配布物を再生成するための手順です。

## フォルダ構成

```text
code/native_ui/                          # C++ / Qt ソースコード
requirements.txt                         # Python補助ツール用ライブラリ
environment.yml                          # Conda環境構築用
standalone_exe/                          # 確認済み配布物の配置先
```

## 最終配布物

現在リポジトリに同梱している確認済み配布物は次の通りです。

- Windows: `standalone_exe/windows/ASEappSurfaceBuilder-1.3.0-Windows.exe`
- Windows: `standalone_exe/windows/ASEappSurfaceBuilder-1.2.0-Windows.exe`
- Windows: `standalone_exe/windows/ASEappSurfaceBuilder-1.1.2-Windows.exe`
- Windows: `standalone_exe/windows/ASEappSurfaceBuilder-1.1.1-Windows.exe`
- Windows: `standalone_exe/windows/ASEappSurfaceBuilder-1.1.0-Windows.exe`
- Windows: `standalone_exe/windows/ASEappSurfaceBuilder-1.0.0-Windows.exe`
- macOS: `standalone_exe/macos/ASEappSurfaceBuilder-1.2.0-macOS.dmg`
- macOS: `standalone_exe/macos/ASEappSurfaceBuilder-1.1.0-macOS.dmg`
- macOS: `standalone_exe/macos/ASEappSurfaceBuilder-1.0.0-macOS.dmg`

次回以降の GitHub Releases には次の名前でアップロードします。

- Windows 単体 launcher: `ASEappSurfaceBuilder-1.3.0-Windows.exe`
- Windows ZIP: `ASEappSurfaceBuilder-1.3.0-Windows.zip`
- Linux: `ASEappSurfaceBuilder-1.3.0-Linux.tar.gz`
- macOS: `ASEappSurfaceBuilder-1.3.0-macOS.dmg`

配布物はローカルでは `standalone_exe/` や `code/native_ui/dist/` に生成します。確認済みの最終成果物だけを `standalone_exe/<platform>/` に置き、途中生成物はコミットしません。

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
standalone_exe/windows/ASEappSurfaceBuilder-1.3.0-Windows.exe
```

現在このリポジトリに同梱している Windows 版は `standalone_exe/windows/ASEappSurfaceBuilder-1.3.0-Windows.exe` です。互換確認用として `standalone_exe/windows/ASEappSurfaceBuilder-1.2.0-Windows.exe`、`standalone_exe/windows/ASEappSurfaceBuilder-1.1.2-Windows.exe`、`standalone_exe/windows/ASEappSurfaceBuilder-1.1.1-Windows.exe`、`standalone_exe/windows/ASEappSurfaceBuilder-1.1.0-Windows.exe`、`standalone_exe/windows/ASEappSurfaceBuilder-1.0.0-Windows.exe` も残しています。

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
$zip = (Resolve-Path 'code/native_ui/dist/ASEappSurfaceBuilder-1.3.0-Windows.zip').Path
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

## Linux ビルド

```bash
conda activate aseapp-surface-builder
cmake -S code/native_ui -B code/native_ui/build -DCMAKE_PREFIX_PATH="$CONDA_PREFIX"
cmake --build code/native_ui/build --config Release --parallel 2
cmake --build code/native_ui/build --target package --config Release
```

## macOS ビルド

macOS では次のスクリプトを使ってください。

```bash
./code/native_ui/package_macos.sh
```

このスクリプトは、Release ビルド、Qt ランタイム同梱、自己署名、DMG 作成、署名検証、DMG 検証をまとめて実行します。`project(... VERSION ...)` の値に合わせてファイル名が決まり、v1.3.0 では次を作成します。

```text
standalone_exe/macos/ASEappSurfaceBuilder-1.3.0-macOS.dmg
```

互換確認用として `standalone_exe/macos/ASEappSurfaceBuilder-1.2.0-macOS.dmg`、`standalone_exe/macos/ASEappSurfaceBuilder-1.1.0-macOS.dmg`、`standalone_exe/macos/ASEappSurfaceBuilder-1.0.0-macOS.dmg` も残しています。

`Documents/GitHub` など macOS FileProvider 配下で直接 CPack staging を作ると、`com.apple.FinderInfo` などの拡張属性が `.app` に付いて `codesign` が失敗することがあります。そのため、スクリプトは staging を `/private/tmp/aseapp-surface-builder-cpack` に作成し、検証済み DMG だけを `standalone_exe/macos/` へコピーします。

### macOS 自己署名について

既定では `ASEapp Surface Builder Local Code Signing` という自己署名 Code Signing 証明書をローカルキーチェーンに作成し、その証明書で `.app` と `.dmg` を署名します。署名は `.app` に同梱されるため、他の Mac で再署名する必要は通常ありません。

ただし、自己署名は Apple Developer ID 署名ではないため、配布先 Mac では Gatekeeper の警告が出ることがあります。DMG には次を同梱します。

- `ASEappSurfaceBuilderLocalCodeSigning.cer`: 自己署名の公開証明書
- `ASEapp-macOS-Allow-This-App.command`: アプリを `~/Applications` にコピーし、公開証明書をログインキーチェーンに信頼登録し、コピーしたアプリのダウンロード隔離属性を解除して起動する補助スクリプト。macOS が許可する環境では Gatekeeper のローカル例外登録も試みます。
- `README-macOS.txt`: 配布先 Mac 向けの説明

配布先で完全に警告なしにするには Apple Developer ID + Notarization が必要です。Developer ID を取得しない場合、自己署名 + 配布先での明示的な信頼登録が macOS の仕様上の上限です。

ad-hoc 署名に戻したい場合だけ、次のように指定します。

```bash
ASEAPP_CODESIGN_IDENTITY=- ./code/native_ui/package_macos.sh
```

### Developer ID を使う場合

Developer ID 証明書と `notarytool` の keychain profile がある場合は、次のように正式署名と notarization まで実行できます。

```bash
export ASEAPP_CODESIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)"
export ASEAPP_NOTARY_PROFILE="aseapp-notary"
./code/native_ui/package_macos.sh
```

`ASEAPP_NOTARY_PROFILE` を指定した場合は、notarization に必要な Hardened Runtime も有効になります。自己署名の通常ビルドでは、同梱Qtライブラリの実行時検証と衝突するため Hardened Runtime は有効にしません。

`ASEAPP_NOTARY_PROFILE` は事前に次のようなコマンドで作成してください。

```bash
xcrun notarytool store-credentials aseapp-notary \
  --apple-id "apple-id@example.com" \
  --team-id "TEAMID" \
  --password "app-specific-password"
```

## 削除してよい生成物

次は再生成可能な一時/中間生成物、または GitHub Releases に添付する配布バイナリです。公開リポジトリでは追跡しません。

- `$tmp/`
- `code/native_ui/build/`
- `code/native_ui/install/`
- `code/native_ui/dist/`
- `standalone_exe/`
