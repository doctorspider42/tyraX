#!/usr/bin/env bash
# The negative test for BagArray's enforcement property.
#
# WHY THIS EXISTS. The whole argument for making the content stamp a TYPE
# rather than a rule is that a raw write DOES NOT COMPILE. That is a claim, and
# a claim about a compile failure is worth nothing unless somebody has watched
# the compiler refuse - a wrapper whose `data()` quietly went non-const would
# read as enforced and enforce nothing, which is strictly worse than no wrapper
# at all. So this compiles the real generated header six times: once positively
# (the sanctioned API must build) and five times adversarially (each escape
# route must be REFUSED).
#
# It needs no PS2 toolchain and no emulator: the header is guarded with
# TYRAX_BAG_ARRAY_NO_TYRA, which drops only the four bind() overloads, so the
# storage and every access path is the shipped one.
#
# Usage:  tools/bag-array-enforcement.sh <path/to/inc/bag_array.gen.hpp>
#         tools/bag-array-enforcement.sh            # generates its own fixture
#
# Exit 0 = the type enforces. Exit 1 = it does not, and the case is named.

set -u

HDR="${1:-}"
CXX="${CXX:-g++}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

if [ -z "$HDR" ]; then
  echo "usage: $0 <inc/bag_array.gen.hpp>" >&2
  echo "  (generate one with: tyrax-editor --new <name> <dir>)" >&2
  exit 2
fi
if [ ! -f "$HDR" ]; then
  echo "not found: $HDR" >&2
  exit 2
fi

# The generated header lives in the project's namespace; the harness does not
# care which, so pull it in wholesale.
NS="$(grep -m1 -oE '^namespace [A-Za-z_][A-Za-z0-9_]* \{' "$HDR" | awk '{print $2}')"
if [ -z "$NS" ]; then
  echo "could not read the namespace out of $HDR" >&2
  exit 2
fi

cp "$HDR" "$TMP/bag_array.gen.hpp"

preamble() {
  cat <<EOF
#define TYRAX_BAG_ARRAY_NO_TYRA 1
#include "bag_array.gen.hpp"
namespace $NS { unsigned int g_contentStamp = 0; }
using namespace $NS;
struct V { float x, y, z, w; };
EOF
}

fail=0
pass=0

try() {  # try <expect: ok|refused> <name> <body...>
  local expect="$1" name="$2"; shift 2
  { preamble; echo "int main() {"; printf '%s\n' "$@"; echo "return 0; }"; } \
      > "$TMP/t.cpp"
  local out
  out="$("$CXX" -std=gnu++17 -I "$TMP" -c "$TMP/t.cpp" -o "$TMP/t.o" 2>&1)"
  local rc=$?
  if [ "$expect" = ok ]; then
    if [ $rc -eq 0 ]; then
      pass=$((pass+1)); echo "  PASS  (compiles, as it must)  $name"
    else
      fail=$((fail+1)); echo "  FAIL  (REFUSED, but it is the sanctioned API)  $name"
      echo "$out" | sed 's/^/          /' | head -5
    fi
  else
    if [ $rc -ne 0 ]; then
      pass=$((pass+1)); echo "  PASS  (refused, as it must)   $name"
    else
      fail=$((fail+1)); echo "  FAIL  (COMPILED - the type does NOT enforce)  $name"
    fi
  fi
}

echo "BagArray enforcement check: $HDR"
echo "compiler: $($CXX --version | head -1)"
echo

# --- the positive control. Without this a header that fails to compile at all
# --- would "pass" every negative case and the run would be meaningless.
try ok "sanctioned: mutate through the API, read through const data()" \
  'BagArray<V> a;' \
  'a.push_back(V{1,2,3,4});' \
  'a[0].x = 9.0f;' \
  'a.resize(4);' \
  'const V* r = a.data();' \
  '(void)r; (void)a.size(); (void)a.stampPtr();'

try ok "sanctioned: the stamp MOVES on every mutation" \
  'BagArray<V> a; a.push_back(V{});' \
  'unsigned s0 = a.stamp(); a.push_back(V{});' \
  'unsigned s1 = a.stamp(); a[0].x = 1.0f;' \
  'unsigned s2 = a.stamp(); a.clear();' \
  'unsigned s3 = a.stamp();' \
  'if (s0 == s1 || s1 == s2 || s2 == s3) return 1;'

# --- the five escapes. Each of these compiled before the type existed.
try refused "raw write through data()" \
  'BagArray<V> a; a.resize(1);' \
  'a.data()[0].x = 1.0f;'

try refused "take a writable pointer from data()" \
  'BagArray<V> a; a.resize(1);' \
  'V* p = a.data(); p[0].x = 1.0f;'

try refused "write through a const reference obtained from a const array" \
  'const BagArray<V> a;' \
  'a[0].x = 1.0f;'

try refused "hand the storage to memcpy as a destination" \
  '#include <cstring>' \
  'BagArray<V> a; a.resize(2); V src{};' \
  'memcpy(a.data(), &src, sizeof(V));'

try refused "write through the stamp word itself" \
  'BagArray<V> a;' \
  'unsigned int* s = a.stampPtr(); *s = 0;'

echo
echo "passed $pass, failed $fail"
[ "$fail" -eq 0 ] || exit 1
echo "BagArray enforces: every escape route is a compile error."
