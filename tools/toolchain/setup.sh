#!/usr/bin/env bash
# Installs the pinned, Docker-free TyraX PS2 toolchain into a user-writable
# directory. This is also called lazily by native-build.sh on the first build.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
ROOT=${1:-${TYRAX_PS2DEV:-"${XDG_CACHE_HOME:-$HOME/.cache}/tyrax/ps2dev"}}
PS2DEV_URL=https://github.com/ps2dev/ps2dev/releases/download/v2.0.0/ps2dev-ubuntu-latest.tar.gz
PS2DEV_SHA256=c8e5dedccf62084d476894e88cd0451b57f1ed288e741eb3c1263010aaffc028
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

tree_hash() {
  find "$1" -type f ! -name openvcl ! -path '*/build/*' -print0 \
    | sort -z | xargs -0 sha256sum | sha256sum | awk '{print $1}'
}
OPENVCL_TREE_SHA=$(tree_hash "$REPO/vendor/openvcl")
VCLPP_TREE_SHA=$(tree_hash "$REPO/vendor/vclpp")
BIN2S_TREE_SHA=$(tree_hash "$HERE/bin2s")
AUDSRV_TREE_SHA=$(tree_hash "$REPO/vendor/tyra/audsrv/ee")
SETUP_SHA=$(sha256sum "$HERE/setup.sh" | awk '{print $1}')

MARKER="$ROOT/.tyrax-toolchain"
EXPECTED="setup-$SETUP_SHA ps2dev-v2.0.0-$PS2DEV_SHA256 openvcl-$OPENVCL_COMMIT-$OPENVCL_TREE_SHA vclpp-$VCLPP_COMMIT-$VCLPP_TREE_SHA bin2s-$BIN2S_TREE_SHA audsrv-$AUDSRV_TREE_SHA"
if [ -f "$MARKER" ] && [ "$(cat "$MARKER")" = "$EXPECTED" ]; then
  echo "[toolchain] ready: $ROOT"
  exit 0
fi

mkdir -p "$ROOT" "$ROOT/.sources"
ARCHIVE="$ROOT/.sources/ps2dev-v2.0.0-ubuntu-x86_64.tar.gz"
if [ ! -f "$ARCHIVE" ] || [ "$(sha256sum "$ARCHIVE" | awk '{print $1}')" != "$PS2DEV_SHA256" ]; then
  echo "[toolchain] downloading pinned PS2DEV v2.0.0 (254 MiB)..."
  rm -f "$ARCHIVE"
  curl --fail --location --retry 3 --output "$ARCHIVE" "$PS2DEV_URL"
fi
echo "$PS2DEV_SHA256  $ARCHIVE" | sha256sum -c -

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
