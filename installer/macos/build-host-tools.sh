#!/usr/bin/env bash
# Builds the host tools a TyraX.app carries so that building a PS2 game needs
# NOTHING from Homebrew - nor the Xcode Command Line Tools - on the user's Mac
# (docs/native-toolchain.md#macos). rsync, curl, tar and shasum are real
# system tools; /usr/bin/make is not (it is the same xcode-select shim as git
# and clang, and without the Command Line Tools it only offers to install them):
#
#   <out>/bin/gmake      GNU make, run by native-build.sh in place of `make`
#   <out>/bin/machorelink   tools/toolchain/machorelink.c - relinks the PS2DEV
#                        binaries onto those libraries without install_name_tool
#   <out>/bin/openvcl, vclpp, bin2s   the VU tools tools/toolchain/setup.sh
#                        would otherwise compile (with cmake + clang) on first use
#   <out>/lib/libgmp.10, libmpfr.6, libmpc.3, libzstd.1 (.dylib)
#                        what the macOS PS2DEV archive's gcc/binutils load from
#                        Homebrew's opt/ prefix; setup.sh relinks them
#   <out>/src/*.tar.*    the exact sources of everything above - GNU make is
#                        GPL-3.0, GMP/MPFR/MPC LGPL-3.0, and shipping the
#                        corresponding source beside the binaries is the
#                        simplest way to honour that
#
# Everything is built from PINNED, HASHED upstream tarballs for
# MACOSX_DEPLOYMENT_TARGET (15.0 - the PS2DEV archive's own minimum), never
# copied from Homebrew: Homebrew bottles target the machine they were poured on
# (minos 27.0 on the box this was written on), so copying them would ship
# libraries that refuse to load on every older macOS.
#
#   installer/macos/build-host-tools.sh <out-dir> [<work-dir>]
set -euo pipefail

OUT=${1:?usage: build-host-tools.sh <out-dir> [<work-dir>]}
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
WORK=${2:-"$REPO/build-macos/host-tools"}
export MACOSX_DEPLOYMENT_TARGET=${MACOSX_DEPLOYMENT_TARGET:-15.0}
JOBS=$(sysctl -n hw.ncpu)

[ "$(uname -s)" = Darwin ] || { echo "build-host-tools.sh is macOS-only" >&2; exit 2; }
mkdir -p "$OUT/bin" "$OUT/lib" "$OUT/src" "$WORK/dl"
OUT=$(cd "$OUT" && pwd)

#   name|url|sha256
SOURCES=(
  "make|https://ftp.gnu.org/gnu/make/make-4.4.1.tar.gz|dd16fb1d67bfab79a72f5e8390735c49e3e8e70b4945a15ab1f81ddb78658fb3"
  "gmp|https://ftp.gnu.org/gnu/gmp/gmp-6.3.0.tar.xz|a3c2b80201b89e68616f4ad30bc66aee4927c3ce50e33929ca819d5c43538898"
  "mpfr|https://ftp.gnu.org/gnu/mpfr/mpfr-4.2.2.tar.xz|b67ba0383ef7e8a8563734e2e889ef5ec3c3b898a01d00fa0a6869ad81c6ce01"
  "mpc|https://ftp.gnu.org/gnu/mpc/mpc-1.4.1.tar.xz|91204cd32f164bd3b7c992d4a6a8ce6519511aadab30f78b6982d0bf8d73e931"
  "zstd|https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz|eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3"
)

# fetch <name> -> unpacks a verified copy into $WORK/<name> and echoes its dir
fetch() {
  local want="$1" name url sha file dir
  for entry in "${SOURCES[@]}"; do
    IFS='|' read -r name url sha <<<"$entry"
    [ "$name" = "$want" ] || continue
    file="$WORK/dl/$(basename "$url")"
    if [ ! -f "$file" ] || [ "$(shasum -a 256 "$file" | cut -d' ' -f1)" != "$sha" ]; then
      echo "[host-tools] downloading $(basename "$url")" >&2
      curl --fail --location --retry 3 --silent --show-error -o "$file" "$url"
    fi
    [ "$(shasum -a 256 "$file" | cut -d' ' -f1)" = "$sha" ] \
      || { echo "[host-tools] SHA-256 mismatch for $(basename "$url")" >&2; exit 1; }
    cp "$file" "$OUT/src/"
    dir="$WORK/$name"
    rm -rf "$dir"
    mkdir -p "$dir"
    tar -xf "$file" -C "$dir" --strip-components=1
    echo "$dir"
    return 0
  done
  echo "[host-tools] unknown source $want" >&2
  exit 1
}

PREFIX="$WORK/prefix"
rm -rf "$PREFIX"
mkdir -p "$PREFIX"
# Homebrew's /usr/local (Intel) or /opt/homebrew headers and libraries must
# not leak into these builds: every optional dependency is switched off
# explicitly, and check_deps below refuses anything outside the system anyway.
CONF_ENV=(CC=clang CXX=clang++ "CFLAGS=-O2" "CXXFLAGS=-O2")

echo "[host-tools] GNU make"
d=$(fetch make)
(cd "$d" && env "${CONF_ENV[@]}" ./configure --disable-nls --without-guile \
   --disable-dependency-tracking >/dev/null && make -j"$JOBS" >/dev/null)
install -m 0755 "$d/make" "$OUT/bin/gmake"

echo "[host-tools] GMP"
d=$(fetch gmp)
(cd "$d" && env "${CONF_ENV[@]}" ./configure --prefix="$PREFIX" --enable-shared \
   --disable-static --with-pic >/dev/null && make -j"$JOBS" >/dev/null && make install >/dev/null)

echo "[host-tools] MPFR"
d=$(fetch mpfr)
(cd "$d" && env "${CONF_ENV[@]}" ./configure --prefix="$PREFIX" --with-gmp="$PREFIX" \
   --enable-shared --disable-static >/dev/null && make -j"$JOBS" >/dev/null && make install >/dev/null)

echo "[host-tools] MPC"
d=$(fetch mpc)
(cd "$d" && env "${CONF_ENV[@]}" ./configure --prefix="$PREFIX" --with-gmp="$PREFIX" \
   --with-mpfr="$PREFIX" --enable-shared --disable-static >/dev/null \
   && make -j"$JOBS" >/dev/null && make install >/dev/null)

echo "[host-tools] zstd"
d=$(fetch zstd)
(cd "$d/lib" && env "${CONF_ENV[@]}" make -j"$JOBS" libzstd >/dev/null)
cp "$d/lib/libzstd.1.dylib" "$PREFIX/lib/libzstd.1.dylib" 2>/dev/null \
  || cp "$d"/lib/libzstd.1.*.dylib "$PREFIX/lib/libzstd.1.dylib"

# The four libraries under the sonames PS2DEV's binaries ask for, each one
# naming itself and its siblings by @loader_path - so the set works from
# whichever directory setup.sh copies it into.
LIBS=(libgmp.10.dylib libmpfr.6.dylib libmpc.3.dylib libzstd.1.dylib)
for lib in "${LIBS[@]}"; do
  cp -L "$PREFIX/lib/$lib" "$OUT/lib/$lib"
  chmod 0644 "$OUT/lib/$lib"
  install_name_tool -id "@loader_path/$lib" "$OUT/lib/$lib"
done
for lib in "${LIBS[@]}"; do
  otool -L "$OUT/lib/$lib" | tail -n +2 | awk '{print $1}' | while read -r dep; do
    case "$dep" in
      "$PREFIX"/*|"$WORK"/*|/usr/local/lib/libzstd*)
        install_name_tool -change "$dep" "@loader_path/$(basename "$dep")" "$OUT/lib/$lib" ;;
    esac
  done
done

echo "[host-tools] OpenVCL, vclpp, bin2s"
OV_BUILD="$WORK/openvcl"
rm -rf "$OV_BUILD"
cmake -S "$REPO/vendor/openvcl" -B "$OV_BUILD" -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=ON >/dev/null
cmake --build "$OV_BUILD" -j"$JOBS" >/dev/null
ctest --test-dir "$OV_BUILD" --output-on-failure >/dev/null
install -m 0755 "$OV_BUILD/openvcl" "$OUT/bin/openvcl"
clang++ -std=c++14 -O2 -Wall -Wextra -pedantic "$REPO/vendor/vclpp/vclpp_main.cpp" \
  -o "$OUT/bin/vclpp"
clang -O2 "$REPO/tools/toolchain/bin2s/bin2s.c" -o "$OUT/bin/bin2s"
clang -O2 -Wall -Wextra "$REPO/tools/toolchain/machorelink.c" -o "$OUT/bin/machorelink"

# Nothing may reach outside the OS: a dependency on /opt/homebrew or
# /usr/local works on the packaging Mac and nowhere else.
check_deps() {
  local f bad=0
  for f in "$OUT"/bin/* "$OUT"/lib/*; do
    while read -r dep; do
      case "$dep" in
        /usr/lib/*|/System/*|@loader_path/*) ;;
        *) echo "[host-tools] $(basename "$f") depends on $dep" >&2; bad=1 ;;
      esac
    done < <(otool -L "$f" | tail -n +2 | awk '{print $1}')
    local minos
    minos=$(otool -l "$f" | awk '/LC_BUILD_VERSION/{f=1} f&&/minos/{print $2; exit}')
    if [ "$minos" != "$MACOSX_DEPLOYMENT_TARGET" ]; then
      echo "[host-tools] $(basename "$f") targets macOS $minos, not $MACOSX_DEPLOYMENT_TARGET" >&2
      bad=1
    fi
  done
  return $bad
}
check_deps
echo "[host-tools] ready: $OUT"
