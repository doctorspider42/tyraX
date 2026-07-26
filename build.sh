#!/usr/bin/env bash
# Builds the editor on Linux/macOS - the POSIX twin of build.ps1. Usage:
#   ./build.sh          - configure (if needed) + build
#   ./build.sh --run    - build and launch the editor
#   ./build.sh --clean  - remove the build directory first
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

RUN=0
CLEAN=0
for arg in "$@"; do
    case "$arg" in
        --run|-Run)     RUN=1 ;;
        --clean|-Clean) CLEAN=1 ;;
        *) echo "Unknown option: $arg (expected --run and/or --clean)" >&2; exit 2 ;;
    esac
done

# shellcheck source=deps.sh
. ./deps.sh

# The toolchain and the X11/Wayland/GL development headers. We deliberately do
# not install anything (that needs root and a distro guess) - name what is
# missing and print the one command that fixes it, the way build.ps1 names the
# scoop packages.
missing_tools=()
for t in cmake ninja g++ git pkg-config; do
    command -v "$t" >/dev/null 2>&1 || missing_tools+=("$t")
done
if [ ${#missing_tools[@]} -gt 0 ]; then
    echo "Missing build tools: ${missing_tools[*]}" >&2
    echo "Install the toolchain first:" >&2
    echo "  sudo apt install -y $APT_PACKAGES" >&2
    exit 1
fi
# GLFW needs the X11 and GL headers; without them cmake configures and only
# fails much later with an opaque link error, so check up front.
missing_pkgs=()
for p in gl x11 xrandr xinerama xcursor xi; do
    pkg-config --exists "$p" || missing_pkgs+=("$p")
done
if [ ${#missing_pkgs[@]} -gt 0 ]; then
    echo "Missing development headers (pkg-config: ${missing_pkgs[*]})" >&2
    echo "  sudo apt install -y $APT_PACKAGES" >&2
    exit 1
fi

# Dependencies in vendor/. The list comes from deps.sh so this guard can never
# drift behind setup.sh: every dependency CMake compiles is probed by a real
# source file, and a missing one is fixed here instead of surfacing later as
# "Cannot find source file: vendor/..." from cmake. This fires on a fresh
# clone, and just as often in an older worktree after merging a branch that
# added a dependency.
probe_missing() {
    local out=() d url branch dir probe build
    for d in "${VENDOR_DEPS[@]}"; do
        IFS='|' read -r url branch dir probe build <<<"$d"
        [ "$build" = "1" ] || continue
        [ -e "$probe" ] || out+=("$dir")
    done
    echo "${out[*]-}"
}
missing="$(probe_missing)"
if [ -n "$missing" ]; then
    echo "== Missing dependencies ($missing) - running setup.sh =="
    ./setup.sh
    missing="$(probe_missing)"
    if [ -n "$missing" ]; then
        echo "Still missing after setup.sh: $missing. Delete those vendor directories and run ./setup.sh again." >&2
        exit 1
    fi
fi

if [ "$CLEAN" = 1 ] && [ -d build ]; then
    echo "== Cleaning build directory =="
    rm -rf build
fi

if [ ! -f build/build.ninja ]; then
    echo "== Configuring (cmake) =="
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
fi

echo "== Building =="
cmake --build build

echo "OK: build/tyrax-editor"

if [ "$RUN" = 1 ]; then
    exec ./build/tyrax-editor
fi
