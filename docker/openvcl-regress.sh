#!/usr/bin/env bash
# Run the openvcl fork's own regression suite against the assembler an image
# actually carries.
#
#   docker/openvcl-regress.sh [image]        default: tyrax-toolchain:src
#
# Why this exists here and not only in the fork: `docker/openvcl-tyrax.patch` is
# how openvcl reaches this repo, and a bump to it is a one-line diff that no
# reviewer can read. Nine bugs in that compiler were silent - the program
# assembled, fitted micro memory, drew a picture and computed the wrong thing -
# and four of them shipped. This is the command that says a bump reintroduced
# one.
#
# The suite needs the fork's sources (it parses the VU instruction table out of
# them at run time, rather than keeping a second copy that can drift), so point
# OPENVCL_SRC at a checkout of doctorspider42/openvcl-tyrax. Without one this
# exits 2 rather than reporting success.
set -uo pipefail

IMAGE="${1:-tyrax-toolchain:src}"
SRC="${OPENVCL_SRC:-}"

if [ -z "$SRC" ] || [ ! -d "$SRC/test/regress" ]; then
    cat >&2 <<'EOF'
openvcl-regress: set OPENVCL_SRC to a checkout of the fork, e.g.

    git clone -b tyrax https://github.com/doctorspider42/openvcl-tyrax /tmp/openvcl
    OPENVCL_SRC=/tmp/openvcl docker/openvcl-regress.sh

Exiting 2 rather than reporting a pass with nothing run.
EOF
    exit 2
fi

echo "== openvcl regression suite: $IMAGE"
docker run --rm -v "$SRC:/fork" "$IMAGE" sh -c '
    # The two image families have different package managers; try both and let
    # run.sh refuse if neither worked, rather than silently skipping the python
    # checks - that failure mode is exactly what the suite is guarding against.
    (apk add --no-cache python3 bash >/dev/null 2>&1) ||
    (apt-get update -qq >/dev/null 2>&1 && apt-get install -y -qq python3 >/dev/null 2>&1)
    cd /fork/test/regress && bash run.sh /usr/local/bin/openvcl
'
