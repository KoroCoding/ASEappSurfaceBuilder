ASEapp Surface Builder for Windows
==================================

通常の使い方
------------
推奨: このフォルダの `Run-ASEappSurfaceBuilder-1.3.4.cmd` を実行してください。
直接起動する場合は、以下を実行してください。

  ASEappSurfaceBuilder-1.3.4\ASEappSurfaceBuilder-1.3.4.exe

`ASEappSurfaceBuilder-1.3.4` フォルダ内の DLL / plugins / assets / tools は同じフォルダ構成のままにしてください。

セキュリティソフト・Smart App Control について
----------------------------------------------
旧来の単体自己展開 launcher EXE は、Smart App Control / Code Integrity の Enterprise signing policy で止められることがあります。
そのため 1.3.4 では、確認済みの通常ポータブル構成を推奨します。
自己署名証明書は `ASEappSurfaceBuilder-1.3.4` フォルダに同梱していますが、自己署名だけでは組織/Windows の実行制御を必ず回避できません。
ブロックされる場合は、組織の許可リストまたは正式なコード署名証明書での配布が必要です。
