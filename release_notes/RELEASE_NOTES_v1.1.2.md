# ASEapp Surface Builder v1.1.2 リリースノート

リリース日: 2026-05-15
対象: Windows 配布版 `ASEappSurfaceBuilder-1.1.2-Windows.exe`

## 概要

v1.1.2 は、v1.1.1 のユーザー向け機能を変えずに、起動処理・単一起動処理・Windows standalone launcher の保守性を高めた PATCH リリースです。あわせて今後のバージョン番号ルールを `VERSIONING.md` として明文化し、最新版 Windows 配布物を再生成しました。

## 修正・改善

- `main.cpp` から Qt plugin path 設定を `AppStartup` へ分離しました。
- 単一起動制御と既存ウィンドウへのファイル転送を `SingleInstance` へ分離しました。
- Windows standalone launcher の `HANDLE` / `LocalFree` 管理を RAII 化し、エラー経路でも解放処理が分散しないように整理しました。
- `VERSIONING.md` を追加し、PATCH / MINOR / MAJOR の判断基準、タグ名、成果物名の規則を明文化しました。

## 配布物

- Windows 単体版: `ASEappSurfaceBuilder-1.1.2-Windows.exe`
- Windows ZIP 展開版: `ASEappSurfaceBuilder-1.1.2-Windows.zip`
- Windows 旧版 v1.1.1 / v1.1.0 / v1.0.0 は互換確認用として `standalone_exe/windows/` に残しています。
- macOS 版は現時点では v1.1.0 が同梱版です。v1.1.2 は macOS 環境で再パッケージしてください。

## 検証済み項目

- Release ビルド成功。
- GUI 自己テスト `adsorbate_pose_gui_self_test` 成功。
- ASE / extxyz round-trip テスト `ase_extxyz_roundtrip` 成功。
- Windows ZIP 展開版の必須ファイル確認成功。
- Windows 単体 launcher 版の起動確認成功。
- build-tree EXE / standalone EXE の単一起動確認成功。
- CIF サンプル 2 件と生成 VASP ファイルの読み込みスモーク確認成功。
- Windows 単体 launcher 版のローカルコード署名状態確認。

## 注意点

- v1.1.2 は機能追加ではなく、保守性改善と検証済み Windows 配布物の再生成を目的とした PATCH リリースです。
- 自己署名はローカル開発確認向けです。組織管理 PC や Smart App Control 環境では、信頼済み署名が必要になる場合があります。
