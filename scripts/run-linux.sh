#!/usr/bin/env bash
set -euo pipefail
qtnorth_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
qtnorth_build="$qtnorth_root/build"
if [[ ! -x "$qtnorth_build/qtNorth" ]]; then
    echo '先に README.md の手順でビルドしてください。' >&2
    exit 1
fi
# A local verification runtime may be supplied alongside the build.
if [[ -d "$qtnorth_build/runtime/lib" ]]; then
    export LD_LIBRARY_PATH="$qtnorth_build/runtime/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    export QT_PLUGIN_PATH="$qtnorth_build/runtime/plugins"
    if [[ -z "${QT_QPA_PLATFORM:-}" ]]; then
        qtnorth_wayland_socket="${WAYLAND_DISPLAY:-}"
        if [[ "$qtnorth_wayland_socket" != /* ]]; then
            qtnorth_wayland_socket="${XDG_RUNTIME_DIR:-}/$qtnorth_wayland_socket"
        fi
        if [[ -n "${WAYLAND_DISPLAY:-}" && -S "$qtnorth_wayland_socket" && -f "$QT_PLUGIN_PATH/platforms/libqwayland-generic.so" ]]; then
            export QT_QPA_PLATFORM=wayland
        elif [[ -n "${DISPLAY:-}" ]]; then
            export QT_QPA_PLATFORM=xcb
        fi
    fi
fi
exec "$qtnorth_build/qtNorth" "$@"
