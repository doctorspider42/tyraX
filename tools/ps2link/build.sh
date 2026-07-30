#!/usr/bin/env bash
# Builds the TyraX ps2link - the ONLY ps2link the editor deploys to. The POSIX
# twin of build.ps1; keep the two in step (same repo, same commit, same patch).
#
# Reproducible: clones a pinned ps2link, applies tyrax.patch, builds inside the
# official ps2dev/ps2dev toolchain image (Docker), and drops ps2link.elf next to
# this script. Needs Docker running (and the current user in the docker group).
#
#   ./build.sh           # build -> tools/ps2link/ps2link.elf
#   ./build.sh --clean   # nuke the work tree first
#   ./build.sh --load-high   # -> tools/ps2link/ps2link-loadhigh.elf
#
# --load-high links ps2link at 0x01ee8000 (top of RAM) instead of the default
# 0x00094000. The default sits in the "BIOS unused" window below the 0x00100000
# a game loads at - out of the game's way, but right on top of a launcher that
# keeps its own code down there: FreeMcBoot's main menu / shortcuts hand over
# to a low-memory loader, and loading over it is a black screen with no network
# (uLaunchELF, which does not, boots the same ELF fine). The high build swaps
# the problem: it is clear of every launcher, but a game that allocates its way
# past ~31 MB would overwrite it instead. The boot screen prints the address it
# actually loaded at, so there is never any doubt which one is flashed.
#
# Flash the result onto the console's memory card - see docs/ps2link-setup.md.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

CLEAN=0
LOADHIGH=0
for arg in "$@"; do
    case "$arg" in
        --clean|-Clean) CLEAN=1 ;;
        --load-high|-LoadHigh) LOADHIGH=1 ;;
        *) echo "Unknown option: $arg (expected --clean or --load-high)" >&2; exit 2 ;;
    esac
done

here="$PWD"
work="$here/build"
patch="$here/tyrax.patch"

# Pinned so the patch always applies cleanly; bump both when refreshing.
repo='https://github.com/ps2dev/ps2link.git'
commit='0c6138c5553760423070d1797ac475c4d98a06e6'
image='ps2dev/ps2dev:latest'

if ! command -v docker >/dev/null 2>&1; then
    echo "docker not found - the ps2link build runs inside $image." >&2
    exit 1
fi

if [ "$CLEAN" = 1 ] && [ -d "$work" ]; then
    # The container builds as root, so object files under build/ can end up
    # root-owned; a plain rm then fails halfway and the next run would quietly
    # "reuse the existing clone" and build a stale tree.
    rm -rf "$work" 2>/dev/null || docker run --rm -v "$work:/work" "$image" \
        sh -c 'rm -rf /work/..?* /work/.[!.]* /work/*' >/dev/null 2>&1 || true
    rm -rf "$work"
    [ -d "$work" ] && { echo "could not remove $work" >&2; exit 1; }
fi

if [ ! -d "$work/.git" ]; then
    echo "== Cloning ps2link @ ${commit:0:10} =="
    mkdir -p "$work"
    git clone --quiet "$repo" "$work"
    git -C "$work" checkout --quiet "$commit"
else
    echo "== Reusing existing clone (use --clean to reset) =="
    git -C "$work" checkout --quiet -- .
fi

echo "== Applying tyrax.patch =="
git -C "$work" apply --verbose "$patch"

echo "== Building in $image (this pulls the image on first run) =="
# make ee builds the unpacked ELF (no ps2-packer needed). apk adds make/git the
# minimal ps2dev image lacks. Non-login shell so the image's toolchain PATH stays.
docker run --rm -v "$work:/work" -e "LOADHIGH=$LOADHIGH" "$image" sh -c '
set -e
apk add --no-cache make git >/dev/null 2>&1 || true
cd /work
make clean >/dev/null 2>&1 || true
make ee LOADHIGH=$LOADHIGH
'

elf="$work/ee/ps2link.elf"
[ -f "$elf" ] || { echo "expected $elf, not produced" >&2; exit 1; }
out="$here/ps2link.elf"
[ "$LOADHIGH" = 1 ] && out="$here/ps2link-loadhigh.elf"
cp -f "$elf" "$out"
# The container builds as root; hand the artifact back to the invoking user so
# the next build (and copying it onto a memory card) needs no sudo.
chmod u+rw "$out" 2>/dev/null || true
echo "== OK: $out ($(stat -c%s "$out") bytes) =="
echo "Flash this onto your PS2 as PS2LINK.ELF - see docs/ps2link-setup.md."
