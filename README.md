# qtNorth

ビルドには C++17、CMake 3.21以上、Qt 6.4以上（Widgets / Network / SVG / Test）、QtKeychain（Qt 6版）が必要です。

## Linux（Ubuntu）

```sh
sudo apt install g++ cmake ninja-build qt6-base-dev qt6-svg-dev qt6-image-formats-plugins qt6-wayland qtkeychain-qt6-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/qtNorth
```

## Windows

MSVC、Windows SDK、CMake、Ninjaと、同じコンパイラー向けのQt・QtKeychainを用意します。開発者用PowerShellで実行してください。以下のQt・QtKeychainのパスは環境に合わせて変更します。

```powershell
cmake -S . -B build-windows/app -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:/Qt/6.10.3/msvc2022_64;C:/QtKeychain"
cmake --build build-windows/app --parallel
```

## FreeBSD

依存パッケージをrootでインストールします。

```sh
pkg install cmake-core ninja qt6-base qt6-svg qt6-imageformats qtkeychain-qt6 xdg-utils
```

通常ユーザーでビルド・テスト・起動します。

```sh
./scripts/build-freebsd.sh
./build-freebsd/qtNorth
```
