# ASEapp Surface Builder v1.3.2 リリースノート

対象: macOS 配布版 `ASEappSurfaceBuilder-1.3.2-macOS.dmg`

## 概要

v1.3.2 は、原子種ペアごとのボンド表示最大距離編集と macOS 起動時の見え方を修正した PATCH リリースです。Windows 版の同梱バイナリは今回この Mac では再生成していません。

## 修正内容

- 通常導線を `ボンド表示最大距離` に統一し、Ga-N が何十行も並ぶ原子対単位の一覧を通常 UI から外しました。
- H 追加後も、原子種の組み合わせごとに `Ga-N`、`Ga-H`、`N-H` のように1行へ集約されます。
- この画面では原子座標を変更せず、表示するボンドの最大距離だけを更新します。
- 元素ペアの表示順は、アルファベット順ではなく構造内に現れた原子種順に揃えました。
- macOS でも MainWindow が表示されるまで起動スプラッシュを表示します。

## 配布物

- macOS DMG: `ASEappSurfaceBuilder-1.3.2-macOS.dmg`

## 検証

- Release build
- CTest
- Ga/N/H 原子種ペア集約と表示最大距離変更の GUI self-test
- macOS DMG 作成、codesign 検証、hdiutil verify
