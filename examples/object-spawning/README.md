# object-spawning example

Runtime object spawning driven entirely from a flow graph — clone an
authored object into the world on a timer and remove it again, the missing
piece for GTA-style traffic (spawn a few templates around the player,
despawn what falls behind).

Open `object-spawning.tyra` in the editor and Build & Run (`F5`), or build
headless: `tyrax-editor.exe --build <this folder> --run`.

## What to do

You spawn facing an empty patch of ground. Every few seconds an animated
**wobbler** pops into existence a few steps ahead, wobbles for three
seconds, then vanishes — on repeat. Nothing is placed there in the editor;
the wobbler you see is a live clone.

## How it is wired

The scene has three pieces:

- **`wobbler-proto`** — an animated `.glb` model sitting behind the spawn
  point. It is the *template*: never spawned itself, just cloned.
- **`spawn-here`** — an Empty marking where clones appear.
- **`spawner`** — an Empty carrying the flow graph:

  ```
  Every 5s ─┬─► Spawn Object (wobbler-proto)  ──[object]──┐
            │        ▲ position from Get Position(spawn-here)
            │                                             │
            └─► Delay 3s ─► Despawn Object ◄──────────────┘
  ```

  **Spawn Object** clones `wobbler-proto` at the marker's position; its
  object output is the **clone**, not the template, so the object link into
  **Despawn Object** removes exactly the instance that was spawned. The
  **Delay** gates the despawn to three seconds after each spawn.

Clones live in a fixed pool (32 slots) on top of the authored objects, so
spawning past the cap simply fails until a slot frees — no allocation at
runtime. A clone inherits its template's streaming layer, so unloading that
layer despawns it too.

See [docs/object-scripts.md](../../docs/object-scripts.md) for the flow
graph, and [docs/animated-models.md](../../docs/animated-models.md) for the
skeletal model.
