# ASEapp Surface Builder v1.3.1 リリースノート

対象: Windows 配布版 `ASEappSurfaceBuilder-1.3.1-Windows.exe`

## 概要

v1.3.1 は、v1.3.0 の Windows 単体 launcher / ZIP の packaging 修正版です。アプリ機能は v1.3.0 と同じで、Smart App Control / Windows Application Control 環境で同梱 DLL が止まりやすかった署名方法を修正しました。

## 修正内容

- `package_windows_launcher.ps1` が第三者提供の Qt / OpenSSL / FreeType などの DLL をローカル自己署名で再署名しないように変更しました。これにより、元の DLL ハッシュを維持し、Windows の reputation 判定で不利になりにくくしています。
- 単体 launcher の抽出キャッシュに `ASEAPP_PAYLOAD_MANIFEST.tsv` を同梱し、起動前にキャッシュ内ファイルのサイズと FNV-1a 64-bit hash を検証するようにしました。破損・欠落したキャッシュは再展開されます。
- Windows が `0xC0E90002` で DLL をブロックした場合、launcher が該当 payload cache を削除し、次回起動で古いキャッシュを使い回さないようにしました。

## 配布物

- Windows 単体版: `ASEappSurfaceBuilder-1.3.1-Windows.exe`
- Windows ZIP 展開版: `ASEappSurfaceBuilder-1.3.1-Windows.zip`

## 検証

- Release build
- CTest
- ZIP payload inspection
- Authenticode 状態確認
- Windows Smart App Control / Code Integrity event 差分確認つきの起動 smoke test
