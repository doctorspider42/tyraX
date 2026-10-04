#!/bin/sh
# Builds and runs the ps2link host: protocol harness (test_net_fio.c) with the
# PC's own gcc. The Linux twin of run.ps1; keep the two in step.
#
# Needs tools/ps2link/build to exist (run tools/ps2link/build.sh once) - the
# harness compiles the real patched iop/net_fio.c out of that work tree.
#
#   ./run.sh              # patched tree: expected to pass
#   ./run.sh --pristine   # upstream file at the pinned commit: expected to FAIL
#
# The --pristine run is the point of the harness: it is what shows the tests
# actually catch the bugs tyrax.patch fixes. See tools/ps2link/README.md.
set -e

here=$(cd "$(dirname "$0")" && pwd)
build=$(cd "$here/.." && pwd)/build
out=$here/harness

pristine=0
[ "$1" = "--pristine" ] && pristine=1

if [ ! -f "$build/iop/net_fio.c" ]; then
    echo "no ps2link work tree at $build - run tools/ps2link/build.sh first" >&2
    exit 1
fi

srcdir=$build/iop

if [ $pristine -eq 1 ]; then
    # Pull the untouched file straight out of git so the comparison cannot be
    # spoiled by leftover edits in build/.
    srcdir=$here/pristine
    mkdir -p "$srcdir"
    commit=$(sed -n "s/^commit='\([0-9a-f]*\)'/\1/p" "$here/../build.sh")
    git -C "$build" show "$commit:iop/net_fio.c" > "$srcdir/net_fio.c"
    cp -f "$build/iop/net_fio.h" "$srcdir"
    echo "== Harness against PRISTINE upstream $(echo "$commit" | cut -c1-10) (expected to FAIL) =="
else
    echo "== Harness against the patched tree (expected to pass) =="
fi

gcc -o "$out" "$here/test_net_fio.c" \
    -I "$here/shim" -I "$srcdir" -I "$build/include" -Wall

rc=0
"$out" || rc=$?

if [ $pristine -eq 1 ]; then
    if [ $rc -eq 0 ]; then
        echo "the pristine tree PASSED - the tests no longer catch the bugs" >&2
        exit 1
    fi
    echo "== As expected: the unpatched tree fails =="
    exit 0
fi

[ $rc -eq 0 ] || { echo "harness failed (exit $rc)" >&2; exit $rc; }
echo "== OK =="
