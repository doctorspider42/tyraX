# Camera takes: phone-recorded 6DoF camera moves

Record a real camera move on a phone (ARKit world tracking — walk around a
room looking around), then import it as keyframes on a **Cutscene Director**
camera track. The handheld motion becomes a PS2 cutscene camera move.

In the editor: **Tools > Cutscene Director**, select a sequence, then
**Import take...** (button next to the transport, or right-click the camera
lane label). Pick a file, tune the mapping in the modal, Import. The take
lands as ordinary *free* camera shots (linear easing), so the PS2 runtime
needs nothing new — scrub the playhead to preview it immediately.

## Pipeline

The implementation (`src/camtake.hpp/.cpp`) is split in two, on purpose:

1. **Take acquisition** — anything that produces a `CamTake` (a list of
   `CamTakeSample`: time, position, rotation quaternion, optional FOV).
   Today: the two file loaders below. Phase 2 will add a live Wi-Fi/USB
   receiver that appends samples to the same `CamTake` while the editor runs.
2. **Take → keys** (`bakeCamTake`) — mapping into the scene, resampling and
   decimation. Pure functions of `(CamTake, CamTakeMapping)`, callable on a
   partially filled take, so a live buffer can be re-baked as it grows.

New sources implement acquisition only; the mapping/decimation/UI is shared.

## Canonical take space

All loaders convert into the **ARKit world-tracking convention**:

- right-handed, **Y up** (gravity-aligned), positions in **meters**
- rotation is a quaternion **(x, y, z, w)** that rotates camera-local axes
  into world axes; the camera looks down its local **−Z** with **+Y** up
- timestamps in **seconds** (any epoch — the bake rebases to the first sample)

This matches the game world's axes, so mapping into the scene is only
scale + yaw + origin placement. Note the PS2 camera key stores eye + look-at
only — **phone roll is dropped** at bake time.

## Canonical CSV format (app-agnostic)

Any phone app or script can produce this. One sample per line:

```
# comment lines and blank lines are ignored
# t,px,py,pz,qx,qy,qz,qw[,fov]
0.000000, -0.0997, 0.1762, 0.0399, 0.012, -0.704, 0.008, 0.710, 58.3
0.016667, -0.0998, 0.1761, 0.0401, 0.013, -0.704, 0.008, 0.710, 58.3
```

- `t` — seconds; `px,py,pz` — meters; `qx,qy,qz,qw` — the quaternion above
  (normalized on load); `fov` — optional vertical FOV in degrees.
- Fields are comma-separated; whitespace around fields is tolerated.
- Lines must parse or the load fails with a line number; samples are sorted
  by `t` on load. File extension: `.csv` (or `.txt`).

This is a direct dump of ARKit's `camera.transform` (position column +
rotation quaternion) plus the timestamp — no conversion needed on the phone.

## CamTrackAR `.hfcs` (the primary loader)

[CamTrackAR](https://fxhome.com/product/camtrackar) (FXhome, iPhone) records
ARKit tracks and exports a HitFilm *composite shot* — plain XML. The loader
reads the `CameraLayer` animation with a minimal XML subset reader
(`src/camtake.cpp`); semantics follow FXhome's official Blender importer:

- **positions** are HitFilm "pixels": `meters = px / 2.8352 / 1000`
- **orientation** Euler degrees were *inverted on export* — the loader negates
  them back — and apply in ZYX order (Z innermost): `R = Rx · Ry · Rz`
- HitFilm world axes match the canonical space one-for-one, and the
  effective camera convention is looking down local **−Z** as well (the
  Blender script assigns the converted rotation to a Blender camera, which
  looks down −Z) — so the decoded orientation is stored as-is, no extra flip.
  Derived empirically against the reference take: with −Z forward the
  recorded pitch runs 37–65° below the horizon (a phone picked up off a
  desk); the +Z reading would put the AR camera face-down on the desk
- **key times**: key *i* sits at frame *i* → `t = i / FrameRate`
  (`AudioVideoSettings/FrameRate`)
- **FOV** per frame from the `zoom` channel (lens zoom in pixels):
  `fov = 2 · atan(0.5 · HeightPx / zoomPx)`

`.hfcs` also contains `PointLayer` AR anchors; they are not imported (yet).

## Mapping controls (the import modal)

- **Scale** — game units per meter (default 1).
- **Extra yaw** — rotates the whole path about +Y, pivoting on the first
  sample (point the recorded walk in the direction the shot needs).
- **Origin** — where the take's *first sample* lands; defaults to the current
  preview camera, "From view" re-grabs it.
- **Start at playhead** — offsets key times so the take starts at the
  playhead instead of t = 0.
- **Tolerance** — decimation error bound in world units (below).
- **Replace / Append** — what happens to the shots already on the camera lane.
  Import enables the sequence's camera track and extends its duration if the
  take runs past it.

## Decimation (why the tolerance matters)

The PS2 keyframe tables are compiled into `sequences.gen.cpp` and grow with
every key — a raw 60 Hz take would be ~1800 keys for 30 s. The bake runs
time-parameterized Ramer–Douglas–Peucker over the (eye, look-at) curve: a
sample is dropped only if linear interpolation between the kept neighbours
stays within **tolerance** world units of the true eye *and* look-at (the
look-at sits 2 m in front of the eye, so at scale 1 the default 0.05 u
tolerance is also ~1.4° of view direction). The real 6.6 s test take (395
samples) bakes to **12 keys** at the default tolerance; the modal shows the
resulting key count live. All imported keys use linear easing — at these key
densities the take itself is the ease.

FOV is averaged over the take for now (per-key FOV would fight the
decimator); keys are plain `SeqCameraKey`s, so everything stays hand-editable
on the dopesheet afterwards.

## Phase 2 (planned): live streaming from the phone

The acquisition/bake split above is the contract: a streaming receiver
(e.g. the documented Record3D protocol, or a tiny companion iOS app over UDP)
will push `CamTakeSample`s into a `CamTake` on a background thread while the
editor runs; `bakeCamTake` already works on a growing buffer, and previewing
a take path should stay independent from the baked sequence keys (bake once
on "stop recording"). Nothing in the current pipeline assumes a complete
file — keep it that way.
