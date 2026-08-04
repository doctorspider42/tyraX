#!/usr/bin/env bash
# Builds the TyraX toolchain image - the image generated games compile in. The
# POSIX twin of build.ps1; keep the two in step (same defaults, same flags).
#
# CI publishes this image from .github/workflows/toolchain-image.yml; this script
# is for building it locally, which is how you test a change to the Dockerfile
# before pushing one.
#
#   ./build.sh                        # -> tyrax-toolchain:local
#   ./build.sh --tag ghcr.io/OWNER/tyrax-toolchain:test --push
#   ./build.sh --no-cache
#
# --toolchain replaces the digest-pinned compile environment the image inherits
# (docker/Dockerfile: TOOLCHAIN_IMAGE). That is a compiler change, not a
# packaging one - read "Why the toolchain is inherited" in
# docs/toolchain-image.md before using it, and rebuild + boot a game to verify.
#
#   ./build.sh --toolchain ps2dev/ps2dev:v2.0.0   # GCC 15.2, breaks vcl (musl)
#
# Point a project at the result by writing ONE line into the project directory:
#
#   echo TYRAX_IMAGE=tyrax-toolchain:local > <project>/.env
#
# and rebuilding - docker-compose.yml is regenerated on every build but reads
# that variable, so nothing in the editor has to change. See
# docs/toolchain-image.md.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."   # the build context is the repo root

TAG='tyrax-toolchain:local'
TOOLCHAIN=''
VCL_IMPL='legacy'
PUSH=0
EXTRA=()
while [ $# -gt 0 ]; do
    case "$1" in
        --tag|-Tag)             TAG="$2"; shift 2 ;;
        --toolchain|-Toolchain) TOOLCHAIN="$2"; shift 2 ;;
        --vcl-impl|-VclImpl)    VCL_IMPL="$2"; shift 2 ;;
        --push|-Push)           PUSH=1; shift ;;
        --no-cache)             EXTRA+=(--no-cache); shift ;;
        *) echo "Unknown option: $1 (expected --tag, --toolchain, --vcl-impl, --push, --no-cache)" >&2; exit 2 ;;
    esac
done
case "$VCL_IMPL" in
    legacy|openvcl) ;;
    *) echo "--vcl-impl must be 'legacy' or 'openvcl', got '$VCL_IMPL'" >&2; exit 2 ;;
esac

if ! command -v docker >/dev/null 2>&1; then
    echo "docker not found - this builds a Docker image." >&2
    exit 1
fi

EXTRA+=(--build-arg "VCL_IMPL=$VCL_IMPL")
[ -n "$TOOLCHAIN" ] && EXTRA+=(--build-arg "TOOLCHAIN_IMAGE=$TOOLCHAIN")
[ "$PUSH" = 1 ] && EXTRA+=(--push)

# BuildKit, not the classic builder: Dockerfile.dockerignore (which is what keeps
# vendor/ and build/ out of a repo-root context) is a BuildKit feature.
export DOCKER_BUILDKIT=1

echo "== Building $TAG =="
docker build -f docker/Dockerfile -t "$TAG" "${EXTRA[@]+${EXTRA[@]}}" .

# A green build proves the layers ran, not that the image can compile anything.
# These four are the ones a broken image fails on silently: vclpp/vcl missing
# their C++ runtime shows up as every VU1 program failing, not as a missing tool.
echo "== Checking the image =="
docker run --rm "$TAG" sh -c '
set -e
for t in vcl vclpp make rsync mips64r5900el-ps2-elf-g++ dvp-as; do
    command -v "$t" >/dev/null || { echo "MISSING: $t"; exit 1; }
done
vclpp 2>&1 | grep -q Usage || { echo "vclpp does not run"; exit 1; }
# The smallest program that exercises the whole VU chain: VCL -> VSM -> object.
# A bare `nop` will NOT do - the vcl in this image rejects it outside RAW mode.
printf ".init_vf_all\n.init_vi_all\n--enter\n--endenter\n\tadd.xyzw\tvf01, vf00, vf00\n--exit\n--endexit\n" > /tmp/t.vcl
vcl /tmp/t.vcl > /tmp/t.vsm
[ -s /tmp/t.vsm ] || { echo "vcl produced nothing"; exit 1; }
dvp-as /tmp/t.vsm -o /tmp/t.o
ls -l /usr/local/share/tyrax/ps2link/
echo "OK"'
echo "== OK: $TAG =="
