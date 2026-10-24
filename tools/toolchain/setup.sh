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
# The EE half is compiled here, the IOP half (audsrv.irx) is the committed
# artifact installed below - both have to move the stamp, or a rebuilt irx
# never reaches the engine (a new stamp drops the engine objects that embed it).
AUDSRV_TREE_SHA=$(tree_hash "$REPO/vendor/tyra/audsrv/ee")-$(sha256 "$REPO/vendor/tyra/audsrv/bin/audsrv.irx" | awk '{print substr($1, 1, 16)}')
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

# macOS: the PS2DEV archive's gcc/binutils load gmp, mpfr, mpc and zstd from
# Homebrew's opt/ prefix. They are relinked onto copies the install owns - the
# ones a TyraX.app carries in tools/macos/lib (built for the app's minimum
# macOS by installer/macos/build-host-tools.sh), else Homebrew's in a checkout
# - so a Homebrew upgrade (libmpc.3 -> .4) cannot break a working toolchain,
# and an installed TyraX needs no Homebrew at all. machorelink rewrites the
# references to @loader_path/<lib> in place; install_name_tool would need the
# Xcode Command Line Tools, which an app user may not have.
MAC_BUNDLE="$REPO/tools/macos"
MAC_LIBS=(libgmp.10.dylib libmpfr.6.dylib libmpc.3.dylib libzstd.1.dylib)
MAC_LIB_SRC=
HOSTLIBS_ID=
if [ "$(uname -s)" = Darwin ]; then
  if [ -f "$MAC_BUNDLE/lib/libmpc.3.dylib" ]; then
    MAC_LIB_SRC="$MAC_BUNDLE/lib"
  else
    brew_prefix=$(brew --prefix 2>/dev/null || true)
    MAC_LIB_SRC="$brew_prefix/opt"
  fi
  mac_lib() {  # mac_lib <name> -> the source file of one of MAC_LIBS
    if [ "$MAC_LIB_SRC" = "$MAC_BUNDLE/lib" ]; then echo "$MAC_LIB_SRC/$1"; return; fi
    case "$1" in
      libgmp*) echo "$MAC_LIB_SRC/gmp/lib/$1" ;;
      libmpfr*) echo "$MAC_LIB_SRC/mpfr/lib/$1" ;;
      libmpc*) echo "$MAC_LIB_SRC/libmpc/lib/$1" ;;
      libzstd*) echo "$MAC_LIB_SRC/zstd/lib/$1" ;;
    esac
  }
  HOSTLIBS_ID=$(for l in "${MAC_LIBS[@]}"; do sha256 "$(mac_lib "$l")"; done \
                | awk '{print $1}' | sha256 | awk '{print $1}')
  HOSTLIBS_ID=" hostlibs-$HOSTLIBS_ID-$(sha256 "$HERE/machorelink.c" | awk '{print $1}')"
fi

relink_macos_toolchain() {
  local relink="$MAC_BUNDLE/bin/machorelink" libdir="$ROOT/lib/tyrax-host" l f
  if [ ! -x "$relink" ]; then
    relink="$ROOT/.sources/machorelink"
    cc -O2 "$HERE/machorelink.c" -o "$relink"
  fi
  local args=()
  for l in "${MAC_LIBS[@]}"; do args+=(-l "$l"); done
  mkdir -p "$libdir"
  for l in "${MAC_LIBS[@]}"; do
    install -m 0644 "$(mac_lib "$l")" "$libdir/$l"
  done
  # Homebrew's copies name themselves and each other by absolute opt/ paths.
  "$relink" "${args[@]}" "$libdir"/*.dylib | while IFS= read -r f; do
    codesign -f -s - "$f" 2>/dev/null
  done
  echo "[toolchain] relinking PS2DEV onto its own gmp/mpfr/mpc/zstd..."
  find "$ROOT" \( -path "$ROOT/.sources" -o -path "$libdir" \) -prune -o -type f \
       \( -perm -u+x -o -name '*.dylib' -o -name '*.so' \) -print0 \
    | xargs -0 "$relink" "${args[@]}" > "$ROOT/.sources/relinked.txt"
  # Each listed binary finds the four by @loader_path: hard links, so the
  # copies cost no space and stay one file. The list holds every NAME that
  # references them (machorelink.c: the archive's hard-linked twins must each
  # be re-signed, or the one codesign did not rewrite dies with "Killed: 9").
  while IFS= read -r f; do
    for l in "${MAC_LIBS[@]}"; do ln -f "$libdir/$l" "$(dirname "$f")/$l"; done
    codesign -f -s - "$f" 2>/dev/null
  done < "$ROOT/.sources/relinked.txt"
  echo "[toolchain] relinked $(wc -l < "$ROOT/.sources/relinked.txt" | tr -d ' ') binaries."
}

BASE_MARKER="$ROOT/.ps2dev-base"
BASE_EXPECTED="ps2dev-v2.0.0-$PS2DEV_SHA256$HOSTLIBS_ID"
if [ ! -f "$BASE_MARKER" ] || [ "$(cat "$BASE_MARKER")" != "$BASE_EXPECTED" ]; then
  echo "[toolchain] installing PS2DEV..."
  find "$ROOT" -mindepth 1 -maxdepth 1 ! -name .sources -exec rm -rf -- {} +
  tar -xzf "$ARCHIVE" --strip-components=1 -C "$ROOT"
  [ -z "$HOSTLIBS_ID" ] || relink_macos_toolchain
  printf '%s\n' "$BASE_EXPECTED" > "$BASE_MARKER"
else
  echo "[toolchain] PS2DEV base is already installed."
fi

export PS2DEV="$ROOT"
export PS2SDK="$ROOT/ps2sdk"
export PATH="$ROOT/bin:$ROOT/ee/bin:$ROOT/iop/bin:$ROOT/dvp/bin:$ROOT/ps2sdk/bin:$PATH"

if [ -x "$MAC_BUNDLE/bin/openvcl" ] && [ -x "$MAC_BUNDLE/bin/vclpp" ] \
   && [ -x "$MAC_BUNDLE/bin/bin2s" ]; then
  # A TyraX.app: installer/macos/build-host-tools.sh built these from the very
  # vendor/openvcl, vendor/vclpp and bin2s trees the app carries (and ran the
  # OpenVCL suite), so no cmake or compiler is needed on this Mac.
  echo "[toolchain] installing the bundled OpenVCL, vclpp and bin2s..."
  install -m 0755 "$MAC_BUNDLE/bin/openvcl" "$ROOT/bin/openvcl"
  install -m 0755 "$MAC_BUNDLE/bin/vclpp" "$ROOT/bin/vclpp"
  install -m 0755 "$MAC_BUNDLE/bin/bin2s" "$ROOT/ps2sdk/bin/bin2s"
else
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
fi

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
