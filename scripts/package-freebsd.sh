#!/bin/sh
set -eu

if [ "$(uname -s)" != FreeBSD ]; then
    echo 'Run this script on FreeBSD to produce a native FreeBSD archive.' >&2
    exit 1
fi
qtnorth_root=$(CDPATH='' cd -P "$(dirname "$0")/.." && pwd)
qtnorth_build=${QTNORTH_BUILD_DIR:-"$qtnorth_root/build-freebsd"}
qtnorth_output=${QTNORTH_OUTPUT_DIR:-"$qtnorth_root/out"}
"$qtnorth_root/scripts/build-freebsd.sh"

qtnorth_version=$(sed -n 's/^project(qtNorth VERSION \([0-9.]*\).*/\1/p' "$qtnorth_root/CMakeLists.txt")
qtnorth_major=$(uname -r | cut -d . -f 1)
qtnorth_arch=$(uname -p)
qtnorth_name="qtNorth-$qtnorth_version-freebsd$qtnorth_major-$qtnorth_arch"
mkdir -p "$qtnorth_output"
qtnorth_work=$(mktemp -d "$qtnorth_output/.qtnorth-freebsd.XXXXXX")
trap 'rm -rf "$qtnorth_work"' EXIT HUP INT TERM
qtnorth_stage="$qtnorth_work/$qtnorth_name"
mkdir -p "$qtnorth_stage/bin"
install -m 755 "$qtnorth_build/qtNorth" "$qtnorth_stage/bin/qtNorth"
strip -s "$qtnorth_stage/bin/qtNorth"
install -m 755 "$qtnorth_root/packaging/freebsd/qtNorth" "$qtnorth_stage/qtNorth"
install -m 644 "$qtnorth_root/packaging/freebsd/README.txt" "$qtnorth_stage/README.txt"
mkdir -p "$qtnorth_stage/share/applications" "$qtnorth_stage/share/icons/hicolor/scalable/apps"
install -m 644 "$qtnorth_root/packaging/rip.north.qtNorth.desktop" "$qtnorth_stage/share/applications/"
install -m 644 "$qtnorth_root/assets/qtNorth-icon.svg" "$qtnorth_stage/share/icons/hicolor/scalable/apps/qtnorth.svg"
mkdir -p "$qtnorth_stage/source"
for qtnorth_item in src assets scripts packaging tests docs CMakeLists.txt README.md DISCLAIMER.txt .gitignore; do
    cp -R "$qtnorth_root/$qtnorth_item" "$qtnorth_stage/source/"
done
install -m 644 "$qtnorth_root/DISCLAIMER.txt" "$qtnorth_stage/DISCLAIMER.txt"
find "$qtnorth_stage/source" -type d -name __pycache__ -prune -exec rm -rf {} +
{
    freebsd-version -u
    uname -m
    pkg query '%n-%v' qt6-base qt6-svg qt6-imageformats qtkeychain-qt6
} > "$qtnorth_stage/build-info.txt"

# Exercise the stripped binary from a path with spaces, independently of the build.
qtnorth_check="$qtnorth_work/launch check"
cp -R "$qtnorth_stage" "$qtnorth_check"
(cd / && QT_QPA_PLATFORM=offscreen "$qtnorth_check/qtNorth" --version)
qtnorth_archive="$qtnorth_output/$qtnorth_name.tar.gz"
tar -czf "$qtnorth_work/archive.tar.gz" -C "$qtnorth_work" "$qtnorth_name"
mv "$qtnorth_work/archive.tar.gz" "$qtnorth_archive"
(cd "$qtnorth_output" && sha256 "$qtnorth_name.tar.gz") > "$qtnorth_archive.sha256"
printf 'Package ready: %s\n' "$qtnorth_archive"
