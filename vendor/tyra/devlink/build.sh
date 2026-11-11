#!/usr/bin/env bash
# Builds devlink.irx (docs/devlink.md) and the engine's embedded copy of it.
# The POSIX twin of build.ps1; keep the two in step.
#
#   ./build.sh            native toolchain when installed, else Docker
#   ./build.sh --docker   force the pinned ps2dev/ps2dev image
#
# Uses PS2DEV v2.0.0 - the same toolchain tools/ps2link is built with, which is
# what makes this module's ps2ip imports match the ps2ip ps2link keeps resident.
# The artifacts are committed (bin/devlink.irx and
# vendor/tyra/engine/src/debug/devlink_irx.cpp): rebuild and commit them
# together with any change under iop/.
set -euo pipefail

cd "$(dirname "$0")"
HERE=$(pwd)
IMAGE=ps2dev/ps2dev:v2.0.0
TOOLCHAIN="$HOME/.config/tyra-editor/toolchain/ps2dev"

DOCKER=0
for arg in "$@"; do
  case "$arg" in
    --docker) DOCKER=1 ;;
    *) echo "unknown option: $arg" >&2; exit 2 ;;
  esac
done

if [ "$DOCKER" = 0 ] && [ -x "$TOOLCHAIN/iop/bin/mipsel-none-elf-gcc" ]; then
  echo "== devlink: native toolchain ($TOOLCHAIN) =="
  export PS2DEV="$TOOLCHAIN"
  export PS2SDK="$PS2DEV/ps2sdk"
  export PATH="$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2SDK/bin:$PATH"
  bash ./inner-build.sh
else
  echo "== devlink: Docker ($IMAGE) =="
  # The engine copy is written one level up, so mount vendor/tyra.
  docker run --rm -v "$HERE/..:/tyra" -w /tyra/devlink "$IMAGE" \
    sh -c 'apk add --no-cache make bash >/dev/null 2>&1 || true; bash ./inner-build.sh'
fi
