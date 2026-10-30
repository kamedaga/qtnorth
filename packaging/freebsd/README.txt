qtNorth for FreeBSD amd64

対応OS・Qtのバージョンはbuild-info.txtを参照してください。
依存パッケージをrootでインストールします。

  pkg install qt6-base qt6-svg qt6-imageformats qtkeychain-qt6 xdg-utils

展開したフォルダー内で、通常ユーザーのデスクトップ端末から起動します。

  ./qtNorth

認証情報の保存にはD-Busと、起動・解除済みのキーリングまたはKWalletが必要です。
ソースとビルド手順はsource/にあります。
