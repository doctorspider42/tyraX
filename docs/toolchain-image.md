# The toolchain image

Every generated game is compiled inside a Docker container, and that container's
image is the PS2 toolchain: `mips64r5900el-ps2-elf-g++`, the PS2SDK, `vcl`/`vclpp`
for the VU1 microprograms. This page is about **where that image comes from** —
which, as of this change, is this repository.

## What it was

Every project's `docker-compose.yml` named `h4570/tyra`, with no tag, so Docker
resolved `:latest`. That image is:

| | |
|---|---|
| published | 2022-07-07 (unchanged since) |
| digest | `sha256:1fa117cbd140b3e6506a41654fb1476c73c568ed1522a6470744eee50fab525f` |
| size / platform | 812 MB, `linux/amd64` only |
| base | Ubuntu 20.04 |
| compiler | GCC 11.3.0 |
| built from | a `Dockerfile` in the **upstream Tyra repo**, not here |

So the reference had two problems. Nothing in this repo could rebuild the image,
and `:latest` is a moving target — a rebuild upstream, or a fresh clone on a new
machine, could quietly get a different toolchain than the one every game here was
tuned against.

## What it is now

[`docker/Dockerfile`](../docker/Dockerfile) builds the image from this repo, and
[`.github/workflows/toolchain-image.yml`](../.github/workflows/toolchain-image.yml)
publishes it to `ghcr.io/<owner>/tyrax-toolchain`. Two deliberate changes over
the old image, and nothing else:

1. **The toolchain layer is pinned by digest**, not `:latest`. Same bits, but they
   cannot move under us. Bumping that digest is a toolchain change and gets
   verified like one (build a game, boot it — `tyra-testing` layer 3).
2. **Our patched ps2link ships inside the image**, at
   `/usr/local/share/tyrax/ps2link/`:

   | file | what it is |
   |---|---|
   | `ps2link.elf` | high (`0x01ee8000`), unpacked, USB HID — the dev default |
   | `ps2link-low-nousb-packed.elf` | low + packed + no USB — **the build to flash** |

   Both are built from the same pinned upstream commit and the same
   `tools/ps2link/tyrax.patch` as [`tools/ps2link/build.ps1`](../tools/ps2link/build.ps1)
   / `build.sh`, so a console can be flashed straight out of the image instead of
   building it per machine. See [ps2link-setup.md](ps2link-setup.md).

The compiler, the PS2SDK, `vcl` and `vclpp` are **untouched** — the image is a
strict superset of the old one. Why, in painful detail, below.

## Building and using it

```powershell
docker\build.ps1                    # -> tyrax-toolchain:local, then checks it
```

`docker/build.sh` is the Linux twin. Both take `-Tag`/`--tag`, `-Push`/`--push`
and `-NoCache`/`--no-cache`, and both run the same checks the CI workflow does
(every tool present, a real VCL program through `vcl` → `dvp-as`, both ps2link
ELFs non-empty). The build context is the repo root — `tools/ps2link/tyrax.patch`
has to be in it — and `docker/Dockerfile.dockerignore` keeps the rest of the repo
(`vendor/`, `build/`, `examples/`) out.

To point a project at an image, write one line into the **project** directory:

```
TYRAX_IMAGE=ghcr.io/doctorspider42/tyrax-toolchain:latest
```

in `.env`. The generated `docker-compose.yml` reads
`image: ${TYRAX_IMAGE:-h4570/tyra}`, and `docker compose` interpolates it from
`.env` (or from the environment, which wins over `.env`). This indirection exists
because `docker-compose.yml` is **regenerated on every build** — an edit to that
line would be overwritten, an override in `.env` survives. `.env` is in the
generated `.gitignore`: it is one machine's choice, not the project's.

`.gitignore` is written when a project is created and never refreshed (it has no
ownership marker, so rewriting it would clobber whatever the team added), which
means **projects made before this change do not ignore `.env`** — add the line by
hand there, including in `examples/`. `docker compose config` in the project
directory prints the image that will actually be used, and
`docker ps -a --filter name=<project>` says which one the existing container came
from; a container that already exists is recreated on the next build (the Runner
always runs `compose up -d`, which reconciles it with the compose file).

The default is still `h4570/tyra`, because this repo is private and so is the
published package — a fresh clone can pull the old image without credentials and
cannot pull ours. **When the repo goes public:** make the GHCR package public in
its package settings, then change the default in `TPL_COMPOSE`
([`src/templates.cpp`](../src/templates.cpp)) and the mentions in `README.md`.
That is the whole switch.

## Why the toolchain is inherited, not rebuilt

The obvious version of this change — base the image on the official
`ps2dev/ps2dev`, build the VU tools from source, get a small multi-arch image with
no 4-year-old layers — was tried and measured before being rejected. The blocker
is `vcl`.

**`vcl` is a prebuilt 32-bit x86 binary with no source in the loop.** 482 736 B,
`ELF 32-bit i386`, glibc, `sha256:83bee75205b5b0f37dbefc2c2769eba2c353befa532472c49aedb2ebaf168274`.
Upstream's Dockerfile `wget`s it from `h4570/tyra/raw/master/assets/vcl` — verified
byte-identical to `/usr/bin/vcl` in the image. That single binary is why the image
also carries `qemu-i386`, `binfmt-support` and `libstdc++5:i386`.

**The official ps2dev images are Alpine, so they cannot host it.** `ps2dev/ps2dev`
is musl-based (`v1.3.0` = Alpine 3.14 / GCC 11.1, `v2.0.0` = Alpine 3.23 /
GCC 15.2); a glibc i386 binary will not run there. Keeping `vcl` therefore means a
glibc base, which means building the whole PS2DEV toolchain from source on Ubuntu
the way upstream does (`ps2dev/build-all.sh`, ~1 h, and it clones its
sub-projects at their tips — so it is not reproducible either, just slow).

**The from-source replacement is not a drop-in.** `openvcl` — a free
reimplementation originally by Jesper Svennevid and Daniel Collin, now maintained
by Francisco Javier Trujillo Mata under AFL-2.0, and explicitly written from
public VCL documentation and examples rather than by reverse-engineering the
binary (v0.4.0 at `a5867c3`) — builds cleanly on Alpine in ~80 s, runs natively, and
would delete the qemu/i386 layers *and* unlock arm64. Then it was run against the
engine's VU1 microprograms — same `vclpp` output fed to both, `.vsm` compared:

| result, as it stood before any fixes | count |
|---|---|
| byte-identical to `vcl` | **0** of 25 |
| different schedule produced | 21 |
| failed to compile at all | **4** |

The four failures were `stapip_clip_d_vu1`, `stapip_clip_td_vu1`,
`stapip_cull_td_vu1` and `vu0_rt_kernel` (the VU0 raytracer). Worse, of the "21
that compiled" most were quietly wrong — see the `clipw` finding below. `.vsm` is
*scheduled* VU assembly, so a different scheduler is free to be different — and
also free to change cycle counts and VU1 package sizes, which this engine has been
tuned around program by program (see `tyra-engine-dev`).

**All four failures were subsequently fixed** and every program compiles under
openvcl today; what stops the switch now is code size, not correctness. The rest of
that story, with the measurements, is "The openvcl migration" below — this section
stands as the reason the *default* is still the inherited toolchain.

Also worth knowing if you try: `openvcl` accepts a bare `nop` as a whole program;
the legacy `vcl` rejects it outside RAW mode (`instruction 'nop' is unsupported`).
That is why the image checks use a real `add.xyzw`.

## The openvcl migration

Started because the licensing inventory above makes it a prerequisite, not an
optimisation: a publishable image cannot contain Sony's `vcl`. Two source-level
defects were found and fixed first — both were latent bugs in *our* sources that
only the stricter assembler noticed, and **both are free under the legacy one**
(verified by diffing its emitted instruction stream, not by reasoning).

**1. `clipw` was being silently dropped.** The engine wrote

```
clipw.xyz   t_vertex,   t_vertex
fcand       VI01,       0x3FFFF
```

`CLIPw.xyz` takes `vf_dest_xyz, vf_src_w` — the `w` was always implied, and Sony's
vcl infers it. openvcl requires it spelled out, and when it is missing it does not
fail the build: it **drops the `clipw` and keeps the `fcand`**, so the emitted
microprogram tests clip flags nobody set. Measured on `stapip_cull_c`: legacy
output 3 `clipw`, openvcl output 0, `fcand` 3 in both. Every vertex pipeline in
the renderer uses that macro, so "21 of 25 programs merely differ" was too kind a
reading of the first sweep — those 21 were wrong, not just different.

The fix is one token, `t_vertex[w]`, in 45 places (the two shared macros in
`vcl_sml.i` / `tyra_macros.i` plus the `clip`/`billboard` programs that spell it
inline). Under the legacy vcl the emitted instruction stream is **byte-identical**
before and after (`stapip_cull_c`: 175 instructions, zero diff), so this is a
spelling correction, not a behavior change.

**2. Zero-init by self-subtraction reads an uninitialized register.** The VU0
raytracer zeroed its accumulators with `sub.x bT, bT, bT` — deliberately (`x-x` is
a true zero even over saturated garbage), but it reads `bT` before anything writes
it, and openvcl refuses the whole program: *"Read-attempt from uninitialized float
register"*. Rewritten as `sub.x bT, vf00, vf00` (vf00 is hardwired `0,0,0,1`), 16
places in `vu0_rt_kernel.vclpp`. Again byte-identical under the legacy vcl (483
instructions, zero diff), and openvcl then compiles the kernel.

**3. openvcl needed exactly one fix of its own** — [`docker/openvcl-tyrax.patch`](../docker/openvcl-tyrax.patch),
applied in the image's `openvcl-build` stage (openvcl is AFL-2.0; same pattern as
`tools/ps2link/tyrax.patch`), and it is worth upstreaming: *accept CLIPw's implied
w component*. The ISA gives the second operand no other meaning, and the original
VCL infers it. Rejecting it would be defensible; what openvcl did instead was emit
the program **without** that instruction. Fixed by defaulting the field to `w` when
the source omits it, so the engine's sources need no change at all (and must not
have one — see the vclpp trap above). openvcl's own 419 tests stay green.

A second hunk was written and then thrown away, which is worth recording because it
is the kind of fix that looks right: relaxing the register allocator's conflict
test so live ranges that merely *touch* (one ending on the line the next begins)
could share a register. It bought exactly the one register `stapip_clip_d` was
short of — and it fails an upstream test that pins the strict rule deliberately.
The pressure belonged to the engine anyway (next paragraph), so the tool did not
need changing at all.

**4. Three programs wanted more registers than exist** — and the fix was in the
engine, not the tool. `stapip_clip_td` peaked at **35** simultaneous float values,
`stapip_cull_td` at **34** and `stapip_clip_d` at **32**, against 31 allocatable.
Real pressure, not a counting artefact: `begin:` in those programs is a *loop*
(`b begin` at the bottom, one iteration per batch), so everything loaded above it
stays live through the whole body.

Five of those values are GIF tags (`gifSetTag`, `lodGifTag`, `testsTag`,
`texBufferClutGifTag`, `alphaGifTag`) that are read **once per batch**, by the tag
store at the top of the loop — and were being loaded before the loop and held for
its entire duration. Moving those loads next to their reader frees five registers
across the whole body and costs nothing: the batch already stored the tag block
exactly once. It is the same trick the engine already used one level down, where
the light registers are reloaded per triangle "so they do not stay live across the
Sutherland-Hodgman block".

### Where it stands: compiles, does not fit

Over the engine's 25 microprograms, current sources:

| | legacy `vcl` | patched `openvcl` |
|---|---|---|
| compile | 25 | **25** |
| lose a `clipw` | 0 | **0** |
| cycles emitted | 3982 | **5913** (was 6728) |
| a game builds | yes | **yes** |
| a game runs | yes | **no** |

The openvcl-built game boots and dies on its own assertion:

```
| VU1 pipeline programs overflow into the draw-finish program
| File : src/renderer/core/paths/path1/path1.cpp:145
```

Which is the engine catching the real problem: **openvcl emits 71% more
instructions than Sony's vcl**, and VU1 micro memory holds 2048 of them, full
stop. Measured over the 15 programs both assemblers had produced at the time:

| | instructions |
|---|---|
| legacy `vcl` | 1925 |
| patched `openvcl` | 3295 (**+71%**) |

Per program the spread runs from +18% (`mcpip_as_is`) to +150% (`dynpip_c`:
99 → 248). So the blocker on `openvcl` is no longer correctness - it compiles
everything and drops nothing - but **scheduling density**.

### How much density is missing, and where

Measured over all 25 programs (`--dump-schedule-info` gives openvcl's own slot
list, so the numbers are its opinion of its own output, not a reconstruction):

| | cycles |
|---|---|
| openvcl | 6728 |
| **floor: a perfect packer** (`max(upper ops, lower ops)`) | **2989** |
| Sony's vcl | 3982 |
| of openvcl's total: pure `nop`/`nop` stall cycles | **1860** |

Two things follow, and the first one is the reason this is worth finishing:

1. **The floor is 25% BELOW what Sony's vcl achieves.** openvcl is emitting the
   same work - 4836 occupied pipe slots against the legacy 4822, a 0.3%
   difference - so nothing is missing from its output except packing. A good
   scheduler here would not just match VCL, it could beat it.
2. **openvcl sits 98% above that floor; Sony's vcl sits 33% above it.** The gap is
   entirely stalls and unpaired cycles: 6% of openvcl's rows use both pipes,
   against 25% for legacy.

The scheduler is not naive, which is worth knowing before touching it: it builds a
real dependency graph (with ACC/MAC/CLIP modelled as precise resources), keeps a
ready list with critical-path-ish priorities, scores candidates by latency delay,
and tries to pair a partner into the other pipe. What limits it is **window**: it
runs per *segment*, and segments are cut at every non-schedulable token, at every
change of the ignorable-flag-WAW mask, and at the last MAC/CLIP reader - which in a
vertex pipeline is several cuts per vertex. It can only hide a latency with work
that is inside its current segment, so the cuts are where the 1860 stall cycles
come from. `stapip_cull_c`'s vertex loop is a fair sample: 187 cycles for 118
upper-pipe and 41 lower-pipe ops, of which 50 cycles are pure stalls.

### What was done about it: -12%, and the segment is where it lives

Two changes, both in `docker/openvcl-tyrax.patch`, both with openvcl's 419 tests
still green:

- **The flag-WAW mask now comes from the segment's last token, not from each
  token.** That mask answers exactly one question - is this flag read *after* the
  range the dependency graph covers - because inside the range
  `addPreciseImplicitFlagDependencies` finds every reader itself and orders the
  writers against it. Deriving it per token forced a segment break at every change
  and again at the last MAC/CLIP reader. On its own this changes no output; it is
  what makes the next change honest.
- **`--schedule-flag-readers`** lets instructions that implicitly read MAC/CLIP
  (i.e. every `fcand`) take part in list scheduling instead of ending the segment.
  In a vertex pipeline that is one break per vertex, at the `fcand` after each
  `clipw`. Result: **6728 → 5913 cycles (-12%)**, 25/25 still compile, no `clipw`
  lost.

It is a flag, off by default, because it does change one thing: with a cheaper
unpipelined estimate, the `--enable-known-loop-optimizations` path stops
software-pipelining the loops its own tests pin down. That path is opt-in and
**never fires on this engine's programs** (measured: 0 of 25 emit a `MAIN_LOOP`
label, with or without the flag), so the image's `vcl` wrapper passes
`--schedule-flag-readers` and upstream's defaults stay exactly as they were.

Also measured and rejected: **one segment per basic block** with a conservative
mask - a further -3%, but 12 test failures, most of them software-pipelining ones.
Those cuts are load-bearing for the pipeliner; the mask fix above is the same idea
done properly.

### Where the remaining 48% sits

`openvcl 5913` against `legacy 3982` is +1931 cycles, and **1251 of those are
still stall padding**. It is concentrated, not spread:

| program | openvcl | legacy | gap | padding |
|---|---|---|---|---|
| `vu0_rt_kernel` | 961 | 483 | **+478** | 418 |
| `stapip_clip_d` | 336 | 209 | +127 | 85 |
| `stapip_clip_tce` | 378 | 254 | +124 | 84 |
| `stapip_clip_td` | 345 | 222 | +123 | 78 |
| `stapip_clip_c` | 370 | 257 | +113 | 77 |

**The actual pass/fail number is smaller and sharper than the 48%.** What has to
fit is the *resident* VU1 set that `StaPipQBufferRenderer` uploads: five `cull_*`
plus five `clip_*` programs, against a ceiling of **2042** instructions
(`VU1_MICRO_MEM_SIZE` 2048 minus the draw-finish helper parked at the top).

| the resident 10-program set | instructions |
|---|---|
| legacy `vcl` | **2035** — seven words of headroom |
| patched `openvcl` | **3072** |

So the target is not "be as good as Sony", it is **−34% on ten specific
programs**. The engine's own comment next to that upload is worth reading first:
the set only fits at all because the five clip programs share one rotating fan
emitter (inlining three emit copies each measured 2162 against the same ceiling).

**Separately, one program is a quarter of the total cycle gap**, and it is ours: the VU0 raytracer.
Its 24 basic blocks include three at 36-53% stall cycles with almost no pairing
(block 20: 102 cycles, 54 of them padding, 2 paired rows) - long serial FMAC
chains from the branchless nearest-hit mixing, where a 4-cycle latency has nothing
inside the same block to hide behind. Note it also has a hard ceiling of its own:
VU0 micro memory is 4 KB = 512 instructions, so at 961 that kernel cannot load at
all, while legacy's 483 just fits.

So the remaining work is **latency hiding for serial chains**, which means either
scheduling across basic-block boundaries in openvcl, or exposing more ILP in the
kernel source (interleaving the independent sphere/slab/triangle math by hand -
that would help both assemblers).

Dead ends, so nobody repeats them:

- **`--LoopCS` is not the answer.** openvcl's software pipeliner only recognises
  loops carrying that directive, which this engine never emits - but adding it to
  a vertex loop changes *nothing* for either tool (`stapip_cull_c`: openvcl 200 →
  201 cycles, legacy 175 → 175, i.e. Sony's vcl ignores it here too). Sony's
  advantage is not pipelining, it is plain better scheduling inside the body.
- `-C` (REDUCE_CODE) and `-f` (ALIGN_CODE) are inert in this version - measured,
  byte-identical output. `--bthres` is a register-allocation knob, not a
  scheduling one. There is no flag to flip.

Reproducing the cycle numbers takes one command per program, and openvcl reports
them itself (no parsing of the emitted `.vsm` required):

```bash
# in the toolchain image, with an openvcl binary and a vclpp'd program
openvcl --dump-schedule-info prog.vcl | head -1     # program_cycle_count=...
openvcl --show-reg-alloc      prog.vcl              # per-alias live ranges
```

The per-slot lines of `--dump-schedule-info` carry `upper=`/`lower=`/`padding=`,
which is where the pipe-occupancy and stall figures above come from.

The loop that produced the totals, for whoever iterates on the scheduler next —
build openvcl from a clone, then run everything inside the toolchain image, which
owns `vclpp` and the legacy `vcl` (the binary is glibc-compatible with it, both
being Ubuntu 20.04):

```bash
# 1. build the candidate, drop the binary somewhere shared
docker run --rm -v "$CLONE:/p" -v "$OUT:/out" ubuntu:20.04 sh -c '
  apt-get update >/dev/null && apt-get install -y --no-install-recommends g++ cmake make >/dev/null
  cp -r /p /b && cmake -S /b -B /b/build -DCMAKE_BUILD_TYPE=Release -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF >/dev/null
  cmake --build /b/build -j"$(nproc)" >/dev/null && install -m755 /b/openvcl /out/openvcl'

# 2. sweep the engine's 25 programs and total the cycles
docker run --rm -v "<repo>/vendor/tyra:/e:ro" -v "$OUT:/out" tyrax-toolchain:local sh -c '
  cd /e/engine; tot=0
  for f in $(find src -name "*.vclpp"); do
    vclpp "$f" /tmp/p.vcl >/dev/null 2>&1 || continue
    c=$(/out/openvcl --dump-schedule-info /tmp/p.vcl 2>/dev/null | grep -oE "program_cycle_count=[0-9]+" | cut -d= -f2)
    tot=$((tot + ${c:-0}))
  done; echo "cycles: $tot"'
```

And always run openvcl's own suite next to it (`cmake -B build && ctest`,
419 tests, ~8 s) — both experiments above were rejected on its verdict, not on the
cycle count.

`VCL_IMPL=openvcl` therefore stays what it is: a switch for continuing that work,
not a supported configuration. The default is `legacy`, and the image's own check
asserts it.

### A bug this work uncovered in the shipped engine

Chasing the above turned up something worse than a tooling wart, and it had
nothing to do with openvcl. **`vclpp` expands a macro to nothing if anything -
including a comment - sits between the `#macro` line and its first instruction.**
No error, no warning, exit 0, and every caller builds green with the instructions
simply gone.

Commit `93a7657` (2026-07-14, "Rebrand fork to TyraX") added a note inside
`PerformClipCheck` in `vcl_sml.i`. From then until 2026-08-04, that macro expanded
to nothing, so **`mcpip_cull` - the blocks pipeline's cull program, its only
caller - shipped with no frustum clip check at all** (`clipw` 0, `fcand` 0 in the
generated `.vcl`). Off-screen blocks were never ADC-masked, so the GS was kicked
for geometry that should have been culled. Fixed by moving every such note above
the `#macro` line, and the trap is now recorded in `tyra-engine-dev`'s pitfalls
with a one-command check.

Same failure mode bit this migration twice more, which is why the check is worth
running whenever a `.i` macro is touched: first when the `clipw` operand was
"corrected" to a bracketed field inside a macro body (brackets are vclpp's
register-array index), then again in the comment that was documenting it.

### Trying it

The image carries **both** assemblers. `openvcl` is always at
`/usr/local/bin/openvcl` and the legacy one at `/usr/bin/vcl`; the `VCL_IMPL`
build argument decides which one wears the name `vcl` that `Makefile.base`
invokes, and the image records its choice in `/usr/local/share/tyrax/vcl-impl`.

```powershell
docker\build.ps1 -Tag tyrax-toolchain:openvcl -VclImpl openvcl
```

**Switching implementations requires a rebuild.** The Runner rebuilds `libtyra`
when the engine *sources* change; changing the image changes neither the sources
nor their checksums, so the previously compiled microcode would be reused and the
switch would appear to do nothing. Use *Build > Rebuild* (`--build <dir>
--rebuild`), which drops `/tyra/engine/obj` and `/tyra/engine/bin`.

### Reproducing the comparison

```bash
# 1. openvcl + vclpp from source, on the current official toolchain
docker build -t vclprobe - <<'EOF'
FROM ps2dev/ps2dev:v2.0.0
RUN apk add --no-cache build-base cmake git make
RUN git clone https://github.com/glampert/vclpp.git /t/vclpp && make -C /t/vclpp && cp /t/vclpp/vclpp /usr/bin/
RUN git clone https://github.com/ps2dev/openvcl.git /t/openvcl && cd /t/openvcl \
    && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF \
    && cmake --build build -j"$(nproc)"
EOF
# 2. run both over vendor/tyra/engine's *.vclpp (cwd must be the engine dir -
#    its #includes are relative to it) and cmp the .vsm files. The legacy pass
#    takes ~10 min for the 25 programs; openvcl takes seconds.
```

## Licensing of the published image

This is not a licensing opinion, it is the inventory — but it decides one thing:
**publishing this image is redistribution, and the Dockerfile living in this repo
is not.** [THIRD-PARTY-LICENSES.md](../THIRD-PARTY-LICENSES.md) tracks what the
*repo* ships and its "Redistributed in this repo?" column is unaffected by
`docker/` — build instructions carry no obligations. Pushing the image does. What
it contains:

| in the image | license |
|---|---|
| GCC, binutils (`mips64r5900el-ps2-elf-*`) | GPL-3.0-or-later — a public image carries the written-offer/source duty |
| PS2SDK | Academic Free License v2.0 |
| ps2link (our patched build) | upstream declares no SPDX license (`Other`) |
| vclpp | MIT |
| **`vcl`** | **none — and it is Sony's own tool, see below** |
| Ubuntu 20.04 base | many, per-package |

`vcl` is the one that matters, and it is worse than "license file missing". The
binary identifies itself:

```
$ grep -aoE 'Sony[^"]{0,60}|vcl 1\.4[a-z0-9]*' vcl
Sony Computer Entertainment America (c) 2001 for %s
vcl 1.4beta7
```

It is **VCL 1.4beta7, Sony Computer Entertainment America, © 2001** — a tool from
the official PS2 SDK ("VU Command Line" is an SCE trademark) that has circulated
in the homebrew scene ever since. There is no license text because none was ever
granted: it is proprietary, and every game in this repo is built with it. That is
a fact about the toolchain, not a decision anyone here made — but publishing an
image that contains it would be redistributing Sony's SDK tool, which is why:

- the GHCR package **stays private** while the repo is private, and going public
  is gated on resolving `vcl` — that is, on the `openvcl` migration above. Which
  makes that migration the thing that makes a public image clean, not a
  nice-to-have;
- the image label says `NOASSERTION`, not `Apache-2.0`.

**Why this lives here and not in its own repo.** Splitting the toolchain into a
separate repository was considered for exactly this reason and does not help:
the same bits get redistributed under the same terms whichever repo's CI pushes
them, and the editor's own Apache-2.0 story was never at risk from a Dockerfile
sitting next to it. What a split *would* cost is real: `tools/ps2link/tyrax.patch`
is iterated with the F6 deploy work and the image builds ps2link from it, so a
split turns one commit into two plus a pin to keep in step — against this repo's
"things that must move together are edited together" rule. Revisit it if the
image ever becomes an artifact for people outside TyraX (its own README, issues
and release notes); if that happens, ps2link should stay behind as a release
asset of *this* repo rather than follow the compiler image, since it is a
host-side artifact and not a compile dependency.

## The arm64 question

The old image is `linux/amd64` only, and so is ours — it inherits that layer. This
is the one thing an Apple Silicon or arm64-CI user pays for the decision above:
the toolchain runs under emulation there. Both pieces that block a native arm64
image are the same piece — the i386 `vcl` and the amd64-only base — so arm64
arrives with the `openvcl` migration, not before. The workflow already takes a
`platforms` input for the day that lands.

## Pinning, and why `:latest` was the actual bug

`tools/ps2link/build.*` used `ps2dev/ps2dev:latest` while calling itself
reproducible. The same commit, the same patch, the same flags:

| when / with what | `ps2link.elf` |
|---|---|
| `:latest`, when the local artifact was built | 285 364 B |
| `:latest`, a few weeks later | 285 620 B |
| `ps2dev/ps2dev:v2.0.0` (pinned, now) | 284 340 B, every time |

Nothing in this repo changed between the first two rows. Both build scripts and
`docker/Dockerfile` now pin the same tag, so the image's ps2link and a hand-built
one are the same ELF.

## Still open

- **The GHCR package is private** until the repo is, so nobody outside can pull
  the image yet; the workflow pushes it regardless (`GITHUB_TOKEN` can).
- **The toolchain jump is unexplored.** GCC 11.3 → 15.2 on a glibc base is the
  interesting experiment (it needs the from-source ps2dev build above), and it is
  independent of the `openvcl` question.
- **`vendor/tyra`'s audsrv overlay is still applied at container start** by the
  Runner rather than baked into the image, because `vendor/` is fetched by
  `deps.*` and is not in the image's build context. Baking it in would remove one
  first-build step; the stamping logic in `src/runner.cpp` explains why it is
  cheap as it stands.
