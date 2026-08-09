# Frame pacing

Frame pacing is how a generated game pays for a frame that misses its vsync
deadline: with double buffering it waits out a whole second field and the frame
rate halves, and with triple buffering (*Project > Preferences > Build > Triple
buffering*) it presents that frame one field late instead and keeps the EE
working. This page covers what the cliff is, what the third buffer costs in GS
VRAM, and the measurements.

## The cliff

The PlayStation 2 presents on a vertical blank: 50 Hz in PAL (a 20 ms field),
60 Hz in NTSC (16.67 ms). With two buffers the engine cannot start the next
frame until the finished one is on screen, because the only other buffer is the
one being scanned out — so `RendererCore::endFrame` blocks:

```cpp
if (isFrameLimitOn) graph_wait_vsync();
gs.flipBuffers(isFrameLimitOn);
```

A frame that takes 20.4 ms on PAL therefore finishes 0.4 ms after a vblank,
waits **19.6 ms** doing nothing, and is presented at the next one: 40 ms per
frame, 25 fps. Half the machine is idle to pay for 2% of overrun, and the rate
can only ever be 50, 25, 16.7 or 12.5 — the staircase every PS2 developer knows.

## What triple buffering changes

A third display buffer breaks the dependency: the finished frame is **queued**
rather than presented, an INTC vblank handler latches it into `DISPFB` at the
next field, and the EE immediately starts drawing into the buffer the handler
just released. The pacing becomes "at most one frame in flight ahead of the
display" instead of "at most one frame per vblank of EE time".

Three states, and they are always distinct while a frame is queued — which is
exactly why the feature needs a third buffer and cannot be done with two:

| slot | who owns it |
|---|---|
| `displayedBuffer` | the GS, scanning it out |
| `pendingBuffer` | finished, waiting for the next vblank (-1 = none) |
| `context` | the EE/GS, drawing into it |

The queue needs **no interrupt masking**, and that property is worth keeping:
the handler only ever acts when `pendingBuffer >= 0`, so while the main thread
has waited for it to reach -1 the handler is inert and `displayedBuffer` cannot
move under it. `flipBuffers` therefore writes `context` first and
`pendingBuffer` last — that store is what hands ownership over.

The other ordering rule is the `draw_finish` handshake inside
`emitDrawTargetSwitch`: the GIF is in-order, so when FINISH comes back every
triangle of the finished frame has been rasterised. Queueing before that would
let the handler put a half-drawn frame on screen.

## The measurements

`examples`-scale fixture (fpp preset, interlaced-field, PAL), with a calibrated
COP0 busy-wait added to every frame so the frame time can be placed on either
side of the 20 ms field. The frame rate is computed **on the EE from its own
COP0 cycle counter**, so PCSX2's speed percentage cannot get into the number.

| extra load per frame | double (2 buffers) | triple (3 buffers) |
|---|---|---|
| none | 50.42 | 50.42 |
| 5 300 000 cycles (~18.0 ms) | **25.21** | **46.26** |
| 6 600 000 cycles (~22.4 ms) | **25.21** | **38.33** |

Read three things off that table. With no overrun the two are identical, so the
feature costs nothing when a game already makes its deadline. At ~18 ms of added
load double buffering has already fallen to exactly half while triple returns
**1.83x** the frame rate for identical work. And triple's numbers are simply
`1 / frametime` — 21.6 ms gives 46.3, 26.1 ms gives 38.3 — because rendering is
no longer quantised to the field; presentation still is, so some frames are
shown for one field and some for two.

What the table does **not** say is that the world runs smoother in every sense:
this is throughput, not latency, and a game at 38 fps is still a game at 38 fps.

## What it costs, and when the engine refuses

A third display buffer is a full one: **229 376 words at 512x448x32** (0.875 MB
of the ~1.08 MB texture heap), 114 688 in `InterlacedField`, 262 144 in
`Pal576i`. GS VRAM is 1 048 576 words total, so this is not a rounding error and
the mode decides whether the feature is possible at all:

| display mode | 3 buffers + z | left for everything else |
|---|---|---|
| `InterlacedField` (512x224) | 458 752 | 589 824 — comfortable |
| `Progressive480p` (448x448) | 802 816 | 245 760 — **fits**, tightly |
| `HiDef1080i` (448x540) | 983 040 | 65 536 — **refused** |
| `Interlaced` (512x448) | 917 504 | 131 072 — **refused** |
| `Pal576i` (512x512) | 1 048 576 | 0 — **refused** |

Two of the five have room, and 480p is the surprise: its 448x448 buffer is small
enough that three of them plus z leave 0.94 MB, of which about 0.56 MB survives
as texture heap. Booted and confirmed - `GS buffers: frame 448x448 x3`, no
refusal line, 0.51 MB free. So a project in that mode gets no warning because
there is nothing to warn about.

**"Does it fit" is the wrong question, and asking it that way is a boot crash.**
At 512x512 three buffers plus z are exactly the whole 4 MB: the allocation
succeeds and the *next* one fails, which is a live `Out of VRAM for post fx
buffers` assertion before the first frame. What has to survive the third buffer
is everything `RendererCore::init` still allocates after `gs.init` — post fx
(~12 288 words), the env-map target and its z (32 768), the camera feed and its
z (32 768), and the projected-shadow slots a game may claim later (~20 480) —
plus a texture heap worth having. `RendererCoreGS::allocateVramBuffers` checks
for `kThirdBufferReserveWords + kThirdBufferMinTextureWords` (384 KB + 256 KB)
of headroom and stays double buffered when it is not there, saying so through
`TYRA_SOFT_ERROR`. It is a refusal and not a warning on purpose: the alternative
boots into an assert or into a scene with no textures.

So in practice **triple buffering is an interlaced-field feature**, which is a
happy pairing rather than a consolation — that mode already halves the fill cost
per field, so it is the mode a game reaches for when it is fighting for frames
in the first place.

The one combination that puts it within reach at full 512x448 is the
[neural upscaler](neural-upscaler.md): BLSS sizes the z buffer from the *raster*
rather than the display buffer, which at 2x2 gives back 172 032 words — more
than enough to pay for the third display buffer. That has not been measured on
hardware yet (see [backlog](backlog.md)), and it comes with two corrections the
first version of this page did not have:

- **The upscaler's own low-res colour target is not in `getHeapWords()` when
  the check runs**, and is not in the reserve either. It is allocated *after*
  the layout (`blss.configure` → the VRAM rebuild → `blss.allocate`), and the
  384 KB reserve names post fx, the env map, the camera feed and the shadow
  slots — nothing else. So the headroom check subtracts it explicitly. At
  512x448 with the 1x2 raster that target is 114 688 words, which is the
  difference between a 576 KB texture heap and a 128 KB one, i.e. between a
  workable scene and one that asserts on its first big texture.
- **A project whose scenes disagree about the upscaler gets none of the
  saving.** Mixed resolution pins z at the full display raster
  (`setZRasterScale`, docs/neural-upscaler.md "Per scene"), so it pays for the
  low-res target *and* a full-size z. `project::tripleBufferingFit` therefore
  takes the whole `Project` and asks `project::blssUse`, not the project
  default — asking the default alone promised a third buffer to projects that
  cannot have one.

## Where it lives

- `RendererSettings::getTripleBuffering` / `getFrameBufferCount` — what the
  project asked for. `RendererCoreGS::getFrameBufferCount` is what it **got**;
  read that one, never the setting, when the answer matters.
- `RendererCoreGS::flipBuffers(bool throttle)` — both paths. With two buffers it
  is the stock present and `RendererCore::endFrame` has already waited for
  vsync; with three, the wait moves inside and blocks on a free buffer instead
  of on the display.
- `RendererCoreGS::onVblank` — interrupt context. Everything it touches must
  stay interrupt-safe: `presentFrameBuffer` is GS privileged-register stores and
  nothing else.
- `EngineOptions::tripleBuffering` — the game-side switch, emitted by codegen
  from `ProjectSettings::tripleBuffering`.

The buffer count reaches the game's `bin/log.txt` at boot, which is the first
thing to check when the feature seems not to be doing anything:

```
LOG: GS buffers: frame 512x224 x3, z 512x224 at 229376
```

`x2` there means the engine refused — the soft-error line above it says with
which numbers. The editor asks the same question before you ever
build: `project::tripleBufferingFit` is the host twin of that check, and the
Preferences checkbox warns in the dialog when the current display mode has no
room. **Change one, change the other** — the two agree on the reserve
constants by convention, not by construction.

## Limits

- **Not switchable at runtime.** The buffer count is decided when the permanent
  VRAM region is laid out. `RendererCore::setDisplayOutput` re-runs that layout,
  so a mode switch re-decides it, and a mode with no room silently drops back to
  two buffers — correctly, but the game is not told.
- **Unverified on real hardware.** Everything above is PCSX2. The INTC vblank
  handler and the `DISPFB` latch are exactly the kind of thing an emulator is
  friendlier about than a console, so this owes a hardware pass.
- **It does not help a game that is far over budget.** Doubling the frame time
  still halves the rate; what triple buffering removes is the *cliff*, the
  region between "just made it" and "missed by a hair" where double buffering
  costs a whole field for nothing.
