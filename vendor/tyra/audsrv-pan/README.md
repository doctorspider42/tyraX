# audsrv with per-channel L/R panning

The Tyra image (`h4570/tyra`) ships an older PS2SDK whose `audsrv` exposes only
`audsrv_adpcm_set_volume(ch, vol)` - a single mono volume that sets the SPU2
voice's left and right levels equally. Sound emitters therefore always played
dead-center, with no positional stereo.

Upstream PS2SDK added per-channel L/R support in commit `66ae317d`
("Implement positional audio in adpcm", 2023-01): the IOP `audsrv_adpcm_set_volume`
grew to `(ch, voll, volr)` and the EE side gained
`audsrv_adpcm_set_volume_and_pan(ch, volume, pan)` (pan -100..100). The build
system was later rewritten to require `srxfixup` / a newer toolchain
(`00f199ae`, 2025-01), which the image's toolchain cannot run.

These three artifacts are that newer audsrv, pinned to the last commit **before**
the build-system change so it still builds with the image toolchain, and they
carry the panning API:

| File | What |
|---|---|
| `audsrv.irx` | IOP module (embedded into libtyra via `engine/src/irx/audsrv.irx-em`) |
| `libaudsrv.a` | EE-side RPC stubs with `audsrv_adpcm_set_volume_and_pan` |
| `audsrv.h` | matching header (3-arg `audsrv_adpcm_set_volume` + `_and_pan`) |

`src/runner.cpp` overlays all three over the image's `$PS2SDK` copies at the
start of every build (before libtyra is (re)built), so the engine embeds this
IRX, links this EE lib and compiles against this header without any
include/link-order juggling. The overlay is idempotent and survives container
rebuilds because it runs every build.

## Rebuild recipe

Pinned PS2SDK commit: **`e78a9cb2ea816a72a7466000c51558fd2b57f5a7`** (`00f199ae~1`).

Inside the toolchain container (has `git` + network via the project Dockerfile):

```sh
git clone --filter=blob:none https://github.com/ps2dev/ps2sdk.git /tmp/pf
cd /tmp/pf && git checkout e78a9cb2
export PS2SDKSRC=/tmp/pf
cd iop/sound/audsrv && make CC=gcc            # -> irx/audsrv.irx
cd /tmp/pf/ee/rpc/audsrv && make CC=gcc       # -> lib/libaudsrv.a, include/audsrv.h
```

Then copy `audsrv.irx`, `libaudsrv.a` and `audsrv.h` here.
