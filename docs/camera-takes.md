# Camera takes: phone-recorded 6DoF camera moves

Record a real camera move on a phone (ARKit world tracking — walk around a
room looking around), then import it as keyframes on a **Cutscene Director**
camera track. The handheld motion becomes a PS2 cutscene camera move.

In the editor: **Tools > Cutscene Director**, select a sequence, then
**Import take...** (button next to the transport, or right-click the camera
lane label). Pick a file, choose the target (free camera shots, or a specific
Camera entity — see below), tune the mapping in the modal, Import. Either way
the take lands as ordinary linear-eased keys, so the PS2 runtime needs nothing
new — scrub the playhead to preview it immediately.

## Pipeline

The implementation (`src/camtake.hpp/.cpp`) is split in two, on purpose:

1. **Take acquisition** — anything that produces a `CamTake` (a list of
   `CamTakeSample`: time, position, rotation quaternion, optional FOV). Two
   sources: the file loaders below, and the **live phone camera link**
   (`src/phonecam.hpp`, [docs/phone-camera.md](phone-camera.md)), which appends
   samples over Wi-Fi while the editor runs.
2. **Take → keys** (`bakeCamTake`) — mapping into the scene, resampling and
   decimation. Pure functions of `(CamTake, CamTakeMapping)`, callable on a
   partially filled take, so a live buffer can be re-baked as it grows.

New sources implement acquisition only; the mapping/decimation/UI is shared.
`mapCamSample()` — one sample → eye + look-at — is public for exactly that
reason: the live link previews the camera through it, so what you see while
moving the phone is what the baked keys reproduce.

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

- **positions** are HitFilm "pixels": `meters = px * 2.8352 / 1000` (exactly
  FXhome's importer `blenderScale = (1/1000) * PixelsPerMM`; Blender units there
  are metres). So one metre walked in real life is ~1 game unit at *Scale* 1.
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

## Import target: a Camera entity

Every camera shot films from a Camera entity, so a take is imported **into a
Camera entity** (the modal's **Into camera** selector; add one with *+ Add
object > Gameplay > Camera* first). The take is baked into that entity's
**transform track**: position keys = the recorded eye, rotation keys = the
Euler whose +Z lens points along the recorded view (the inverse of the
runtime's `seqCameraForward`, so the shot films exactly along the path). The
entity's **FOV** is set from the take, and a camera-lane key **bound** to the
entity is added if the lane has none yet. Because the shot follows the entity's
live pose, this is a real dolly move — and **two cameras in one scene each
carry their own recording** (import take A into `cam-1`, take B into `cam-2`,
then cut between them on the camera lane).

## Mapping controls (the import modal)

- **Scale** — game units per real metre (default 1, i.e. 1 unit ≈ 1 m; raise it
  to make the recorded move cover more of the map).
- **Extra yaw** — rotates the whole path about +Y, pivoting on the first
  sample (point the recorded walk in the direction the shot needs).
- **Origin** — where the take's *first sample* lands; defaults to the current
  preview camera. **From view** drops it at the editor camera *and* sets the
  yaw so the take's first sample looks where you are looking (frame the shot in
  the viewport, hit From view, the recorded path is aimed there).
- **Start at playhead** — offsets key times so the take starts at the
  playhead instead of t = 0.
- **Tolerance** — decimation error bound in world units (below).
- **Replace / Append** (free target only) — what happens to the shots already
  on the camera lane. A Camera-entity target always rewrites that entity's own
  track. Import enables the sequence's camera track and extends its duration if
  the take runs past it.

## Adjust after importing (re-position / re-orient the path)

After an Import the recording and its mapping stay loaded, so an **Adjust
imported take** section appears under the sequence options while that sequence
is open. Its **Start point**, **Start yaw** and **Scale** controls re-bake the
same target in place — move or turn the whole recorded path without
re-importing the file. **Done** stops tracking; the keys become ordinary
editable keyframes. (Switching sequences, or a fresh Import, ends the adjust.)

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

## Live streaming from the phone

Shipped: see **[docs/phone-camera.md](phone-camera.md)**. A companion iOS app
([its own repo](https://github.com/doctorspider42/tyrax-cam)) streams ARKit pose
into a `CamTake` on a worker thread while
the editor streams the viewport image back to the phone, and the Cutscene
Director records the move into keys as it happens. It reuses this whole file's
second half unchanged; the only additions to the bake were an explicit mapping
**anchor** (a live stream has no meaningful "first sample" to pivot on) and
**fixed-rate resampling** (`CamTakeMapping::keyRate`) for the recorder's
keyframe-density control — the tolerance decimator above is still what a file
import uses.

Nothing in the pipeline assumes a complete file — keep it that way.
