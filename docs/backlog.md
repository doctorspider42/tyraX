# Backlog

What is queued and what is being worked on. This is the forward-looking half of
the retired `PROGRESS.md`; the ~15 800 lines of finished-work entries that used
to sit alongside it are **not** lost - they are in git history:

```
git log --diff-filter=D --  PROGRESS.md      # the commit that retired it
git show <that commit>^:PROGRESS.md          # the whole file as it last stood
git log -p --follow -- PROGRESS.md           # or read it entry by entry
```

Finished work is now recorded where it is useful rather than in one growing
file: the commit message says what changed and why, the PR description carries
the verification, and any fact worth reusing belongs in the relevant
`docs/` page or skill.

## In progress

- Lighting-effects batch: dynamic point lights (done, 113), sun lens flare,
  god rays, dynamic light on animated models, visible beams, blob shadows.

## Queued (rough order)

- **Finish opt-in dynamic lighting per object** (branch
  `claude/gi-dynamic-lighting-wip`; docs/global-illumination.md is the
  surrounding design). An object may opt into being lit by the LIT VU1 program
  with its four light colours re-read from the probe grid every frame - the
  deal animated models already take - so it relights with zero latency,
  including while it spins. Entry (132) settled the three suspects the WIP
  commit left, plus two more nobody had listed, and established that the
  reported banding was a misread of the screen: the dyn-lit cylinder shades
  smoothly. What is left before this can merge:
  - **The owner has not seen it in their own scene.** Everything so far is a
    scratch fixture of primitives plus a two-material model.
  - **One probe sample per OBJECT, taken at its origin.** That is the whole
    point (it moves, so it cannot be baked), but it means a large object is lit
    as if it stood at its own centre, and an object whose origin sits inside
    geometry reads that occlusion over its whole surface. Decide whether that
    is the documented deal or whether big objects want a second sample.
  - **The ANIMATED-model path still reconstructs along the sun** (entry 133 did
    the dyn-lit one). `updateAndRenderAnimObjects` evaluates L1 along
    `SCENE_LIGHT_*` and every model shares one `animLightDirs`, so a character
    in a bounce-lit interior leans the same wrong way this just fixed. Same
    shape of fix: per-model directions plus the dominant-L1 direction. Kept
    separate because it touches every animated model in every project.
  - **A textured dyn-lit part is untested.** `litScale` handles the 128 vs 255
    split by construction, but no fixture has exercised it.
  - Then: a README bullet, a docs/global-illumination.md section, and an
    example (or a dyn-lit prop dropped into `examples/gi-showcase`).

  **The two engine-level facts this work established** are worth keeping
  whatever happens to the feature - both are now in
  docs/global-illumination.md's trap list: the untextured/textured colour-space
  split for a lit bag, and `StaPipVU1Cull_D` never reading the colour bag at
  all (the albedo must be folded into the light colours).

- **Session internet exposure** — today sessions are LAN (or any mesh VPN:
  Tailscale/ZeroTier make remote peers look local, zero code). The researched
  built-in options, in preference order: a Cloudflare quick tunnel
  (`cloudflared`, free, no account, random URL per session = invite link;
  needs a WebSocket `wire::Transport` impl - client side via native WinHTTP,
  no OpenSSL), playit.gg (free TCP tunnels, account required), UPnP
  (miniupnpc, best-effort, dies on CGNAT). The `wire::Transport` interface is
  the only integration point - protocol/session code never sees sockets.

- Hands-on pass over the Flow Graph editor UX (needs a human with a mouse)
- Object physics vs objects (stacking), player physics polish (pad feel)
- Model picking uses the unit-box approximation (big models pick imprecisely -
  the parser now exposes the real AABB, the viewport pick could use it)
- HUD images draggable directly in the viewport
- Positional audio (volume falloff by distance to an object)
- Compressed music streaming (SPU2-native ADPCM/VAG, ~3.5:1 vs 16-bit PCM) -
  needs a custom double-buffered SPU RAM streamer in the engine; audsrv only
  streams PCM and plays ADPCM one-shots
- Flow graph: more nodes (timers with reset, variables)
- Animations: measure VU0 skinning (entry 34) on real hardware once ps2link
  is installed - PCSX2 prices COP2 ops like FPU ops so it can only show
  parity; the SIMD win (one FMAC = 4 lanes) is a hardware-only effect.
- Animations stage 3 (optional) - VU1 skinning microprogram: matrix palette
  in VU memory, weights in the vertex stream, new VCL program (respect the
  vcl_sml.i history first). Measure EE headroom before starting - skinning
  now runs on VU0 in macro mode (entry 34) at ~37% EE load for 3 characters
  in PCSX2, so the EE is not the bottleneck yet. Palette per batch limited
  by VU memory (~24-32 bones).
- Engine perf, next target: the packager's per-frame package arrays are
  pooled now (entry 79 - measured worth only ~2%) and the per-package bbox
  classification is now the cheap object-space AABB test (entry 100 - 47->73
  FPS precise / 120 FPS vu1-clipping on the 98k PCSX2 benchmark, so the
  companion work below is DONE in PCSX2 terms); the real endgame is the
  engine author's own TODO in
  stapip_clipper.hpp - move clipping to VU1 entirely ("too much time").
  **Measured on real PS2 (2026-07-11**; clipbench: 128x128 terrain at detail
  128 = ~98k verts, spinning FPP camera, COP0 timers around `clipper.clip` +
  loop/pre-endFrame markers, 4 runs x ~2400 frames): the EE clipper costs
  **8.6-9.8 ms per frame (max 11.6 ms)** in "precise" mode - ~28% of the
  frame - at ~1.3 us per crossing-package vertex; ~52% of surviving packages
  crossed the frustum. The frame is **100% EE-bound**: with vsync off, EE
  work is ~33 ms and endFrame is 0.5 ms (GS/VU1 idle). But the same scene in
  "fast" mode still misses 50 FPS (EE ~26.5 ms, and it submits 841 vs 356
  packages because only the precise path classifies per package), so moving
  clipping to VU1 *alone* gets this scene to ~24.4 ms EE - still 25 FPS
  vsync-locked (~41 FPS with vsync off, up from ~30). Real payoffs: ~9-11 ms
  of EE headroom, scenes hovering just over the 20 ms budget flip 25->50,
  the sky dome / animated objects / particles (hardcoded fullClipChecks=true)
  come along for free, and "fast" mode's vanishing-triangle artifacts stop
  being the price of speed. Worth doing **together with packager pooling**
  to actually reach <20 ms on heavy scenes. Full design + milestones:
  `docs/vu1-clipping-plan.md`; PCSX2 undercounts the clip cost ~15-20%, so
  measure on hardware. Reusable instrumented scene:
  %TEMP%\tyra-editor-test\clipbench (terrain_game.cpp owns a perfTick() +
  auto-spin patch, codegen marker removed).

- **ps2link: the low build boots from FMCB when packed and small, but our reset
  then kills it (2026-07-31).** Not blocking: the default `ps2link.elf` (high,
  unpacked) boots from that menu *and* survives Stop, verified over four
  Run -> Stop cycles. This is about getting off the top-of-RAM address, which
  is only free until a scene allocates past ~31 MB.

  The original question - why our low build black-screened from the FMCB menu
  while the owner's stock `PS2LINK.ELF` booted - **is answered**. Everything
  below was flashed to the same console and booted from that menu; uLaunchELF
  boots all of them.

  | build | ELF | decompressed image ends at | FMCB menu |
  |---|---|---|---|
  | owner's stock release, Apr 2024, packed | 89 364 B | `0x000dea20` | yes |
  | stock upstream, packed by us | 103 364 B | `0x000dea20` | **yes** |
  | ours r4 `--no-usb`, packed | 104 164 B | `0x000df3a0` | **yes** |
  | ours r4 full, packed | 118 740 B | `0x000eb5a0` | no |
  | any of the above, **unpacked** | 233-285 KB | — | no |
  | ours r4 full, high, unpacked | 285 492 B | — | yes |

  Two independent causes, which is why single-variable theories kept failing:

  1. **Unpacked never works from that menu.** A raw ELF's PT_LOAD sits at
     `0x00094000` and the loader writes it over its own resident code. Upstream
     ships `ps2-packer`'d releases (entry `0x01d0001c`, one segment high in
     memory) that decompress down to `0x00094000` after the loader is done -
     which is what `make` gives us and `make ee` does not.
  2. **Packed still fails if the decompressed image reaches too far.**
     `0x000df3a0` boots, `0x000eb5a0` does not, so something FMCB keeps
     resident sits between them and our USB HID stack's ~50 KB is what pushes
     us over it.

  The new problem: **the packed low build dies on the r4 reset.** Stop kills
  the game and the console then answers nothing, not even ping - a full EE
  death, where the same reset on the high unpacked build restarts ps2link
  cleanly. Both run identical code at `0x00094000` once decompressed, so the
  suspect is `pkoReset()`'s closing `ExecPS2(&__start)` re-entering an image
  that a packer stub, not a loader, put there. Worth a screen-breadcrumb run
  (`pkoReset` narrates on the TV) before anything else.

  If that is solved, the low+packed+no-USB build is the one to ship: it boots
  from every launcher and leaves the top of RAM alone. The USB HID stack would
  then need loading from the card at runtime (`SifLoadModule` from next to
  `PS2LINK.ELF`) rather than baked into the image - which is also how it stops
  costing 50 KB of that window.

