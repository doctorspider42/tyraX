#!/usr/bin/env bash
# Checks or installs the Linux or macOS host packages used to assemble TyraX's pinned
# PS2DEV/OpenVCL toolchain. Installation is always an explicit opt-in.
set -euo pipefail

MODE=${1:---check}
case "$MODE" in
  --check|--install) ;;
  *) echo "usage: prepare-host.sh [--check|--install]" >&2; exit 2 ;;
esac

# macOS: the Xcode Command Line Tools give clang (as gcc/g++), and Homebrew
# gives the two tools whose system versions are too old for the build: GNU
# make 4 (`gmake`; the system make is 3.81) and rsync 3 (the system one is
# openrsync, without the itemized output native-build.sh reads). The macOS
# PS2DEV archive is NOT self-contained: its gcc/binutils load gmp, mpfr,
# libmpc and zstd from Homebrew's opt/ prefix (otool -L), and without them the
# EE compiler dies on its first file with "Library not loaded: libmpc.3.dylib".
if [ "$(uname -s)" = Darwin ]; then
  for d in /opt/homebrew/bin /usr/local/bin; do
    [ -d "$d" ] && PATH="$d:$PATH"
  done
  missing=()
  xcode-select -p >/dev/null 2>&1 || missing+=(xcode-command-line-tools)
  for tool in curl tar shasum cmake gmake; do
    command -v "$tool" >/dev/null 2>&1 || missing+=("$tool")
  done
  # Captured first: `grep -q` closing the pipe early fails it under pipefail.
  brew_prefix=$(brew --prefix 2>/dev/null || true)
  for lib in gmp/lib/libgmp.10 mpfr/lib/libmpfr.6 libmpc/lib/libmpc.3 zstd/lib/libzstd.1; do
    [ -n "$brew_prefix" ] && [ -f "$brew_prefix/opt/$lib.dylib" ] \
      || missing+=("$(basename "$lib").dylib")
  done
  rsync_version=$(rsync --version 2>/dev/null | head -n 1 || true)
  case "$rsync_version" in "rsync "*" version 3"*) ;; *) missing+=(rsync-3) ;; esac
  if [ "${#missing[@]}" -eq 0 ]; then
    echo "[toolchain] macOS host prerequisites are ready."
    exit 0
  fi
  echo "[toolchain] missing host tools: ${missing[*]}" >&2
  if [ "$MODE" = --check ]; then
    echo "[toolchain] Run tools/toolchain/prepare-host.sh --install (Homebrew), or by hand:" >&2
    echo "[toolchain]   xcode-select --install; brew install cmake make rsync gmp mpfr libmpc zstd" >&2
    exit 1
  fi
  if ! xcode-select -p >/dev/null 2>&1; then
    echo "[toolchain] Install the Xcode Command Line Tools first: xcode-select --install" >&2
    exit 1
  fi
  if ! command -v brew >/dev/null 2>&1; then
    echo "[toolchain] Homebrew is required (https://brew.sh), then: brew install cmake make rsync gmp mpfr libmpc zstd" >&2
    exit 1
  fi
  echo "[toolchain] Installing host prerequisites with Homebrew..."
  brew install cmake make rsync gmp mpfr libmpc zstd
  exec bash "$0" --check
fi

required=(curl tar sha256sum cmake make g++ gcc rsync)
missing=()
for tool in "${required[@]}"; do
  command -v "$tool" >/dev/null 2>&1 || missing+=("$tool")
done

if [ "${#missing[@]}" -eq 0 ]; then
  echo "[toolchain] Linux host prerequisites are ready."
  exit 0
fi

echo "[toolchain] missing host tools: ${missing[*]}" >&2
if [ "$MODE" = --check ]; then
  echo "[toolchain] Run tools/toolchain/prepare-host.sh --install, or on Windows:" >&2
  echo "[toolchain]   powershell -File tools/toolchain/prepare-host.ps1 -Install" >&2
  exit 1
fi

if ! command -v apt-get >/dev/null 2>&1; then
  echo "[toolchain] Automatic host setup currently supports apt-based Debian/Ubuntu distributions." >&2
  echo "[toolchain] Install equivalents of: build-essential cmake curl rsync ca-certificates" >&2
  exit 1
fi

if [ "$(id -u)" -eq 0 ]; then
  elevate=()
elif command -v sudo >/dev/null 2>&1; then
  elevate=(sudo)
else
  echo "[toolchain] sudo is required to install packages in this distribution." >&2
  exit 1
fi

echo "[toolchain] Installing host prerequisites with apt..."
"${elevate[@]}" apt-get update
"${elevate[@]}" apt-get install -y \
  build-essential ca-certificates cmake coreutils curl findutils gawk rsync tar

for tool in "${required[@]}"; do
  command -v "$tool" >/dev/null 2>&1 || {
    echo "[toolchain] host setup completed, but $tool is still unavailable" >&2
    exit 1
  }
done
echo "[toolchain] Linux host prerequisites are ready."
