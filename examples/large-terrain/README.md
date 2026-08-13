# large-terrain example

A 2048×2048 world that never fits in the PS2's 32 MB all at once, kept
playable by [terrain chunking](../../src/templates.cpp) and view-distance
streaming — plus a crowd of animated models to stress the EE.

Open `large-terrain.tyra` in the editor and Build & Run (`F5`), or build
headless: `tyrax-editor.exe --build <this folder> --run`. It ships in the
**debug** profile so the on-screen **FPS** and free-**MEM** readouts are
visible (top-left). Walk with the left stick, look with the right.

## What it shows

- **Chunked, streamed terrain.** The heightmap is cut into 16×16-cell tiles
  built on demand; with **View distance 100** (Preferences > Terrain) only
  the ring of tiles around you stays in memory, so RAM is flat no matter how
  far you walk. Distance **fog** hides the ring edge. Terrain detail is 512
  (a 4-unit grid over the whole 2048-unit map), sculpted into hills, a ridge,
  a crater and a valley.
- **~1100 scattered props** (trees, rocks, pillars) with a per-object
  **draw distance** — the cheapest LOD there is: far props are simply not
  submitted.
- **80 animated "wobbler" models** (skeletal `.glb`) spread across the map,
  each looping its `Wiggle` clip. They exercise the animation LOD chain:
  **Animation LOD 30** (distant instances re-pose every 2nd/4th frame) and
  **Mesh LOD 22** (distant instances draw a decimated mesh). Walk into a
  cluster to pile several full-rate skinned instances on screen and watch
  the FPS/MEM overlay respond.

## Notes

- The **MEM** readout barely moves as you roam: terrain ring, props and
  wobblers are all resident from the start (no per-object streaming here),
  and the chunk ring recycles its buffers in place. It only drops/climbs
  when a streaming **layer** loads or unloads — see
  [layer-streaming](../layer-streaming) for that.
- If the FPS/MEM overlay freezes the first frame on your PCSX2 build (a
  known emulator-only quirk), turn it off in Project > Preferences > Build;
  it renders fine on real hardware.
- [docs/animated-models.md](../../docs/animated-models.md) covers the
  skeletal models and their LOD chain.
