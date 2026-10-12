#!/usr/bin/env bash
# Installs the pinned, Docker-free TyraX PS2 toolchain into a user-writable
# directory. This is also called lazily by native-build.sh on the first build.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
# The SAME directory the editor installs into (Runner: configDir()/toolchain/
# ps2dev), so a manual install and a Build & Run share one copy. They used to
# disagree - this script defaulted to ~/.cache/tyrax/ps2dev while the editor
# used ~/.config/tyra-editor/toolchain/ps2dev - which meant anybody who
# followed the documented manual command downloaded the 254 MiB archive, built
# OpenVCL and ran its tests, and then watched the editor do all of it again
# into another 943 MB directory. setup.ps1 has always defaulted to the
# editor's own path on Windows; this is that parity restored.
ROOT=${1:-${TYRAX_PS2DEV:-"${XDG_CONFIG_HOME:-$HOME/.config}/tyra-editor/toolchain/ps2dev"}}
# One pinned PS2DEV v2.0.0 archive per host: the upstream release builds the
# same toolchain for Linux and macOS, so only the asset (and its hash) differ.
# The Windows path never reaches this switch - it runs this script inside WSL.
case "$(uname -s)-$(uname -m)" in
  Linux-x86_64)
    PS2DEV_ASSET=ps2dev-ubuntu-latest.tar.gz
    PS2DEV_HOST=ubuntu-x86_64
    PS2DEV_SHA256=c8e5dedccf62084d476894e88cd0451b57f1ed288e741eb3c1263010aaffc028 ;;
  Darwin-arm64)
    PS2DEV_ASSET=ps2dev-macos-latest.tar.gz
    PS2DEV_HOST=macos-arm64
    PS2DEV_SHA256=6f6df659d2d49738f21f53a53c361a74182d0e4c2495e31e76972ff818bc21cb ;;
  Darwin-x86_64)
    PS2DEV_ASSET=ps2dev-macos-15-intel.tar.gz
    PS2DEV_HOST=macos-x86_64
    PS2DEV_SHA256=833d2dc1e3cbfa2091047e4539e1bd0bd2836668f886a1c8a75e6e09deb56bef ;;
  *)
    echo "[toolchain] no pinned PS2DEV build for $(uname -s) $(uname -m)" >&2
    exit 1 ;;
esac
PS2DEV_URL=https://github.com/ps2dev/ps2dev/releases/download/v2.0.0/$PS2DEV_ASSET
OPENVCL_COMMIT=89efa51e354abea6bb2b790848e78147b6a994a1
VCLPP_COMMIT=00e44ecf78e34e16325b173ba39b5fdacdd01666
VCL_FLAGS='--schedule-flag-readers --fmac-interlock --sce-latencies --emit-delay-fillers --branch-interlock --branch-bubble-on-dependency --loop-liveness-always --upper-move-with-w --pair-best-of-two --pair-best-of-many --trim-uncarried-ranges --coalesce-float-writes --split-dead-float-ranges --sink-loads --sink-loads-across-stores --sink-loads-into-loops --sink-loads-past-branches --sink-loads-best-of --drop-dead-writes --exempt-full-clip-masks --clip-exemption-best-of'

case "$ROOT" in
  /*) ;;
  *) echo "[toolchain] install root must be absolute: $ROOT" >&2; exit 2 ;;
esac
if [ "$ROOT" = / ] || [ "$ROOT" = "$HOME" ]; then
  echo "[toolchain] refusing unsafe install root: $ROOT" >&2
  exit 2
fi

bash "$HERE/prepare-host.sh" --check

# macOS: Homebrew's cmake (to build OpenVCL) must be found also when the
# editor was started from Finder with a bare PATH.
if [ "$(uname -s)" = Darwin ]; then
  for d in /opt/homebrew/bin /usr/local/bin; do
    [ -d "$d" ] && PATH="$d:$PATH"
  done
  export PATH
fi

# coreutils' sha256sum on Linux, the BSD shasum on macOS - same output shape.
sha256() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$@"; else shasum -a 256 "$@"; fi
}

# xargs runs a program, not a function, so tree_hash names the tool itself.
if command -v sha256sum >/dev/null 2>&1; then SHA256=(sha256sum); else SHA256=(shasum -a 256); fi
tree_hash() {
  # Hash relative names: identical sources in two worktrees are one toolchain.
  # Absolute names made switching checkouts reinstall OpenVCL and purge objects.
  (cd "$1" && find . -type f ! -name openvcl ! -path '*/build/*' -print0 \
    | sort -z | xargs -0 "${SHA256[@]}" | "${SHA256[@]}" | awk '{print $1}')
}
OPENVCL_TREE_SHA=$(tree_hash "$REPO/vendor/openvcl")
VCLPP_TREE_SHA=$(tree_hash "$REPO/vendor/vclpp")
BIN2S_TREE_SHA=$(tree_hash "$HERE/bin2s")
AUDSRV_TREE_SHA=$(tree_hash "$REPO/vendor/tyra/audsrv/ee")
SETUP_SHA=$(sha256 "$HERE/setup.sh" | awk '{print $1}')

MARKER="$ROOT/.tyrax-toolchain"
EXPECTED="setup-$SETUP_SHA ps2dev-v2.0.0-$PS2DEV_SHA256 openvcl-$OPENVCL_COMMIT-$OPENVCL_TREE_SHA vclpp-$VCLPP_COMMIT-$VCLPP_TREE_SHA bin2s-$BIN2S_TREE_SHA audsrv-$AUDSRV_TREE_SHA"
if [ -f "$MARKER" ] && [ "$(cat "$MARKER")" = "$EXPECTED" ]; then
  echo "[toolchain] ready: $ROOT"
  exit 0
fi

mkdir -p "$ROOT" "$ROOT/.sources"
ARCHIVE="$ROOT/.sources/ps2dev-v2.0.0-$PS2DEV_HOST.tar.gz"
if [ ! -f "$ARCHIVE" ] || [ "$(sha256 "$ARCHIVE" | awk '{print $1}')" != "$PS2DEV_SHA256" ]; then
  echo "[toolchain] downloading pinned PS2DEV v2.0.0 (254 MiB)..."
  rm -f "$ARCHIVE"
  curl --fail --location --retry 3 --output "$ARCHIVE" "$PS2DEV_URL"
fi
echo "$PS2DEV_SHA256  $ARCHIVE" | sha256 -c -

BASE_MARKER="$ROOT/.ps2dev-base"
BASE_EXPECTED="ps2dev-v2.0.0-$PS2DEV_SHA256"
if [ ! -f "$BASE_MARKER" ] || [ "$(cat "$BASE_MARKER")" != "$BASE_EXPECTED" ]; then
  echo "[toolchain] installing PS2DEV..."
  find "$ROOT" -mindepth 1 -maxdepth 1 ! -name .sources -exec rm -rf -- {} +
  tar -xzf "$ARCHIVE" --strip-components=1 -C "$ROOT"
  printf '%s\n' "$BASE_EXPECTED" > "$BASE_MARKER"
else
  echo "[toolchain] PS2DEV base is already installed."
fi

export PS2DEV="$ROOT"
export PS2SDK="$ROOT/ps2sdk"
export PATH="$ROOT/bin:$ROOT/ee/bin:$ROOT/iop/bin:$ROOT/dvp/bin:$ROOT/ps2sdk/bin:$PATH"

echo "[toolchain] building the in-tree OpenVCL fork..."
BUILD_TMP=$(mktemp -d "${TMPDIR:-/tmp}/tyrax-toolchain.XXXXXXXX")
trap 'rm -rf -- "$BUILD_TMP"' EXIT
OV_BUILD="$BUILD_TMP/openvcl"
cmake -S "$REPO/vendor/openvcl" -B "$OV_BUILD" -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=ON
cmake --build "$OV_BUILD" -j"$(getconf _NPROCESSORS_ONLN)"
ctest --test-dir "$OV_BUILD" --output-on-failure
install -m 0755 "$OV_BUILD/openvcl" "$ROOT/bin/openvcl"

echo "[toolchain] building vclpp and bin2s from in-tree sources..."
g++ -std=c++14 -O2 -Wall -Wextra -pedantic \
  "$REPO/vendor/vclpp/vclpp_main.cpp" -o "$ROOT/bin/vclpp"
gcc -O2 "$HERE/bin2s/bin2s.c" -o "$ROOT/ps2sdk/bin/bin2s"

cat > "$ROOT/bin/vcl" <<EOF
#!/usr/bin/env bash
exec "\$(dirname "\$0")/openvcl" $VCL_FLAGS "\$@"
EOF
chmod 0755 "$ROOT/bin/vcl" "$ROOT/bin/openvcl" "$ROOT/bin/vclpp" \
  "$ROOT/ps2sdk/bin/bin2s"

# Build the vendored TyraX EE audsrv fork directly against the installed SDK.
# This intentionally avoids cloning a second PS2SDK source tree at setup time.
AUD_BUILD="$ROOT/.sources/audsrv-build"
rm -rf "$AUD_BUILD"
mkdir -p "$AUD_BUILD"
echo "[toolchain] building the TyraX audsrv fork..."
mips64r5900el-ps2-elf-gcc -O2 -G0 -Wall -Wextra \
  -D_EE \
  -I"$REPO/vendor/tyra/audsrv/ee/include" -I"$PS2SDK/ee/include" -I"$PS2SDK/common/include" \
  -c "$REPO/vendor/tyra/audsrv/ee/src/audsrv_rpc.c" -o "$AUD_BUILD/audsrv_rpc.o"
mips64r5900el-ps2-elf-gcc -O2 -G0 -Wall -Wextra \
  -D_EE \
  -I"$REPO/vendor/tyra/audsrv/ee/include" -I"$PS2SDK/ee/include" -I"$PS2SDK/common/include" \
  -c "$REPO/vendor/tyra/audsrv/ee/src/erl-support.c" -o "$AUD_BUILD/erl-support.o"
mips64r5900el-ps2-elf-ar rcs "$AUD_BUILD/libaudsrv.a" \
  "$AUD_BUILD/audsrv_rpc.o" "$AUD_BUILD/erl-support.o"
install -m 0644 "$AUD_BUILD/libaudsrv.a" "$ROOT/ps2sdk/ee/lib/libaudsrv.a"
install -m 0644 "$REPO/vendor/tyra/audsrv/ee/include/audsrv.h" "$ROOT/ps2sdk/ee/include/audsrv.h"
install -m 0644 "$REPO/vendor/tyra/audsrv/bin/audsrv.irx" "$ROOT/ps2sdk/iop/irx/audsrv.irx"

printf '%s\n' "$EXPECTED" > "$MARKER"
echo "[toolchain] ready: $ROOT"
