#!/usr/bin/env bash
# Builds the TyraX audsrv - the audio server every generated game links and
# loads. The POSIX twin of build.ps1; keep the two in step.
#
# Reproducible: fetches a PINNED ps2sdk into .work/ (gitignored), overlays the
# sources next to this script over its iop/sound/audsrv + ee/rpc/audsrv, builds
# inside the project's own toolchain image, and drops the three artifacts into
# bin/. Needs Docker running and network for the first fetch.
#
#   ./build.sh           # -> bin/audsrv.irx, bin/libaudsrv.a, bin/audsrv.h
#   ./build.sh --clean   # drop the work tree first (forces a fresh fetch)
#   ./build.sh --check   # build, then DIFF against the committed bin/ and
#                        # restore it - proves the sources produce what ships
#
# Why a work tree at all: audsrv's Makefiles are ordinary ps2sdk module
# Makefiles that include $(PS2SDKSRC)/Defs.make and the iop/ee Rules, so they
# only build inside a ps2sdk source tree. Overlaying is what keeps OUR sources
# the thing being compiled while ps2sdk supplies the build system around them.
#
# Why the artifacts are committed rather than built on demand: every game build
# overlays them into the container (src/runner.cpp), and an IOP toolchain run
# per game build would cost far more than it buys. Rebuild them here whenever
# the sources change, and commit both together.
#
# See README.md next to this script for the licence position (audsrv is LGPL
# v2, unlike the rest of PS2SDK) and for what TyraX changed.
set -euo pipefail

# The ps2sdk commit our sources were taken from: 00f199ae~1, the last one
# before the build system started requiring srxfixup / a newer toolchain than
# the image has. Do not bump it without rebuilding and re-testing a game.
PS2SDK_COMMIT=e78a9cb2ea816a72a7466000c51558fd2b57f5a7
PS2SDK_URL=https://github.com/ps2dev/ps2sdk.git
IMAGE=h4570/tyra

# The container path the sources are built at. It is part of the output: the EE
# objects keep debug info, so the build path ends up inside libaudsrv.a. Keeping
# it fixed is what makes two builds on two machines comparable.
BUILD_PATH=/tmp/pf

cd "$(dirname "$0")"
HERE=$(pwd)
WORK="$HERE/.work/ps2sdk"

CLEAN=0
CHECK=0
for arg in "$@"; do
  case "$arg" in
    --clean) CLEAN=1 ;;
    --check) CHECK=1 ;;
    *) echo "unknown option: $arg" >&2; exit 2 ;;
  esac
done

if [ "$CLEAN" = 1 ] && [ -d "$HERE/.work" ]; then
  echo "[audsrv] dropping the work tree..."
  # The container builds as root, so its objects are root-owned.
  docker run --rm -v "$HERE/.work:/w" "$IMAGE" sh -c 'rm -rf /w/*' >/dev/null 2>&1 || true
  rm -rf "$HERE/.work"
fi

if [ ! -d "$WORK/.git" ]; then
  echo "[audsrv] fetching ps2sdk @ ${PS2SDK_COMMIT:0:8}..."
  mkdir -p "$WORK"
  git -C "$WORK" init -q
  git -C "$WORK" fetch -q --depth 1 "$PS2SDK_URL" "$PS2SDK_COMMIT"
  git -C "$WORK" checkout -q FETCH_HEAD
fi

echo "[audsrv] overlaying the TyraX sources..."
# Copy, do not symlink: the build runs in a container where a host symlink
# pointing outside the mount resolves to nothing. The removal goes through the
# container because a previous build left root-owned obj/irx/lib directories
# behind (Docker on Linux runs it as root).
docker run --rm -v "$WORK:/w" "$IMAGE" \
  sh -c 'rm -rf /w/iop/sound/audsrv /w/ee/rpc/audsrv' >/dev/null
mkdir -p "$WORK/iop/sound/audsrv" "$WORK/ee/rpc/audsrv"
cp -r "$HERE/iop/." "$WORK/iop/sound/audsrv/"
cp -r "$HERE/ee/." "$WORK/ee/rpc/audsrv/"

echo "[audsrv] building (Docker: $IMAGE)..."
docker run --rm -v "$WORK:$BUILD_PATH" -w "$BUILD_PATH" "$IMAGE" sh -c "
  set -e
  export PS2SDKSRC=$BUILD_PATH
  cd $BUILD_PATH/iop/sound/audsrv && rm -rf obj irx && make CC=gcc
  cd $BUILD_PATH/ee/rpc/audsrv   && rm -rf obj lib && make CC=gcc
"

OUT="$HERE/bin"
if [ "$CHECK" = 1 ]; then
  OUT="$HERE/.work/out"
  rm -rf "$OUT"
fi
mkdir -p "$OUT"
cp "$WORK/iop/sound/audsrv/irx/audsrv.irx" "$OUT/audsrv.irx"
cp "$WORK/ee/rpc/audsrv/lib/libaudsrv.a"   "$OUT/libaudsrv.a"
cp "$WORK/ee/rpc/audsrv/include/audsrv.h"  "$OUT/audsrv.h"
chmod 644 "$OUT/audsrv.irx" "$OUT/libaudsrv.a" "$OUT/audsrv.h"

if [ "$CHECK" = 1 ]; then
  echo "[audsrv] comparing against the committed bin/ ..."
  rc=0
  cmp -s "$OUT/audsrv.irx" "$HERE/bin/audsrv.irx" \
    && echo "  audsrv.irx   byte-identical" \
    || { echo "  audsrv.irx   DIFFERS"; rc=1; }
  cmp -s "$OUT/audsrv.h" "$HERE/bin/audsrv.h" \
    && echo "  audsrv.h     byte-identical" \
    || { echo "  audsrv.h     DIFFERS"; rc=1; }
  # libaudsrv.a is NOT expected to be byte-identical: ar stamps each member
  # with the build time, and gcc's LTO section names carry a per-compilation
  # random id. Compare the member SIZES instead - those move when the code
  # does. A real change shows up as a size difference or in audsrv.irx.
  if [ "$(ar tv "$OUT/libaudsrv.a" | awk '{print $3, $NF}')" \
     = "$(ar tv "$HERE/bin/libaudsrv.a" | awk '{print $3, $NF}')" ]; then
    echo "  libaudsrv.a  same members, same sizes (ar timestamps and gcc's"
    echo "               LTO ids differ by design - see the note in build.sh)"
  else
    echo "  libaudsrv.a  MEMBERS DIFFER"; rc=1
  fi
  exit $rc
fi

echo "[audsrv] done:"
ls -l "$OUT"
echo
echo "Commit bin/ together with whatever source change produced it, then"
echo "rebuild a game (the Runner re-applies the overlay when these bytes change)."
