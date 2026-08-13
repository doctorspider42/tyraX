# audsrv — the TyraX fork

`audsrv` is the audio server every generated game links against and loads: it
streams the music, plays the ADPCM sound effects, and owns the SPU2's voices.
This directory is a **source fork** of the PS2SDK module, not a copy of a
binary — the sources are here, the build is one script, and the artifacts it
produces are committed next to them.

```
iop/     the IOP module (audsrv.irx) — the server itself
ee/      the EE-side RPC stubs (libaudsrv.a + audsrv.h) — what a game calls
bin/     the built artifacts, committed; what src/runner.cpp overlays
build.sh / build.ps1   rebuild bin/ from the sources above (Docker)
```

## Why a fork at all

The `h4570/tyra` image ships an older PS2SDK whose `audsrv` exposes only
`audsrv_adpcm_set_volume(ch, vol)` — one mono volume, setting the voice's left
and right levels equally. Sound emitters therefore always played dead centre,
with no positional stereo.

Upstream PS2SDK added per-channel L/R support in commit `66ae317d` ("Implement
positional audio in adpcm", 2023-01): the IOP `audsrv_adpcm_set_volume` grew to
`(ch, voll, volr)` and the EE side gained
`audsrv_adpcm_set_volume_and_pan(ch, volume, pan)` (pan -100..100). The build
system was later rewritten to require `srxfixup` / a newer toolchain
(`00f199ae`, 2025-01), which the image's toolchain cannot run.

So the sources here are upstream PS2SDK pinned at **`e78a9cb2ea816a72a7466000c51558fd2b57f5a7`**
(`00f199ae~1`) — the last commit that carries the panning API *and* still builds
with the image's toolchain.

Every TyraX departure from upstream gets a `Modified by TyraX` comment where it
happens, the same rule the engine fork follows.

## TyraX change 1: voices on BOTH SPU2 cores

Upstream puts every ADPCM voice on core 1 and leaves core 0 muted. That caps
the effects at 24 voices, and — the reason this change exists — leaves the
SPU2's **second reverb unit** unreachable, because a reverb is per core and
only the voices on that core can feed it. One reverb unit is why reverb zones
cannot cross-fade between different presets (see `docs/reverb.md`).

- `iop/src/adpcm.c` — channels are now `0..47`. **0-23 are core 1's voices,
  exactly as before**, so every existing caller (and audsrv's own "channels
  16-23 are the sound emitters" convention) behaves identically; 24-47 are core
  0's. `ADPCM_CH_CORE` / `ADPCM_CH_VOICE` are the mapping, the allocator fills
  core 1 first and spills to core 0, and `audsrv_adpcm_init` keys off and
  levels both cores' voices.
- `iop/src/audsrv.c` — core 0's master volume was pinned at 0 in
  `update_volume()`. Its output already reaches core 1 at full level (the
  `AVOL` set two lines above IS the core-0-into-core-1 volume), so raising this
  is the **entire** routing change. `cdrom.c` has always done the same for
  CDDA, which is the precedent that says it is safe.
- Both `audsrv.h` headers gain `AUDSRV_ADPCM_CHANNELS` and the
  `AUDSRV_ADPCM_CH_CORE` / `_CH_VOICE` macros, because a caller that talks to
  libsd directly — the reverb's per-voice send bits are per core — needs to
  know which core a channel is on.

Verified in PCSX2: with the sound emitters temporarily moved to channel 40
(core 0) the game still plays them at the same level, and — the useful half —
they arrive **dry** while a Hall reverb zone is active, because that zone
drives the core 1 unit. Moving them back restored the 450-500 ms tails. Two
independent confirmations that the bus/core mapping is real. Not yet confirmed
on hardware.

## TyraX change 2: playing over a busy channel

`audsrv_ch_play_adpcm` refuses a channel whose previous sample has not
finished (`-AUDSRV_ERR_NO_MORE_CHANNELS`). That is right for a caller asking
for *any* free voice and wrong for one that has PINNED a channel: pinning is
how a game says "this sound owns this voice", and the whole point of it is
usually that a new footstep, beep or gunshot **replaces** the previous copy
instead of being dropped. The refusal is a software check in `adpcm.c`, not a
hardware limit - keying a voice that is already sounding restarts it.

- `AUDSRV_ADPCM_FORCE` (0x40, in both `audsrv.h` headers) is ORed into the
  channel number. `audsrv_ch_play_adpcm` masks it off and skips the ENDX check
  when it is set.
- **The flag rides in the channel number on purpose.** That number already
  travels EE -> RPC -> IOP untouched (`call_rpc_2(AUDSRV_PLAY_ADPCM, ch, id)`,
  dispatched as `audsrv_ch_play_adpcm(data[0], data[1])`), so this needed no
  new export in `exports.tab`, no new RPC id and no signature change on either
  side. An added IRX export means touching the import list of everything that
  links the module.
- Channels are 0-47, so bit 6 is free. It is only honoured for `ch > 0`:
  channel -1 means "any free voice", and masking a negative number would turn
  it into a nonsense channel.

The engine side is `Tyra::AudioAdpcm::forcePlay` (plus `endedMask`, which reads
the SPU2's ENDX register so a caller can tell a finished voice from a busy one
without guessing). What uses them is documented in `docs/sound.md`.

**Open on hardware: whether a forced restart clicks.** It depends where in the
waveform the interrupted sample was, and PCSX2 does not model the SPU2 closely
enough to answer it. If it does click, the fixes in order are dropping the
victim's volume and playing on the next frame, or a KOFF plus the ADSR release
- both cost a frame of latency, so neither is worth paying before someone
actually hears the problem.

## TyraX change 3: one source tree, two PS2SDKs

Not a feature - the price of the fork outliving the image it was written for.
TyraX now has two toolchain images (`docs/toolchain-image.md`): the inherited
`h4570/tyra` with a 2022 ps2sdk, and one built from a current ps2dev base. The
committed `bin/libaudsrv.a` only serves the first (it carries GCC 11.3 LTO
bytecode, which a newer GCC refuses), so the second compiles the **EE half from
these sources** with its own compiler.

That crosses a rename. Upstream moved ten EE-side SIF RPC entry points to
`sce`-prefixed names, and the two SDKs export **disjoint** sets - measured, not
assumed: `h4570/tyra`'s `libkernel.a` has `SifCallRpc` and not `sceSifCallRpc`,
a current one has `sceSifCallRpc` and not `SifCallRpc`. So the sources compile
either way and then fail to LINK on the other one, with an *"undefined reference
to `SifCallRpc'"* that says nothing about SDK versions.

- `ee/src/sif-compat.h` aliases the ten. It writes the OLD names and maps them
  forward rather than the reverse, so the diff against upstream stays at one
  new header plus one `#include` instead of ten renamed call sites.
- It is included **first, before any ps2sdk header**. The aliases have to
  rewrite the DECLARATIONS too, and `kernel.h` carries two of them
  (`SifSetDma`, `SifDmaStat`); after it, the calls get renamed while the
  prototypes do not, which `-Werror` reports as an implicit declaration of
  `sceSifSetDma` rather than as an include-order problem.
- `ee/Makefile` turns `TYRAX_PS2SDK_SCE_SIF` in the environment into the define.
  It goes in the makefile and not on `make`'s command line because `Rules.make`
  does `EE_CFLAGS := ... $(EE_INCS) $(EE_CFLAGS)`, and a command-line
  `EE_CFLAGS` overrides that whole assignment - include paths included, which
  fails as *"compilation terminated"* on a missing header rather than as a bad
  flag.

**The switch has to come from outside, and it cannot be detected here.** This
module is always compiled against the pinned ps2sdk source tree above, so every
header the preprocessor can see is the old one whichever image is building;
what differs is the ps2sdk the result gets LINKED against.
`__has_include(<sifrpc-common.h>)` was tried and is exactly that trap - the file
is present in a current image and still invisible to this compile, so it
silently chose the old names and the game failed to link. The image that knows
which ps2sdk it ships is the one that sets the variable.

`SifAllocIopHeap`, `SifFreeIopHeap` and `SifInitIopHeap` kept their names
upstream and resolve from `libkernel.a` either way, which is why the list is ten
and not thirteen.

`build.sh` / `build.ps1` and the committed `bin/` are unaffected: they target the
old SDK, so they set nothing and get the old names, exactly as before.

## Licence: this module is LGPL v2, not AFL

Worth reading before you touch this, because it is **not** the licence the rest
of PS2SDK carries. Every audsrv file, IOP and EE alike, says:

> Copyright 2005, ps2dev — *Licenced under GNU Library General Public License
> version 2*

while ps2sdk's own `README`/`LICENSE` put the SDK under the Academic Free
License 2.0. Upstream contradicts itself (the file headers also say "Review
ps2sdk README & LICENSE files for further details", which points back at AFL);
TyraX takes the **stricter, more specific** reading — the per-file notice — and
treats audsrv as LGPL-2.0-only (there is no "or later" in those headers).

What that means in practice:

- **Forking and modifying it is explicitly allowed.** That is what the LGPL is
  for; no permission and no upstream PR are needed.
- **The modified library's source must be available.** It is, right here — which
  is one of the reasons the sources were vendored: shipping only binaries plus a
  rebuild recipe was a weaker position.
- **It is still not "publish your game" copyleft.** The *Library* GPL covers the
  library, not the application. It does ask that whoever receives a binary can
  relink it against a modified audsrv, which for a statically linked PS2 ELF is
  satisfied by this directory being public.

`THIRD-PARTY-LICENSES.md` and `LICENSE-EXCEPTION.md` at the repo root carry the
same split — the rest of PS2SDK is AFL 2.0 and attribution-only, audsrv is LGPL
v2 and asks for the library's source too.

## Rebuilding

```bash
./build.sh            # -> bin/audsrv.irx, bin/libaudsrv.a, bin/audsrv.h
./build.sh --check    # build and DIFF against the committed bin/
./build.sh --clean    # drop the work tree, force a fresh ps2sdk fetch
```

(`build.ps1` is the Windows twin and takes `-Check` / `-Clean`.)

The script fetches the pinned ps2sdk into `.work/` (gitignored), overlays these
sources over its `iop/sound/audsrv` and `ee/rpc/audsrv`, and builds inside the
`h4570/tyra` image. The overlay is needed because audsrv's Makefiles are
ordinary ps2sdk module Makefiles: they include `$(PS2SDKSRC)/Defs.make` and the
iop/ee `Rules`, so they only build inside a ps2sdk source tree.

**The ps2sdk it fetches has a mirror**, tried when the upstream fetch fails -
`doctorspider42/tyrax-vendor-ps2sdk`, the same arrangement and the same naming
`deps.sh` uses for every vendored dependency. Worth being precise about what
that protects, because the obvious fear is bigger than the real exposure:

- audsrv's own SOURCES are in this directory, so an upstream that disappears
  cannot take the module with it. That is the LGPL position above, and it is
  also the supply-chain one.
- games keep building either way - the toolchain and the installed SDK come
  from the `h4570/tyra` image, not from this fetch.
- what an upstream loss would actually cost is the ability to REBUILD audsrv
  from source: these Makefiles need a ps2sdk source TREE around them. The
  mirror is that tree.

A plain GitHub fork serves arbitrary reachable SHAs, so the mirror needs no
tyrax-specific tag - and if the pin is ever bumped, `gh repo sync` the mirror
first, or it will not have the new commit.

**Commit `bin/` in the same commit as any source change.** The artifacts are
committed rather than built per game because `src/runner.cpp` overlays them into
the build container on every build, and an IOP toolchain run per game build
would cost far more than it buys.

### What "reproducible" means here

`./build.sh --check` on the vendoring commit reported:

| | |
|---|---|
| `audsrv.irx` | **byte-identical** to what shipped before (it is linked `-s`, so nothing datestamped survives) |
| `audsrv.h` | **byte-identical** |
| `libaudsrv.a` | same members, same sizes; the bytes differ |

The archive is not bit-reproducible and does not need to be: `ar` stamps every
member with the build time, and gcc's LTO section names (`.gnu.lto_.profile.<id>`)
carry a per-compilation random id. Both were confirmed to be the *only*
differences — the object sizes match exactly, and they only match if the build
path does too, which is why the script pins it at `/tmp/pf` (the EE objects keep
debug info, so the path is inside the archive).

That is also the check to run after any source change: `audsrv.irx` and the
member sizes are what move when the code does.
