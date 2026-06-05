ASEapp Surface Builder for Windows
==================================

通常の使い方
------------
単体 launcher 版を使う場合は、`ASEappSurfaceBuilder-1.3.3-Windows.exe` を実行してください。
ZIP 展開版を使う場合は、フォルダ構成を崩さずに `bin\ASEappNativeUI.exe` を実行してください。
`bin` / `plugins` / `translations` を含む一式を同じ場所に置いてください。

署名について
------------
このローカル開発ビルドは、Smart App Control / Code Integrity で自己署名ランチャーがブロックされる環境があるため、既定では署名なしで作成しています。
配布先で警告を減らすには、Microsoft Trusted Signing、Microsoft Store、または公開 CA の OV/EV コード署名証明書など、Windows が信頼する署名で再署名してください。

補足
----
- `ASEapp-Windows-Trust-LocalCertificate.ps1` は、ローカル自己署名で作成した場合だけ利用する補助スクリプトです。
- SmartScreen / Smart App Control / 組織ポリシーによるブロックは、ローカル自己署名だけでは解除できない場合があります。
- ファイルをインターネットから取得した場合は、必要に応じてプロパティの「許可する」または `Unblock-File` でダウンロードマークを解除してください。
