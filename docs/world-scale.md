# World scale: units, meters, and imports that land at the right size

The engine has no unit. A "unit" is whatever a project decides it is, and the
generated game never learns otherwise — it just multiplies the numbers it is
given.

That is fine until something arrives **from reality**, because reality is
metric: a phone camera take records meters, and a model exported from
Blender/Maya/Mixamo carries meters (or a unit the importer normalizes to
meters). If the project was authored at, say, five units per meter, those
imports land five times too small, every time, and the fix is a scale factor
guessed by hand at each import.

**Units per meter** (*Project > Preferences > World*) is the one place that
conversion is written down. It is authoring metadata only — host-side,
never generated into the game, not part of undo — and it changes nothing on
its own: it does not move, rescale or reinterpret anything already in a scene.
It only tells the importers how big a meter is here.

The default is **1.0** — one unit is one meter — which is what every project
saved before this existed loads as, and what every importer assumed all along.

## How big is that, actually?

Two readouts answer this, and both speak units *and* meters:

- **Properties > Size**, under the Scale row of any object with an extent.
  Every primitive is a **unit shape** — a Box is a 1×1×1 cube around its
  origin, a Sphere is 1 unit across — so an untouched box at scale 1 is one
  unit on a side: one metre in a metric project, 20 cm in a 5 units-per-metre
  one. A Model's size is its own bounds times its scale. Flat shapes report a
  zero axis (a Plane lies in XZ, a decal/mirror/portal quad stands in XY).
- **The measuring tape** — the *Measure (7)* button in the viewport's tool
  row. Click a point, then a second one, and read the distance between them
  in units and metres plus the per-axis split (`dx dy dz` — the numbers you
  actually type into a scale or position field). The end follows the cursor
  until the second click, so the readout updates live; a third click starts
  over, **Esc** clears, the button or **7** leaves the tool. Both ends land on
  the same surfaces a pasted object rests on (object boxes and the terrain
  heightfield), so the tape measures the scene rather than an arbitrary plane.
  While it is active it owns the clicks — no picking, no rubber-band select,
  no gizmo.

## Which scale is my project actually at?

Nothing measures it for you; the world is whatever you built. Three readings
that usually settle it:

- Measure something whose real size you know: a doorway is ~2 m, a person
  ~1.7 m, a car ~1.5 m tall and ~4.5 m long. Distance in units ÷ size in
  metres = units per metre.
- Read the same thing off an object's **Size** row in the Properties panel.
- The **At this scale** line under the setting restates the FPP player in
  metric: eye height, walk speed and gravity. If it says the player is 0.36 m
  tall and sprints at 4 m/s, the world is ~5 units per meter.

## Walk speed, and why worlds drift off metric

That last reading is worth knowing about for its own sake, because the stock
FPP numbers used to disagree with each other. `eyeHeight` (1.8), `gravity`
(9.8) and `jumpSpeed` (4.5) are metric as written, but walk speed is stored as
**movement per 1/50 s** — the generated game's step unit. The old default,
0.4, is therefore **20 units/s**: 72 km/h for a 1.8-unit-tall person. Tune a
world until walking *feels* right at that speed and you end up several times
larger than metric, which is why "my world is 5 units per metre" is such a
common answer.

Two changes:

- **The default is now 0.1 = 5 units/s** — a brisk run in a metric project,
  and in the same ballpark as most first-person games (4–6 m/s). It applies to
  **new projects only**; anything already saved keeps the speed it was tuned
  at, because the value is stored in the project file.
- **The editor edits it in units per second**, with the metric equivalent
  beside the field, in both places it appears (*Preferences > FPP camera* and
  a Player object's own **Walk speed**). The stored per-step form is an engine
  detail and typing fractions of it was the actual complaint; the tooltip
  still shows the stored number.

If an existing project feels like a stagecoach, this is the field to look at:
set it to something like 5 units/s (or, at 5 units per metre, 25 units/s for
the same 5 m/s) rather than rebuilding the world.

## Models: the real-world size of an asset

A model asset can carry **how many meters one unit of the file measures**
(`Project::modelUnitMeters`, the `"modelUnits"` manifest section). The editor
asks for it right after an import, and the **Size...** button next to every
model in *Project > Assets* changes it afterwards:

- **Source units** — Meters / Centimeters / Inches / Custom. `.glb` and `.fbx`
  always arrive in meters (the importers normalize them), so those only need
  this when the source itself was modeled at the wrong size. An `.obj` carries
  no unit at all, which is exactly why the dialog asks.
- **Real height (m)** — the same number from the other end: type how tall the
  thing is in reality and the factor follows.
- The dialog shows what that means here: the size in world units and the
  object scale it produces.

An object created from that model is inserted at

```
scale = metersPerFileUnit * unitsPerMeter
```

(`Project::modelInsertScale`). No entry = scale 1, so procedural assets
authored in world units already — the Tree Generator's output — are untouched,
and a project left at 1 unit per meter behaves exactly as it did before.

Nothing rewrites the asset file: the size lives in the manifest, so it can be
corrected later without re-importing, and re-exporting the model from the DCC
does not undo it. Objects **already placed** keep the scale they were inserted
with (resizing an asset must not move a scene under you); the dialog offers a
one-shot "also rescale N object(s) already using this model" for when that is
what you meant.

## Camera takes

`CamTakeMapping::scale` *is* units per meter, so the import modal
(docs/camera-takes.md) seeds it from the world scale instead of 1, and the
decimation tolerance — a world-unit distance — scales with it. The modal
prints the recorded path length both ways ("4.80 m walked → 24.0 units") and
offers a **World scale** button to snap back if you tuned the field by hand.

## Adding something that reads the real world

Anything new that imports real-world measurements (a motion-capture format, a
photogrammetry mesh, a lidar scan) converts with
`ProjectSettings::unitsPerMeter` and nothing else — do not add a second scale
field with its own default, and do not bake the factor into an asset file
where it can no longer be corrected.
