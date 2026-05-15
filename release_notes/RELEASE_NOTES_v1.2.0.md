# ASEapp Surface Builder v1.2.0 リリースノート

対象: Windows 配布版 `ASEappSurfaceBuilder-1.2.0-Windows.exe` / macOS 配布版 `ASEappSurfaceBuilder-1.2.0-macOS.dmg`

## 概要

v1.2.0 は、VESTA に近い描画内容と Windows standalone 起動体験を改善した MINOR リリースです。既存ファイル形式の互換性を保ったまま、周期境界をまたぐ格子外表示、VESTA風ボンド描画、原子一覧PNG出力の調整機能を追加しました。

## 主な変更

- `格子外` トグルで、周期境界でつながる単位格子外の複製原子・ボンドを VESTA 風に表示/非表示できるようにしました。
- ボンドを両端原子の色で半分ずつ、端から中心まで同じ太さで表示するようにしました。
- 初期表示の原子サイズとフィット範囲を VESTA の ball-and-stick 表示に近づけました。
- `magnemite_wiley-iron-oxides.cif` のように近接重複サイトを含む CIF で、原子が不自然に小さくなる問題を抑制しました。
- 起動直後に splash を表示し、standalone payload 展開後にフェードアウトするようにしました。
- standalone payload を `%LOCALAPPDATA%` 配下にハッシュ付きでキャッシュし、2回目以降の起動を軽くしました。
- 原子一覧PNG出力に横プレビュー、原子名 prefix/suffix、表示順入れ替えを追加しました。
- GUI self-test に `格子外` トグルで周期境界ボンドが増える検証を追加しました。

## 配布物

- Windows 単体版: `ASEappSurfaceBuilder-1.2.0-Windows.exe`
- Windows ZIP 展開版: `ASEappSurfaceBuilder-1.2.0-Windows.zip`
- macOS DMG: `ASEappSurfaceBuilder-1.2.0-macOS.dmg`

macOS DMG には `ASEappNativeUI.app`、Applications へのリンク、自己署名証明書、初回起動補助用の `ASEapp-macOS-Allow-This-App.command`、macOS 向け README を同梱しています。

## 検証

- Release build
- CTest
- GUI self-test JSON
- Windows standalone launcher 再生成
- Authenticode 署名状態確認
- Qt platform plugin 同梱確認
- macOS package script による Release build / Qt runtime 同梱 / DMG 作成
- macOS staged app と DMG の `codesign --verify`
- `hdiutil verify` と DMG mount 後の同梱ファイル確認
