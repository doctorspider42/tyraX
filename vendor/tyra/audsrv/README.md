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

**TyraX changes so far: none.** The fork exists so that changing it is possible
at all — the previous arrangement shipped three prebuilt blobs and a recipe,
which meant every idea about audsrv started with "first, reconstruct the
sources". Any TyraX departure from upstream gets a `Modified by TyraX` comment
at the top of the file it touches, the same rule the engine fork follows.

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
