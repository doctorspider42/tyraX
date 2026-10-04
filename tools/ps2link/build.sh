#!/usr/bin/env bash
# Builds the TyraX ps2link - the ONLY ps2link the editor deploys to. The POSIX
# twin of build.ps1; keep the two in step (same repo, same commit, same patch).
#
# Reproducible: clones a pinned ps2link, applies tyrax.patch, builds inside the
# official ps2dev/ps2dev toolchain image (Docker), and drops ps2link.elf next to
# this script. Needs Docker running (and the current user in the docker group).
#
#   ./build.sh           # build -> tools/ps2link/ps2link.elf  (0x01ee8000)
#   ./build.sh --clean   # nuke the work tree first
#   ./build.sh --low     # -> tools/ps2link/ps2link-low.elf   (0x00094000)
#   ./build.sh --no-usb  # -> ...-nousb.elf: no usbd/ps2kbd/ps2mouse baked in
#   ./build.sh --packed  # -> ...-packed.elf: run ps2-packer over it
#
# --packed runs ps2-packer over the image, the way upstream's releases ship:
# what a launcher loads is then a small stub high in memory that decompresses
# the real image down once the loader is done with its own code. That is what a
# LOW build needs to boot from FreeMcBoot's menu at all - an unpacked low image
# lands on that loader mid-load and black-screens. The default here is
# unpacked, because the high build boots from every launcher tried either way
# and unpacked is the shape that has been measured through Run -> Stop cycles.
#
# Two link addresses, and which one works depends on how you boot it.
#
# 0x00094000 is the "BIOS unused" window below the 0x00100000 a game loads at.
# It keeps ps2link clear of the game - but it is also where a launcher keeps
# its own resident loader, so FreeMcBoot's menu and its shortcuts black-screen
# on it (uLaunchELF, which loads elsewhere, boots it fine). 0x01ee8000 is the
# top of RAM: clear of every launcher, at the price of being in reach of a game
# that allocates its way past ~31 MB.
#
# Upstream ships the low one as its default and publishes both from CI
# ("default" and "highloading"). We default to HIGH instead, because booting
# from the FMCB menu is the normal way to start a console here and a black
# screen is a worse failure than a memory ceiling no scene has come near. The
# boot screen prints the address it actually loaded at, so a flashed card
# always says which one it is.
#
# Flash the result onto the console's memory card - see docs/ps2link-setup.md.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

CLEAN=0
LOADHIGH=1
NOUSB=0
PACKED=0
for arg in "$@"; do
    case "$arg" in
        --clean|-Clean) CLEAN=1 ;;
        --low|-Low) LOADHIGH=0 ;;
        --no-usb|-NoUsb) NOUSB=1 ;;
        --packed|-Packed) PACKED=1 ;;
        *) echo "Unknown option: $arg (expected --clean, --low, --no-usb or --packed)" >&2; exit 2 ;;
    esac
done
# `make` runs ps2-packer over ee/ps2link.elf and writes bin/PS2LINK.ELF;
# `make ee` stops at the raw image.
MAKETARGET="ee"
[ "$PACKED" = 1 ] && MAKETARGET=""

here="$PWD"
work="$here/build"
patch="$here/tyrax.patch"

# Pinned so the patch always applies cleanly; bump both when refreshing.
repo='https://github.com/ps2dev/ps2link.git'
commit='0c6138c5553760423070d1797ac475c4d98a06e6'
# Pinned to a release tag, and the SAME tag docker/Dockerfile builds its copy of
# ps2link with, so the image's /usr/local/share/tyrax/ps2link/ and this script
# produce the same ELF. It used to be ':latest', which quietly made a script
# whose whole point is reproducibility a moving target: the same commit and the
# same patch measured 285 364 B when tools/ps2link/ps2link.elf was committed and
# 285 620 B a few weeks later, with nothing here changed. v2.0.0 gives 284 340 B
# every time.
image='ps2dev/ps2dev:v2.0.0'

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
[ "$PACKED" = 1 ] && elf="$work/bin/PS2LINK.ELF"
[ -f "$elf" ] || { echo "expected $elf, not produced" >&2; exit 1; }
suffix=""
[ "$LOADHIGH" = 0 ] && suffix="$suffix-low"
[ "$NOUSB" = 1 ] && suffix="$suffix-nousb"
[ "$PACKED" = 1 ] && suffix="$suffix-packed"
out="$here/ps2link$suffix.elf"
cp -f "$elf" "$out"
# The container builds as root; hand the artifact back to the invoking user so
# the next build (and copying it onto a memory card) needs no sudo.
chmod u+rw "$out" 2>/dev/null || true
echo "== OK: $out ($(stat -c%s "$out") bytes) =="
echo "Flash this onto your PS2 as PS2LINK.ELF - see docs/ps2link-setup.md."
if [ "$LOADHIGH" = 0 ] && [ "$PACKED" = 0 ]; then
    echo "NOTE: an unpacked low build black-screens from FreeMcBoot's menu -"
    echo "      it lands on that loader mid-load. Add --packed for a build that"
    echo "      boots there (it still dies on Stop - docs/backlog.md)."
fi
if [ "$NOUSB" = 1 ]; then
    echo "      No keyboard or mouse over ps2link with this one - it bakes in"
    echo "      no usbd/ps2kbd/ps2mouse, and a game cannot load them itself on"
    echo "      a network-booted ps2link. The banner reads 'r6 (no USB)'."
fi
