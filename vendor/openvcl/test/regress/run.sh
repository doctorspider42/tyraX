#!/usr/bin/env bash
# Run every reproducer through a built openvcl and assert the property it
# protects.  See README.md for why there are three kinds of assertion and why
# every instrument here is controlled against Sony's vcl.
#
#   ./run.sh /path/to/built/openvcl [--verbose]
#
# Exit status is 0 only when every case passes.  Cases come from cases.tsv, so
# adding one is a line of data plus a .vcl rather than a code change.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ISA="$HERE/../../src/VuInstructionInfo.cpp"
OPENVCL="${1:-}"
VERBOSE="${2:-}"

if [ -z "$OPENVCL" ] || [ ! -x "$OPENVCL" ]; then
    echo "usage: $0 /path/to/built/openvcl [--verbose]" >&2
    exit 2
fi
if [ ! -f "$ISA" ]; then
    echo "cannot find $ISA - the checkers parse the ISA table out of the source" >&2
    exit 2
fi
# Refuse to run rather than pass silently.  Three of the four kinds below are
# python; without it they produce no output, `flagged` never matches, and every
# one of them reports ok.  That is an assertion that cannot fail, which is the
# exact failure mode this suite exists to prevent - and it caught the author of
# this file out once already.
if ! command -v python3 >/dev/null 2>&1; then
    echo "python3 not found - ORDER, VALUE and PATH cases would silently pass" >&2
    exit 2
fi

# The flag list the TyraX toolchain image passes.  Here rather than per case,
# because a case is about a property and not about a configuration - and so a
# flag that stops existing breaks this loudly, in one place.
FLAGS=(--schedule-flag-readers --fmac-interlock --sce-latencies --emit-delay-fillers
       --branch-interlock --branch-bubble-on-dependency --loop-liveness-always
       --upper-move-with-w --pair-best-of-two --pair-best-of-many
       --trim-uncarried-ranges --coalesce-float-writes --sink-loads
       --sink-loads-across-stores --sink-loads-into-loops --sink-loads-past-branches
       --sink-loads-best-of --drop-dead-writes --exempt-full-clip-masks
       --clip-exemption-best-of --split-dead-float-ranges)

OUT="$(mktemp -d)"; trap 'rm -rf "$OUT"' EXIT
mkdir -p "$OUT/src" "$OUT/vsm"

# --- compile every non-XFAIL case once ------------------------------------
declare -A KIND ARG BUG
order=()
while IFS=$'\t' read -r name kind arg bug _rest; do
    case "$name" in ''|'#'*) continue ;; esac
    KIND["$name"]="$kind"; ARG["$name"]="$arg"; BUG["$name"]="$bug"
    order+=("$name")
done < "$HERE/cases.tsv"

# Every compile is bounded, TIME case or not. A compiler that does not terminate
# is the one failure this suite could not report: without a bound the harness
# hangs with it and says nothing at all, which is worse than a red line.
# DEFAULT_TMO is generous - it is there to keep the suite answering, not to
# measure anything. A TIME case sets its own, and that one IS the assertion.
DEFAULT_TMO=120
if ! command -v timeout >/dev/null 2>&1; then
    echo "timeout(1) not found - a non-terminating compile would hang this suite" >&2
    exit 2
fi

fail=0; pass=0; xfail=0; failed=""
declare -A RC
for name in "${order[@]}"; do
    [ "${KIND[$name]}" = "XFAIL" ] && continue
    src="$HERE/src/$name.vcl"
    [ -f "$src" ] || { echo "  FAIL  $name - no src/$name.vcl"; fail=$((fail+1)); failed="$failed $name"; continue; }
    cp "$src" "$OUT/src/"
    tmo="$DEFAULT_TMO"
    [ "${KIND[$name]}" = "TIME" ] && tmo="${ARG[$name]}"
    # A case may ask for the list MINUS one flag, with arg `no:--the-flag`.
    # Some defects live only in a flag's OFF path - the one the twenty-one hide -
    # and a suite that can only compile one configuration cannot assert those at
    # all. Dropping exactly one flag keeps the rest of the configuration this
    # file exercises, so a case that fails still names one thing.
    caseFlags=("${FLAGS[@]}")
    case "${ARG[$name]}" in
    no:*)
        drop="${ARG[$name]#no:}"
        caseFlags=()
        for f in "${FLAGS[@]}"; do
            [ "$f" = "$drop" ] || caseFlags+=("$f")
        done
        if [ "${#caseFlags[@]}" -eq "${#FLAGS[@]}" ]; then
            echo "  FAIL  $name - arg says drop $drop, which is not in the flag list"
            fail=$((fail+1)); failed="$failed $name"; continue
        fi
        ;;
    esac
    timeout "$tmo" "$OPENVCL" "${caseFlags[@]}" "$src" > "$OUT/vsm/$name.vsm" 2>"$OUT/$name.err"
    RC["$name"]=$?
    # 124 is GNU coreutils' "still running", 143 the shell's view of the SIGTERM
    # that killed it, 15 busybox's. Anything else is a compiler that answered.
    case "${RC[$name]}" in
        124|143|137|15) touch "$OUT/timeout.$name" ;;
    esac
    if [ "${RC[$name]}" -ne 0 ]; then
        # A TIME case is JUDGED below on this status, so it must not be counted
        # here as well - the whole point of the kind is that not finishing is the
        # failure it reports, in its own words.
        if [ "${KIND[$name]}" != "TIME" ]; then
            echo "  FAIL  $name - did not compile (${BUG[$name]})"
            [ "$VERBOSE" = "--verbose" ] && sed 's/^/          /' "$OUT/$name.err"
            fail=$((fail+1)); failed="$failed $name"
        fi
        # Removed either way: half a .vsm from a killed compiler is not an input
        # the value oracles below should be reading.
        rm -f "$OUT/vsm/$name.vsm"
    fi
done

# --- ORDER and VALUE run once over the whole directory --------------------
ORDER_OUT="$(python3 "$HERE/lib/p8-flagorder.py" "$ISA" "$OUT/src" "$OUT/vsm" 2>&1 || true)"
VALUE_OUT="$(python3 "$HERE/lib/pa-dag.py"       "$ISA" "$OUT/src" "$OUT/vsm" 2>&1 || true)"
# PATH is the same question as VALUE but path-sensitive with bounded unrolling.
# It exists as a separate kind because the straight-line oracle CANNOT see a
# loop-carried or branch-conditional defect - three cases in this suite passed
# under it on a build that had the bug, which is an assertion that cannot fail.
PATH_OUT="$(python3 "$HERE/lib/pb-dag.py"       "$ISA" "$OUT/src" "$OUT/vsm" 2>&1 || true)"
# COND compares, at every conditional branch, the expression the branch TESTS.
# The other three kinds compare values that are STORED, so a value whose only
# reader is a branch condition was never compared at all - which is why four
# rounds of value oracles walked past the delay-slot bug.
COND_OUT="$(python3 "$HERE/lib/pd-cond.py"       "$ISA" "$OUT/src" "$OUT/vsm" 2>&1 || true)"
# BRGAP is not a value question at all. The four kinds above compare WHAT a
# program computes; this one compares WHEN a value is readable, which is the only
# way to see a register written one row before the branch that tests it. Both
# value oracles reported 0 divergent over the engine's whole shipping corpus on a
# build carrying exactly that defect, so this is a fourth question and not a
# fourth case.
BRGAP_OUT="$(python3 "$HERE/lib/pz-brgap.py"     "$ISA" "$OUT/src" "$OUT/vsm" 2>&1 || true)"
[ "$VERBOSE" = "--verbose" ] && { echo "--- p8-flagorder"; echo "$ORDER_OUT"; echo "--- pa-dag"; echo "$VALUE_OUT"; echo "--- pb-dag"; echo "$PATH_OUT"; echo "--- pd-cond"; echo "$COND_OUT"; echo "--- pz-brgap"; echo "$BRGAP_OUT"; }

flagged() {  # flagged <tool-output> <case-name> -> 0 if the tool named this case
    echo "$1" | grep -qE "(^|[^A-Za-z0-9_])$2([^A-Za-z0-9_]|$)"
}

for name in "${order[@]}"; do
    kind="${KIND[$name]}"
    if [ "$kind" = "XFAIL" ]; then
        printf '  xfail %-30s %s\n' "$name" "${BUG[$name]}"
        xfail=$((xfail+1)); continue
    fi
    if [ "$kind" = "TIME" ]; then
        # The only kind whose assertion is about the COMPILER rather than about
        # what it emitted: this program must finish. A build that loops forever
        # on one control-flow shape hangs a project build instead of failing it,
        # which is the one failure mode a user cannot diagnose. The bound is not
        # a performance budget - it is orders of magnitude above the real time -
        # so a machine being slow today cannot turn this red.
        if [ -f "$OUT/timeout.$name" ]; then
            printf '  FAIL  %-30s %s - did not finish inside %ss\n' \
                   "$name" "${BUG[$name]}" "${ARG[$name]}"
            fail=$((fail+1)); failed="$failed $name"
        elif [ "${RC[$name]}" != "0" ] || [ ! -s "$OUT/vsm/$name.vsm" ]; then
            printf '  FAIL  %-30s %s - compile failed (rc %s)\n' \
                   "$name" "${BUG[$name]}" "${RC[$name]}"
            [ "$VERBOSE" = "--verbose" ] && sed 's/^/          /' "$OUT/$name.err"
            fail=$((fail+1)); failed="$failed $name"
        else
            printf '  ok    %-30s %s\n' "$name" "$kind"; pass=$((pass+1))
        fi
        continue
    fi
    [ -f "$OUT/vsm/$name.vsm" ] || continue   # compile failure already counted
    ok=1; detail=""
    case "$kind" in
    COUNT)
        mnem="${ARG[$name]%%:*}"; want="${ARG[$name]##*:}"
        got=$(grep -c -i -E "(^|[[:space:]])$mnem" "$OUT/vsm/$name.vsm" || true)
        [ "$got" -ge "$want" ] || { ok=0; detail="emitted $got x $mnem, source needs $want"; }
        ;;
    # The other direction, and the only kind here that can fail on a compiler
    # which has become MORE conservative.  Every value oracle in this suite is
    # satisfied by an emitter that waits for everything, so a fix that pads
    # blind passes all of them - which is how a correctness case ends up with a
    # control that asserts nothing.
    MAXCOUNT)
        mnem="${ARG[$name]%%:*}"; want="${ARG[$name]##*:}"
        got=$(grep -c -i -E "(^|[[:space:]])$mnem" "$OUT/vsm/$name.vsm" || true)
        [ "$got" -le "$want" ] || { ok=0; detail="emitted $got x $mnem, at most $want is legal here"; }
        ;;
    ORDER) flagged "$ORDER_OUT" "$name" && { ok=0; detail="flag-order violation"; } ;;
    VALUE) flagged "$VALUE_OUT" "$name" && { ok=0; detail="stored value diverges from the source"; } ;;
    PATH)  flagged "$PATH_OUT"  "$name" && { ok=0; detail="stored value diverges along some path"; } ;;
    COND)  flagged "$COND_OUT"  "$name" && { ok=0; detail="a branch condition differs from the source"; } ;;
    BRGAP) flagged "$BRGAP_OUT" "$name" && { ok=0; detail="a branch tests a register produced too close to it along some path"; } ;;
    *)     ok=0; detail="unknown kind $kind" ;;
    esac
    if [ "$ok" = 1 ]; then
        printf '  ok    %-30s %s\n' "$name" "$kind"; pass=$((pass+1))
    else
        printf '  FAIL  %-30s %s - %s\n' "$name" "${BUG[$name]}" "$detail"
        fail=$((fail+1)); failed="$failed $name"
    fi
done

echo
echo "  passed $pass, failed $fail, xfail $xfail"
[ "$fail" -gt 0 ] && { echo "  reintroduced:$failed"; exit 1; }
exit 0
