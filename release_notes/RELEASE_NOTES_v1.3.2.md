# ASEapp Surface Builder v1.3.2 リリースノート

対象: Windows 配布版 `ASEappSurfaceBuilder-1.3.2-Windows.exe` / macOS 配布版 `ASEappSurfaceBuilder-1.3.2-macOS.dmg`

## 概要

v1.3.2 は、原子種ペアごとのボンド表示最大距離編集と macOS 起動時の見え方を修正した PATCH リリースです。macOS DMG に続き、Windows 単体 launcher も同じバージョンとして再生成しました。

## 修正内容

- 通常導線を `ボンド表示最大距離` に統一し、Ga-N が何十行も並ぶ原子対単位の一覧を通常 UI から外しました。
- H 追加後も、原子種の組み合わせごとに `Ga-N`、`Ga-H`、`N-H` のように1行へ集約されます。
- この画面では原子座標を変更せず、表示するボンドの最大距離だけを更新します。
- 元素ペアの表示順は、アルファベット順ではなく構造内に現れた原子種順に揃えました。
- macOS でも MainWindow が表示されるまで起動スプラッシュを表示します。
- Windows ローカル自己署名版向けに、公開証明書、信頼登録 PowerShell、Windows 向け README を配布物へ同梱するようにしました。

## 配布物

- Windows 単体 launcher: `ASEappSurfaceBuilder-1.3.2-Windows.exe`
- Windows ZIP 展開版: `ASEappSurfaceBuilder-1.3.2-Windows.zip`
- Windows 自己署名信頼補助: `ASEappSurfaceBuilderLocalCodeSigning.cer` / `ASEapp-Windows-Trust-LocalCertificate.ps1` / `README-Windows.txt`
- macOS DMG: `ASEappSurfaceBuilder-1.3.2-macOS.dmg`

## 検証

- Release build
- CTest
- Ga/N/H 原子種ペア集約と表示最大距離変更の GUI self-test
- Windows ZIP payload inspection / Authenticode 署名確認 / 起動 smoke test
- macOS DMG 作成、codesign 検証、hdiutil verify
