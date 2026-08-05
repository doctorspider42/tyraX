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
