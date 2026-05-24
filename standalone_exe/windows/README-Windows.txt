ASEapp Surface Builder for Windows
==================================

通常の使い方
------------
単体 launcher 版を使う場合は、ASEappSurfaceBuilder-1.3.2-Windows.exe を実行します。

ZIP 展開版を使う場合は、フォルダ構成を崩さずに bin\ASEappNativeUI.exe を実行します。
bin / plugins / translations を含む一式を同じ場所に置いてください。

自己署名証明書を信頼して開く
----------------------------
この Windows 版は、開発確認用の自己署名コード署名で ASEapp 製 EXE を署名している場合があります。
配布元を信頼できる場合だけ、次の PowerShell をこの README と同じフォルダで実行してください。

  powershell -ExecutionPolicy Bypass -File .\ASEapp-Windows-Trust-LocalCertificate.ps1

このスクリプトは、同梱の ASEappSurfaceBuilderLocalCodeSigning.cer を現在の Windows ユーザーの
Trusted Root Certification Authorities と Trusted Publishers に登録し、同じフォルダ配下の ASEapp
ファイルからダウンロード隔離マークを解除します。管理者権限は通常不要です。

注意
----
- この操作は、この自己署名証明書で署名された ASEapp 製 EXE を現在のユーザーで信頼するためのものです。
- Microsoft や公的 CA が発行した正式なコード署名になるわけではありません。
- SmartScreen / Smart App Control / 組織管理ポリシーによるブロックは残ることがあります。
- 完全に警告を減らして広く配布するには、Microsoft Store、Microsoft Trusted Signing、または OV/EV などの信頼済みコード署名を使ってください。
- 配布元を信頼できない場合は、スクリプトを実行しないでください。
