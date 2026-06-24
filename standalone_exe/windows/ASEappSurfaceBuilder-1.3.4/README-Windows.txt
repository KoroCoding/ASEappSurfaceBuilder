ASEapp Surface Builder 1.3.4 for Windows
========================================

このフォルダ内の `ASEappSurfaceBuilder-1.3.4.exe` を実行してください。

注意
----
この EXE は同じフォルダ内の Qt DLL、VC++ runtime、`plugins`、`assets`、`tools` を使用します。
フォルダ構成を崩したり、EXE だけ別の場所へ移動したりしないでください。

セキュリティソフト・Smart App Control について
----------------------------------------------
旧来の単体自己展開 launcher EXE では、Smart App Control / Code Integrity の Enterprise signing policy により起動前にブロックされる場合がありました。
この 1.3.4 フォルダ構成では起動確認済みです。
自己署名証明書と信頼化スクリプトは同梱していますが、自己署名だけでは組織/Windows の実行制御を必ず回避できません。
