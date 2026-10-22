# Actual Pool2 EE producer host oracle

This private host-only fixture compares frozen `wild-pool-table-physical-v3`
against `wild-pool2-ee-physical-v1`, including the candidate's local lazy-color
generation rollover refinement. The earlier global-counter producer result is
preserved unchanged in sibling `wild-pool2-ee-producer-host-v1`.

Run `python verify.py`. It extracts both actual `poolBatchAdd`, `poolBatchFlush`
and `PoolBatch` declarations without changing their bodies. Namespace wrapping
allows both implementations to share the same input pools. The actual generated
`BagArray` header is used unchanged (and must match between source trees). The
actual candidate `stapip_pool_color_table.hpp`, including `materializeRange`, is
included unchanged. Minimal type/layout shims provide 16-byte Vec4/Color, bag
pointers and renderer submission counting; the renderer performs no implicit
fallback fill. No producer implementation is mirrored by the oracle.

Both `-O0` and `-O2`, C++17, `-Wall -Wextra -Werror -static` builds pass with no
warnings: **645,441 assertions, 1,474 paired flushes, 256 compact color-only
updates and 702 actual-header partial fills per optimization**.

The test compares exact position/ST bytes and candidate coefficient expansion
against the actual legacy producer's per-vertex RGBA. It exercises 1..16 eligible
members plus the 17-member boundary, independent color/FIX changes, geometry and
ST stamp changes, reversed membership order, 0/1/0/1 arm transitions, unchanged
warm calls, ignored source alpha changes, 93-vertex shape fallback and local
UINT32_MAX generation rollover. Partial fills cross the 96-vertex member
boundary; untouched bytes and ready words outside the requested range are
checked, as are repeated fills and rejected overflowing ranges.

Compact color-only updates preserve geometry bytes/stamps, bbox stamp, legacy
per-vertex color BagArray stamp, and lazy backing bytes/ready words before any
fallback materialization. Source movement changes bbox stamps. Warm flushes
preserve all observed bytes/stamps. Binding pointers and color generation
metadata are checked in both arms. Exact byte checks and actual wrapper mutation
stamps establish these host semantics; they are not an instrumented count of
every CPU store instruction.

`proof.json` records actual complete input-file SHA-256 values, independently
hashed extracted bodies/declarations, harness SHA-256, compiler version,
commands, exit codes, identical assertion totals and executable SHA-256 values.
All source input hashes are revalidated after the builds/runs. Compile and run
logs, extracted text, generated oracle and local binaries remain here.

No candidate/production files were edited. No native game build, emulator,
console, cache or shared build resource was used. This does not establish target
ABI, DMA/cache safety, actual pixels, hardware timing, FPS or speed improvement.
