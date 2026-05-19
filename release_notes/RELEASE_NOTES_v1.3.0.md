# ASEapp Surface Builder v1.3.0 リリースノート

対象: Windows 配布版 `ASEappSurfaceBuilder-1.3.0-Windows.exe`

## 概要

v1.3.0 は、Supercell 作成に任意の 3x3 整数格子変換行列を入力できるようにした MINOR リリースです。既存の a/b/c 倍率入力は既定ワークフローとして残しつつ、必要な場合だけ行列入力へ切り替えられます。

## 主な変更

- Supercell ダイアログに `格子変換行列を直接入力する` モードを追加しました。
- `sqrt(2) x sqrt(2) R45` のような 45 度回転スーパーセルを、プリセットではなくユーザー入力の行列で作成できます。例: `[1 1 0; -1 1 0; 0 0 1]`。
- 行列の determinant と生成後の原子数見込みを Apply 前に表示し、determinant が正でない行列や大きすぎるセルは実行前に止めます。
- 行列変換後のセル、原子数、分率座標範囲を GUI self-test で検証するチェックを追加しました。
- 操作ガイドと同梱 quick-start 文書を、行列入力 Supercell に合わせて更新しました。

## 配布物

- Windows 単体版: `ASEappSurfaceBuilder-1.3.0-Windows.exe`
- Windows ZIP 展開版: `ASEappSurfaceBuilder-1.3.0-Windows.zip`

macOS DMG はこの Windows ビルド作業では再生成していません。macOS 版は別途 macOS 環境で同じバージョンとして再ビルドしてください。

## 検証

- Release build
- CTest
- GUI self-test JSON
- Windows ZIP package 再生成
- Windows standalone launcher 再生成
- Qt platform plugin 同梱確認
- Windows standalone launcher 起動スモーク確認
