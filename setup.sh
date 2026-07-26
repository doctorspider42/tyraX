#!/usr/bin/env bash
# Clones third-party dependencies into vendor/ and fetches the PS2 deploy
# tools - the POSIX twin of setup.ps1. The lists themselves live in deps.sh,
# which build.sh reads too, so a new dependency added THERE is picked up by the
# build guard for free.
#
#   ./setup.sh          - vendor/ and tools/ only (no root needed)
#   ./setup.sh --deps   - also install this distro's toolchain + dev headers
#
# --deps is separate because it is the only part that needs root, and on a
# machine that already has a toolchain it is pure noise. setup.ps1 has no
# equivalent: on Windows the toolchain comes from scoop, which is per-user.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
# shellcheck source=deps.sh
. ./deps.sh

WITH_DEPS=0
for arg in "$@"; do
    case "$arg" in
        --deps|--with-deps) WITH_DEPS=1 ;;
        *) echo "Unknown option: $arg (expected --deps)" >&2; exit 2 ;;
    esac
done

install_system_packages() {
    local spec manager install packages root
    spec="$(tyrax_system_packages)"
    if [ -z "$spec" ]; then
        echo "No supported package manager found (apt/dnf/pacman/zypper)." >&2
        echo "Install by hand: a C++20 toolchain, cmake, ninja, git, pkg-config," >&2
        echo "the X11/Wayland/GL development headers, and zenity." >&2
        return 1
    fi
    IFS='|' read -r manager install packages <<<"$spec"
    root="$(tyrax_root_prefix)"
    if [ "$root" = "-" ]; then
        echo "Need root to install packages, but neither sudo nor pkexec is available." >&2
        echo "Run this as root:" >&2
        echo "  $install $packages" >&2
        return 1
    fi
    echo "== Installing system packages ($manager) =="
    # apt needs an index refresh on a fresh image or half the names 404.
    [ "$manager" = "apt" ] && $root apt-get update -qq
    # Word splitting is the point here - these are argument lists, not paths.
    # shellcheck disable=SC2086
    DEBIAN_FRONTEND=noninteractive $root $install $packages
}

if [ "$WITH_DEPS" = 1 ]; then
    install_system_packages
fi

fetch() {  # fetch <url> <outfile> - curl or wget, whichever this box has
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL "$1" -o "$2"
    elif command -v wget >/dev/null 2>&1; then
        wget -q "$1" -O "$2"
    else
        echo "Neither curl nor wget is installed - cannot fetch $1" >&2
        return 1
    fi
}

for d in "${VENDOR_DEPS[@]}"; do
    IFS='|' read -r url branch dir probe _build <<<"$d"
    if [ -e "$probe" ]; then
        echo "OK: $dir already present"
        continue
    fi
    if [ -d "$dir" ]; then
        # A directory without its probe file: a stale/partial checkout, or
        # vendor/tyra whose engine sources are tracked in this repo. Cloning
        # into a non-empty directory just fails, so say what to do instead.
        echo "NOTE: $dir exists but $probe is missing - delete the directory and re-run setup.sh if the build complains."
        continue
    fi
    echo "Cloning $url ($branch) -> $dir"
    git clone --depth 1 --branch "$branch" "$url" "$dir"
done

# Ensure the stb single-headers we #include are present, even when vendor/stb
# is a stale/partial directory that predates the full clone above (no probe
# file, so the clone step skips it). Back-fill any missing header directly.
for h in "${STB_HEADERS[@]}"; do
    if [ ! -e "vendor/stb/$h" ]; then
        echo "Fetching $h"
        mkdir -p vendor/stb
        fetch "https://raw.githubusercontent.com/nothings/stb/master/$h" "vendor/stb/$h"
    fi
done

for t in "${PS2_TOOLS[@]}"; do
    IFS='|' read -r url dir probe <<<"$t"
    if [ -e "$probe" ]; then
        echo "OK: $dir already present"
        continue
    fi
    echo "Fetching $url"
    mkdir -p "$dir"
    tarball="$dir/download.tar.gz"
    # The PS2 tools are optional (they only matter for "Run on PS2"), so a
    # download failure must not take the whole setup down with it.
    if fetch "$url" "$tarball" && tar -xzf "$tarball" -C "$dir"; then
        chmod +x "$probe" 2>/dev/null || true
    else
        echo "WARNING: could not fetch $url - 'Run on PS2' will be unavailable."
    fi
    rm -f "$tarball"
done

echo "setup.sh: done."
