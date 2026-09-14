# cloth-curtain

The **cloth / soft body** demo ([docs/cloth.md](../../docs/cloth.md)): a stone
doorway with a curtain hanging in it, a banner on two hooks and a pennant on a
pole, all simulated every frame on VU0. Walk forward and the curtain lifts
around you; turn back and it is still swinging.

## What's in the scene

- **`curtain`** — an 11x13 sheet pinned by its **top edge**, filling the
  doorway. 143 particles, 240 triangles, still air. This is the one to walk
  through.
- **`banner`** — a 9x11 sheet on its **two top corners**, beside a pole, in a
  9 units/s² gust bearing 15°. It never stops moving.
- **`flag`** — a 9x7 sheet pinned down its **left edge** on a pole, light
  gravity and a hard 16 units/s² gust straight down +Z.
- **`door-left` / `door-right` / `door-lintel`** — the frame the curtain hangs
  in, so the pass-through has somewhere to be.
- The Player starts six units back, facing the door.

Three pinning modes in one scene on purpose: *what a sheet is attached to* is
the setting that changes its whole character, and it is the first thing worth
trying on your own cloth.

## Things to try

- **Walk through the curtain.** It lifts around you and keeps swinging. Then
  turn round and watch it settle.
- Select the curtain and drag **Player radius** (Properties > Cloth). At 0 you
  pass straight through and it never moves; at 2 it billows off you from a
  metre away.
- Drag **Stiffness** to 1 and watch the sheet stretch under its own weight;
  drag it to 4 and it goes taut.
- Drag **Damping** to 0 — the curtain never stops after you walk through it.
- Move the banner's **pole and cloth together** with the gizmo: the pinned
  corners follow the object live while the fabric swings behind.
- Give the curtain a material with a `map_Kd` — the UVs run 0..1 across the
  whole sheet.

## Cost

Debug profile with the **frame profiler** on, so the HUD prints a `CLOTH`
phase: the solver's own EE time, measured rather than inferred. In PCSX2 this
scene reads `FRAME 20.01` / `CLOTH 1.58` ms for its three sheets and 305
particles at a locked 50 FPS, and `CLOTH 1.43` ms at 60 Hz with the project
switched to NTSC — one solver step per displayed frame in either region
(docs/cloth.md, "Frame rate and region"). Those are emulator numbers and do not
transfer per function — take them on hardware before quoting them.

## Build & run

Open the folder in TyraX and press F5, or headlessly:

```bash
build/tyrax-editor --build examples/cloth-curtain --run
```

```powershell
build\tyrax-editor.exe --build examples\cloth-curtain --run
```
