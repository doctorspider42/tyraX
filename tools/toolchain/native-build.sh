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
PROJECT_HOST=
NATIVE_DEBUG_FLAGS=
# DrvFS is particularly expensive for make's dependency stats and the EE
# linker's large objects. Keep the public Windows install/project paths, but
# compile their mirrors on WSL's Linux filesystem. Linux builds need no mirror.
# The override is useful for A/B timings and distributions without disk space.
if command -v wslpath >/dev/null 2>&1 && [[ "$PROJECT" == /mnt/* ]] \
   && [ "${TYRAX_NATIVE_DIRECT:-0}" != 1 ]; then
  PROJECT_HOST=$PROJECT
  host_toolchain=$PS2DEV_ROOT
  stage_root="$HOME/.cache/tyrax/native"
  path_key() { printf '%s' "$1" | sha256sum | cut -c1-24; }
  PS2DEV_ROOT="$stage_root/toolchains/$(path_key "$host_toolchain")"
  CACHE_ROOT="$stage_root/engines/$(path_key "$CACHE_ROOT")"
  PROJECT="$stage_root/projects/$(path_key "$PROJECT_HOST")"
  mkdir -p "$PS2DEV_ROOT" "$CACHE_ROOT" "$PROJECT"
  if ! cmp -s "$host_toolchain/.tyrax-toolchain" "$PS2DEV_ROOT/.tyrax-toolchain"; then
    echo "[editor] Mirroring the toolchain into the WSL filesystem (once per toolchain update)..."
    rsync -rlt --delete --exclude=/.sources/ --exclude=/.tyrax-toolchain \
      "$host_toolchain/" "$PS2DEV_ROOT/"
    cp "$host_toolchain/.tyrax-toolchain" "$PS2DEV_ROOT/.tyrax-toolchain"
  fi
  echo "[editor] Using WSL filesystem build cache..."
  # --delete only touches the hashed mirror, never the authored project. obj/
  # and bin/ are owned by make, while the editor owns all incoming sources.
  rsync -rlt --delete --exclude=/obj/ --exclude=/bin/ --exclude=/.git/ \
    --exclude=/.res-baked/gi/ --exclude=/.res-baked/shadow/ \
    "$PROJECT_HOST/" "$PROJECT/"
  # Retain the authored ignore file even when make later cleans the mirror.
  if [ -f "$PROJECT_HOST/bin/.gitignore" ]; then
    mkdir -p "$PROJECT/bin"
    cp "$PROJECT_HOST/bin/.gitignore" "$PROJECT/bin/.gitignore"
  fi
  # Debugger source locations refer to the authored checkout, not its mirror.
  printf -v NATIVE_DEBUG_FLAGS '%q ' \
    "-fdebug-prefix-map=$PROJECT=$PROJECT_HOST" \
    "-fdebug-prefix-map=$CACHE_ROOT/tyra/engine=$ENGINE_SOURCE/engine" \
    "-fdebug-prefix-map=$PS2DEV_ROOT=$host_toolchain"
fi
export PS2DEV="$PS2DEV_ROOT"
export PS2SDK="$PS2DEV_ROOT/ps2sdk"
export PATH="$PS2DEV_ROOT/bin:$PS2DEV_ROOT/ee/bin:$PS2DEV_ROOT/iop/bin:$PS2DEV_ROOT/dvp/bin:$PS2SDK/bin:$PATH"

ENGINE_ROOT="$CACHE_ROOT/tyra"
ENGINE="$ENGINE_ROOT/engine"

# Drop generated trees, including ones an earlier Docker build owns.
#
# Docker Desktop's compiler container writes into the project through a Windows
# bind mount as uid 0, and WSL keeps that ownership in the file's metadata. The
# native backend then runs as the normal user and cannot unlink those files -
# every `rm` fails with "Permission denied" even though the NTFS ACL grants the
# user full control, and the build dies on the very first clean. Windows itself
# ignores the WSL metadata, so a Windows-side delete gets rid of them; `make`
# recreates the trees as the current user afterwards.
#
# A dropped tree's own `.gitignore` is put back: bin/.gitignore and
# obj/.gitignore are COMMITTED (they are what keeps those otherwise-empty
# directories in git), so wiping the tree left every checkout showing a deleted
# tracked file until someone noticed and restored it by hand.
drop_dirs() {
  local dir win keep
  for dir in "$@"; do
    [ -e "$dir" ] || continue
    keep=
    if [ -f "$dir/.gitignore" ]; then
      keep=$(mktemp)
      cp "$dir/.gitignore" "$keep"
    fi
    if ! { rm -rf "$dir" 2>/dev/null && [ ! -e "$dir" ]; }; then
      if command -v wslpath >/dev/null 2>&1 \
         && win=$(wslpath -w "$dir" 2>/dev/null) && [ -n "$win" ]; then
        echo "[editor] ${dir##*/} is owned by an old Docker build - removing it via Windows..."
        (cd /mnt/c 2>/dev/null || cd /; cmd.exe /c rd /s /q "$win") >/dev/null 2>&1 || true
      fi
      [ ! -e "$dir" ] || rm -rf "$dir"  # still there: fail loudly, with the real reason
    fi
    if [ -n "$keep" ]; then
      mkdir -p "$dir"
      cp "$keep" "$dir/.gitignore"
      rm -f "$keep"
    fi
  done
}

mkdir -p "$ENGINE" "$PROJECT/bin" "$PROJECT/obj"

TOOLCHAIN_MARKER="$PS2DEV_ROOT/.tyrax-toolchain"
ENGINE_TOOLCHAIN_MARKER="$ENGINE_ROOT/.tyrax-toolchain"
if [ ! -f "$ENGINE_TOOLCHAIN_MARKER" ] \
   || ! cmp -s "$TOOLCHAIN_MARKER" "$ENGINE_TOOLCHAIN_MARKER"; then
  echo "[editor] Toolchain changed - rebuilding engine and game objects..."
  drop_dirs "$ENGINE/obj" "$ENGINE/bin" "$PROJECT/obj" "$PROJECT/bin"
  cp "$TOOLCHAIN_MARKER" "$ENGINE_TOOLCHAIN_MARKER"
fi

# Every project needs its own identity: another game's build may already have
# updated the shared engine stamp while this game's objects are still old.
GAME_TOOLCHAIN_MARKER="$PROJECT/obj/.tyrax-toolchain"
if ! cmp -s "$TOOLCHAIN_MARKER" "$GAME_TOOLCHAIN_MARKER"; then
  echo "[editor] Game toolchain changed - rebuilding game objects..."
  drop_dirs "$PROJECT/obj" "$PROJECT/bin"
  mkdir -p "$PROJECT/obj"
  cp "$TOOLCHAIN_MARKER" "$GAME_TOOLCHAIN_MARKER"
fi

if [ "$REBUILD" = 1 ]; then
  echo "[editor] Rebuild: dropping native game and engine objects..."
  drop_dirs "$PROJECT/obj" "$PROJECT/bin" "$ENGINE/obj" "$ENGINE/bin"
  if [ -n "$PROJECT_HOST" ]; then
    drop_dirs "$PROJECT_HOST/obj" "$PROJECT_HOST/bin"
  fi
fi

SYNC_LOG="$CACHE_ROOT/engine-sync.txt"
mkdir -p "$CACHE_ROOT"
rsync -rlci --delete --exclude=obj --exclude=bin \
  "$ENGINE_SOURCE/engine/" "$ENGINE/" > "$SYNC_LOG"
# Empty output is normal; a failed source sync must still stop the build.
sed -i '/^.d/d' "$SYNC_LOG"
if ! cmp -s "$ENGINE_SOURCE/Makefile.base" "$ENGINE_ROOT/Makefile.base"; then
  cp "$ENGINE_SOURCE/Makefile.base" "$ENGINE_ROOT/Makefile.base"
fi

if [ -s "$SYNC_LOG" ] || [ ! -f "$ENGINE/bin/libtyra.a" ]; then
  echo "[editor] Engine sources changed - rebuilding libtyra..."
  if grep -qE '[.](vclpp|vcl|vsm|i|h)$' "$SYNC_LOG"; then
    echo "[editor] VU1 sources changed - rebuilding the microprograms..."
    find "$ENGINE/obj" -type f \( -name '*_vu1.o' -o -name '*_vu1.o.vcl' \
      -o -name '*_vu1.o.vsm' \) -delete 2>/dev/null || true
  fi
  # Also drop removed archive members when a source disappears from SOURCES.
  rm -f "$ENGINE/bin/libtyra.a"
fi
# Always ask make: a failed compile can leave synced sources beside stale
# objects, and a changed Makefile must invalidate the old compiler flags.
# An unchanged engine is a cheap no-op now that its archive target is real.
make -C "$ENGINE" -j"$(getconf _NPROCESSORS_ONLN)" NATIVE_DEBUG_FLAGS="$NATIVE_DEBUG_FLAGS"

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
  ENGINEDIR="$ENGINE" TYRA_MAKEFILE="$ENGINE_ROOT/Makefile.base" \
  NATIVE_DEBUG_FLAGS="$NATIVE_DEBUG_FLAGS"

# Sound effects: the make resources phase copied the WAV sources into bin/;
# convert only stale targets, then remove source and orphaned copies.
while IFS= read -r -d '' wav; do
  rel=${wav#"$PROJECT/res/"}
  out="$PROJECT/bin/${rel%.wav}.adpcm"
  input="$PROJECT/.res-baked/$rel"
  [ -f "$input" ] || { echo "[editor] Missing normalized sound: $rel" >&2; exit 1; }
  mkdir -p "$(dirname "$out")"
  loop=0
  case "$wav" in *-loop.wav) loop=1;; esac
  if [ -f "$PROJECT/inc/vehicle_sound_loops.gen.txt" ] &&
     grep -Fqx -- "res/$rel" "$PROJECT/inc/vehicle_sound_loops.gen.txt"; then
    loop=1
  fi
  stale=0
  if [ ! -e "$out" ] || [ "$input" -nt "$out" ]; then
    stale=1
  else
    # Loop intent can change without changing the source WAV's timestamp.
    # Offset 6 is adpenc's loop byte; check both loop and one-shot transitions.
    loop_byte=$(od -An -tu1 -j6 -N1 "$out" 2>/dev/null | tr -d '[:space:]') || loop_byte=invalid
    channels=$(od -An -tu1 -j5 -N1 "$out" 2>/dev/null | tr -d '[:space:]') || channels=invalid
    [ "$channels" = 1 ] || stale=1
    [ "$loop_byte" = "$loop" ] || stale=1
  fi
  if [ "$stale" = 1 ]; then
    if [ "$loop" = 1 ]; then
      echo "[editor] adpenc -L ${wav#"$PROJECT/"}"
      adpenc -L "$input" "$out"
    else
      echo "[editor] adpenc ${wav#"$PROJECT/"}"
      adpenc "$input" "$out"
    fi
  fi
done < <(find "$PROJECT/res/sfx" -maxdepth 3 -type f -name '*.wav' -print0 2>/dev/null)
find "$PROJECT/bin/sfx" -maxdepth 3 -type f -name '*.wav' -delete 2>/dev/null || true
while IFS= read -r -d '' out; do
  rel=${out#"$PROJECT/bin/"}
  [ -e "$PROJECT/res/${rel%.adpcm}.wav" ] || rm -f "$out"
done < <(find "$PROJECT/bin/sfx" -maxdepth 3 -type f -name '*.adpcm' -print0 2>/dev/null)

# Rebuild drops obj/, including its identity; restore it after a successful build.
cp "$TOOLCHAIN_MARKER" "$GAME_TOOLCHAIN_MARKER"
if [ -n "$PROJECT_HOST" ]; then
  # Keep Windows-side runtime channels (Live Link/debugger/launch markers) and
  # timestamps intact. Only new or changed generated outputs travel back.
  rsync -rlt "$PROJECT/bin/" "$PROJECT_HOST/bin/"
  # The codec removed orphaned sounds and WAVs in the mirror; propagate those
  # deletions in the generated sound directory, without deleting runtime files.
  if [ -d "$PROJECT/bin/sfx" ]; then
    rsync -rlt --delete "$PROJECT/bin/sfx/" "$PROJECT_HOST/bin/sfx/"
  fi
  PROJECT=$PROJECT_HOST
fi
echo "[editor] Native build complete: $PROJECT/bin"
