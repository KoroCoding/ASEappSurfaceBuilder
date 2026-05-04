ASEapp Surface Builder for macOS
================================

通常の使い方
------------
1. ASEappNativeUI.app を Applications へドラッグします。
2. 右クリックまたは Control + クリックで「開く」を選びます。
3. macOS の確認ダイアログでもう一度「開く」を選びます。

自己署名証明書を信頼して開く
----------------------------
Developer ID 署名を使わない配布物なので、macOS Gatekeeper は初回起動時に警告を出すことがあります。

警告を減らして使う場合は、DMG 内の次を実行してください。

  ASEapp-macOS-Allow-This-App.command

このスクリプトは、アプリを ~/Applications にコピーし、同梱の自己署名コード署名証明書をログインキーチェーンへ信頼登録し、コピーしたアプリのダウンロード隔離属性を解除してから起動します。macOS が許可する環境では Gatekeeper のローカル例外登録も試みます。

注意
----
- 他の Mac で再署名する必要は通常ありません。
- ただし、Developer ID + Notarization ではないため、完全に警告なしの配布にはできません。
- `ASEapp-macOS-Allow-This-App.command` は、この DMG の配布元を信頼できる場合だけ実行してください。
