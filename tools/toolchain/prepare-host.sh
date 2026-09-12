#!/usr/bin/env bash
# Checks or installs the Linux host packages used to assemble TyraX's pinned
# PS2DEV/OpenVCL toolchain. Installation is always an explicit opt-in.
set -euo pipefail

MODE=${1:---check}
case "$MODE" in
  --check|--install) ;;
  *) echo "usage: prepare-host.sh [--check|--install]" >&2; exit 2 ;;
esac

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
