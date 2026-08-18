# layer-streaming example

Two buildings joined by a corridor, streamed GTA3-style with
[streaming layers](../../docs/streaming-layers.md): only the building you
are in (plus the corridor) stays in the PS2's memory.

Open `layer-streaming.tyra` in the editor and Build & Run (`F5`), or build
headless: `tyrax-editor.exe --build <this folder> --run`.

## What to do

You spawn inside **building A** (warm colors, crates and an orb). Walk
through the door into the corridor and keep going:

1. A third of the way in, an invisible trigger fires **Set Layer Loaded (load)
   building-b** — the far building streams in ahead of you, a few objects
   per frame, while you walk.
2. Near the far end another trigger fires **Set Layer Loaded (unload) building-a** —
   the building behind you vanishes from memory.
3. Walk back and the mirrored triggers swap them again. Endlessly.

To watch it, enable **Show FPS** and **Show memory usage** (Project >
Preferences > Build, debug profile): **MEM** drops on every unload and climbs
while a building streams in, and **FPS** stays at full rate — loads are
spread one asset per frame, so there is no hitch. (The overlay ships disabled
here while a known engine regression makes it freeze the first frame —
unrelated to layers.)

## How it is wired

- Two layers in the Project panel's **Layers** section: `building-a`
  (**start** on) and `building-b` (**start** off).
- Every wall and prop of a building is assigned to its layer in
  Properties; the corridor walls and the trigger markers have no layer,
  so they are always resident.
- Four **Empty** markers sit in the corridor, each with a two-node flow
  graph (`Near Object` radius 3.5 → `Set Layer Loaded`, load/unload pins), in
  walking order:

  ```
  [A] .. unload-B .. load-B ...... load-A .. unload-A .. [B]
  ```

  Whichever direction you walk, you first pass a no-op (unloading the
  building that isn't loaded), then the load for the building ahead, and
  only close to the far door the unload for the one behind you. Requests
  are idempotent, so walking back and forth needs no extra logic.

Buildings reset when they stream back in — position, colors, visibility
return to what was authored in the editor, like GTA3 interiors.
