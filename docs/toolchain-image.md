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

> **Two images, one published.** This section describes
> [`docker/Dockerfile`](../docker/Dockerfile), which inherits from `h4570/tyra`
> and was the first answer. It is now the **A/B reference** rather than the
> shipped image: it is the only one that can run Sony's `vcl`, which every
> comparison in this file is measured against, and it is built locally
> (`docker/build.*`) rather than published. What CI publishes is
> [`docker/Dockerfile.fromsource`](../docker/Dockerfile.fromsource) - see
> *Building it from source instead*, further down. Read this section anyway: the
> reasoning here is what the other image had to answer.

[`docker/Dockerfile`](../docker/Dockerfile) builds the image from this repo. Two
deliberate changes over the old image, and nothing else:

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
docker\build.ps1 -FromSource        # the published one, from the ps2dev base
```

`docker/build.sh` is the Linux twin (`--from-source`). Both take `-Tag`/`--tag`,
`-Push`/`--push` and `-NoCache`/`--no-cache`, and both run the same checks the CI
workflow does (every tool present, a real VCL program through `vcl` → `dvp-as`,
both ps2link ELFs non-empty). The build context is the repo root —
`tools/ps2link/tyrax.patch` has to be in it — and the matching
`<dockerfile>.dockerignore` keeps the rest of the repo out. They differ in what
they let through: the inherited image needs nothing from `vendor/`, the
from-source one needs the audsrv fork's EE sources, because it compiles them.

**Only the from-source image is published.** CI builds, checks and pushes
`ghcr.io/<owner>/tyrax-toolchain-src`; the inherited one is not in the workflow
at all. A reference is needed rarely and by whoever is doing the comparison, and
a second job on every push to `docker/` plus a second GHCR package is a standing
cost for that. The consequence is worth stating plainly rather than discovering:
**a change to `docker/Dockerfile` gets no CI coverage** — build it locally before
committing one.

## Choosing which image a build uses

Two doors, and they are for different jobs.

**In the editor: *Edit > Preferences > Build toolchain*.** A combo with the images
worth naming - the project default, the original `h4570/tyra`, this repo's
published one (`tyrax-toolchain-src`), a locally built one (`tyrax-toolchain:local`,
the tag `docker/build.*` writes, which is how the unpublished A/B reference is
selected) - plus a free-text field for anything else.
It is a **machine-global** setting (`editor.ini`, next to the PCSX2 path and the
ps2link IP), not project data: which images a PC has pulled is a property of that
PC, and a value stored in a shared `.tyra` would name an image a teammate does not
have. **Leaving it empty is a real choice** - the editor then exports nothing and
the project's own `.env` decides, exactly as it did before the setting existed.
That is also the *fall back to the original Tyra image* switch: pick it, build, and
the game is compiled by Sony's `vcl` again.

The Runner applies it as an environment variable on its `docker compose` commands
rather than by writing `.env` - that file is the user's own override sheet and
rewriting it from under them is not the editor's business. An exported variable
outranks `.env`, so an explicit choice in the editor wins over one made there.
One asymmetry worth knowing: **headless builds (`tyrax-editor --build`) do not read
`editor.ini`** and therefore follow `.env`, the same way they auto-detect the
emulator rather than using the configured path.

**In a project: `.env`.** Write one line into the **project** directory:

```
TYRAX_IMAGE=ghcr.io/doctorspider42/tyrax-toolchain-src:latest
```

The generated `docker-compose.yml` reads
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
cannot pull ours. That is the only thing holding the default back; the technical
side is done, including the one dependency that used to make the two images
genuinely different (audsrv — see below). **When the repo goes public:** make the
GHCR package public in its package settings, then change the default in
`TPL_COMPOSE` ([`src/templates.cpp`](../src/templates.cpp)) and the mentions in
`README.md`. That is the whole switch.

Until then the pointer is the editor preset, which needs a `docker login ghcr.io`
once per machine. The Runner's audsrv overlay is what keeps `h4570/tyra` a
working choice meanwhile: it applies the committed `vendor/tyra/audsrv/bin/`
artifacts to an image that does not carry the fork, and skips when the image
already does.

## Why the toolchain was inherited, and what changed

> **Resolved.** Everything in this section was true until openvcl became good
> enough, and it is kept because the reasoning is what makes the resolution
> readable. [`docker/Dockerfile.fromsource`](../docker/Dockerfile.fromsource) is
> the image this section says is impossible; "Building it from source instead"
> below is what it cost. The inherited image stays the default, for one reason
> that has nothing to do with the argument here: it is the only one that can run
> Sony's `vcl`, and every A/B in this document is measured against it.

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

### Where it started: compiles, does not fit

Over the engine's 25 microprograms, at the point the density work began (where it
ended up, including a game that runs and renders identically, is two sections
down):

| | legacy `vcl` | patched `openvcl` |
|---|---|---|
| compile | 25 | **25** |
| lose a `clipw` | 0 | **0** |
| words emitted | 3982 | **4208** (was 6969) |
| resident VU1 set, ceiling 2042 | 2035 | **2180** (was 3072) |
| a game builds | yes | **yes** |
| a game runs | yes | **no — 138 words over** |

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

### Ask the tool you are replacing

The single most productive hour of this work was reading `vcl -h`. SCE's binary
lists its own passes, and they are all **on by default**:

```
-t<nseconds>  set timeout for optimiser to <nseconds>, default is 4
-L            globally disable loop code generation
-S            enabled staggered memory accesses in loop mode
-C            disable code size reduction pass
-P            disable unused instruction/operand pruning pass
-d            enable only dumb code generation
```

Which turns the "what does Sony do that we don't" question into experiments you
can just run, on the engine's own programs:

| `stapip_clip_c` under SCE vcl | instructions |
|---|---|
| default | 257 |
| `-L` (no loop code generation) | 257 — **no effect** |
| `-P` (no pruning) | 257 — no effect |
| `-d` (**dumb** code generation) | 262 — nearly as good as default |
| `-C` (no code size reduction) | **327** |

So its advantage is neither software pipelining nor a clever scheduler: it is the
**code size reduction pass**, worth 70 instructions (21%) on that one program. And
what that pass removes is not instructions - it is *stall rows*: `nop`/`nop` count
goes 88 → 18 while the paired-row count stays at 43 → 44.

### Why those stalls can go: the FMAC interlocks

The VU stalls by itself when an instruction's source is still in the FMAC pipeline,
so padding a VF-to-VF wait with `nop`s buys nothing and costs micro memory. That is
not a reading of a manual, it is what SCE's shipped output does: `stapip_clip_c`
contains **41 VF dependencies with a gap of only 2-3 cycles**, e.g.

```
mul.x    VF21,VF15,VF21     ...3 rows later...   mulx.xyz VF23,VF12,VF21
lq       VF23,0(VI02)       ...3 rows later...   mulax    ACC,VF02,VF23x
```

If the hardware did not interlock, the tool that built PS2 games would be emitting
garbage. What it *does* still pad for is everything the hardware does **not**
interlock, and the 18 rows it keeps sit exactly there: in front of branches
(`ibne`/`ibeq`/`ibltz`), flag reads (`fcand`), integer ops reading a just-written
VI, and Q consumers.

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
- **`--fmac-interlock`** stops paying for interlocked VF waits in instruction
  words. `VuLatencyTracker` gained `manualReadHazardDelay()` - the part of a wait
  the hardware will *not* resolve on its own (integer pipeline, MAC/CLIP flags,
  Q/P) - and a padding slot now carries `emitCycleCount` beside `cycleCount`, so
  the cycle model stays honest while the emitter writes only the words that are
  really needed. The same distinction then loosened the **pairing** gate, which had
  been refusing to share an instruction word over a latency the hardware stalls
  for anyway (same-word read/write hazards are checked separately and still are).

Together, on emitted words - which is what micro memory actually holds:

| | total words | resident 10-program set |
|---|---|---|
| openvcl before | 6969 | 3489 |
| + `--fmac-interlock` | 4659 | 2385 |
| + looser pairing | **4208** | **2177** |
| SCE `vcl` | 3982 | 2035 (ceiling 2042) |

`stapip_clip_c` is the clean way to see what is left: both tools emit **exactly 283
operations**, and openvcl needs 286 rows for them against SCE's 257. The whole
difference is 43 stall rows against 18, plus 4 pairings (40 against 44).

Measured on the built objects rather than on emitted text - which is the check the
engine's own comment asks for, and it agrees with the table above to within three
words:

```bash
# per program: section bytes / 8 = instructions
docker run --rm -v "tyra-engine-<hash>:/tyra" tyrax-toolchain:local sh -c '
  for n in cull/stapip_cull_c ... clip/stapip_clip_tce; do
    o=$(find /tyra/engine/obj -path "*${n}_vu1.o")
    mips64r5900el-ps2-elf-size -A "$o" | awk "/^\.vutext|^\.text/{s+=\$2} END{print s/8}"
  done'
```

| resident set, from the objects | instructions |
|---|---|
| openvcl | **2180** |
| ceiling | 2042 |
| **over by** | **138** |

Per program the overshoot is almost entirely the five clip variants (+29, +21, +20,
+28, +30); the cull five are within +13 of SCE and two of them are already
*smaller*.

### Calibrating the rest against SCE, not against a manual

The same trick works for the latencies that are *not* interlocked, and it needs no
documentation at all: run the analysis over SCE's own output and take, per
producer/consumer class, the **smallest distance it ever emits**. A tool that
shipped games does not emit an illegal gap, so that minimum is the requirement.
Then run the identical analysis over openvcl's output and compare what it believes:

| dependency | SCE emits (min) | openvcl assumed |
|---|---|---|
| integer ALU → integer op | 1 | 1 |
| `xtop` → integer op | 1 | 1 |
| `mtir` → integer op | 1 | 1 |
| integer ALU → branch condition | 2 | 2 |
| **`clipw` → `fcand` (CLIP flags)** | **1** | **4** |
| **`ilw` (memory) → integer op** | **3** | **5** |

The two mismatches were worth 22 and 61 words on the resident set. The flag one is
verified down to the rows - SCE emits five `clipw` → `fcand` pairs exactly one row
apart, and the `0x3FFFF`/`0x3F` masks say the `fcand` is testing the `clipw` right
above it:

```
clipw.xyz VF15xyz,VF15 | ior   VI09,VI09,VI08
addw.x    VF05,VF05,.. | fcand VI01,63
```

`--sce-latencies` sets both (flag visibility 1, load result at issue+3).

### Delay slots: a word each, and most of them were empty

A branch's delay slot is an instruction word whether or not anything useful sits in
it. `fillBranchDelaySlots()` runs *before* scheduling and gives up after **one**
try - the instruction immediately preceding the branch in source order - so on
`stapip_clip_c` 13 of 20 slots stayed empty, against 9 of 20 for SCE, whose filled
slots hold `sq`/`isw` as often as integer ops (34 against 37 of its 94 filled
slots across the corpus).

`--emit-delay-fillers` does two things about that, both reusing openvcl's own
legality test `canMoveIntoBranchDelaySlot` (no label, no second branch, and the
branch must not read what the candidate writes - it used to see the new value):

- the pre-scheduling pass keeps **walking back** when the immediate predecessor
  will not fit, accepting a candidate further up as long as it can cross
  everything in between (`vuTokenCanMoveBefore`, the same rule the scheduler uses),
  and plain stores are now candidates too;
- the emitter offers, as a last resort, the instruction the *schedule* placed just
  before the branch - taking its already-emitted row back and re-emitting the pair
  as branch + filler.

### Where it stands now: it fits

| resident set (`nm`-measured, see the budget note) | instructions |
|---|---|
| openvcl, `--fmac-interlock` only | 2180 |
| + flag latency 1 | 2116 |
| + load ready at +3 | 2094 |
| + delay fillers | 2075 |
| + branch interlock | 2070 |
| + **branch bubble on dependency** | **2040** |
| SCE `vcl` | 2042 |
| ceiling | 2042 |
| **spare** | **2** |

**The budget arithmetic, because everyone re-derives it wrong once.** The ceiling is
not a constant: `Path1::createProgramsCache` asserts against
`drawFinishAddr = VU1_MICRO_MEM_SIZE - <draw-finish size>`, which is
2048 - 6 = **2042** today. And each program contributes
`VU1Program::calculateProgramSize()`, which is `(CodeEnd - CodeStart) / 8` **rounded
up to an even instruction count** - MPG uploads in 64-bit pairs. Ten programs means
up to ten words of rounding, so summing raw rows out of the `.vsm` files understates
the total. Ask the objects instead:

```bash
# in the project directory, after a build
docker compose exec -T compiler sh -c 'cd /tyra/engine/obj && for f in $(find . -name "stapip_cull_*_vu1.o" -o -name "stapip_clip_*_vu1.o" -o -name "draw_finish.o" | sort); do
  s=$(mips64r5900el-ps2-elf-nm "$f" | grep -i CodeStart | awk "{print \$1}")
  e=$(mips64r5900el-ps2-elf-nm "$f" | grep -i CodeEnd | awk "{print \$1}")
  n=$(( (0x$e - 0x$s) / 8 )); r=$n; [ $((n % 2)) -eq 1 ] && r=$((n+1))
  printf "%-44s %4d -> %4d\n" "$(basename $f)" "$n" "$r"; done'
```

Measured that way **SCE's set lands on 2042 exactly** - not one word of slack - and
openvcl's on 2040.

Per program openvcl is now within a few words of SCE everywhere - **and smaller on
two of the ten**:

| | openvcl | SCE | |
|---|---|---|---|
| `cull_td` | 149 | 155 | **-6** |
| `cull_d` | 142 | 145 | **-3** |
| `cull_c` | 177 | 175 | +2 |
| `cull_tc` | 184 | 181 | +3 |
| `clip_d` | 213 | 209 | +4 |
| `clip_td` | 226 | 222 | +4 |
| `clip_tc` | 278 | 270 | +8 |
| `clip_tce` | 262 | 254 | +8 |
| `cull_tce` | 177 | 167 | +10 |
| `clip_c` | 267 | 257 | +10 |

The remaining words are stall rows that SCE does not need because of *where it puts
things*. Over the ten resident programs:

| nop/nop rows | SCE | openvcl |
|---|---|---|
| in front of a branch | 46 in 36 sites | 90 in 80 sites |
| unfilled delay slots | 39 | 35 |
| the `[E]` / `b begin` epilogue | 30 | 30 |
| everything else | 19 | 30 |
| **total** | **104** | **130** |

**Almost all of it was one shape: a stall in front of a branch, and openvcl had
twice as many of those sites.** That is now fixed - see "The last 28 words" below.
Both assemblers pay one word when an ordinary integer op feeds a branch (SCE never
comes closer than two instructions either, 24 of its 45 cases sit at exactly two);
what openvcl was doing was paying it *unconditionally*.

**Three levers were built and measured as dead ends first, recorded because they
each look like the obvious fix:**

* **Rank the ready list by the critical path in CYCLES** rather than in
  instructions, counting each dependence edge at its real issue distance, and hand
  the segment scheduler the token that follows it so the producers of a branch's
  operands can rise. Correct in principle, **zero words on this engine**: the
  producers it wants to hoist are pinned where they are by an anti-dependence, not
  by their priority.
* **Prefer a register nobody read recently** when several are free. That
  anti-dependence comes from first-fit allocation reusing the lowest non-conflicting
  register - `ilw.x VI01,8(VI00)` cannot move above the four rows that read VI01 as
  the buffer pointer, and SCE gives that flag a register of its own. Also zero:
  instrumented, and the allocator's free-register search **never runs** on these
  programs. Every alias is already decided by the preallocation and two-address
  chain passes above it, so the choice this would improve does not exist yet.
* **Swap the last two emitted rows** instead of padding in front of a branch, with
  the crossing checked both ways and the latency model replayed over the new order.
  Zero as well, and the trace says why: at the segment boundary the scheduler no
  longer reports a hazard at all (17 calls, 0 with a delay). The rows come from the
  emitter's own padding path, one level below where this pass sits.

All three failed for the same reason, which is the answer: **the rows were never the
scheduler's.** `CodeGenerator::padForBranchPreBubble` inserts one nop in front of a
branch, and its test is `branchNeedsPreBubble` - which looks only at the branch's own
flags:

```cpp
if( access.instructionFlags & VU_INSTR_REGISTER_BRANCH )  return true;
return !(access.instructionFlags & VU_INSTR_UNCONDITIONAL_BRANCH);
```

Every conditional branch, always, whether or not anything above it produces an
operand. That is the entire 80-against-36 difference.

### The last 28 words: `--branch-bubble-on-dependency`

The bubble now gets decided per branch, from the slots the strict emitter is holding:
does the row above write a register this branch reads? Three details, each one
measured rather than assumed:

* **Do not ask the latency tracker.** The first attempt gated the bubble on
  `manualReadHazardDelay` and produced **32 sites where a branch reads a register
  written by the row directly above it** - a distance SCE never emits. Cause:
  `recordWrites()` returns early for anything with `latency() <= 1`, i.e. for most
  integer ops, so the tracker has never heard of this hazard. **The unconditional
  bubble was its implementation.** Caught by re-running the SCE-gap measurement, not
  by a test.
* **Interlocked producers still do not need it.** A load or flag reader above the
  branch answers "yes, I write it" but the hardware stalls anyway
  (`--branch-interlock`), so the bubble has to skip those too - otherwise it takes
  back exactly the words that flag saved.
* **Look two rows up only when the row above can be taken away.** With
  `--emit-delay-fillers` the i-1 row can be retracted into the delay slot, which
  promotes i-2 to being adjacent. Checking i-2 unconditionally kept bubbles SCE does
  not need - `fcand` / `iadd VI10` / `fcand` / `ibne VI10,VI01` in `clip_c` and
  `clip_d`, where SCE emits no bubble at all. Gated on the filler's own legality test
  so the two decisions cannot disagree.

Result: **2070 → 2040 against the 2042 ceiling**, 100 stall rows against SCE's 104,
and every hazard distance now matches SCE's measured minimum exactly:

| producer of the branch's operand | SCE | openvcl |
|---|---|---|
| ordinary integer op | 2 | **2** |
| integer load | 1 | **1** |
| flag reader | 1 | **1** |
| `mtir` | 2 | **2** |

Over all 25 programs openvcl now emits 3996 words against SCE's 3982 - +0.35%, from
+75% where this started.

### The last +14 words are a scheduling-ORDER problem, and a peephole cannot reach them

With the padding fixed, the remaining difference over all 25 programs inverted its
cause. openvcl now emits **fewer** stall rows than SCE (161 against 191) and **more**
single-slot rows (2840 against 2760): it packs 995 paired rows where SCE packs 1031.
Each missing pair is one word, which is the whole +14.

The profile says exactly which pairs are missing:

| pair | SCE | openvcl |
|---|---|---|
| `ftoi` + `iadd` | 30 | 6 |
| `ftoi` + `sq` | 37 | 14 |
| `min` + `loi` | 22 | 3 |
| `max` + `lq` | 18 | 7 |
| `add` + `div` | 16 | 3 |

with `ftoi` sitting alone in the upper pipe 92 times against SCE's 23, and `iadd`
alone in the lower pipe 407 against 336 — adjacent rows that look like they should be
one row.

**They should not be, and that is the finding.** `chooseReadyPairPartner` only
considers instructions that are dependency-ready at the instant the primary is chosen,
so the obvious fix is to retry against rows already emitted. Implemented, gated,
instrumented: on `dynpip_c` it gets 641 chances and takes **zero**. First against the
row above only — 484 of the refusals are "same pipe", because the ready list is
dominated by lower-pipe work and the scheduler emits runs of it. Then widened to search
four rows back, with every crossed row checked by `vuTokenCanMoveBefore`: still zero,
because the adjacent singles are **genuinely dependent on each other** — which is also
why no partner was ready in the first place.

So the missing pairs are not a missing merge step. They are the consequence of an
ORDER that leaves dependent instructions adjacent, where SCE interleaves independent
work between them. Closing it means changing how the ready list is ranked (lookahead,
or a cost model that prefers a candidate leaving a pairable successor), not another
peephole — the same conclusion the pre-branch stalls reached before their real cause
turned out to be elsewhere. The dead-end code is not in the shipped patch.

**Verified, not just counted:** 419/419 upstream tests, 25/25 outputs through
`dvp-as`, the loop-carried liveness checker clean, and in PCSX2 with the VU1 clipper
the full openvcl build **boots with 0 assertions** (it no longer overflows) while
nine of the ten resident programs are pixel-identical to a legacy build - the tenth
is the `stapip_clip_c` miscompile below, which predates all of this. The EE-clipper
path stays pixel-identical too.

Every output was checked through `dvp-as` as well as counted: 25 of 25 assemble.

### A fifth flag, and SCE annotating the answer

`--branch-interlock` is worth its own note because SCE's output *says* what the rule
is. Asked for the minimum distance it keeps between an integer write and a branch
reading it, its 25 programs answer in three different ways:

| producer of the branch's operand | SCE's minimum | openvcl's | |
|---|---|---|---|
| ordinary integer op | 2 (24 cases at exactly 2) | 2 | agrees |
| integer **load** | **1** | 3 | 2 words wasted per site |
| **flag reader** (`fcand`) | **1** | 2 | 1 word wasted per site |

And the gap-1 cases are not an artefact of reading the listing linearly - no label
sits between them, and SCE labels one of them itself:

```
ilw.x  VI01,8(VI00)
iblez  VI01,multiColor      ;  STALL_LATENCY ?3
```

That comment is SCE saying "three cycles, left to the hardware". It is the same
distinction `--fmac-interlock` draws for the FMAC pipeline, one file down: the cycles
are real, the instruction words are not ours to spend. Five programs put an `ilw`
directly in front of the branch that reads it, five more put `fcand VI01,8` there.

Worth **-5 words** on the resident set (2075 → 2070) - less than the 15 the sites
add up to, because at some of them a second hazard needs the row anyway. 419/419
upstream tests still pass, 25 of 25 outputs still assemble, and the loop-carried
liveness checker stays clean for both assemblers.

### It runs - and the picture is identical to Sony's

Counting instructions says a program fits. It does not say the microcode is
*right*, and until 2026-08-05 nothing built by `openvcl` had ever been executed.
It has now, because a project does not have to use the set that overflows: with
the **EE clipper** (Project Preferences > Rendering > Clipping, anything other
than `vu1`) the resident VU1 set is five `cull_*` plus five `as_is_*` programs -
**1403** instructions against the same 2042 ceiling, 639 to spare.

A terrain project built that way, `VCL_IMPL=openvcl` with all four flags, in
PCSX2, **after the miscompile the next section describes was fixed**:

| | boots | `bin/log.txt` asserts | FPS | framebuffer vs legacy |
|---|---|---|---|---|
| openvcl, all four flags | yes | 0 | 50 (PAL cap) | **0 of 514600 pixels differ** |

Pixel-identical, not "looks the same": the emulator's framebuffer area was
compared channel by channel against a legacy build of the same project, and the
maximum channel delta is 0. The four density flags change *where* instructions
sit, and this is the evidence that they do not change what the program computes.

Two things it does NOT say, both worth stating because the first run of this
experiment claimed more than it had:

* **openvcl with no flags does not even fit this set.** It trips the same
  overflow assertion (the flags are what make openvcl competitive, not a tuning
  nicety), so "openvcl renders correctly" always means openvcl *with the image's
  flags*.
* This is one scene on one emulator. It exercises `cull_*` and `as_is_c`; the
  textured variants are only covered by the static checks, and the clip programs
  are covered separately (two of them run, one is miscompiled - see
  "And a second one, still open" below).

So for a project on the EE clipper the migration is done. For the VU1 clipper it
waits on two things: **28 words**, and a correctness bug in `stapip_clip_c`.

**The trap that cost the first attempt, and the fix that closed it:** switching
the image did not rebuild the microcode. The engine's make keys off `.vclpp`
timestamps and an image swap touches no source, so three consecutive "bisection"
probes each booted the *previous* probe's VU objects and produced three identical
screenshots of a program none of them had built - including a "corrupted terrain"
that belonged to no configuration at all.

The Runner now stamps the assembler itself (`/tyra/.vcl-stamp`, the md5 of the
resolved `vcl` and `vclpp` - which for the openvcl form is the wrapper script, so
a change of `VCL_FLAGS` counts too, `src/runner.cpp`). Swapping the image is
enough on its own:

```
[editor] VU assembler changed - rebuilding the microprograms (takes a minute or two)...
```

Verified both ways: a switch rebuilds all 25 programs with no `--rebuild`, and a
second build with the same image compiles nothing at all. If that line is missing
after a swap, do not trust the picture - count the work in the log
(`grep -cE '(^| )vcl ' `, one line per program) before believing it.

**The stamp had the same hole once**, and it cost an hour: it hashed what
`command -v vcl` resolves to, which for the openvcl form is the *wrapper script*. Its
text carries the flags, so a flag change was caught - but a rebuilt `openvcl` behind
unchanged flags was not, and the previous binary's microcode was relinked while the
numbers said the new one should fit. It now hashes the wrapper AND the openvcl binary
it calls.

### What the density costs at runtime: nothing measurable

openvcl needs 5913 cycles over the 25 programs where SCE needs 3982, which sounds
like it should show up as frames. It does not. The documented perfbench scene (128x128
terrain, ~98k verts, debug build, `showFps`, vsync off so 50/25 quantisation cannot
hide anything), ten HUD samples per configuration, read by exact pixel signature:

| | readings | mean |
|---|---|---|
| SCE `vcl` | 70x5, 69x2, 68x2, 64x1 | 68.8 |
| openvcl | 70x6, 69x1, 68x2, 64x1 | 68.9 |

Indistinguishable - and there is a reason rather than luck: **the per-vertex loops are
the same length**. Summed over the seven programs the scene touches, both assemblers
emit 554 rows of vertex loop, with openvcl shorter on `cull_d` (-3) and `cull_td`
(-6). Every extra word it spends is in per-batch setup, which runs once per `xtop`,
not once per vertex. The static cycle figure counts that setup - and counts stalls the
hardware absorbs - so it overstates the runtime cost badly.

### A miscompile this uncovered

The first openvcl build that really ran drew **no terrain**: sky, a horizon smear
and one green quad. Not a crash, not an assertion - 50 FPS of wrong picture. What
it turned out to be is worth writing down, because the shape is generic and
nothing in the build would have told us.

**Attributing it needed a new instrument.** Whole-set openvcl builds are all-or-
nothing, and without the density flags they do not fit, so "which program is
wrong?" could not be asked. The answer was a `vcl` that dispatches per input file:

```dockerfile
FROM tyrax-toolchain:openvcl
ARG PATTERN='*none*'
ARG FLAGS=''
RUN printf '#!/bin/sh\ncase "$1" in\n  %s) exec /usr/local/bin/openvcl %s "$@" ;;\n  *) exec /usr/bin/vcl "$@" ;;\nesac\n' \
        "${PATTERN}" "${FLAGS}" > /usr/local/bin/vcl && chmod 0755 /usr/local/bin/vcl
```

One program from openvcl and 24 from Sony's vcl fits trivially, so size stops
interfering and each program can be tested alone. Five probes (`*_cull_*` clean →
`*_as_is_*` wrong → `*_as_is_t*` clean → `*_as_is_d_*` clean → `*_as_is_c_*`
**wrong**) put it on **one** microprogram, `stapip_as_is_c`. A sixth, with no
flags at all, reproduced it - so the defect was in openvcl's base code generation
and none of the four density flags was implicated.

**The defect: live ranges do not survive the loop back edge.** These programs end
in `b begin`, so `begin:` is a loop over batches, and the GIF tags loaded above it
have to stay live through the whole body. openvcl instead reuses those registers
for per-vertex values:

```
lq VF01, 19(VI00)              ; gifSetTag, loaded once, above begin:
...
sq VF01, 0(VI07)               ; written into the GIF tag block, every batch
...
add.xyzw VF01, VF00, VF02      ; and VF01 is the per-vertex colour inside the loop
```

The first batch is correct; every batch after it writes a *colour* where the GS
expects a GIFtag, so its geometry never draws. Sony's vcl keeps them apart.

**Nine programs had it, and a static check found them all.** Eyeballing does not
scale to 25 microprograms, so the condition got a checker
(`scratchpad/loopcarry.py` in the working notes): per register **component**, flag
anything written in the preamble, read in the body before the body writes it, and
written in the body. Field masks are not optional - Sony's `clip_tce` loads
`VF11.xyz` before the loop and uses `VF11.w` as scratch inside it, which is
correct and looks like a violation if you track whole registers. It reports **0 of
25 for Sony's vcl** and **9 of 25 for openvcl**: `as_is_c`/`tc`/`tce`,
`clip_c`/`tc`/`tce`, `cull_c`/`tc`/`tce` - exactly the variants that the earlier
register-pressure work had not already moved.

**Fixed in the engine, and it is the same fix three siblings already carried**:
the tag loads moved inside `begin:`, next to the store that is their only reader
(`stapip_as_is_c` and the eight others). It costs nothing - the tag block is
written once per batch either way - it cuts peak register pressure, and it makes
the source robust under both assemblers rather than relying on one of them being
clever. After it: 25 of 25 compile, the checker is clean for both assemblers, and
the render is the pixel-identical one in the table above. (The `cull_*` and `clip_*`
siblings still carry it. The five `as_is_*` do not, for the reason in the next section.)

**The obvious explanation for it is wrong**, and that mattered for months. "openvcl's
liveness ignores the back edge" does not survive testing: narrow the register pool with
`.init_vf vf01-vf03`, keep a value live across `b begin` and make the body need the whole
pool - with and without an inner loop - and openvcl **refuses cleanly** (`Register
allocation ran out of registers`) rather than clobbering anything. So the trigger is
narrower than that. Written up for upstream in
[upstream-openvcl.md](upstream-openvcl.md).

#### Fixed in the tool after all, and main proved it

The engine-side move above was a workaround, and workarounds get reverted by people who
do not know they are workarounds. `main` later rewrote all five `as_is_*` programs
macro-free and put the GIF-tag loads back **above** `begin:` - the exact shape that
miscompiled. Merging that in was therefore a test of whether the register-allocator work
(loop live-range extension across the back edge for *both* register files, plus tying
carried writes to the alias their readers use) had fixed the class or only hidden it.

It fixed it, and the flag that does it is isolated:

| openvcl invocation | loop-carry checker on main's five `as_is_*` |
|---|---|
| no flags | **`as_is_c`, `as_is_tc`, `as_is_tce` clobber** VF01 and 3-4 more |
| `--loop-liveness-always` alone | clean |
| all ten image flags | clean |
| all ten **minus** `--loop-liveness-always` | the same three clobber again |

So `--loop-liveness-always` is both necessary and sufficient here, and **upstream's
default still miscompiles this shape** - worth saying plainly, because the flag is off by
default and the engine only gets it from the image's `vcl` wrapper. End to end: main's
sources, openvcl, `vu-lab` forced through the EE clipper (`CLIP_VU1S = {false}`, verified
in the generated header rather than the intent field) render **0 of 514600 pixels**
different from Sony's build, with the two assemblers demonstrably emitting different code
for it (558 words against 562).

What is still not pinned down is the exact trigger inside
`extendLoopDirectiveRange`. The guard is visible in the source - the extension is
all-or-nothing and returns early when the set would not fit, which is precisely a silent
licence to emit wrong code - but four attempts at a minimal reproducer all failed to make
it fire, and each rules something out:

* a 4-to-12 register pool with the body needing all of it - **refuses cleanly**, so
  pool pressure alone is the honest failure, not the bug;
* 42 float aliases against 32 registers - range still extended, no bail;
* 20 integer aliases against 16 registers - range still extended, no bail;
* forcing order instead of pressure (read back the quadword the store just wrote, so
  every temporary starts after the carried value's last read) - range still extended.

`as_is_c` differs from all four in one structural way: it has an **inner** loop
(`vertexLoop`), so `extendLoopDirectiveRange` runs twice over nested ranges. That is the
next thing to try, and it is in [backlog.md](backlog.md) rather than blocking anything -
the flag closes the bug either way, and `scratchpad/loopcarry.py` catches the shape if it
ever comes back.

### And a second one, still open: `stapip_clip_c`

The same harness found a second miscompile, and this one is **not** the liveness bug
(the checker is clean on it) and not caused by any of the five flags (it reproduces
with four of them, and with none of the new ones).

Testing it needed a trick, because a program that does not fit cannot be booted: put
`stapip_clip_c` on openvcl **together with `cull_d` and `cull_td`, where openvcl is
smaller than SCE** (-3 and -6). That buys the seven words `clip_c` costs and the
resident set fits at 2033, so the VU1 clipper can actually run:

| VU1-clipper build, `clipping: "vu1"` | asserts | framebuffer vs legacy |
|---|---|---|
| `cull_d` + `cull_td` from openvcl | 0 | **0 of 514600 pixels differ** |
| the same plus `clip_c` | 0 | **453644 differ** |

### Coverage: what a second scene found

Everything verified in an emulator up to this point ran only `stapip_*` on ONE
untextured terrain scene. Two real examples, each built with openvcl everywhere except
the programs under suspicion, changed the picture:

| scene | openvcl everywhere except | result |
|---|---|---|
| `blocks-terrain` (blocks pipeline, vu1) | `clip_c` | **pixel-identical** |
| `raytraced-mirror` (textures, VU0 raytracer, vu1) | `clip_c` | corrupt |
| `raytraced-mirror` | `clip_c`, `clip_tc` | **clean** |

So `mcpip_as_is`, `mcpip_cull`, `dynpip_*`, `billboard_*`, `vu0_rt_kernel`, every
`cull_*` and `clip_d`/`clip_td`/`clip_tce` are all correct - and there is a **second**
miscompiled program, `stapip_clip_tc`, which the first scene could not see because it
has no textures.

**Both broken programs are the colour-carrying clippers** (`_c` = colour, `_tc` =
texture + colour), and both fail the same way. In `clip_tc`'s `edgeAdvance` SCE writes
the previous-vertex state to four distinct registers:

```
max VF14,VF18,VF18     ; ppos = cpos
max VF15,VF19,VF19     ; pcol = ccol
move VF16,VF20         ; pst  = cst
addx.x VF17,VF00,VF21x ; pd   = cd
```

openvcl gives `pcol` and `pst` the same one:

```
move VF22, VF18        ; ppos = cpos
move VF18, VF20        ; pcol = ccol
move VF18, VF21        ; pst  = cst   <- clobbers pcol
```

The `--loop-liveness-always` fix removed this collision from `clip_c` but not from
`clip_tc`. The guess at the time - that the two-address chain pre-pass decides the
register before the extended ranges are consulted - was wrong; what it actually is
[has its own section below](#the-second-defect-a-carried-name-is-two-aliases-and-only-one-of-them-was-extended).
Note the pattern alone is not proof:
`clip_td` and `clip_tce` carry the same duplicate destination and render correctly,
where the second copy overwrites something genuinely dead.

A note on method: **`raytraced-mirror` is not pixel-comparable across runs** (it
animates, and the two builds settle at different frame rates), so its verdicts here are
visual - a crate shredded into slivers and a pillar collapsed into a black wedge are
not a phase difference. `blocks-terrain` is static and compares exactly.

The clip family was then measured the same way, one program at a time:

| from openvcl, VU1 clipper | asserts | framebuffer vs legacy |
|---|---|---|
| `clip_d` | 0 | **0 of 514600 differ** |
| `clip_td` | 0 | **0** |
| `clip_tc` | 0 | **0** |
| `clip_tce` | 0 | **0** |
| `cull_d` + `cull_td` | 0 | **0** |
| everything except `clip_c` | 0 | **0** |
| `clip_c` | 0 | **453644** |

**One program.** And it is not the tag-load move that fixed nine others: the source
as it stood *before* that change is miscompiled too (a different pixel count, both
wrong), so the engine change is exonerated.

The static routes are exhausted - no instruction is dropped or added (283 in both),
and generalising the liveness checker to every backward branch drowns in false
positives, because "set up before the loop, read inside, written inside" is also the
shape of every induction variable, in both assemblers' output.

**The VU1 packet tap turned the screenshot into a number.** Arm a capture of the same
flush under each build (`arm-vucap.py`, see `tyra-testing`; the flush index has to be
named or "the next packet" is a different draw each time) and decode both:

| flush 0, identical scene | Sony `vcl` | openvcl |
|---|---|---|
| DMA chain + VU1 memory | — | **byte-identical** |
| GIF packets staged | 14 | 14 |
| GS vertices staged | 72 | **81** |
| triangles staged | 24 | **27** |

So the input to VU1 is provably the same and **openvcl's `clip_c` emits three
triangles too many** - nine extra GS vertices, i.e. the Sutherland-Hodgman clipper
keeps three output vertices it should have dropped. Not a transform error: a
*vertex-count* error.

Two hypotheses were then eliminated, both cleanly:

* **The filled branch delay slot.** openvcl puts `iadd VI08, VI13, VI00` in the delay
  slot of `ibeq VI03,VI12,triNext`, where SCE keeps a `nop` - so VI08 is clobbered on
  the taken path too. Harmless: nothing reads VI08 after `triNext`. It is also
  upstream's own pre-pass, not `--emit-delay-fillers` (it appears with no flags at
  all).
* **Flag visibility of 1 cycle** (`--sce-latencies`). SCE emits each `fcand` three rows
  after the *next* `clipw`, reading the previous one's flags, where openvcl emits it one
  row after its own - which looks exactly like a latency the flag got wrong. It is not:
  **`clip_d` has the identical shape and renders pixel-identically.** If flag
  visibility were the bug, clip_d would be broken too.

The edge loop's output accounting was next, and it produced the real find. In
`edgeAdvance` openvcl emitted

```
move VF15, VF17      ; ppos = cpos
move VF15, VF19      ; pcol = ccol   <- the SAME register
```

for the source's two distinct names, so `ppos` was destroyed and `pcol` never updated;
SCE writes VF14 and VF15. **A dead write is never something a source asks for** - a
check for "written, then overwritten before anything reads it" finds 0 of them in
openvcl's output after the fix below and 6 benign ones in SCE's.

### The root cause: a back edge only counts if the source said `--loop`

`RegisterAllocator::extendLoopDirectiveLiveRanges` walks every branch, keeps the
backward ones, and then:

```cpp
if( !loopTargetHasLoopDirective( target, tokens.end() ) )
    continue;
```

**A live range is extended across a back edge only when the loop carries a `--loop`
directive.** Hand-written VU code - `label:` … `ibne label`, which is what this engine
and most real VU code looks like - gets no extension at all, so a value written at the
bottom of a loop and read at the top is not live as far as the allocator knows, and its
register goes to someone else. That is the whole loop-carried class, including the GIF
tag clobber that was worked around engine-side earlier.

`extendLoopDirectiveRange` has a second defect behind it: it extends **every** float
alias appearing anywhere in the loop, and bails out entirely -

```cpp
if( overlappingAliases.size() > availableFloats )
    return;                       // and nothing is extended
```

- when those outnumber the registers. Correctness traded for a compile that succeeds.
Applied to every back edge, the blunt version runs the allocator out of registers on
23 of 25 programs.

`--loop-liveness-always` fixes both: it drops the directive requirement, and it extends
only the aliases that are actually **live-in** (first access inside the loop is a read,
so the next iteration depends on the previous one). Temporaries written before they are
read keep their own ranges.

Result: `clip_c`'s staged output goes from **27 triangles / 81 GS vertices to 24 / 72,
matching SCE exactly** on the captured flush, and the frame moves from 453644 differing
pixels to 368945 - **improved, not fixed**: the remaining flushes still differ (±6
triangles), so at least one more defect is in there. Everything else stays put: 25/25
compile and assemble, 419/419 upstream tests, the resident set still 2040, the EE path
still pixel-identical, and nine of the ten resident VU1-clipper programs still
pixel-identical.

### The second defect: a carried name is two aliases, and only one of them was extended

Finding it needed `--show-reg-alloc` to say something it did not say. Upstream's dump
prints live ranges for anonymous `Alias` objects - no name, no register, and it runs
*before* allocation, so the one question that matters ("which two values share a
register?") could not be asked. Three small additions fix that: `Alias` carries the
source-level name it was created for, `BranchState::updateDependency` records it, and a
`=== Final assignment ===` block reports name → register after allocation.

With that, `clip_tc`'s edge loop reads:

```
VF15 <- ppos #112  ranges: [228-286]      <- what edgeCross reads
VF16 <- pstq #113  ranges: [229-286]
VF17 <- pcol #114  ranges: [230-286]
VF22 <- ppos #126  ranges: [281-281]      <- what edgeAdvance writes
VF18 <- pstq #127  ranges: [282-282]
VF18 <- pcol #128  ranges: [283-283]
```

**openvcl spawns a fresh `Alias` for every write, so one source name is several
aliases** - and the update at the bottom of the loop is a *different* alias from the one
the readers at the top hold. It gets its own register: `move ppos, cpos` writes VF22
while `edgeCross` keeps reading VF15. The write goes nowhere anyone looks, so every
iteration lerps against the same stale vertex. That is the real defect, and it is
present even where no collision is visible - which is why `clip_c` rendered wrong while
the dead-write checker called it clean.

The VF18 collision is a *consequence*. Nothing downstream reads those tail aliases, so
each gets a live range **one line long** - the line of its own write. Two one-line
ranges do not intersect, so the allocator may legally put two carried names in one
register, and the second write destroys the first.

One fix covers both, and it reuses machinery openvcl already has. The two-address chain
exists so `isubiu x, x, 1` writes the register it read; `tieCarriedWritesToLiveInAliases`
ties every in-loop write of a **carried** name (one whose first access inside the loop is
a read) to the alias its readers use, and the chain pre-pass then hands the whole chain a
single register:

```
VF15 <- ppos #126  ranges: [281-281]      after: the register edgeCross reads
VF16 <- pstq #127  ranges: [282-282]
VF17 <- pcol #128  ranges: [283-283]
```

It rides along with `--loop-liveness-always` (it runs wherever the live-range extension
runs), and it costs nothing: `clip_c` 258 words and `clip_tc` 270 → 272, unchanged from
before the fix, 25/25 compile and assemble, upstream's tests still green.

Two static checks came out of this and are worth keeping, because each answers a
different question about one dump:

* `splitname.py` - **which names got more than one register.** Abutting short ranges are
  normal (a value recomputed step by step); a carried name split in two is the bug above.
* `overlap.py` - **which registers hold two names on one line.** The dual question, and
  the stronger one. Ranges with holes are what allow it: `color2` is `[72-75], [77-206]`,
  dead on line 76 - the line where a *different* colour is loaded - so any alias covering
  76 may be handed VF14. Both clippers report 0 after the fix.

### What this bought, and the one thing still wrong

**Check what the project is actually running before believing a green frame.** The
`vclab` working copy used for the probes above had `"clipping": "precise"` - the EE
clipper - which generates `constexpr bool CLIP_VU1S[SCENE_COUNT] = {false}` and leaves
the five `clip_*` programs off VU1 entirely. So its pixel-identical result under the
whole openvcl set is a real result about the **EE** path and says nothing about the VU1
clipper. Set `"clipping": "vu1"` in the `.tyra` (two scenes, in this project) and
re-measure before drawing that conclusion. With it on:

| `vclab`, VU1 clipper | pixels vs legacy |
|---|---|
| SCE `vcl` | reference |
| everything from openvcl | **506784 of 514600 differ** |
| all but `clip_c` from openvcl | **0** |

So nine of the ten resident programs - `clip_tc` among them - are correct from the fixed
openvcl, and `clip_c` alone is not. The resident set fits either way: **2040 against
SCE's 2042**.

The other two scenes agree, and four more probes pin it to that one program:

| VU1 clipper | frame |
|---|---|
| `raytraced-mirror`, everything from openvcl | **blank** |
| `raytraced-mirror`, all but `clip_c` and `clip_tc` from openvcl | matches legacy |
| `raytraced-mirror`, all but `clip_tc` from openvcl | **blank** |
| `raytraced-mirror`, **only** `clip_c` from openvcl, 24 programs from SCE | **blank** |
| `blocks-terrain`, everything from openvcl | **blank** |

`clip_c` alone, with no interaction. Note what the second row rules out: with the two
clippers held back, the other 23 programs from the *fixed* openvcl render this scene
correctly, so the carried-write fix broke nothing. Not a micro-memory overflow either:
the
mixed set measures exactly 2042 with `draw_finish` at 2042, the assert passes, the game
logs 5520 frames and binds textures normally. Nothing is drawn anyway, and in this
pipeline **every** triangle passes through the clip program's emit path, so a defect
there takes the whole frame - sky included - which is what the picture shows.

A measurement note that cost a wrong conclusion first: **count words with `nm` on the
built object, not rows in the `.vsm`.** Row counts run 2-6 high per program (they pick up
what sits outside `CodeStart`/`CodeEnd`), which put SCE's resident set at 2042 when the
uploader actually sees 2040, and made a mixed build look like a 2-word overflow when it
fits exactly:

| resident 10-program set | `nm` words |
|---|---|
| SCE `vcl` | **2042** - the ceiling exactly, no headroom |
| patched `openvcl` | **2040** |

Three explanations were checked and eliminated, so the next attempt should not start
there. **Register allocation is internally consistent**: `overlap.py` reports no
register holding two names on one line, and no carried name split across registers.
**The instruction multiset matches SCE** (`instrdiff2.py`, as it did before). And
**every branch delay slot is legitimate**: `delayslot.py` compares all 20 branches
against SCE's, and each of the six sites where openvcl fills a slot SCE left empty -
or filled differently - holds an instruction the source itself placed *before* the
branch (`srcEnd = srcBase + 6` above `ibeq triOr,vi00,fanStart`, the `emitNxt`/`fanPtr`
rotations inside `fanEmitLoop`, the src/dst swap above `ibne ...,planeLoop`). Those are
meant to run on both paths.

### The cause: the CLIP flag is positional, and our own flag shortened its wait

The remaining defect is not in what openvcl computes but in **when it reads a flag**,
and it was introduced by one of our density flags.

`--sce-latencies` sets flag visibility to 1 - for MAC flags and the CLIP flag alike -
calibrated from the shortest gaps SCE's own output contains. That minimum was taken over
two populations that behave differently. MAC flags summarise the last FMAC. The CLIP flag
register is a **24-bit shift window** that every `CLIP` pushes six new bits into, so
reading it early does not return a partly-settled answer: it returns a **different
vertex's** answer, with the mask silently selecting the wrong window position. A
full-window test (`fcand VI01,0x3FFFF`, "is anything outside") survives that. A
single-bit positional test - which is exactly what a Sutherland-Hodgman edge loop runs -
does not.

Over all 25 programs, counting only positional tests (mask below `0x3FFFF`):

| producer → flag read | SCE `vcl` | openvcl |
|---|---|---|
| gap 1 | 5 (all full-window) | **72** |
| gap 2 | 7 | 1 |
| gap 3 | **36** | 0 |
| gap 4 | 5 | 0 |

Per program, SCE keeps every positional test at 3 and the edge loop's `fcand VI01,2` at
4; openvcl kept all of them at 1, in all five `clip_*` programs and both billboards. So
`clip_d`/`td`/`tc`/`tce` carry the same defect and merely got lucky on this geometry -
which also retires the earlier "flag visibility of 1 cycle" hypothesis being *eliminated*
because `clip_d` renders correctly. It was not eliminated; it was under-tested.

The source is unambiguous about the intent - each `fcand` reads the `clipw` directly
above it:

```
clipw.xyz   vertex1,    vertex1
fcand       VI01,       0xF
```

SCE reaches the same semantics by a scheduling trick that looks alarming and is correct:
it issues `clipw` #N+1 *before* reading #N's flags, precisely because #N+1's bits are not
visible yet. `clipflags.py` shows the two window depths side by side, and openvcl's now
match the source's order.

`g_clipFlagVisibilityLatency` is tracked separately and stays at 4 whatever
`--sce-latencies` says. That alone left sites at a gap of 2-3, and the reason is worth
keeping: **the scheduler's constraint was being honoured, in the wrong unit.**
Instrumenting the check shows it asked and got `delay=3` / `delay=2` at exactly those
sites - but cycles and emitted rows are not the same measurement, because a wait the
hardware interlocks is a cycle that costs no word. Four cycles of separation can come
out as two rows. The CLIP window is counted in *instructions*, so
`CodeGenerator::padForClipFlagWindow` now enforces it at the emitter: walk back over the
rows already emitted, count instruction rows to the one holding a CLIP, pad with nops
until the reader is far enough. A label is a barrier - a reader below one may be entered
from anywhere, so the distance is unknown and padding it would be guesswork.

Every positional test in all five `clip_*` programs now sits exactly 4 rows behind its
own CLIP with no other CLIP in between, which is both the source's order and SCE's
minimum. **And it still does not fix the miscompile.** The honest numbers:

| VU1 packet capture, same draw | triangles staged | GS vertices |
|---|---|---|
| SCE `vcl` | **24** | 72 |
| openvcl, flag read at gap 1 | 28 | 84 |
| openvcl, CLIP window enforced (gap 4) | 17 | 51 |
| openvcl, window raised to 8 | 17 | 51 |

The count moves with the timing, which is the mechanism confirmed; raising the window to
8 changes nothing, so the flag now reads a settled value and **a separate defect
remains.**

Where that defect is NOT, so the next attempt starts somewhere new. Every `fcand` in
this program writes the same VI01, so the emission order has to be strictly test,
consumer, test, consumer - a consumer left behind the next test would read a different
plane's answer. It is not: `flagchain.py` reports 0 overwrites-before-read for both
assemblers. And `edgeCross`'s divide is structurally identical - `div` then two rows
later the `mulq` paired with `waitq`, in both - so the interpolation is not reading `q`
early, which matters because the clipper is iterative and bad positions from one plane
would change the next plane's decisions. Also eliminated on the way: `--emit-delay-fillers` retraction is not what
shortened the gaps (building without it leaves every gap byte-identical), the CLIP window
*depth* matches the source (each `fcand` reads its own `clipw`, none intervening), and
`sjudge` - whose `.z` is written once above `begin:` and read by every `clipw.xyz` in the
loop - holds VF11 alone and is never overwritten inside it.

The cost is **98 instructions** (2140 against the 2042 ceiling), so the VU1-clipper set no
longer fits. The padding is pure nops for a structural reason: the scheduler still
measures this wait in cycles, where it is free, so it cannot fill rows it does not know
about. Teaching it to count this one in rows is how the words come back - SCE pays the
same waits and fits in 2042. A compiler that miscompiles to save words is not a smaller
compiler.

To boot a clip program that no longer fits, shrink the *set* rather than the program:
`StaPipQBufferRenderer::setProgramsCache` uploads ten, and the packet tap names the
program each mesh runs (`program @174` for all twelve of `vclab`'s, i.e. `programs[1]`),
so uploading two is enough to judge the clipper. SCE under that harness stages the same
24/72 as under the full set, which is what makes it a valid harness rather than a
different experiment.

### Bisecting `clip_c` by stage: what is cleared, and what is left

Static comparison was exhausted, so the next instrument was the program itself. Each
variant is a source change applied to **both** assemblers, booted under the two-program
harness, and read through the VU1 packet tap. Because the change is identical on both
sides, a variant where the two agree clears everything it exercises.

| variant | what it does | SCE | openvcl |
|---|---|---|---|
| A | every triangle **skips** the clipping path (`ibeq vi00,vi00,fanStart`) | 23 tris / 69 verts | **23 / 69, identical to the digit** |
| B | every triangle **enters** the plane loop (branch never taken) | 24 / 72 | 17 / 51 |
| C | plane loop as a **pure copy** - both edge branches never taken | 23 / 69 | **23 / 69, identical** |
| D | real tests, but the crossing vertex is stored as `cpos`, not the lerp | 29 / 87 | 21 / 63 |
| E | `pd` **recomputed** in the loop instead of carried across the back edge | 24 / 72 | 17 / 51 |

Read together:

* **A clears the whole emit side** - the transform, the fan triangulation,
  `EmitClipVertexC`, the GIF tag block, the vertex counting. Identical output.
* **C clears the plane loop's bookkeeping** - `curPtr`/`dstPtr`, the `srcEnd = dstPtr`
  swap, the loop bounds, the five-plane iteration. Identical output.
* **D says the difference is in the tests, not the interpolation**: neutralising the
  stored value moves both sides by about the same amount and the gap survives.
* **E clears the carried `pd`** - taking it off the back edge changes nothing.

So the defect is in the edge test path and nothing else: `pd`/`cd` against the plane, the
`sjudge` assembly, the `clipw`, the two `fcand`s and the `vertMask` comparison. And
inside that path the static reading is *identical* between the two assemblers -
`sjudge` is built the same way, `clipw` takes its `w` from the component that holds it,
both `fcand` masks reach their own consumer, `planeV`/`planeE`/`cvec` hold their own
registers with no overlap, and the accumulation of `dot(cpos, planeV)` differs only in
which register accumulates.

A sixth variant sharpened that further, by accident. **F** encodes `pd`/`cd` into the
emitted colour so the tap would report the test's input per vertex - three FMACs, in the
store paths, after the tests and with no `clipw` among them. SCE stayed at 24/72.
**openvcl moved from 17/51 to 11/33.**

That is the most useful fact in this whole bisection: openvcl's clipper answers
differently when instructions that cannot affect the tests are added below them, while
SCE's answer does not move. A result that depends on unrelated scheduling is a value
being read before it is settled, or read from somewhere nothing wrote. It also means the
totals are a poor instrument - they move for reasons unrelated to the defect - so the
next step has to read the decisions themselves.

Variant F could not deliver them as written: `--dump-vucap` prints only the first few
staged packets, and those belong to an earlier batch, so the instrumented colours never
appeared. Either widen the decode, or parse `vucap.bin` directly for the packet the clip
program staged.

### Found: carried INTEGERS were never getting their live ranges extended

The loop-liveness work covered `Alias::FLOAT` only, and the defect that blocked this
migration from the start was an integer one. An integer read at the top of a loop body and
written lower down has its live range end at that last use; the register is handed to
another name for the rest of the loop, and the next iteration reads whatever that name
left behind. In `stapip_clip_c` the plane loop lost its bookkeeping that way, which is
why **every triangle went through every clip plane** where SCE discards most of them at
the first.

The tell was a debug pointer that refused to advance - `iaddiu p, p, 1` at the bottom of
a loop, both aliases correctly tied to VI01 by the existing two-address chain, and the
range `[30-178]` stopping well short of the loop end at ~340, after which VI01 belonged to
`sceFlag`, `fanPtr` and `emitCnt` in turn. The instrument found its own bug, and it was
the program's bug too.

`extendLoopDirectiveRange` now collects both register files, extends both, and counts the
overlap guard per file (32 floats, 16 integers). The carried-write tie follows for free,
since it works from the same live-in set.

| VU1 clipper, same scene | SCE `vcl` | openvcl |
|---|---|---|
| staged triangles / GS vertices | 24 / 72 | **24 / 72** |
| frame | reference | **0 of 514600 pixels differ** |
| 25 programs compile + assemble | yes | yes |
| upstream tests | - | 419/419 |

Three things about the CLIP flag window were settled on the way here, and they are worth
keeping straight because two of them are corrections:

* **It is genuinely required.** Without it the same build stages 27/81. Both fixes are
  needed; neither alone is enough.
* **4 is the right figure, and it is upstream's own.** `test_flag_latency.cpp` asserts
  `clipw followed by fcand has at least 4 cycles between`; 3 fails it. Probing 8 and 16
  changed nothing, which is what finally cleared the flag window as the *cause* and sent
  the search back to the register file.
* **Only positional reads need it.** A full-window mask (`0x3FFFF`) reads the same answer
  whichever position the bits occupy, and SCE puts those adjacent to their `clipw`.
  Padding them cost the five cull programs 22 instructions for nothing.

**What is left is size, and only size.** The resident set is **2074 against the 2042
ceiling** - 32 words, down from 118 when the fix first landed. The padding is nops because the scheduler measures this wait in cycles, where
an interlocked wait is free, so it has no reason to move work into the gap. Raising the
scheduler's figure to 8 to make it spread things out was measured and is worse (2224) -
nothing to fill with, so it just stalls longer. SCE fits while paying the same waits, so
the words exist; reaching them needs a scheduler that counts this one in emitted rows.

### Closing the size gap: six flag reads become two

A positional flag read has to sit four emitted rows behind its CLIP, and the engine's
`processing` block had six of them - so six places where those rows had to be paid for in
nops. They all feed one OR, though, and the CLIP register is a **24-bit window of four
6-bit entries, newest at bits 0..5**. Four tests can be pushed and read together:

```
clipw v1, s1, v2, s2   ->  fcand VI01, 0x3CA3CA    v1 0xF<<18 | s1 0xA<<12 |
                                                   v2 0xF<<6  | s2 0xA
clipw v3, s3           ->  fcand VI01, 0x3CA       v3 0xF<<6  | s3 0xA
```

Four fewer `fcand` and four fewer `ior` per program, and two padded sites instead of six.

| resident VU1 set | SCE | openvcl |
|---|---|---|
| before batching | 2042 | 2118 |
| after | **2040** | **2074** |

Both builds verified in PCSX2 after the change: SCE stays pixel-identical to its own
reference (0 of 514600), and openvcl matches SCE exactly - 24/72 staged, 0 pixels.

One thing that looked obvious and did nothing: moving the polygon-buffer stores up into
the gap, so the wait would be filled with work instead of nops. Byte-identical output.
The scheduler is not bound by source order within a block - it already had that freedom
and did not use it, because it measures this wait in cycles where an interlocked wait is
free.

And counting what the remaining words actually are reframes the last mile. Across the five
clip programs openvcl is 43 rows over SCE, split:

| | SCE | openvcl |
|---|---|---|
| rows with both slots empty (`nop`/`nop`) | 86 | 104 |
| rows with exactly one slot used | 891 | 938 |

So **18 rows are padding and 25 are pairing** - instructions openvcl emits alone where SCE
puts two in a row. That is the same pairing deficit measured at the start of this work
(995 paired rows against 1031), and it is now the larger half of what stands between this
assembler and the ceiling.

**But it is not the ready list's choice of primary.** The obvious fix - the list picks a
primary on its own merits and only then looks for a partner, so prefer a primary that has
one - was tried twice and measured **exactly zero** both times: first as a tie-break
(scores are almost never equal, so it never fired), then as a full point of merit against
an unpairable candidate. Byte-identical output, 2074 either way, tests green throughout.

What that rules out is worth more than the attempt: the scheduler already pairs wherever a
partner is *available*. The half-empty rows are ones where the partner exists in the block
but is not ready yet - its dependencies are unmet at that cycle. Closing them means
choosing an order that makes partners ready, which is a different and much larger change
than weighting the choice of primary.

And the 18 padding rows are already gone as a cost. With the six tests batched into two,
the scheduler keeps all three flag reads four rows behind their CLIP **by itself** - the
emitter's backstop finds nothing to add, and `clip_c` measures the same 266 words with it
on or off. It stays in as a backstop for programs that are not this engine's, but it is no
longer paying for anything here. Which means the whole remaining 32 words are pairing.

### And the pairing gap has a name: `move` belongs in the UPPER pipe

Bounding the problem first: a VU row holds one upper and one lower instruction, so the
fewest rows a given mix can occupy is `max(uppers, lowers)`. Over the five clip programs
SCE sits 460 rows above its own bound and openvcl 485, so **neither is limited by the mix**
- dependencies and latencies dominate, and there is no easy win in "pair harder".

What differs is the mix itself:

| five clip programs | SCE | openvcl |
|---|---|---|
| upper-pipe instructions | 606 | 591 |
| lower-pipe instructions | **749** | **767** |
| `move` (lower pipe) | 3 | **18** |
| `max r,r,r` (a move promoted to the upper pipe) | **25** | **0** |

The lower pipe is the bottleneck in every one of these programs, and SCE keeps it clear by
emitting a move as `max dst,src,src` in the *upper* slot - 25 times. openvcl does it never,
and pays 18 lower-pipe slots for moves that had somewhere else to be. That is where the
lower count's +18 comes from, and it is the same order as the words still missing.

openvcl already has the machinery (`isVuMoveAsUpperMaxCandidate`, `emitsAsUpperMove`); it
is gated on MAC WAW being ignorable, because `max r,r,r` writes the MAC flags where `move`
does not. These programs read the CLIP flags, not MAC, so the gate is not what closes it. The gate
is open here - `vuIgnoredFlagWawResourcesForRemaining` finds no MAC reader anywhere in
these programs - and the refusal is two lines further in, in the candidate test itself:

```cpp
unsigned int fields = token.fields();
if( fields == 0 || (fields & Token::W) )
    return false;
```

**Both halves reject every move this engine writes.** A move covering `w` is refused
outright, and a bare `move dst, src` carries no suffix at all, so `fields()` is 0 - which
means *all four*, not none - and that is refused too.

Lifting both, and emitting the mask as `xyzw` when it is 0 (the naive fix produces `max.`
with nothing after the dot, which `dvp-as` rejects - that is presumably why the refusal was
there), is measured:

| resident VU1 set | SCE | openvcl |
|---|---|---|
| moves in the lower pipe | 3 | 18 → **0** |
| set | 2040 | 2074 → **2060** |

All ten still assemble. **Eighteen words from the ceiling.**

Upstream pins the exclusion with an explicit test -
`CHECK(!isVuMoveAsUpperMaxCandidate("move.xyzw vf04, vf05"))` in
`test_vu_scheduling_rules.cpp` - and breaking that would trade the safety net for the win,
so this ships as **`--upper-move-with-w`**: off by default, upstream's suite untouched,
passed by the image's `vcl` wrapper like the other six. What justifies the flag existing is
SCE's own output, which promotes full-width moves 25 times across these five programs and
renders pixel-identically doing it.

### And four instructions per clipper that never needed to exist

`sjudge.w` is the guard the `clipw` compares against, and the `move sjudge, cvec` above
`begin:` already puts `cvec.w` there. Nothing writes `.w` afterwards - every other store
into `sjudge` is `.x`, `.y` or `.xy` - yet `mul.w sjudge, vf00, cvec[w]` recomputed
`1 * cvec.w` once per vertex and once per edge. Four dead instructions in each of the five
clip programs, and both assemblers were faithfully emitting them.

| resident VU1 set | SCE | openvcl |
|---|---|---|
| before the flag and the dead code | 2040 | 2074 |
| `--upper-move-with-w` | 2040 | 2060 |
| dead `mul.w` removed | **2028** | **2048** |

Verified after both: SCE stays pixel-identical to its own reference (0 of 514600) and
openvcl matches SCE exactly - 24/72 staged, 0 pixels, ten of ten assembling, upstream's
tests green with the flag off.

**Six words from the ceiling.** SCE now has 14 words of headroom where it had none, and
openvcl needs 6 more. What remains is scheduling and nothing else: over the five clip
programs openvcl makes 121 pairs where SCE makes 146, and emits 99 rows with both slots
empty where SCE emits 86. The pipe mix is no longer the difference - both use exactly 749
lower-pipe slots now.

One more source shrink was tried and reverted: collapsing `add.x sjudge, vf00, pd[x]` plus
half of `sub.xy` into a single `sub.x sjudge, pd, cvec[w]`. It removes an instruction from
the edge loop, which is a runtime win, but the scheduler absorbed it into a slot that was
already free and neither assembler's word count moved. Unverified runtime gains do not
land.

Two more attempts at the six, both measuring nothing, both recorded so they are not
repeated:

* **A priority term for unblocking the other pipe.** `buildDependencyPriorities` is pure
  critical-path height, and an instruction whose successor is of the opposite pipe is the
  one that makes a *pair* possible next cycle - so give it a point of height. Tests green,
  output byte-identical. Heights here differ by more than one, so a single point flips
  nothing, exactly as the earlier tie-break flipped nothing.
* **A flag hurting the two worst programs.** `clip_tce` is +12 over SCE and `cull_tce` +6,
  so each of the eight flags was removed in turn and both programs remeasured. Every one of
  them helps or is neutral; nothing to reclaim by dropping any:

| dropped flag | `clip_tce` | `cull_tce` |
|---|---|---|
| none | **258** | **174** |
| `--fmac-interlock` | 362 | 248 |
| `--schedule-flag-readers` | 266 | 186 |
| `--branch-bubble-on-dependency` | 264 | 178 |
| `--emit-delay-fillers` | 262 | 176 |
| `--branch-interlock` | 262 | 174 |
| `--sce-latencies` | 260 | 176 |
| `--upper-move-with-w` | 260 | 174 |
| `--loop-liveness-always` | 258 | 174 |

(`--loop-liveness-always` is the correctness flag and costs nothing in size, which is worth
knowing on its own.)

So the six words are a genuine scheduling deficit, not a flag and not dead source.

### The instrument, and what it says

`--show-pair-misses` re-derives the four reasons `chooseReadyPairPartner` can refuse a
candidate and tallies them wherever it comes back empty. On `stapip_clip_c`, over the 170
rows that went out with one slot used:

| reason a partner was unavailable | rows |
|---|---|
| `notReady` - the partner exists but its dependencies are unmet at this cycle | **132** |
| `samePipe` - every ready candidate needs the same slot | **71** |
| no unemitted candidate at all | 19 |
| `latency` | 2 |
| `resource` | **0** |

So resource conflicts are not a factor at all and latency barely is. Either the partner is
blocked, or the whole ready set is one-sided - a primary `lq` with eight ready candidates,
all of them lower pipe.

That points somewhere new: **a row that is going out half empty is a free choice**, so it
can be spent on the next row by preferring a candidate that unblocks an instruction of the
other pipe. Implemented, and it is the first thing in this whole effort to move the clip
programs to parity:

| | SCE | openvcl before | with the re-pick |
|---|---|---|---|
| `clip_c` | 258 | 260 | **258** |
| `clip_tc` | 270 | 272 | **270** |
| `cull_c` | 176 | 174 | 178 |
| `cull_tc` | 182 | 182 | 186 |
| `cull_tce` | 168 | 174 | 176 |
| set | 2028 | 2048 | 2052 |

**The clips reach SCE exactly and the culls lose more than the clips win.** Two ways of
restraining it were measured: requiring the replacement to score no worse by the ordinary
metric makes it never fire (2048, unchanged - the same reason a tie-break never fires), and
restricting it to the primary's own pipe changes nothing, because the candidates it picks
were same-pipe already. The mechanism is right and its scope is wrong - and the answer turned out to be **not to
pick a scope at all.**

### `--pair-best-of-two`: schedule it both ways and keep the shorter

Every hand-picked scope failed. Requiring the replacement to score no worse never fires.
Restricting it to the primary's own pipe changes nothing, because the candidates were
same-pipe already. "Only in loop bodies" needs a signal the scheduler does not have, and
plumbing one through three layers to encode a guess is the wrong shape of fix.

So the segment is scheduled **twice** - with the re-pick and without - on copies of the
latency tracker and cycle counter, and whichever came out shorter is re-run for real. Three
passes over a segment instead of one, in an assembler that finishes all 25 programs in
seconds.

| resident VU1 set (ceiling **2042**) | SCE | openvcl |
|---|---|---|
| `stapip_cull_c` | 176 | **174** |
| `stapip_cull_d` | 146 | **142** |
| `stapip_cull_td` | 156 | **148** |
| `stapip_cull_tc` | 182 | **182** |
| `stapip_cull_tce` | 168 | 174 |
| `stapip_clip_c` | 258 | **258** |
| `stapip_clip_d` | 206 | 210 |
| `stapip_clip_td` | 220 | 224 |
| `stapip_clip_tc` | 270 | **270** |
| `stapip_clip_tce` | 246 | 256 |
| **total** | **2028** | **2038 — it fits, with four words spare** |

Off by default: it reorders a cyclic prefix that `test_software_pipeline.cpp` pins down.
Upstream's 419 stay green and the image's `vcl` wrapper passes it, like the other eight.

**Verified with the whole ten-program VU1-clipper set built by openvcl and no harness:**

| scene, `clipping: "vu1"` | result |
|---|---|
| `vclab` | **0 of 514600 pixels** differ from Sony's build |
| `blocks-terrain` | **0** |
| `raytraced-mirror` | renders correctly (animates, so the verdict is visual) |

The last two were blank frames before this work. openvcl now builds every microprogram this
engine has, the resident set fits micro memory, and the frames are Sony's frames.

### `--pair-best-of-many`: a table of heuristics instead of two, and words instead of slots

`--pair-best-of-two` established the shape of the fix - do not pick a scope, schedule the
segment both ways and keep the shorter - and left the obvious question open: why two. A
third variant costs compile time and **cannot** lengthen the output, because a variant that
loses is discarded. So the two became a table.

**What to put in the table came from the instrument.** `--show-pair-misses` now also names
the pipe that went hungry (`primary=` / `starved=`), and aggregated over the ten resident
programs it says the deficit was never one phenomenon:

| program | openvcl - SCE, before | single-slot rows | `samePipe` | `notReady` |
|---|---|---|---|---|
| `stapip_clip_tce` | +10 | 484 | **313** | 114 |
| `stapip_cull_tce` | +6 | 367 | **267** | 82 |
| `stapip_clip_c` | 0 | 502 | 201 | **244** |

The two env/matcap programs, which between them carried the whole deficit, are
`samePipe`-bound: at the cycle the row goes out half empty *every* ready instruction wants
the slot already taken. No choice of partner fixes that - there is none. `stapip_clip_c`,
which already matched SCE exactly, is `notReady`-bound instead. One fixed heuristic was
being asked to serve two different shortages, and that is what a table fixes.

**Five knobs, swept, then trimmed to the winners.** The ready list's score is source order
plus a stall penalty, minus bonuses for long-latency producers, latency loads, pipe
alternation and critical-path height. Each became a strategy parameter, plus one that did
not exist before: fill the row's free slot with the **least** valuable legal partner rather
than the best one, leaving the valuable one to be a primary on a row of its own. A 26-entry
sweep over the ten programs found only four points that ever produced a shorter segment; a
second sweep of the two remaining dimensions found two more. The shipped table is those
seven, and nothing else:

| # | strategy | segments won | words saved |
|---|---|---|---|
| 0 | plain - what openvcl always did | - | - |
| 1 | the `--pair-best-of-two` re-pick | 7 | 7 |
| 2 | critical-path height at 1.5x | 5 | 5 |
| 3 | height at 3x, with the re-pick | 3 | 6 |
| 4 | stop hoisting divides and rsqrts ahead of everything | 1 | 2 |
| 5 | cheapest legal partner | 3 | 5 |
| 6 | cheapest legal partner, with the re-pick | 1 | 1 |

Trimming to the winners is **exact, not an approximation**: the minimum over a subset that
still contains every segment's argmin is the same minimum. The full 26 and these seven emit
the same words, in a fifth of the compile time.

**The comparison itself was also wrong.** `--pair-best-of-two` compared
`std::vector::size()`, which counts a multi-cycle NOP padding slot as one entry - the code
generator writes `min(emitCycleCount, cycleCount)` words for it, and exactly one for every
other slot, `waitq`/`waitp` included, because `emitUpperWithWait` folds the wait into the
same row. The table is compared on emitted words instead.

| resident VU1 set (ceiling **2042**) | SCE | openvcl before | openvcl now |
|---|---|---|---|
| `stapip_cull_c` | 176 | **174** | **174** |
| `stapip_cull_d` | 146 | **142** | **142** |
| `stapip_cull_td` | 156 | **148** | **148** |
| `stapip_cull_tc` | 182 | 182 | **180** |
| `stapip_cull_tce` | 168 | 174 | 172 |
| `stapip_clip_c` | 258 | 258 | **256** |
| `stapip_clip_d` | 206 | 210 | 210 |
| `stapip_clip_td` | 220 | 224 | 222 |
| `stapip_clip_tc` | 270 | 270 | **268** |
| `stapip_clip_tce` | 246 | 256 | 254 |
| **total** | 2028 | 2038 | **2026 - smaller than Sony's vcl** |

Six of the ten now match SCE or beat it, and what is left of the deficit is the two clip
programs the env macro feeds (`clip_tce` +8, `clip_d` +4, `clip_td` +2, `cull_tce` +4).
Off by default for the same reason as `--pair-best-of-two`, and it supersedes that flag when
both are given - its table's first two entries *are* that pair, so the minimum it takes is
over a superset and can never be worse.

**Verified:** `vclab` with `clipping: "vu1"` built by this openvcl renders **0 of 514600
pixels** different from Sony's build, with 0 asserts; all 25 microprograms compile and pass
`dvp-as`; upstream's 419 assertions stay green. Check the build log says the VU assembler
changed before trusting the picture - the flag string is what the Runner hashes, so a stale
microcode volume will otherwise hand you the previous build's frame.

The cost is compile time: eight trial schedules per segment instead of three. The ten
resident programs take roughly a minute each on this machine against a few seconds before,
which is paid once per microcode rebuild and not per game build.

**Dead ends, every one measured over the ten resident programs. Do not repeat these:**

- **Rewarding a candidate for unblocking an opposite-pipe successor.** This is the direct
  answer to a `samePipe` shortage and the reason the knob was built at all. At two units of
  critical-path height and at ten, with and without the re-pick, it **never once produced a
  shorter segment**. Making other-pipe work ready one cycle sooner does not help when the
  shortage is longer than one cycle - and in these programs it is a long run of one pipe,
  not a one-cycle dip. The instrument pointed at a real phenomenon and the obvious remedy
  for it is still the wrong one; what actually paid on the same programs was hoarding the
  scarce pipe (strategy 5) rather than trying to manufacture more of it.
- **Breaking ties toward later source order** instead of earlier - the sign of the score's
  source-order term. Never wins, alone or combined with any other knob.
- **The pipe-alternation bonus** at 0, 300 and 900 against its default 100: never wins.
- **The latency-load bonus** at 0 and 900 against its default 300: never wins.
- **Critical-path height** at 6, 10, 40, 100, 200 and 400: never wins. Only 30 and 60 do,
  which is why the table carries exactly those two and no sweep of that axis is left to do.

### Reading the batches instead of the totals

Two things made that possible. `--dump-vucap --full` prints every staged packet and every
vertex instead of the first four - the packet that differs is rarely the first. And
`--dump-vucap --peek <qw>[,n]` prints raw data-memory quadwords as floats and words, so an
instrumented microprogram can report its own intermediate values: **1016..1023 are free**
in the static pipeline's map, and code above `begin:` runs once per activation while
`begin:` loops per batch - which is the hook that turns those eight quadwords into a ring
with one slot per batch.

With `--full`, the divergence stops being a total and becomes two packets:

| staged packet | SCE | openvcl |
|---|---|---|
| gif 5 @VU1 72 | nloop=**24** | nloop=**21** |
| gif 13 @VU1 533 | nloop=**24** | nloop=**6** |

Every other packet matches. And the vertex data says the two fail differently: gif 13's
first six vertices are identical and then openvcl simply stops (peeking past its nloop
shows unwritten slots - zero colours), while gif 5 diverges at v3 onto stale memory.

The instrumented runs then answered the two questions that matter:

* **The GIF tag never lies.** Recording `outCount` next to a counter of what the fan
  emitter actually wrote gives `claimed == written` in both assemblers, every batch. So
  this is not a patched-NLOOP bug.
* **The tests are not being fed different numbers, nor answering differently.** Parking
  `sjudge` as `clipw` sees it, the raw `pd`/`cd`, and both `fcand` results in spare
  quadwords gives **bit-identical values in both** - `sjudge` = (-4141.83, -4151.66, 0,
  4096), `pd`/`cd` = (-45.835, -55.6648), both answers 1.
* **What differs is the polygon.** Per batch openvcl reports 21 where SCE reports 24 -
  and a fan over an n-vertex polygon emits n-2 triangles, so that is **exactly one
  polygon vertex fewer**, consistently.

One more measurement is in flight and its instrument is not yet trusted: summing the
polygon size after every plane of every triangle gives SCE 84 quadwords against openvcl's
252 and 216, which would mean openvcl's plane loop doing about three times the work while
emitting fewer vertices. That is either the finding or an artefact of the two extra
integer aliases the accumulator needs - four of them made **both** assemblers run out of
integer registers, which is worth knowing on its own about how tight `clip_c` is.

Two more mechanisms are out. **The wrap edge is not it**: re-loading the previous vertex
through `prevPtr` on every iteration - which makes the first edge's `ppos` explicit
instead of inherited from the plane prologue - leaves the counts exactly where they were
(24 against 17). And **the plane loop's head is equivalent**: openvcl initialises
`curPtr = srcBase` and `dstPtr = dstBase` inside `planeLoop`, on every entry, as SCE does
(SCE just reuses one register as a copy of `srcEnd` to address `ppos` at `-2`/`-1` before
it becomes `curPtr`).

Also settled, and it retires a whole line of suspicion: **the density flags are innocent.**
The two-program harness leaves enough micro memory to build `clip_c` with no flags but
`--loop-liveness-always`, and it stages the same 17/51. The defect is in openvcl's base
code generation.

### The sharpest statement so far: openvcl clips triangles that need no clipping

Counting plane-loop completions per batch needs two extra integer aliases, and - unlike
every richer instrument tried here - it perturbs neither build: SCE still stages 72/24 and
openvcl still 51/17 with the counter in place. So this one can be believed:

| per batch (7 input triangles, 6 clip planes) | SCE | openvcl |
|---|---|---|
| plane-loop completions | **12** | **42** |
| polygon vertices emitted while clipping | 42 | 126 |
| of those, vertices kept vs intersections | 34 / 8 | 110 / 16 |

42 is 7 x 6: **every triangle goes through every plane.** SCE's 12 is two triangles' worth
- it sends the other five straight to the fan, because `ibeq triOr, vi00, fanStart` fires
for them. The kept-to-crossing ratio is about the same on both sides (81% against 87%), so
the per-edge decisions are not obviously wrong; there are simply three times as many edges
to decide, because polygons that should never have entered the plane loop are in it.

A per-triangle read of `triOr` itself is the obvious next step and it is where the
instruments start lying: adding the two counters per triangle changed openvcl's staged
output to 39/13, and storing `triOr` before its branch changed SCE's to 48/16. **An
instrument that moves the thing it measures is not evidence** - which is itself the
recurring signature here, since openvcl's clip_c answers differently every time unrelated
instructions are added and SCE's never does.

Worth recording from the perturbed runs anyway, as a lead rather than a result: SCE's
`triOr` reads 1, 1, 0, 0, 0, 0, 0 across the seven triangles with `outCount` climbing
3, 9, 12, 15, 18, 21, 24, while openvcl's reads 1, 0, 0, 0, 0, 0, 0 with `outCount` stuck
at 0 for every triangle - which would mean the name is split across two registers under
that instrumentation, the accumulating one being invisible from `processing`. In the
unmodified program it is not split (`VI06` for both aliases), so this says more about how
tight clip_c's integer file is than about the defect.

An engine-side workaround for this class was tried and **rejected on measurement**:
carry the previous clip vertex through `prevPtr` (which the program already keeps for the
wrap-around edge) and re-load it, instead of copying it into registers at `edgeAdvance`.
It removes the collision by construction and needs no compiler change, but SCE's own
output grows - `clip_c` 262 → 263 rows, which rounds up to an even upload and pushes the
resident set 2042 → 2044. Moving the loads onto the crossing path in `edgeCross`, where a
divide's latency looked like free cover, is worse still (264 / 278). **The tool bug is
the tool's to fix**; a source change that breaks the assembler we still ship is not a
fix.

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
inside the same block to hide behind.

**That number was cycles, and it read as a second overflow for a while - it is not
one.** VU0 micro memory holds 512 instructions, and measured as *instructions* the
kernel is **469 under openvcl against SCE's 483**: it fits, with openvcl's copy the
smaller of the two. The cycle gap is still real and still worth closing, but nothing
here fails to load.

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

- the image label says `NOASSERTION`, not `Apache-2.0`;
- going public was gated on resolving `vcl` — that is, on the `openvcl`
  migration above. Which is what made that migration the thing that makes a
  public image clean, not a nice-to-have.

**That gate is now open**, and it is why the published image changed identity.
The image carrying Sony's `vcl` is no longer published at all: CI pushes only
`tyrax-toolchain-src`, which is built from the official ps2dev base and contains
no unlicensed binary. The inherited one stays in the repo as the A/B reference
and is built locally by whoever needs it — building a Dockerfile that consumes a
tool you already have is not redistribution. What still keeps the *package*
private is only the repository's own visibility, which is no longer a licensing
question.

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
arrives with the `openvcl` migration, not before.

**It has now landed, and is still not built by default.** The from-source image
below has nothing x86-only in it and its base carries an arm64 manifest, so
`platforms: linux/amd64,linux/arm64` works. The workflow publishes amd64 anyway
and takes arm64 through its manual `platforms` input, for three reasons worth
writing down rather than re-deciding: nobody here is on arm64, the second arch
is qemu-emulated on a GitHub runner so it more than doubles the job, and the
image checks cannot run it — `load: true` refuses a multi-arch result, so the
arm64 half would be built and never started. An unused, unexercised arch in
every push is a slower CI and a weaker signal at once.

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

## A second corpus, and what it said

Everything above was measured on the engine's **25 handwritten** microprograms. When the
VU authoring layer landed, a project that uses it generates **45 more** into
`examples/vu-lab/src/gen/`, and running both assemblers over them changed the verdict:

| | Sony `vcl` | openvcl |
|---|---|---|
| compile **and** assemble | **45 / 45** | **23 / 45** |
| smaller code, of those that did | 15 | 5 |

So "at least as good as SCE" held for the handwritten set and **did not hold** for the
generated one. Every failure was `Register allocation ran out of registers`.

The 22 split in a way that matters. Fourteen fail whatever flags are passed. The other
eight fail only with `--loop-liveness-always` — and **without** it they compile to silently
wrong code (`loopcarry.py` reports five clobbered registers in each), so openvcl could not
build them in any mode: with the flag it refuses, without it it lies. The honest number was
23, not 31.

**It is not a missing-spilling problem, and it is not a scheduling problem.** Allocation
runs *before* scheduling in openvcl, so none of the density flags can affect it - verified,
the failures reproduce with `--loop-liveness-always` alone. On `vu_script0_d`:

| | peak live floats | held constants | loop body |
|---|---|---|---|
| SCE | **31** (of 31 available - zero headroom) | 17 | 13 |
| openvcl | **33** | 17 | 16 |

Both hold the same 17 whole-program constants (`mvp[0..3]`, `lightMatrix[0..2]`,
`lightDirections[0..2]`, `lightColors[..]`, `gifSetTag`). The gap is entirely loop-body
temporaries, on a program that batches a whole triangle - `vertex1/2/3`, `normal1/2/3`,
`outputColor1/2/3` are live together by design. Two registers, on five lines out of 230.

**Two default-off flags closed half of it.** `--trim-uncarried-ranges` rebuilds an alias's
live range from its own accesses as per-component liveness *with holes*, instead of letting
the branch-state merge (`Dependency::depend` → `Alias::merge`) stretch the survivor across
the whole enclosing loop - which had given `outputColor1/2/3` a range of 192 lines for
values recomputed every iteration. `--coalesce-float-writes` ties a float write to its own
name's previous alias when that alias is dead from the write on, **with a retry** that
withdraws the added edges if allocation then fails.

Then **load sinking** closed the rest. `--sink-loads` splices a load token down the
`std::list<Token>` to just before its first reader; `--sink-loads-across-stores` lets it
pass a store through a different base register (SCE assumes the same - its
`vu_script3_d.vsm` puts `lq.xyz VF25,2(VI05)` at row 199, after three stores through VI07);
`--sink-loads-into-loops` lets it pass one loop header; and `--sink-loads-past-branches`
lets it land where every path out of its old position arrives. That last one needed
post-dominance, decided during the sink pass's own forward walk from a program-wide count
of how many branches name each label, because `VuBasicBlock` is a linear partition with no
edges and the allocator cannot reach it anyway. It is **not** applied speculatively:
allocation runs exactly as before and only retries with it on register exhaustion.

The enabling surgery: `Token::setLineNumber` exists now and the allocator's timeline is
re-derived from list position whenever the pass moves anything. `Line::number()` is
untouched, so the timeline and the line a diagnostic prints are two different things - and
must stay that way.

| | start | trim + coalesce | + sinking | 
|---|---|---|---|
| generated programs compiling | 23 / 45 | 34 / 45 | **45 / 45** |
| silent clobbers (`loopcarry.py`) | - | 0 | **0** |
| resident VU1 set | 2026 | 2010 | **2008** (SCE 2028) |
| upstream tests | 419 | 419 | **419** |
| default output | - | byte-identical | byte-identical, MD5-verified |

The 18 words off the resident set were a side effect, not a goal. All of the above was
re-measured independently of the agents that produced it (`scratchpad/final-verify.sh`,
written separately so a harness and the claim it verifies do not share a bug).

### The size gap on generated code, and what it actually was

With all 45 compiling, openvcl emitted **9506 words against SCE's 9264** - +2.6%, larger in
34 programs. Two plausible culprits were measured and both are innocent:

* **The six allocator flags did not cost it.** On the 23 programs that build under every
  configuration: ten density flags alone 4064 words, all sixteen **4064**. The four sink
  flags cost +30 and trim+coalesce buys 8 back; they net to zero.
* **openvcl was not scheduling less densely.** 1.183 real operations per row against SCE's
  1.181 - parity, and parity *before* any of this work. That is why `--show-pair-misses`,
  the instrument that found the previous size win, found nothing here.

The real answer came from counting opcodes rather than rows: openvcl was emitting **312
instructions SCE never emitted** - 93 `loi`, 65 `lq`, 55 `muli`, 45 `mulw`, 44 `addw`, 38
`addi`. They are dead. TyraX's VU layer writes a whole 4-component constant vector when the
body reads two components, and an `lq` for every quadword a description names. **SCE's `vcl`
does dead-code elimination; openvcl did none.**

`--drop-dead-writes` is a field-precise dead-write pass, iterated to a fixed point (deleting
`add.z k0, vf00, i` is what makes the `loi` above it dead) and deliberately conservative: no
control flow in the analysis, so a write dead on one path and live on another is never
touched; declared names, stores, `xgkick`, auto-incrementing loads, branches and delay-slot
carriers are all excluded; and every FMAC writes MAC, so implicit writes need a global "no
flag reader anywhere" test before a forward walk. Two extra invariants held across all 70
programs: the multiset of stores and `xgkick`s is unchanged, and so is the label sequence.

| | before | after |
|---|---|---|
| generated corpus | 9506 vs SCE 9264 (+242) | **9308 vs 9264 (+44, 0.47%)** |
| per program | 4 smaller / 34 larger | **17 smaller / 24 larger / 4 equal** |
| resident VU1 set | 2008 | **1988** (SCE 2028) |

### The wall: overlapping CLIP chains

After the dead code goes, openvcl emits **fewer real operations than SCE** - 10864 non-`nop`
slots against 10910 - in **more rows**, 9283 against 9240. None of the remaining 43 rows is
work. All of it is `nop nop` in front of a CLIP reader: 91 padding words against SCE's 48,
and +43 is exactly the row gap.

SCE pays the four-row CLIP window once per `_cl` program; openvcl pays it 3 to 11 times,
because SCE keeps two or three `clip`s in flight and openvcl cannot:

| | SCE | openvcl |
|---|---|---|
| a `clip` sharing a row with a CLIP reader | 12 | **0** |
| `clip`-to-`clip` distance 2 / 3 / 4 rows | 10 / 10 / 13 | **0 / 0 / 0** |
| `clip`-to-`clip` distance exactly 5 rows | 3 | 37 |

Two rules produce it: `vuTokenPairResourcesAreIndependent` refuses a `clip` and a CLIP reader
on one row outright, and `emittedRowsSinceClipWrite()` measures against the *nearest
preceding* `clip` rather than the one whose bits the mask selects. Both are the same
omission - the CLIP register is a 24-bit shift window of four 6-bit entries and neither the
scheduler nor the emitter models the shift, only "a `clip` happened N rows ago". Under that
model a second `clip` in flight is indistinguishable from clobbering the first, so refusing
it is the only safe answer.

Closing it means threading mask position and push count through the dependency graph, the
pair test, the latency tracker and `emittedRowsSinceClipWrite`. **Not attempted, on purpose.**
This is the area with the worst defect record in this whole migration - three separate
corrections to the positional CLIP window are in the history above - and it is exactly the
resident clip programs the size gate protects. Forty-three words out of 9264 do not justify
reopening it without hardware to check against.

> **Half of it came out anyway, and it was never the shift window.** The section above is
> still the right description of the *remaining* rows, and `vuTokenPairResourcesAreIndependent`
> is untouched, so the "a `clip` sharing a row with a CLIP reader: SCE 12, openvcl 0" row
> still holds and is now the binding constraint. But 22 of the 43 words were openvcl
> disagreeing with **itself** - see the next section. The verdict on the shift-window
> redesign stands; the number it was weighed against is now 22, not 43.

### 22 of those words were openvcl contradicting itself

Asking where the 43 rows *sit* rather than how many there are turned the whole question
over. Every one is in a **per-triangle** loop; not one is in the per-edge loop of the
Sutherland-Hodgman test, and in that loop - the only genuinely hot one - **both assemblers
pad identically**, 48 rows each on the generated corpus. There is nothing to win where
winning would matter. Weighted by loop nesting, the 43 static words are worth *negative*
cycles: openvcl pays about 4.7 cycles per triangle for clip padding and takes back 8-9 in
the fan emitter it schedules better. The resident set was already both smaller and faster
than SCE's before any of this.

What the same measurement did find is a contradiction inside openvcl.
`CodeGenerator::clipReadIsPositional` exempts **full-window** masks - `0x3FFFF`, `0xFFFFFF`,
the ones asking "is anything outside", which get the same answer from any window position -
while `VuLatencyTracker` held *every* clip reader four cycles behind its `clip` under a bare
`if( readsClip )`. The scheduler was the conservative one, so the emitter's exemption could
never pay: the reader had already been pushed away before the emitter looked. SCE agrees
with our emitter, not our scheduler - in `mcpip_cull_vu1` it puts
`clipw.xyz VF14xyz,VF14w | fcand VI01,262143` in one row.

| flag | effect |
|---|---|
| `--exempt-full-clip-masks` | the mask test lifted into `VuSchedulingRules` so the scheduler asks it too, instead of padding every reader regardless of mask |
| `--clip-exemption-best-of` | schedule the whole program both ways and keep the fewer words, ties to "off" |

The second exists because the first is not free. Freeing a reader reshuffles the list
schedule downstream, and there `stapip_cull_d` and `stapip_cull_td` each came out a row
worse while `stapip_cull_tce` came out a row better - two words on the resident set, 1988 →
1990. **Scheduling noise, not the exemption**, which is why best-of removes it rather than
any change to the rule.

Best-of is deliberately **per program, not per segment**, and that is the load-bearing
decision: freeing a reader can never lengthen the segment it sits in, so a per-segment
best-of would take the exemption nearly everywhere and score itself a guaranteed win while
the rows it costs land in a *later* segment. Only a whole-program word count sees that, and
a whole-program count is what the size gate measures anyway.

| | SCE | 17 flags | + exemption | **+ best-of** |
|---|---|---|---|---|
| generated 45 | 9264 | 9308 (+44) | 9286 (+22) | **9286 (+22)** |
| engine 25 | 3982 | 3992 | 3982 | **3976 (6 under SCE)** |
| resident 10 | 2028 | 1988 | 1990 | **1986** |

The safety property is measured, not argued, over all 234 clip readers in the 70 programs:
**no positional reader moves at all** - not one ends up closer to its `clip` than the stock
scheduler put it - while 29 of the 78 full-window readers move adjacent to theirs. Loop-
weighted, the resident set is 48 cycles *faster* than the 17-flag build rather than 48
slower, `mcpip_cull_vu1`'s gap to SCE goes from +21.8% to +7.7% per block, and all 70
programs together come to -816 cycles.

The cost is compile time: 26 of the 70 programs hold a full-window reader and are scheduled
twice, the other 44 are skipped by a token-list predicate because with no such reader the
second schedule is provably the first. **+7%, about 8.5 s on a full microcode rebuild.**

Two things settled on the way and worth not re-deriving. A variant giving full-window
readers a 1-cycle floor instead of no constraint emits **identical** output, so none of this
is about same-cycle issue. And a comment in the patch claimed SCE never puts a positional
read closer than three rows to its `clip`: **false**, 89 of them are closer, including four
`fcand VI01,63` at 1, 0, 3 and 2 rows in `stapip_billboard_c_vu1`. SCE can do that because
it pairs a reader with the `clip` several rows back rather than the nearest one - it models
the shift window. The comment is corrected; nothing changed behaviour on the strength of it.

Still open and deliberately not taken: `stapip_billboard_c/t_vu1` is +11.7% per batch and
its readers are positional (mask 63), so only lowering `vuClipFlagSchedulingLatency()` below
4 would help - and nobody can prove the hardware number. PCSX2's VU core would accept any
latency, so a green e2e there is evidence of nothing.

Dead ends worth not repeating:

* **Copy coalescing "on `move`" is the wrong target.** The 45 programs contain **57**
  `move` instructions and **3673** two-address self-updates (`op d, d, s`).
* **Coalescing without the retry is a regression**: 26/45 on the generated set but it broke
  `vu0_rt_kernel`, which compiles fine without it. The chain pre-pass treats coalescing as a
  *requirement* - one register free over the union of all members - so a long chain placed
  early starves a later one.
* **Trimming without hole punching** (clip to `[first,last]`) was worth 30 alone / 33
  combined against 31 / 34 with holes.
* **A better colouring order cannot fix the remaining 11.** Each exceeds 31 live *ranges*
  at its peak (32, 33×5, 34×4, 37). The allocator is no longer the bottleneck there.

What those 11 needed was **load sinking**, which SCE does and openvcl could not, because
it allocates before it schedules. In SCE's `vu_script3_d.vsm` the loads land at rows
50, 76, 126 and 195 - the one at 126 reusing the register `vertex1` had just vacated - so
SCE never holds all six vertex and normal registers at once. Pulling each single-`lq`-defined
range forward to its first read (`scratchpad/ra-sinkpeak.py`) brings **8 of the 11** under
31; three would still be over, by 1, 3 and 4. The obstacle is that `Token::m_line` is a
`const Line&` with a cached number and the allocator uses that number as its timeline, so
moving a token breaks the ordering everything depends on.

## Building it from source instead

[`docker/Dockerfile.fromsource`](../docker/Dockerfile.fromsource) assembles the
same toolchain from the official `ps2dev/ps2dev` base instead of inheriting the
2022 `h4570/tyra` layers. `docker\build.ps1 -FromSource` / `docker/build.sh
--from-source` builds it; the editor picks between the two per machine in
*Edit > Preferences > Build toolchain*.

The base is **pinned by digest**, not by tag, the same way the inherited one is.
Not because it is old - it is three months old against the inherited image's four
years (Alpine 3.23, GCC 15.2, binutils 2.45, newlib 4.6.0), and it is multi-arch
including **arm64**, which the inherited image cannot be while it carries
`qemu-i386` to run Sony's `vcl`. The pin is because `ps2dev/build-all.sh` clones
its sub-projects at their tips, so a tag can move under us; a digest cannot.
Rebuilding that toolchain ourselves would buy only the pinning we get from the
digest, and cost an hour per CI run plus ownership of GCC and binutils bugs on
MIPS R5900.

**What it removes:** the last binary with no source in the loop. What is left is
GCC and binutils (GPL-3.0-or-later, so a public push carries their source-offer
duty), PS2SDK and openvcl (AFL-2.0), vclpp (MIT) and ps2link.

**`vclpp` was never a blocker**, contrary to the assumption that it was a second
`vcl`. h4570's own Dockerfile builds it from
[glampert/vclpp](https://github.com/glampert/vclpp) (MIT, one `.cpp`), and today's
HEAD emits `.vsm` byte-identical to the 2022 binary on all 70 programs - it only
stops writing trailing whitespace.

### What the base actually cost

Seven obstacles, and they are three different kinds of thing:

**The base is incomplete for compiling** (three). `libstdc++` for the two VU
tools, and `gmp`/`mpfr4`/`mpc1` - GCC's OWN runtime deps, without which `cc1plus`
cannot start - plus `make` and `rsync`. Each surfaces as a hundred lines of
"symbol not found" or "Error loading shared library", which reads like a broken
image rather than a missing package.

**Upstream removed a tool** (one). `bin2s` turns an IRX into an assembly blob with
`<name>_irx` / `size_<name>_irx` symbols, and `Makefile.base` embeds ten modules
that way. ps2sdk deleted it in `8dafdfde` ("Remove bin2s and bin2o in favor of
bin2c"), which is in v2.0.0. `bin2c` is not a drop-in - it emits a C array, so
switching means changing every extern declaration and call site in the engine,
which is an upstream Tyra change. The tool is rebuilt from the commit before its
removal instead; still AFL-2.0 ps2sdk source, just no longer shipped.

**Our own assumptions** (one). The Runner installed the container-side C++
compiler with `apt-get`, which Alpine does not have - and the failure surfaced as
"A VU source failed to build", sending the reader into their own C++. It asks the
image now.

**Real engine bugs the old image was hiding** (three), and these are the argument
for doing this at all:

* `renderer_3d_pipeline.hpp` used `std::function` without including
  `<functional>`. GCC 11 reached it transitively; GCC 15 does not.
* `FileUtils::fromCwd` concatenated the working directory with a file name and
  relied on `getcwd()` returning a trailing separator. The stock image's ps2sdk
  returns `host:/dir/bin/`, a current one returns `host:/dir/bin` - so every path
  became `.../binlivepad.bin`, and PCSX2 refused it with *"Denying access to path
  outside of ELF directory"*, which points at the emulator's sandbox rather than
  at a missing slash. The game could open no file at all, including its own log.
  `getCwd()` also ignored `getcwd()`'s return value, handing callers stack
  garbage on failure.
* `RendererCoreTexture::unregisterAllocation` searched for a texture id and, if
  it was not registered, erased at an **uninitialised** index - a corrupted
  vector or a crash from a caller that merely asked twice. GCC 11 did not see
  it; GCC 15 does, and `-Werror` turns that into a build failure. It now erases
  inside the loop and warns instead.

### audsrv: the one dependency that had to be ported, not copied

The image cannot just carry the committed `vendor/tyra/audsrv/bin/libaudsrv.a`.
It is built by the stock image's GCC 11.3 and carries that compiler's LTO
bytecode, which GCC 15.2 refuses outright - and one committed artifact cannot
serve two toolchains. Nor can the fork simply be dropped: the engine calls
`AUDSRV_ADPCM_CH_CORE`, `_CH_VOICE`, `_CHANNELS` and `_FORCE`, none of which
exist in a stock audsrv, so without it the engine does not **compile** - never
mind losing positional audio and the second SPU2 reverb unit (`docs/reverb.md`).

So the EE half is compiled inside the image, from the same sources, by the
compiler that will link it. What made that a port rather than a copy is a
rename: upstream moved ten EE-side SIF RPC entry points to `sce`-prefixed names,
and the two SDKs export **disjoint** sets - measured, not assumed.

| | `SifCallRpc` | `sceSifCallRpc` |
|---|---|---|
| `h4570/tyra`'s ps2sdk (2022) | in `libkernel.a` | absent |
| a current ps2dev ps2sdk | absent | in `libkernel.a` |

`vendor/tyra/audsrv/ee/src/sif-compat.h` aliases the ten, gated on
`TYRAX_PS2SDK_SCE_SIF`. The switch has to come from **outside** the compile and
that is not laziness: the module is always built against the pinned old ps2sdk
source tree, so every header the preprocessor can see is the old one whichever
image is building - only the link target differs. (`__has_include(<sifrpc-common.h>)`
was tried and is exactly that trap: the file is present in a current image and
still invisible to this compile, so it silently chose the old names and the game
failed to link.) `Dockerfile.fromsource` sets the variable because it is the one
place that knows which ps2sdk the result will be linked against; the inherited
image sets nothing and gets the old names, unchanged.

Only the EE half is rebuilt. `audsrv.irx` is an IOP module the engine **embeds
as data** (`bin2s` → a blob in `libtyra`), so no EE compiler links it and the
committed one is toolchain-agnostic - which also sidesteps the IOP compiler
having been renamed (`mipsel-ps2-irx-gcc` here, `mipsel-none-elf-gcc` on a
current base). That last prebuilt artifact is the open item in `docs/backlog.md`.

The Runner's overlay is now skipped by asking the image whether it already
carries the fork (`grep AUDSRV_ADPCM_CH_CORE` in its `audsrv.h`) rather than by
probing whether the committed library links - otherwise it would put the other
toolchain's artifacts straight back over the ones the image built.

### Verified

| | inherited (`h4570/tyra`, GCC 11.3) | from source (`ps2dev`, GCC 15.2) |
|---|---|---|
| engine programs | 25/25 | **25/25** |
| generated programs | 45/45 | **45/45** |
| resident VU1 set | 1986 words | **1986** (SCE 2028, ceiling 2042) |
| engine corpus, all 25 | 3976 words | **3976** (SCE 3982 - openvcl is 6 under) |
| generated corpus, all 45 | 9286 words | **9286** (SCE 9264 - +22, 0.24%) |
| microcode, all 70 | - | **byte-identical to the inherited build** |
| `examples/vu-lab` | builds, runs | **builds, runs, 0 assertions** |
| VU1 packet on the sampled flush | - | **identical to both the inherited build and Sony's** |
| audsrv | committed `bin/`, overlaid at container start | **EE half compiled in the image, `sce`-prefixed, fork intact** |

The audsrv row is checked, not assumed. `audsrv_init()` sits under a
`TYRA_ASSERT` and is one of the calls the compat header rewrites, so a game that
reaches its render loop has already completed the EE→IOP RPC handshake with the
fork's own IRX - a mismatch halts on-screen instead. `vu-lab` boots to the scene
with 0 assertions, and the log shows the Runner reporting *"Image already carries
the TyraX audsrv - skipping the overlay."* Both spellings were verified in both
directions: without the define the build emits `SifCallRpc`/`SifSetDma` and links
on the old SDK, with it `sceSifCallRpc`/`sceSifSetDma` and links on the current
one. `SifAllocIopHeap`/`FreeIopHeap`/`InitIopHeap` kept their names upstream and
resolve from `libkernel.a` either way, which is why the header lists ten and not
thirteen.

## Still open

- **The GHCR package is private** until the repo is, so nobody outside can pull
  the image yet; the workflow pushes it regardless (`GITHUB_TOKEN` can). This is
  now the *only* thing between the from-source image and being the default for
  every generated project — everything technical is done and measured.
- **`docker/Dockerfile` has no CI coverage** now that it is the unpublished
  reference. Breaking it would be found by the next person doing an A/B, which
  is exactly the moment you do not want to be debugging a Dockerfile. Cheap
  insurance if it ever bites: a build-and-check-only job for it, on
  `pull_request` alone.
- **`vendor/tyra`'s audsrv overlay is still applied at container start** on the
  *inherited* image, by the Runner rather than baked in, because `vendor/` is
  fetched by `deps.*` and is not in that image's build context. The from-source
  image bakes it (its `.dockerignore` lets exactly those paths through) and the
  Runner detects that and skips. Baking it into the inherited image too would
  remove one first-build step; the stamping logic in `src/runner.cpp` explains
  why it is cheap as it stands.
