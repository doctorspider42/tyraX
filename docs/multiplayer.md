# Two-player games (shared screen & split screen)

> Working demo: [examples/two-players](../examples/two-players) — title-menu
> 1P/2P choice, split-screen third-person avatars, pad-2 hot-join.

TyraX games can host a second local player: both players on one screen with a
camera that frames the pair (**shared screen**), or each player with their own
view (**split screen**, P1 top / P2 bottom). Player 2 can join and leave while
the game is running.

## Authoring

1. **Preferences.** *Project > Preferences > Multiplayer > Two players*:
   - **Off** – single player; every 2P code path is compiled out.
   - **Shared screen** – one camera orbits the midpoint of the two avatars and
     stretches its boom with their separation. Both players should use
     third-person Player objects (a shared camera has nobody's eyes to look
     through).
   - **Split screen (top / bottom)** – the scene renders twice per frame, the
     top half from player 1's camera and the bottom half from player 2's. Any
     player mode works (walk/FPP, noclip, third person).
2. **A second Player object.** In each scene that should support two players,
   insert a second *Player* object (*Insert > Player*). Scene order decides the
   slots: the **first** Player object is P1, the **second** is P2 (the
   properties panel shows which is which). P2 has its own spawn position,
   movement parameters, and (for third person) its own avatar model and clips.
   A scene without a second Player object simply stays single-player.
3. **Joining and leaving mid-game** (both runtime, no rebuild):
   - *Start on pad 2* (Preferences > "Player 2 joins with Start on pad 2",
     default on): pressing Start on the second controller drops P2 into the
     scene at its authored spawn. The pad is opened in a hot-join-friendly way —
     the controller can be plugged in after boot.
   - *Menu option block*: the Menu Editor's **+ Option block > Player count
     (1P/2P)** inserts a ready-made Toggle row (backed by the `opt_players`
     save value, bind `player-count`). Cycling it to "2 Players" activates P2,
     back to "1 Player" removes them — the classic title-screen /
     pause-menu choice. The row and the Start join stay in sync (a pad-2 join
     updates the menu row's save value), and because the state lives in a save
     value it persists through memory-card saves.

## What the game does

- **Input**: player 1 keeps the engine pad (connector 1); player 2 reads a
  second `Tyra::Pad` on connector 2. The pad is *optional* — no controller
  never blocks or asserts, and the game keeps polling so plugging one in
  mid-session works (`Pad::initOptional`, `vendor/tyra` fork). **Connector 1
  behaves the same way now**: it used to spin forever waiting for a pad to
  settle, both at boot and on every frame, so a controller that was absent,
  unplugged mid-game or merely slow to report froze the game outright. Boot
  gives it a few seconds and then comes up without it; a frame without a pad
  reports a centered, silent one and keeps rendering; and the pad is adopted
  (full DualShock + pressure mode) the moment it does appear. It matters over
  ps2link, where a deploy can land seconds after `padman` was loaded and the
  pad answers `DISCONNECT` for a moment — that used to be a frozen Tyra logo.
- **Walkers**: both players run the same per-player walker
  (`updatePlayerWalker(PlayerCtl&, pi, pad)` in the generated
  `terrain_game.cpp`) — terrain bounds, object collision, gravity/jump,
  third-person avatar driving and per-player spring-arm boom all work per
  player. Per-player tuning comes from the `PLAYER_*` / `PLAYER2_*` scene
  tables in `scene_data.hpp` (selected via the `PP_*(pi)` macros).
- **Shared screen**: the frame camera orbits (P1's right stick) around the
  players' midpoint; the boom grows with their separation (`updateSharedCamera`)
  and is spring-armed against geometry like the single-player boom. P2's
  movement is relative to that shared camera (its right stick is unused).
- **Split screen**: the engine fork gains `RendererCore::splitView`
  (`vendor/tyra/.../splitview/`), a raster bracket modeled on the env-map
  redirect: per half it drains PATH1, shifts XYOFFSET so the *central* half of
  the normal full-screen projection lands on that half of the framebuffer (a
  vertical crop — per-pixel scale and proportions stay correct, no projection
  change), scissors to the half and clears its color + depth region. Between
  the halves the game swaps the camera with `renderer3D.update(cam2)` (view +
  frustum planes follow per mesh, so culling is correct per half).
- **Full-screen things stay full-screen**: HUD, menus, texts, post FX (bloom /
  grain / DoF) and cutscene overlays draw once over the whole frame. A cutscene
  camera override suspends the split for its duration (cutscenes own the whole
  screen). The dynamic env map (reflective materials) pauses its refresh while
  split rendering is active — its bracket would reset the half's raster state —
  so reflections keep the last rendered map during split play.
- **Scripts / flow graphs**: `ScriptContext` gains `player2Active` and
  `player2Position` (equal to `playerPosition` while P2 is inactive, so
  "nearest player" logic can read it unconditionally). Existing input-driven
  flow nodes (button triggers etc.) keep reading pad 1.
- **Scene switches**: P2 stays in the game across scene loads as long as the
  new scene has a second Player object; otherwise the game drops back to 1P
  (and the menu row follows). Teleports move P1 and bring an active P2 along
  a step to the side.

## Performance

Split screen renders the 3D scene **twice per frame**, so budget for roughly
2x the scene's EE cost (HUD/menus/post-fx stay single). What the runtime
already does for you:

- Animation playback and skinning run **once per frame**, not once per half —
  the second half re-submits the frame's skinned buffers under its own
  camera/frustum (`splitSecondPass` in the generated game). Without this,
  animations played at 2x speed in split mode and every avatar paid double
  skinning.
- The split brackets don't clear anything: beginFrame's full-screen clear
  covers both halves and the scissor clips z-writes, so no per-half GS fills.
- **No CPU stalls on the half switch**: the per-half XYOFFSET/SCISSOR shift
  rides the VIF1 stream as `[FLUSH, DIRECT]` - the VIF itself waits for the
  previous half's in-flight VU1 work and then streams the register writes
  through PATH2, so the EE never spin-waits in `splitView.begin()` (the first
  version paid three `draw_wait_finish` round-trips per frame). Only the
  final full-screen restore in `end()` stays a CPU handshake - the HUD that
  follows arrives over PATH3, which a VIF-queued write cannot order against.
- **Band culling**: the raster crop keeps the projection full-height, so the
  engine's frustum planes alone would let each half transform ~2x the
  geometry it can show. Two extra planes bound the half's visible vertical
  band, and terrain chunks / static objects wholly outside skip submission
  before the engine even classifies them (`computeSplitBand` in the
  generated game; margin-padded, disabled while looking straight up/down).
- **Terrain + layer streaming follow both players**: the chunk ring streams
  around two foci (P1's view and P2's avatar) — with a single focus the two
  passes would evict each other's terrain every frame and burn the whole
  build budget on churn — and auto-streamed layers load when *either* player
  enters the zone, unloading only when both leave. The chunk pool doubles in
  scenes that can host P2 (only with a view distance set).
- **Particle billboards face each half's own camera**: the simulation builds
  quads facing P1's view; the second half re-faces the same particles for its
  camera before submitting (`orientParticleQuads`), so big fire/fog sprites
  don't show up edge-on to player 2.
- **Static batching** (*Preferences > Rendering > Static object batching*,
  on by default): non-moving primitive objects sharing a material merge into
  combined world-space bags at scene load, so a cluster of decor pays the
  fixed ~1 ms per-bag submit cost once per batch instead of once per object
  — in both halves. Objects with physics, scripts, flow-graph references,
  save-state or a streaming layer stay individual; a batched object mutated
  at runtime (Live Link, Raycast-driven actions, global scripts) drops back
  to the solo path automatically. The boot log line
  `Static batching: N objects in M batches` shows what merged.

**Real-hardware numbers (2026-07-20, PAL, vsync off, the two-players example
at release + mesh LOD 4):** one view of the map costs ~9-11.5 ms of EE time,
so split lands at 20-30 ms (~35-50 FPS) - the halves are exactly 2x, with
the runtime's own overheads measured at: split raster brackets 0.04-0.14 ms
(the VIF-queued switch), beginFrame 0.55 ms, endFrame 0.53 ms, 2D/HUD
0.005 ms. The per-view sink on THIS map is the static-object loop: ~0.7-1.5
ms of fixed per-bag submit cost per object on the real EE (sky 0.5 ms,
terrain 1.0 ms, both avatars' skeletal pass 4.5-5.7 ms per frame - shared
across the halves). PCSX2's fast EE hides per-bag overhead almost entirely,
so budget split scenes against hardware, not the emulator: roughly
**(0.5 + N_bags x ~1 ms + anim) x 2 <= 20 ms**. Small maps with many
separate primitive objects were the worst case; the static-batching pass
above now collapses their N to the batch count, so the rule bites mainly
for objects batching must leave solo (scripted/physical/streamed ones and
models).

What's on you: vertex counts and object counts. A PC-grade avatar (an
early revision of the demo was tested with a 14k-vertex sample model
before switching to the two cats) skinned and submitted per half eats the
20 ms PAL budget fast - and so does a large count of separate small objects, through
the per-bag submit cost above.
The demo gets from 25 to a locked 50 FPS with two knobs — **mesh LOD**
(*Preferences > Rendering > Mesh LOD distance* ~4: third-person cameras sit
~5 units out, so avatars render their 50%-vertex variant nearly always) and
terrain detail 16. Measured in PCSX2 (software renderer, PAL): 1P scene
5.3 ms; split scene 19.2 ms -> 13.2 ms after the skin-reuse + LOD, whole
frame 18.4 ms = locked 50 FPS with vsync.

## Limitations (v1)

- Two players maximum (the two console pad ports; no multitap).
- The split is horizontal (top / bottom) only.
- HUD is shared — there are no per-player HUD elements yet.
- Depth of field uses a single focus for the whole frame; in split mode both
  halves share it. Consider disabling DoF in split-screen projects.
- Flow-graph button/stick nodes read pad 1 only.

## Testing

The harness recipe (see `tyra-testing`): a scratch project with two Player
objects, `"multiplayer": "split"`, and a pause menu carrying the Player-count
block round-trips through save/load and generates the split render path; in
PCSX2 the menu toggle can be driven from the keyboard (pad-1 bindings) to
flip between full-screen 1P and split-screen 2P without a second controller.
Pad-2 hot-join needs a second controller configured in PCSX2 (or real
hardware).
