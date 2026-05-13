# ASEapp Surface Builder v1.1.0 リリースノート

リリース日: 2026-05-05
対象: Windows 配布版 `ASEappSurfaceBuilder-1.1.0-Windows.exe`

## 概要

v1.1.0 は、表面 slab 上で複雑な吸着分子の向きを GUI で微調整し、その結果を ASE に戻して大量配置へ展開するためのアップデートです。v1.0.0 の通常の表面編集機能は維持したまま、吸着分子を剛体グループとして扱う「吸着分子ポーズ編集」を追加しました。

## 主な追加機能

### 吸着分子ポーズ編集

- 選択原子群を吸着分子グループとして登録できます。
- グループ内の相対座標を保持したまま、cartesian / cell 軸 / slab 法線方向へ剛体並進できます。
- pivot 原子、グループ重心、質量中心などを基準に剛体回転できます。
- 選択中の 2 原子を結合軸として、その直線まわりに剛体回転できます。
  - 軸端点の 2 原子は固定されます。
  - 軸外原子だけが回転します。
- 選択結合長を指定値へ調整できます。
  - 可動側成分は結合方向にスライドし、内部相対座標を保持します。
- 並進 / 回転 / 結合長調整の Apply 後位置を、実座標を変えずに半透明プレビューできます。

### ASE 連携出力

- 構造全体の extended XYZ を保存できます。
- 吸着分子のみの plain XYZ を出力できます。
- 吸着分子ポーズを `.pose.json` として保存できます。
- ASE で別 slab / 別吸着サイトへ展開しやすい Python snippet を出力できます。
- 通常 XYZ 保存時は、cell / PBC が標準では保持されないことを警告します。

### 原子配置 UI の整理

- `2原子の間` や `3原子の中心` のような個別モードを整理し、配置位置を「選択原子の中心」に統一しました。
- 2 原子、3 原子、4 原子以上のどの場合でも、選択原子すべての cartesian 座標平均へ 1 個だけ配置します。
- 直上 / 直下は従来どおり、複数選択時に各選択原子へ一括配置できます。

### VESTA 風ボンド距離設定

- 元素ペアごとに、表示するボンド距離の `min Å` / `max Å` を設定できます。
- 設定は VESTA の `SBOND` 相当値を上書きし、次回起動にも保存されます。
- 2 原子を選択して開くと、その元素ペアと実測距離を初期候補として使えます。

## 修正・改善

- Undo / Redo が、構造座標だけでなく選択状態、吸着分子グループ、pivot / 回転軸状態も同期して復元するようになりました。
- 実分子サンプルとして methanol on Cu slab を使った GUI 自己テストを追加しました。
- extended XYZ の native reload と ASE round-trip テストを追加しました。
- GUI ヘルプ、Quick Start、README、配布ドキュメントを v1.1.0 の機能に合わせて更新しました。

## 配布物

- Windows 単体版: `ASEappSurfaceBuilder-1.1.0-Windows.exe`（GitHub Releases 添付）
- Windows ZIP 展開版: `ASEappSurfaceBuilder-1.1.0-Windows.zip`（GitHub Releases 添付）
- 旧版が必要な場合は、GitHub Releases の各タグから取得してください。
- macOS v1.1.0 は macOS 環境で再パッケージしてください。

## 検証済み項目

- Release ビルド成功。
- GUI 自己テスト `adsorbate_pose_gui_self_test` 成功。
- ASE / extxyz round-trip テスト `ase_extxyz_roundtrip` 成功。
- Windows ZIP 展開版の起動確認成功。
- Windows 単体 launcher 版の起動確認成功。
- Windows 単体 launcher 版のローカルコード署名状態: `Valid`。

## 注意点

- plain XYZ は分子単体や座標受け渡しには便利ですが、slab の cell / PBC を標準では保持しません。ASE へ slab として戻す用途では extended XYZ を推奨します。
- 結合長調整は、選択結合を除いた分子グラフから可動側を推定します。環状分子など自動分離できない場合は、可動側原子群を明示的に選択してください。
- 自己署名はローカル開発確認向けです。組織管理 PC や Smart App Control 環境では、信頼済み署名が必要になる場合があります。
