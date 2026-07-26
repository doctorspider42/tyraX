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

# The toolchain and the X11/Wayland/GL development headers. Installing them
# needs root, so this only DIAGNOSES - it names what is missing and prints the
# one command that fixes it on this distro, the way build.ps1 names the scoop
# packages. `./setup.sh --deps` is that command.
say_how_to_fix() {
    local spec manager install packages
    spec="$(tyrax_system_packages)"
    if [ -z "$spec" ]; then
        echo "  Install a C++20 toolchain, cmake, ninja, git, pkg-config, the" >&2
        echo "  X11/Wayland/GL development headers and zenity, then re-run." >&2
        return
    fi
    IFS='|' read -r manager install packages <<<"$spec"
    echo "  ./setup.sh --deps        # installs them with $manager" >&2
    echo "or by hand:" >&2
    echo "  sudo $install $packages" >&2
}

missing_tools=()
for t in cmake ninja g++ git pkg-config; do
    command -v "$t" >/dev/null 2>&1 || missing_tools+=("$t")
done
if [ ${#missing_tools[@]} -gt 0 ]; then
    echo "Missing build tools: ${missing_tools[*]}" >&2
    say_how_to_fix
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
    say_how_to_fix
    exit 1
fi
# zenity/kdialog is a RUNTIME dependency, not a build one: without it the
# editor builds and starts but every Open/Import button silently does nothing
# (platform::pickFile has no backend). Warn, never block.
if ! command -v zenity >/dev/null 2>&1 && ! command -v kdialog >/dev/null 2>&1; then
    echo "NOTE: neither zenity nor kdialog is installed - the editor's file" >&2
    echo "      dialogs (Open project, Import model/texture/WAV) will not open." >&2
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
