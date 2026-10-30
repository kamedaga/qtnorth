#!/bin/sh
set -eu

if [ "$(uname -s)" != FreeBSD ]; then
    echo 'This script builds qtNorth natively on FreeBSD.' >&2
    exit 1
fi

qtnorth_root=$(CDPATH='' cd -P "$(dirname "$0")/.." && pwd)
qtnorth_build=${QTNORTH_BUILD_DIR:-"$qtnorth_root/build-freebsd"}

for qtnorth_tool in cmake ninja; do
    if ! command -v "$qtnorth_tool" >/dev/null 2>&1; then
        echo 'Install the dependencies listed in README.md (FreeBSD) first.' >&2
        exit 1
    fi
done

cmake -S "$qtnorth_root" -B "$qtnorth_build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
    -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build "$qtnorth_build" --parallel "${QTNORTH_JOBS:-4}"
ctest --test-dir "$qtnorth_build" --output-on-failure
printf 'Built and tested: %s/qtNorth\n' "$qtnorth_build"
