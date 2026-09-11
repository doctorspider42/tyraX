#!/usr/bin/env bash
# Build one generated game directly with the bundled PS2DEV/OpenVCL toolchain.
set -euo pipefail

if [ "$#" -ne 5 ]; then
  echo "usage: native-build.sh PROJECT ENGINE_SOURCE CACHE_ROOT PS2DEV_ROOT REBUILD" >&2
  exit 2
fi
PROJECT=$(cd "$1" && pwd)
ENGINE_SOURCE=$(cd "$2" && pwd)
CACHE_ROOT=$3
PS2DEV_ROOT=$4
REBUILD=$5
HERE=$(cd "$(dirname "$0")" && pwd)

case "$CACHE_ROOT" in
  /*) ;;
  *) echo "[editor] native cache root must be absolute: $CACHE_ROOT" >&2; exit 2 ;;
esac
if [ "$CACHE_ROOT" = / ] || [ "$CACHE_ROOT" = "$HOME" ]; then
  echo "[editor] refusing unsafe native cache root: $CACHE_ROOT" >&2
  exit 2
fi

"$HERE/setup.sh" "$PS2DEV_ROOT"
export PS2DEV="$PS2DEV_ROOT"
export PS2SDK="$PS2DEV_ROOT/ps2sdk"
export PATH="$PS2DEV_ROOT/bin:$PS2DEV_ROOT/ee/bin:$PS2DEV_ROOT/iop/bin:$PS2DEV_ROOT/dvp/bin:$PS2SDK/bin:$PATH"

ENGINE_ROOT="$CACHE_ROOT/tyra"
ENGINE="$ENGINE_ROOT/engine"
mkdir -p "$ENGINE" "$PROJECT/bin" "$PROJECT/obj"

TOOLCHAIN_MARKER="$PS2DEV_ROOT/.tyrax-toolchain"
ENGINE_TOOLCHAIN_MARKER="$ENGINE_ROOT/.tyrax-toolchain"
if [ ! -f "$ENGINE_TOOLCHAIN_MARKER" ] \
   || ! cmp -s "$TOOLCHAIN_MARKER" "$ENGINE_TOOLCHAIN_MARKER"; then
  echo "[editor] Toolchain changed - rebuilding engine and game objects..."
  rm -rf "$ENGINE/obj" "$ENGINE/bin" "$PROJECT/obj" "$PROJECT/bin"
  cp "$TOOLCHAIN_MARKER" "$ENGINE_TOOLCHAIN_MARKER"
fi

if [ "$REBUILD" = 1 ]; then
  echo "[editor] Rebuild: dropping native game and engine objects..."
  rm -rf "$PROJECT/obj" "$PROJECT/bin" "$ENGINE/obj" "$ENGINE/bin"
fi

SYNC_LOG="$CACHE_ROOT/engine-sync.txt"
mkdir -p "$CACHE_ROOT"
rsync -rlci --delete --exclude=obj --exclude=bin \
  "$ENGINE_SOURCE/engine/" "$ENGINE/" | grep -v '^.d' > "$SYNC_LOG" || true
cp "$ENGINE_SOURCE/Makefile.base" "$ENGINE_ROOT/Makefile.base"

if [ -s "$SYNC_LOG" ] || [ ! -f "$ENGINE/bin/libtyra.a" ]; then
  echo "[editor] Engine sources changed - rebuilding libtyra..."
  if grep -qE '[.](vclpp|vcl|vsm|i|h)$' "$SYNC_LOG"; then
    echo "[editor] VU1 sources changed - rebuilding the microprograms..."
    find "$ENGINE/obj" -type f \( -name '*_vu1.o' -o -name '*_vu1.o.vcl' \
      -o -name '*_vu1.o.vsm' \) -delete 2>/dev/null || true
  fi
  rm -f "$ENGINE/bin/libtyra.a" "$PROJECT/bin/"'*.elf'
  make -C "$ENGINE" -j"$(getconf _NPROCESSORS_ONLN)"
fi

# Project-authored VU programs are host C++, just as in the Docker backend.
if find "$PROJECT/src/vu" "$PROJECT/src/vu0" -maxdepth 1 -name '*.cpp' \
     -print -quit 2>/dev/null | grep -q .; then
  echo "[editor] Building the project's VU sources..."
  VUGEN="$CACHE_ROOT/vugen"
  mapfile -t VUSRC < <(find "$PROJECT/src/vu" "$PROJECT/src/vu0" \
    -maxdepth 1 -name '*.cpp' -print 2>/dev/null | sort)
  g++ -std=c++17 -O0 -w -I"$PROJECT/vugen" -o "$VUGEN" \
    "$PROJECT"/vugen/*.cpp "${VUSRC[@]}"
  mkdir -p "$PROJECT/src/gen" "$PROJECT/inc/scripts"
  "$VUGEN" "$PROJECT/src/gen" "$PROJECT/inc/scripts"
fi

echo "[editor] Compiling natively (PS2DEV + OpenVCL)..."
# Docker's make recipe leaves the unstripped symbol ELF executable. On a
# Windows bind mount Docker Desktop can record that file as uid 0/mode 0755, so
# a later WSL-native `cp` cannot overwrite it. Remove only such non-writable
# generated outputs; a native link recreates them without throwing away the
# rest of the incremental cache.
for sym in "$PROJECT/bin/"*.elf.sym; do
  [ ! -e "$sym" ] || [ -w "$sym" ] || rm -f "$sym"
done
make -C "$PROJECT" -j"$(getconf _NPROCESSORS_ONLN)" \
  ENGINEDIR="$ENGINE" TYRA_MAKEFILE="$ENGINE_ROOT/Makefile.base"

# Sound effects: the make resources phase copied the WAV sources into bin/;
# convert only stale targets, then remove source and orphaned copies.
while IFS= read -r -d '' wav; do
  rel=${wav#"$PROJECT/res/"}
  out="$PROJECT/bin/${rel%.wav}.adpcm"
  mkdir -p "$(dirname "$out")"
  if [ ! -e "$out" ] || [ "$wav" -nt "$out" ]; then
    echo "[editor] adpenc ${wav#"$PROJECT/"}"
    adpenc "$wav" "$out"
  fi
done < <(find "$PROJECT/res/sfx" -maxdepth 3 -type f -name '*.wav' -print0 2>/dev/null)
find "$PROJECT/bin/sfx" -maxdepth 3 -type f -name '*.wav' -delete 2>/dev/null || true
while IFS= read -r -d '' out; do
  rel=${out#"$PROJECT/bin/"}
  [ -e "$PROJECT/res/${rel%.adpcm}.wav" ] || rm -f "$out"
done < <(find "$PROJECT/bin/sfx" -maxdepth 3 -type f -name '*.adpcm' -print0 2>/dev/null)

echo "[editor] Native build complete: $PROJECT/bin"
