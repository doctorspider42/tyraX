# Where the player starts

The Player object is a marker in the editor; in the game the camera becomes
it. Four things about the start are read from that one object:

- **Position X/Z** - where the player stands.
- **Y** - the starting height only when the scene has no terrain or the player
  flies (docs/terrain.md); otherwise the feet go on the ground under X/Z.
- **Rotation** - the starting **heading and elevation**, read exactly the way
  every other object's rotation is: the player's local forward (+Z) is rotated
  by the object's X, then Y, then Z angles, and the camera looks down that
  vector. So rotation Y is the heading (0 = +Z, 90 = +X) and **rotation X is the
  pitch - positive tilts the look DOWN**, like tilting any object forward. A
  triple the gizmo wraps as `[-180, 89, -180]` reads as heading 91, pitch 0,
  which is what the viewport shows. (Before 1.66.0 only rotation Y was read,
  raw - that triple started the player at 89, and the pitch could not be
  authored at all.)
- **Eye height, speeds, flashlight** - the Player object's own properties
  (docs/player-speeds.md, docs/flashlight.md).

## Frozen-camera fixtures

Any shot that has to be repeatable - an A/B of a rendering change, a screenshot
for a bug report - wants the camera nailed down, because the FPP camera drifts
in yaw between boots and the mouse turns it when keyboard/mouse is on. Set, on
the Player object (or in its `objects/<id>.json`):

```json
"rotation": [8.3, 91, 0],
"player": { "walkSpeed": 0, "lookSpeed": 0, "eyeHeight": 1.8, ... }
```

and `"keyboardMouse": false` in the `.tyra`. The player then stands where you
put it, looking 8.3 degrees down along +X, every boot. Pad scripts (`--pad`)
are for driving a test, not for aiming it: a pitch reached by "stick r 0 60;
wait 0.25" is a different pitch every run.
