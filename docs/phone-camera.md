# Phone camera: the phone as a live viewfinder

Hold your phone, look at the shot on its screen, and move it — the editor camera
moves with you. Press **Record** and the move is written into a **Cutscene
Director** camera track as keyframes, at a density you choose.

This is the live half of [camera takes](camera-takes.md): same destination
(camera keys in a sequence), but streamed over Wi-Fi while you work instead of
imported from a file afterwards.

- Editor side: *Tools > Phone Camera* (the link) and *Tools > Cutscene Director >
  Phone camera* (the recording).
- Phone side: the companion app in [`tools/tyrax-cam`](../tools/tyrax-cam)
  (iOS/ARKit, sideloaded — see its README).
- No phone to hand? Open `http://<editor-ip>:7798` in any browser for a **test
  client** that shows the same stream and can fake a pose.

## The loop

```
   phone (ARKit 6DoF pose, 30 Hz)
        │  ws://<editor>:7798        one WebSocket, both directions
        ▼
   phonecam::Link  ──poses──> App::phoneCamTick ──> viewport camera override
   (worker thread)                    │                       │
        ▲                             │              Viewport::render()
        │                             ▼                       │
   JPEG frames <──encode──  App::phoneCamPushPreview <── grabPreviewRgb
                                      │
                                      └─ recording? -> CamTake -> bakeCamTake
                                                          -> Sequence camera keys
```

Two properties are worth stating, because they are what makes the feature
trustworthy:

1. **The phone sees the editor's own image.** The stream is a downscaled readback
   of the frame the viewport just rendered (colour grading included) — not a
   second, subtly different render. What you frame on the phone is what the
   viewport shows.
2. **The live view and the recorded keys use identical math.** Both go through
   `mapCamSample()` (`src/camtake.cpp`), so the camera you were looking at while
   moving is exactly what the keyframes reproduce.

## Getting connected

1. *Tools > Phone Camera* → **Start link**. The window lists this machine's LAN
   addresses (`192.168.1.20:7798`) and a 6-digit **pairing code**.
2. Type both into the app (or the browser test client) and connect.
3. The first pose that arrives **recentres** automatically: the phone is anchored
   at the editor camera's position and aimed where the viewport was looking. So
   frame the shot roughly in the viewport first, then pick the phone up.

Settings are machine-global (`editor.ini`, not the `.tyra`): port, pairing code,
and the stream's size / frame rate / JPEG quality. Which port is free and how
much your Wi-Fi carries are properties of the PC, not of the game.

**Firewall.** The first *Start link* usually raises a Windows Firewall prompt —
allow `tyrax-editor.exe` on **private** networks. Without it the phone cannot
reach the port and the app says "cannot reach the editor".

**One device at a time.** A second connection is denied with a reason rather
than left to fight the first one for the camera. (Serving the browser test page
is not a connection, so you can read the page while a phone is live.)

## Driving the camera

While a phone is streaming and **Drive the viewport camera** is on, the phone is
the camera — ahead of the Cutscene Director's own preview and the look-through
camera both, because a playhead flying the same lane would fight the person
holding the device. Turning the toggle off keeps the link (and the stream) up
while you take the viewport back.

| Control | What it does |
|---|---|
| **Scale** (u/m) | Game units per real metre. At 1, walking a metre moves the camera one unit. Raise it to cover more map from the same room — a 3 m living room at scale 8 is a 24-unit crane move. |
| **Recentre** | Anchors on the current pose: the camera jumps back to the editor's own viewpoint and re-aims along the viewport view. Everything the phone does is relative to this. Also on the phone, and bindable there. |
| **Yaw** | Which way the phone's forward points in the scene. Recentre sets it; you can trim it by hand. |

Recentre reads the **orbit** camera, not the override the phone installed — so
it always means "come back to the vantage point I framed", however far the phone
wandered.

### Devices without world tracking

A browser (and an iPad without ARKit) reports rotation only. The editor notices
(`Tracking: rotation only` in the window) and the camera turns in place instead
of walking, because the position it receives never changes. Everything else works.

## Recording into a cutscene

*Tools > Cutscene Director*, select a sequence, open **Phone camera**:

- **Into** — a **Camera entity** (the recording becomes that entity's transform
  track, so the game films from a camera that dollies along your path, and a
  bound shot is added to the camera lane) or **Free camera shots** (the camera
  lane is written directly). Exactly the two targets the file importer offers —
  see [camera takes](camera-takes.md) for what each means downstream.
- **Keyframes** — the density:
  - *Project frame rate* — one key per rendered game frame (60 NTSC / 50 PAL,
    read from the project's video system + display mode). Nothing is
    interpolated; the biggest table.
  - *Custom rate* — keys per second. The default 10/s is a good starting point.
  - *Optimize (tolerance)* — no fixed rate at all: keep only the keys the PS2's
    own linear interpolation cannot reproduce within an error bound in world
    units. The smallest table, and the same decimator a file import uses.
- **Start at playhead** — recorded keys begin at the playhead rather than at
  t = 0, so a move can be dropped after an existing shot.
- **Record** / **Stop** — also the phone's own buttons.

While recording, the dopesheet **fills in live** (re-baked at 15 Hz) and the
phone shows a REC badge with the elapsed time and key count. The sequence's
duration is extended to fit; shots that were on the camera lane before you
started are preserved.

Undo sees **one step for the whole recording** — the live re-bakes deliberately
leave the history alone, and the take is committed when you stop.

### Keep the tables small

Keyframes are compiled into `src/gen/sequences.gen.cpp` and ship inside the ELF.
A 30-second move at the project frame rate is 1800 keys; the same move at 10/s is
300, and *Optimize* at the default tolerance typically lands in the low tens. The
Director warns past 600 keys, and a single bake is hard-capped at
`kCamTakeMaxKeys` (2048) — a long recording at a high density is coarsened rather
than allowed to blow the build up.

Practical advice: record at *Custom rate* 10–15/s. Handheld motion is smooth at
that density, and the PS2 lerps between keys the same way the resampler does.

## The stream

| Setting | Default | Notes |
|---|---|---|
| Max size | 480 | Long-edge cap. The viewport aspect is preserved (never cropped). |
| fps | 15 | Frames per second cap. |
| JPEG quality | 60 | 1–100. |

The phone may override all three (it knows its screen and its Wi-Fi); its request
wins. A frame is only grabbed when the previous one has reached the OS, and the
link stops asking for frames while more than 512 KB is queued — a weak link costs
**frame rate, never latency**. Grabbing is a GPU blit into a small framebuffer
followed by a readback of *that*, so a 1600×900 viewport does not stall the
editor's frame.

## Protocol

One WebSocket. Every message is a **binary** frame whose payload is one editor
wire frame:

```
[u32 jsonLen][u32 binLen][jsonLen bytes UTF-8 JSON][binLen bytes raw]   (little-endian)
```

The JSON part carries the message (`"t"` = type); the binary trailer carries bulk
data (the JPEG). WebSocket was chosen because it is built into React Native and
into every browser, so the phone needs no native socket code — the server lives
in `src/wire.cpp` as `wire::makeWebSocketTransport()`, behind the same
`wire::Transport` interface the [collaboration sessions](collaboration.md) use.

**Phone → editor**

| Message | Fields |
|---|---|
| `hello` | `proto` (1), `code`, `name`, `model`, `client`, `sixdof` |
| `pose` | `ts` (seconds, the phone's own clock), `p` `[x,y,z]` metres, `q` `[x,y,z,w]`, optional `fov` (degrees) |
| `cmd` | `cmd`: `record` \| `stop` \| `recenter` |
| `cfg` | `maxw`, `maxh`, `fps`, `quality` |
| `bye` | — |

**Editor → phone**

| Message | Fields |
|---|---|
| `welcome` | `proto`, `editor`, `project` |
| `deny` | `reason` (wrong code, protocol mismatch, another device) — the socket then closes |
| `frame` | `w`, `h`, `seq` + the JPEG in the binary trailer |
| `status` | `rec`, `time`, `keys`, `seq`, `target`, `dens`, `driving` — sent only when something changes |
| `bye` | `reason` |

Poses carry the phone's own timestamps, so network jitter does not distort the
recorded timing. The pose space is the **canonical take space** of
[camera takes](camera-takes.md) — ARKit's world convention, unconverted: Y up,
metres, camera looking down its local −Z. ARKit defines those local axes for a
device held landscape-right, so a portrait phone reports a 90° roll; that is
harmless, because a camera keyframe stores eye + look-at only and rolling about
the view axis does not move −Z. Roll is dropped, deliberately.

### Field of view

The app does **not** send the phone's FOV by default. A phone's wide lens is
about 39° vertical — a real telephoto for a game camera — and baking that into a
shot surprises people. Turn on *Match the phone's lens* in the app if you want
the real thing; otherwise the editor keeps its own 60°.

## The browser test client

The link's port serves a self-contained HTML page to an ordinary `GET`, so
`http://<editor-ip>:7798` gives a working client on any machine: it shows the
live stream, sends synthetic 6DoF poses (drag to look, WASD to walk, Q/E for
height, Shift to hurry) and has the same REC / STOP / RECENTRE buttons.

It exists because it makes the whole chain testable without an iPhone — which is
how the editor side was verified — and because it doubles as a second monitor.
The poses are synthetic on purpose: a half-right `DeviceOrientation` conversion
here would be a misleading reference for the ARKit one.

## Where the code is

| File | Role |
|---|---|
| `src/phonecam.hpp/.cpp` | the link: worker thread, protocol, JPEG encode, pose feed, the test client page |
| `src/wire.cpp` | `makeWebSocketTransport()` — RFC 6455 server behind `wire::Transport` |
| `src/camtake.hpp/.cpp` | `mapCamSample` (shared with the live view), the live anchor, fixed-rate resampling |
| `src/viewport.cpp` | `grabPreviewRgb()` — GPU downscale + readback of the last rendered frame |
| `src/app.cpp` | `phoneCamTick` / `drawPhoneCamWindow` / `phoneCamPushPreview` / the Director's *Phone camera* section |
| `tools/tyrax-cam/` | the iOS app |

Nothing about this reaches the generated game: a recording ends as ordinary
`SeqCameraKey`s, so the PS2 runtime, codegen and project format are untouched.
