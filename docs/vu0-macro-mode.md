# VU0 macro mode: the engine's SIMD math

VU0 macro mode is how the PS2 engine does four-wide float math on the EE - the
console's equivalent of SSE: `Vec4` and `M4x4` are thin inline wrappers around
COP2 instructions (`lqc2`, `vadd`, `vmula`/`vmadd`, `vrsqrt`, `sqc2`), written as
GCC inline assembly. This page is for anyone writing such a block: why it has to
be hand-written, the operand rules that keep GCC from miscompiling it, the
register conventions, and what the inline wrappers measured against the
out-of-line ones they replaced.

## Why it is hand-written

The EE has three SIMD units and the compiler uses none of them. Measured with the
pinned PS2DEV GCC 15.2 at the engine's own flags (`-O3 -G0`):

| What GCC was given | What it emitted |
|---|---|
| a plain `a[i] += k * b[i]` float loop | scalar `mul.s`/`add.s` - no vectorization |
| `float __attribute__((vector_size(16)))` | scalar FPU code, spilled through the stack (slower than plain floats) |
| `int __attribute__((vector_size(16)))` | scalar `addu` plus shifts - never the MMI `paddw` |
| `unsigned __int128` copy | two `ld`/`sd` pairs, not one `lq`/`sq` |

So there are no intrinsics to call: a COP2 (VU0 macro mode) or MMI
(`paddw`, `pmaxw`, `pextlw`, `pcpyld`...) sequence exists only where someone
wrote it. VU0 *micro* mode - a program running on VU0 itself, started with
`vcallms` - is the other route ([raytraced reflections](raytraced-reflections.md),
[VU0 kernels](vu-authoring.md)); macro mode is for the small, inline case.

## Operand rules

GCC never allocates a `$vf` register and does not know their names, so it cannot
see what a block does unless the operands say so. Every rule below is a bug that
was in the engine:

- **Name the memory a block reads and writes as `"m"` operands** - `lqc2 $vf4, %1`
  with `"m"(v.xyzw)`, `sqc2 $vf4, %0` with `"=m"(res.xyzw)`, `"+m"` for in-place.
  A bare pointer in `"r"` tells GCC nothing about the memory behind it: compiled
  inline, `Vec4 a{x,2,3,4}; ... a + b` with the old `"r"` operands **never stored
  `a` or `b`** - the asm added two uninitialized stack slots. The old operators
  only worked because each one was an out-of-line call.
- **More than one quadword: base register plus a dummy `"m"`.** A matrix is four
  rows; write `lqc2 $vf2, 0x10(%2)` off an `"r"` base and pass the whole 64 bytes
  as an extra `"m"(*reinterpret_cast<const float(*)[16]>(p))` operand the asm
  never prints. `0x10+%1` does not work: for a global (`M4x4::Identity`) under
  `-G0` it expands to `0x10+%lo(sym)($2)`, which GAS rejects as a bad expression.
- **Let GCC pick GPR scratch.** A scalar goes into VU0 with `qmtc2 %1, $vf5` from
  an `"r"` operand and comes out with `qmfc2 %0, $vf4` into an `"=r"` float -
  GCC loads it with `lw` straight from memory and stores it with `sw`, no FPU
  round trip. A hardcoded `$2` or `$8` that is not in the clobber list is a
  register GCC thinks is still live.
- **Keep every block `volatile`.** GCC never reorders a volatile asm past another
  one, and code that keeps VU0 state *between* blocks (skinning holds its
  running AABB in `$vf20/$vf21` across the whole vertex loop) relies on that.
- **An out-of-line block that still uses bare pointers needs `"memory"`** - and a
  block that writes an input operand must make it `"+"` (`Math::atan2`).
- **No global labels in anything inline.** `atan2`'s `_atan_00` labels are why it
  stays a call; inline code uses numeric local labels (`1:` / `1b`).

## Register conventions

| Registers | Owner |
|---|---|
| `$vf0` | constant `(0,0,0,1)` - `$vf0w` is 1.0, `$vf0x` is 0.0 |
| `$vf1`-`$vf9`, ACC, Q | scratch of the inline `Vec4`/`M4x4` blocks (clobbered by any of them) |
| `$vf20`, `$vf21` | the skinning loop's running AABB, live across its blocks (`skel_instance.cpp`) |
| `$vf21`-`$vf23` | `Math::atan2` scratch - so no `atan2` inside the skinning loop |
| VU0 micro memory | the raytracer / project kernels; macro mode shares the register file with them |

## `Vec4` semantics worth knowing

- Copying is trivial (`= default`): two 64-bit moves GCC can keep in registers or
  drop, instead of a call into an `lqc2`/`sqc2` pair.
- `a + b` and `a - b` keep **`a.w`**; `normalize()` keeps **`w`**. Before, all
  three stored whatever `$vf6.w` last held - stale, and different from call site
  to call site. Every other lane of every operator is bit-identical to before.
- `a * b` is all four lanes; `a * s`, `a / s`, `cross` (w = 0), `dot3`, `length`,
  `distanceTo`, `lerp` are as they were.

## What inlining measured

A bench built against both engines (`vu0_bench.cpp`, 256 random vectors x 16
reps, COP0 cycles per call, min of 5 runs) in PCSX2 - emulator cycles, so the
**ratios** are the result, not the absolute numbers; console timing is owed:

| Operation | Out of line | Inline | Change |
|---|---:|---:|---:|
| copy | 19.3 | 15.0 | -22% |
| `a + b` | 30.0 | 22.0 | -27% |
| `a * s` | 35.4 | 23.0 | -35% |
| `a.dot3(b)` | 30.4 | 14.0 | -54% |
| `length()` | 27.0 | 15.0 | -44% |
| `distanceTo` | 38.1 | 19.0 | -50% |
| `lerp` | 45.0 | 27.0 | -40% |
| `M4x4 * Vec4` | 48.4 | 35.1 | -27% |
| particle step (`v*k`, `+=`, `p += v*dt`, `distanceTo`) | 93.1 | 69.5 | -25% |

Results hashed bit-for-bit: identical on every lane of every operator except the
`w` fixes above. The raytraced-mirror example, frozen camera, software renderer:
every pixel outside the traced glass is byte-identical between the two engines,
and the glass differs between arms no more than between two boots of one ELF
(the trace races the capture).

## What comes next

Inline wrappers remove the call; they cannot keep values in VU0 registers
*between* operators, because GCC cannot. The next step is fused kernels - one
asm block per hot loop, with its constants resident - starting with the
frustum AABB classification. See [Backlog](backlog.md), "VU0 macro-mode kernels".
