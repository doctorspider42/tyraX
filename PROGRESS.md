# Progress log

Living document: what is being worked on right now, what is done, what is queued.
Each finished feature lands as its own commit.

## In progress

- (nothing — remote collaboration v1 (113-118) is complete; internet
  exposure for sessions is deliberately deferred, see Backlog)

## Also done after the marathon

- (187) **An agent that needs root hangs the session behind a password dialog
  nobody can see.** On Linux the owner kept getting a polkit "Authentication
  Required" popup mid-session. The trigger was an assistant workaround:
  `docker` failed with permission denied on `/var/run/docker.sock`, so a past
  session reached for `pkexec setfacl -m u:spider:rw` to unstick itself. Root
  cause of the docker failure was never the socket — the login session simply
  predated `usermod -aG docker`, so the user was in the group per the user
  database (`id spider`) but not in the running process (`id`); a logout fixes
  it and no privilege escalation is needed at all. Two things made this worse
  than it looks. The owner had already raised `timestamp_timeout` in visudo,
  which is inert here twice over: polkit does not read sudoers, and Ubuntu
  26.04 ships `sudo-rs`, which keeps a separate timestamp per terminal with no
  `tty_tickets` knob to disable it — every Bash tool call is a fresh non-tty
  process, so the cached credential is never reused. And the assistant cannot
  detect the popup: `pkexec` blocks with no output, indistinguishable from a
  slow command, so the session just stalls.
  Fixed by denying the escalation instead of trying to observe it. New
  `.claude/settings.json` puts `pkexec`, `sudo`, `su`, `doas` and `runas` in
  `permissions.deny`, and a new CLAUDE.md section tells the agent to stop and
  report the command, why root was needed and what is blocked — plus the
  docker-group diagnosis specifically, so the next agent reads `id` instead of
  patching the socket. `permissions.deny` and not a `PreToolUse` hook on
  purpose: hooks run a real shell (PowerShell on Windows without Git Bash), so
  a `jq`/`grep` one-liner guarding this would break on the owner's other
  machine, while deny rules are matched inside Claude Code and behave
  identically on both. Verified live in-session, no restart needed:
  `sudo -n true` and `pkexec setfacl -m u:spider:rw /var/run/docker.sock` both
  come back "Permission to use Bash ... has been denied". Known limit, stated
  rather than papered over: the rules match a command prefix, so escalation
  buried inside `bash -c "..."` would still pass — this is a guard against
  accidents, not a sandbox.

- (186) **Two things the owner hit while playing with areas: the unload band was
  eight units of "why is this still loaded", and you cannot debug an invisible
  volume.** (1) A streaming layer on an area zone loaded the instant you entered
  but unloaded only after walking well past the edge. Cause: the area branch
  reused the CIRCLE's hysteresis - a flat `ZONE_HYSTERESIS = 8.0` added to the
  half extent on every axis, i.e. eight units into the next room before the
  layer went. That constant makes sense for a radius (a guess about where the
  room is) and no sense for a box the author drew, so the box now grows by the
  same 15% the circle applies to `r` plus half a unit per side - enough that
  pacing ON the edge cannot thrash the loader, small enough that the boundary
  means what it looks like. (2) New debug preference **Show areas**
  (`Settings::showAreas` -> `DEBUG_SHOW_AREAS`, debug profile only, next to Show
  FPS / memory / profiler): area objects render in the GAME as wireframe boxes.
  Implemented as twelve thin beams through the ordinary `addBox` - an edge IS a
  box (length on one axis, thickness on the other two, parked at one of four
  parallel corners), which keeps the transform, lighting and vertex format
  identical to every other primitive and needed no new mesh code. A wireframe
  and not a translucent solid on purpose: a filled volume hides the objects you
  opened it to look at. `rebuildObjectGeometry`'s `case 17` emits nothing at all
  when the constexpr is false, so a release build is byte-identical.
  Verified in PCSX2. The band: a fixture with a 10x6x10 zone parked on the
  player and a flow graph gliding it away at 3 u/s (no pad input needed - the
  area moves, the player stands still) logged the live centre offset every 15
  frames: `in=1` up to centerDZ 4.48, `in=0 resident=1` at 5.80, unloaded by
  7.23 - the band edge is 6.25 (half of 10*1.15+1), so the overshoot past the
  authored wall went from 8 units to ~1.25. The wireframe: screenshot of a
  10x6x10 area yawed 20 degrees, drawn green around the red box it contains,
  50 FPS. Flag off -> `DEBUG_SHOW_AREAS = false` and no geometry.

- (185) **Catch areas can update every frame: walk into a mirror's area and you
  start reflecting.** (184) resolved catch areas at build on purpose - the
  Mirror philosophy - and the owner immediately hit the other half of it: a
  crate that rolls in front of the glass, or the player stepping up to it,
  never joined the reflection. New per-object switch `catchAreaLive` (a
  checkbox under the picker on Mirror / Portal / feed Camera) re-tests the
  volume every frame instead. The whole design is about paying for it only
  where it is real:
  **only objects that can MOVE are re-tested.** `project::areaLiveCandidates`
  over `project::objectRuntimeMovable` (physics / pickable / usable /
  save-state / layer member / own graph or script / named by a flow node,
  cutscene track or target list) is the candidate set; it bakes into a shared
  `CATCH_CANDIDATES` table sliced per owner (`liveArea`/`firstCand`/`candCount`
  on MirrorData, PortalData, CamFeedData) and whatever the volume holds that
  CANNOT move stays resolved at build in the fixed list. A static room adds
  nothing to the per-frame work. The pleasant surprise while scoping this: that
  movable predicate is the exact complement of the immovability
  `staticBatchEligible` relies on, so **static batching needed no change at
  all** - a live candidate always already has the solo bag a second submission
  needs (`batchBlockedNames` now blocks the candidate names too, explicitly,
  so the two cannot drift). Movable objects are dropped FROM the baked list, or
  an object sitting inside at build would be submitted twice.
  `pointInArea` was split into `areaBasis` (center + rotated axes + half
  extents, computed ONCE per pass, and short-circuiting the six trig calls when
  the area is unrotated - the usual case) and `areaDistSq`, so a candidate
  costs three dot products; `TerrainGame::collectLiveCaught` walks a slice into
  a reused member vector and also scans the **spawn pool**, which no build-time
  table can name. Portals run the same test in `portalCanCross` /
  `portalShowsObject`, so the owner's rule (a portal that shows it lets it
  through) survives. Raytraced mirrors ignore the flag - their VU0 proxies are
  meshes baked per mirror; `portalViewAll` ignores it too. The player follows
  *Reflect player* AND the volume. No cap on the caught count: the panel prints
  `N fixed + K of M movable inside now` and the author watches it (explicitly
  the owner's call - "trzeba uważać, co się robi").
  Verified e2e in PCSX2 (Docker build clean under `-Wall`, 50 FPS): a fixture
  mirror with a live area over three crates - one immovable, one physics body
  inside, one physics body dropped from y=12 - logged the live set every 30
  frames as `n=1 [4,-1]` while the third fell (dropY 11.99 -> 10.05 -> 4.58,
  still outside: the box top is y=4 and the catch sphere is 0.5) and flipped to
  `n=2 [4,5]` at dropY=1.57, holding there after it landed. Screenshot shows
  all three reflected behind the glass. Codegen inspected both ways from one
  fixture: live on -> `MIRRORS {..., liveArea 1, firstCand 0, candCount 2}`,
  `MIRROR_TARGETS = {3}` (only the immovable crate), `CATCH_CANDIDATES = {4,5}`
  (including the crate far outside - a candidate is about *can it move*, not
  *is it inside*); flag off -> `liveArea -1`, `candCount 0`,
  `MIRROR_TARGETS = {3,4}`, i.e. byte-for-byte the old behavior. All 18
  committed example projects regenerated (they had also drifted behind (184) -
  the generated headers gained the Area code that commit never re-emitted).
  Editor-side visuals (the new checkbox and the count line) still want a human
  look, same AMD-GL white-window caveat as (184).

- (184) **Areas: an invisible volume you place instead of typing a distance.**
  New object type `PrimitiveType::Area = 17` (docs/areas.md) - an oriented box
  with NO geometry in the game: a wireframe in the editor (its own pass, so it
  never fills the volume and hides what it encloses), nothing on the console,
  type 17 added to every marker skip list (`collidePlayer`, the USE scan, the
  carry/throw sweep, `physObstacle`, the geometry switch, `flowRaycast`,
  `blocksNavigation`). Four features stopped guessing at numbers: a
  **streaming layer's auto zone** can be an area instead of the center+radius
  circle (`SceneLayer::streamArea` -> `SCENE_LAYER_STREAM_AREAS`, the object
  INDEX so the zone is read live), and **Mirror / Portal / feed Camera** take a
  **catch area** (one field, `SceneObject::catchArea` - an object is only ever
  one of the three) whose contents join their re-draw list. Plus the **In Area**
  flow trigger: rising edge on entry, `Who` = either/P1/P2, and a live
  "inside now" bool output (NOT + On Condition = an exit trigger).
  The two design calls worth recording. (1) Catch areas resolve **at build**,
  not per frame: the Mirror philosophy is that a second submission's cost must
  stay visible, so the Properties panel prints the resolved count and
  `batchBlockedNames` excludes the caught objects (a batched member has no solo
  bag and would silently vanish from the reflection - verified: with AO off the
  caught crate drops to `batchStatic 0` while an identical crate outside the
  area keeps `1`). (2) The point test has exactly TWO implementations and the
  runtime one lives in the generated **data** header (`pointInArea` in
  scene_data.hpp), not in a game-cpp template - that way both generated TUs
  (terrain_game.cpp's layer zones, flow_graph.gen.cpp's trigger) share one
  definition instead of the usual host/game twin pair; `areaCaughtObjects` is
  likewise the single expansion behind the panel preview, the viewport mirror
  preview and codegen. Host vs generated formulations (rotate-three-axes vs
  columns of Rz*Ry*Rx) were cross-checked over **200k random oriented boxes,
  zero mismatches**. Sound-emitter range and point-light radius deliberately
  keep their radius - those describe a falloff, not a boundary.
  Live Link: an area's transform is in its recipe hash (catch areas bake), so
  moving one live flips the chip to LIVE (rebuild) instead of showing half the
  edit; `liveLinkCanSpawnLive` excludes areas.
  Verified e2e in PCSX2 (Docker build clean, `-Wall`, 50 FPS): a fixture with
  the FPP player spawning inside a trigger area logged `AREA-TRIGGER-OK`
  **exactly once** (rising edge, no re-fire over 1000 frames); an auto-streamed
  layer pointed at a far-away area logged `FAR-LAYER-LOADED= false` despite
  `startLoaded: true` (the zone decides), and when a Move Object To slid that
  area onto the player mid-run it flipped to `true` - the live per-frame branch
  and "the area moves its zone" in one run. Codegen inspected:
  `SCENE_LAYER_STREAM_AREAS = {{2}}`, `MIRROR_TARGETS = {3}` (the crate inside
  the area; the one outside absent). The editor-side visuals (wireframe box,
  the Area properties block, the pickers) still need a human look - this
  machine is in the known AMD-GL white-window state, so GUI captures are
  unusable (see the editor-gui-screenshot notes).

- (183) **Merging world scale (177) into the phone camera - the integration git
  could not see.** Bringing main in gave eight textual conflicts, all of them
  adjacent additions to the same lists (a config struct, its reader and writer,
  the layout window registry) plus two independent overlay functions inserted at
  the same line. Those were mechanical. The change that mattered had **no
  conflict at all**: (177) made `CamTakeMapping::scale` mean *units per meter*
  seeded from the project, and the take-import modal was updated to do that - but
  the live phone link builds its own mapping, so it kept defaulting to 1.0. In a
  project where a metre is ten units, importing a recording and *filming the same
  move live* would have disagreed by 10x, silently, with both code paths reading
  correct in isolation. Seeded on **connect** rather than on Recentre: the take
  path re-seeds when a file is opened, and the live equivalent of opening is a
  phone arriving - re-seeding on every recentre would overwrite a scale tuned
  while watching the shot. The recording tolerance is a distance, so it follows
  the same factor, and the modal's **World scale** snap-back button is now next
  to the phone's Scale field too.
  *Also merged by hand:* my two PROGRESS entries were renumbered 177/178 ->
  181/182 because main had taken those numbers and its own entries cross-
  reference them; the SKILL.md `viewport` row kept my text plus main's
  `projectToImage()` note (one row, both facts). The measuring tape from (178)
  gates its clicks on the axis-gizmo veto, and my viewport gear ORs into that
  same flag - so clicking the gear while measuring correctly does not drop a
  measurement point, which is the merge working by construction rather than by
  patching.
  *Verified:* editor builds clean; a scaffolded project regenerates with the roll
  plumbing intact (`upFor`/`rollOf` in `sequences.gen.cpp`, 7 three-argument
  `CameraInfo3D` sites) and **compiles for the PS2 in Docker** with `-Wall`
  silent - the one check that proves the auto-merged `templates.cpp` is still
  valid MIPS C++. Not verified: the merged UI clicked through by hand.

- (182) **Camera roll: Dutch angles from the phone's own tilt, all the way to the
  console.** Asked for as a "tiny fix" alongside locking the phone app to
  landscape; landscape was tiny, this was not - it runs model -> serialization ->
  codegen -> PS2 runtime -> **engine** -> viewport -> UI.
  **The engine turned out to be the easy part.** `CameraInfo3D` already carried an
  `up` field and `Renderer3DFrustumPlanes` already culled against it - only the
  view matrix dropped it, hardcoding world +Y inside a VU0 block whose cross
  products are dead code (it computes `$vf8`/`$vf9` and then stores `$vf6`). So:
  a `M4x4::lookAt` overload taking an up vector (plain C++ - once per frame, not
  per vertex) and `RendererCore3D::update` passing `cameraInfo.up`. It defaults to
  `(0,1,0)`, so every existing caller is bit-identical. Reading `setCamera`'s
  assembly closed the last unknown: it derives `right = cross(up, vz)` then
  `up' = cross(vz, right)`, which for a perpendicular up returns it verbatim (the
  sign of `vz` cancels) - it never hardcodes world up, so a rolled up genuinely
  rolls the camera.
  **The design correction that mattered.** I first assumed a Camera entity's
  `rotation.z` WAS a lens-axis roll and wrote that in a comment. It is not: the
  Euler is applied `Rz*Ry*Rx`, so Z rotates about the WORLD axis last. Measured:
  on a camera pitched 40 deg, `rotation.z = 90` swings where it points by **54
  deg**; they coincide only for an unpitched camera. So free shots get an explicit
  `SeqCameraKey::roll` and a BOUND shot takes its whole basis from the entity's
  orientation (`seqCameraUpFromEuler`) with no separate channel - and a phone
  recording into an entity folds its roll into the entity's Euler through
  `seqEulerFromBasis`. A harness assertion now pins that distinction; the first
  version asserted the equivalence I had wrongly expected, and failed, which is
  how the mistake surfaced.
  **`seqCameraUp` is a three-way twin** (host bake, viewport `camView`, the
  generated player's `upFor`), like the other analytic-bake twins. Roll 0
  reproduces the old hardcoded `(0,1,0)` exactly, so an unrolled cutscene renders
  as before. Roll interpolates as a plain scalar channel and takes the short way
  round at +-180 in both the preview and the player; the camera-take bake unwraps
  it per sample for the same reason the Euler bake does.
  **Phone side:** the app is landscape-only now (you hold it like a camera), the
  measured tilt is zeroed against `anchorRoll` at **Recentre** - so whichever way
  you hold it counts as level - and damped by a **Tilt** slider from "as held" to
  "horizon pinned level", which also throws hand tremble away.
  **Verified** in four layers. The sequence-math harness: up always unit and
  perpendicular, roll 0 gravity-aligned with no lean on a pitched view and exactly
  `(0,1,0)` on a level one, `seqRollFromUp` inverting `seqCameraUp` to 0.0000 deg
  over a spread including straight-up/down, the Euler matrix's lens column
  agreeing with `seqCameraForward`, and basis -> Euler -> basis round-tripping to
  8e-7 including gimbal lock. A GL harness reading the **real view matrix** the
  viewport renders with: 20 combinations of view direction x roll (incl. near
  straight down and rolls past +-90) match `seqCameraUp` to **1.19e-07** - that is
  the twin invariant proved against what actually draws, not against pixels. A
  **Docker PS2 build**: `libtyra.a` rebuilt (m4x4.o, renderer_core_3d.o),
  `sequences.gen.cpp` compiled with the new `CamKey`, ELF linked - the only way to
  compile engine code at all. Plus rendered stills showing the horizon tilting the
  right way and by the right amount.
  *A probe lesson worth keeping:* my first visual check measured the sky/terrain
  boundary angle and disagreed with the requested roll at 40 deg. The feature was
  fine - the probe was measuring the finite ground plane's **edge** across only
  two fixed columns, one of which the boundary had left. Reading the camera basis
  out of the view matrix is the right measurement; fitting pixels was measuring
  the scenery.
  **Not verified: a PCSX2 pixel shot of a rolled horizon** - that needs a flow
  graph to trigger the cutscene, which this scratch project has none of. The data
  and the render path are verified as far as they can be without booting.

- (181) **Phone camera: the phone as a live viewfinder that records cutscene
  camera moves.** The other half of the camera-takes story (152): instead of
  importing a finished ARKit recording, the editor **hosts a link on the LAN**,
  a companion iOS app connects, and from then on the phone screen shows a live
  JPEG stream of the editor viewport while the phone's 6DoF pose drives that
  camera. In the Cutscene Director, *Record* writes the move into camera keys as
  it happens, at a configurable keyframe density. Docs: `docs/phone-camera.md`.
  **The app lives in its own public repo**, `doctorspider42/tyrax-cam` - it has a
  completely different toolchain (Expo/React Native + a Swift ARKit module) and
  its own release cycle (a sideloaded `.ipa`, never built by `build.ps1`), so
  keeping it under `tools/` here bought nothing and hid it from anyone who just
  wants the app. That repo carries a `PROTOCOL.md` stating the wire format from
  the client side, which makes it self-contained (this repo is private, so it
  could not link back into these docs anyway) - and makes the protocol a
  **two-repo contract**: change it in both, and bump `phonecam::kProtoVersion` so
  a stale app is denied at the handshake instead of misbehaving. Its CI builds an
  **unsigned .ipa** on a macOS runner that Sideloadly/AltStore can sign with a
  free Apple ID, plus a fast Linux job that Metro-bundles the JS and asserts the
  local ARKit module is autolinked - that last check exists because autolinking
  dropping the module is otherwise INVISIBLE: the app still builds and still
  runs, it just quietly cannot move the camera.
  **Transport.** `wire::makeWebSocketTransport()` - an RFC 6455 server (SHA-1 +
  base64 upgrade, unmasking, ping/pong, fragmentation) behind the existing
  `wire::Transport` interface, which is exactly the seam the collaboration work
  (113-118) predicted would be reused. WebSocket rather than the raw TCP
  framing because it is what React Native and every browser have built in, so
  the phone needs no native socket module; one binary message carries one
  `encodeFrame` image, so the codec above it is unchanged. It is a per-connection
  `WsCodec` slotted between the socket and the same `FrameDecoder` rather than a
  second Transport class - the accept/poll/send loop stays single. Two contract
  wrinkles that took thinking: a WS peer is announced on **upgrade**, not on
  accept (so an ordinary browser GET, which gets served an HTML page instead,
  never becomes a peer and never emits an unmatched `Disconnected`), and a dying
  codec sets `closeAfterFlush` instead of dropping, or the served page is
  truncated by the close.
  **The image.** `Viewport::grabPreviewRgb` reads back the frame `render()` just
  produced - so the phone sees the editor's own picture, colour grading included,
  not a second slightly different render. It blits into its own small
  framebuffer and reads back *that*: a straight `glReadPixels` of a 1600x900
  viewport is 5.7 MB and stalls the frame. `lastImageFbo_` tracks which target
  holds the final image, so a graded frame streams graded. JPEG encoding happens
  on the link's worker thread (stb_image_write), a pending frame is replaced
  rather than queued, and `previewWanted()` gates on the send backlog - a weak
  link must cost frame rate, never latency.
  **The camera.** The live view and the baked keys had to be the same math or
  the feature is a lie, so `mapCamSample()` came out of `bakeCamTake` and both
  call it. Two `CamTakeMapping` fields are new and both exist for the streaming
  case: `hasAnchor`/`anchor`, because a stream has no meaningful "first sample"
  to pivot on (without an explicit anchor the whole path jumps the moment a
  recording starts mid-stream), and `keyRate`, the fixed-rate resampler behind
  the density control. Density and the RDP tolerance are deliberately exclusive:
  a density that is then decimated away is not a density.
  **Recording.** The buffer is a plain `CamTake`, so the target logic (a Camera
  entity's transform track, or free shots on the camera lane) is the file
  importer's code, parameterized. Re-baked at 15 Hz so the dopesheet visibly
  fills up while you move, which meant making the re-bake **idempotent**: the
  pre-recording camera lane and duration are snapshotted at *Record* and
  restored before each bake, otherwise keys compound and shots authored earlier
  are eaten. Undo sees one step for the whole recording - the live re-bakes
  leave the history alone and only *Stop* commits. The phone can press
  Record/Stop/Recentre itself, which matters more than it sounds: you cannot
  reach the keyboard while holding the camera.
  **Verified** in three layers: three host harnesses, and then the whole thing
  in the real editor, driven from the browser test client the link serves on its
  own port (`http://<editor-ip>:7798` - synthetic poses, drag to look, WASD to
  walk).
  *Harnesses* (all no-GUI, the treegen/placement pattern). The camtake one
  (40 lines + `camtake.cpp`) proved properties rather than eyeballing them:
  `mapCamSample` is **bit-identical** to what the bake writes, re-anchoring is a
  rigid translation (the path's shape survives it), a fixed rate at or above the
  stream rate reproduces the samples, every rate lands evenly spaced and exactly
  on the take's last sample, and the 2048-key cap holds on a 10-minute take. It
  caught a real bug the GUI would have hidden: `t += step` accumulates float
  error, so the final clamp to the take's end emitted **two keys at the same
  time** - a zero-length segment for the PS2 player. Key times come from the
  step index now. The link harness (`phonecam` + `wire` + `json`) ran 100 s of
  continuous streaming: **1279 JPEG frames, 3018 poses**, no drops, a clean
  close-handshake disconnect, and all three refusal paths correct (wrong pairing
  code, protocol mismatch, second device). The grab harness renders on a HIDDEN
  GLFW window (an FBO readback needs a context, not a composited window - which
  is why this one works even in the AMD white-window state of (101)) and checks
  `grabPreviewRgb` at three caps: aspect preserved and never cropped, inside the
  cap, real content, **row 0 is the sky** (the check that catches a silently
  upside-down phone), sane JPEG sizes, and 200 repeated grabs byte-identical.
  *Real editor*, scratch FPP project, browser as the phone: the link hosts and
  lists all three LAN addresses + the pairing code (and raises exactly the
  Windows Firewall prompt the docs warn about); the browser pairs; the stream
  runs at **13 fps of 960x590** of the actual viewport image; the pose drives
  the camera in both channels (pose -1.63/0.36/-1.75 m moved the scene camera
  51.8 -> 49.4 and visibly rotated the axis gizmo); Recentre puts it back and
  re-derives the yaw; **Record and Stop pressed on the phone** captured a 4.16 s
  handheld move as **43 keys** - and the saved `.tyra` says exactly what it
  should: 43 keys, strictly increasing times, **zero duplicate timestamps**,
  spacing 0.1 s dead on the requested 10 keys/s with a short tail key landing on
  the take's real last sample, `ease: 0` throughout, free shots, `cameraEnabled`
  auto-set, 3.89 units of eye travel and 4.43 of look-at pan. The dopesheet
  filled live while the move was happening.
  **The iOS app now compiles** - its CI archives it on a macOS runner, Swift
  ARKit module included, and uploads a 3.7 MB unsigned `.ipa` (scheme `TyraXCam`,
  559 JS modules bundled into the app target). That was the biggest unknown, and
  it took four fixes found by actually running things rather than assuming:
  `expo export` needs `expo-asset` as a direct dependency (not hoisted);
  `expo-modules-autolinking search -p ios` silently skips **apple-only** modules
  because the SDK 52 platform key is `apple`, so my own module looked unlinked;
  its `--json` output flattens the config, so the first version of the
  "is the module linked" assertion passed vacuously; and CI caught the real one -
  archiving `.workspace.schemes[0]` builds **boost**, not the app (CocoaPods adds
  a scheme per pod and it sorts first), which xcodebuild reports as success while
  leaving an archive with no `.app` in it.
  **Still not verified: the app RUNNING on a device.** Nobody has installed it
  yet, so real ARKit tracking quality - how it behaves when you actually walk
  around a room, and whether the 1 u/m default scale feels right - is the one
  thing neither the browser client nor a compiler can stand in for. The app's
  README says so plainly and lists the three sideload routes (the CI `.ipa` +
  Sideloadly/AltStore with a free Apple ID; Xcode with a free Apple ID, 7-day
  expiry; an ad-hoc `.ipa` via EAS).
  *Judgement worth recording:* the phone deliberately wins the camera over both
  the cutscene preview and the look-through camera while it drives. A playhead
  flying the same lane would fight the person holding the device, and there is
  no reading of that fight where the software should win.
- (180) **Recent projects on the startup screen.** User request: with no project
  open the editor should offer the recently used ones on the main screen, pickable
  in one click, plus a way to drop an entry from the list. Until now the empty
  Viewport said "File > New Project (Ctrl+N) to create one" and the daily
  "carry on with yesterday's project" meant Ctrl+O and walking a file dialog to
  the same folder every single time.
  The Viewport's no-project branch is now `drawWelcomeScreen()`: New / Open
  buttons and up to ten entries, most recent first, each a two-line row (project
  name over its path) that opens on click, with an **x** that forgets it. The
  paths live in `editor.ini` as repeated `recentProject=` lines - machine-global
  like the emulator path and the UI scale, because which projects this PC has
  seen is a property of the PC, not of any project (this is the pattern the
  `tyra-editor-dev` skill describes for a new global setting; nothing else in the
  chain needed to move, the list never reaches the game).
  Three things that shaped the implementation:
  *One funnel.* Recording a recent has to happen wherever a project opens, so
  the three local open paths (the CLI/startup argument, the Open dialog, the
  welcome list) collapsed into `openProjectAt(dir)` and record there; the New
  Project modal records after its own attach. `openRemoteProject` deliberately
  does NOT - a joined session's project is a materialized cache copy under
  `remote-cache/<projectId>`, and offering that as "recent" would hand the user
  a stale snapshot of someone else's project.
  *Name and validity come from one directory scan*, done when the list loads or
  changes - never per frame. The display name is the `<name>.tyra` stem (found
  the way `project::load` finds it), so an entry that is no longer a project is
  the same lookup, not a second check: those rows show the folder name greyed
  with *(missing)* and stay listed. Sweeping them automatically would quietly
  eat the list of everyone whose projects live on a drive that is currently
  unplugged; the x is right there when the entry really is dead. Clicking a
  missing row still tries (the drive may be back) and re-probes on failure.
  *Dedupe on a normalized key* (`lexically_normal` + lowercase + no trailing
  slash): the Open dialog and the New Project modal disagree on slash flavour
  and Windows does not care about case, so `D:/proj` and `D:\Proj` are one entry.
  Two ImGui details worth remembering: the name/path are drawn straight into the
  window draw list (as items they would register their full text width and give
  the panel a horizontal scrollbar), and a long path ellipsizes from the LEFT -
  the tail identifies the project, `C:\Users\...` does not.
  Verified by driving the built editor: opening two scratch projects by CLI
  argument produced both `recentProject=` lines in the right order; re-opening
  one of them spelled `SCRIPT-demo` with forward slashes left **two** entries,
  not three, with it moved to the front (which also proves the load path parsed
  the stored list); then synthetic clicks on the welcome screen - hover shows the
  full path in a tooltip, the x dropped `layer-streaming`, and a click on the
  `script-demo` row opened it (title bar, scene tree, terrain in the viewport).
  Screenshots came out fine this time, i.e. the machine was not in its
  white-window state (see the harness notes).

- (179) **Walk speed: sane default, and a field you can actually type into.**
  Third in the (177)/(178) run, same user: "przy domyslnej predkosci chodu
  postac zapierdala jak dyliżans z gorki i z zaglem, trzeba dawac ostry
  ulamek, zeby mialo to sens". Both halves of that are real and they are
  different bugs.
  The **unit** is the one that bites daily: walk speed is stored as movement
  per 1/50 s (the generated game's step - `PP_WALK_SPEED(pi) * g_frameScale`),
  so the whole useful range lived in the bottom few percent of a 0.05..10
  field and every value was a fraction. Both editors (Preferences and a Player
  object's own row) now show and edit **units per second** through one helper
  (`walkSpeedDrag`), with the metric equivalent beside the field and the
  stored number in the tooltip. Storage is untouched on purpose - reinterpreting
  the saved field would have made every existing project 50x slower.
  The **default** came down 0.4 -> 0.1, i.e. 20 -> 5 units/s. 20 units/s next
  to a 1.8-unit eye height is 72 km/h, and (177) already established that this
  mismatch is what quietly pushes projects into being built several times
  larger than metric. Defaults changed together or the halves would disagree:
  `ProjectSettings::walkSpeed`, `SceneObject::playerWalkSpeed`, both
  `numberOr` fallbacks in project.cpp and the no-Player-object fallback in
  `playerFloat("WALK_SPEEDS", ...)`.
  In (177) I deliberately did NOT touch this default; the user asked for it
  directly here, and it is safe because the value is in the project file:
  verified that a `--new` project writes 0.1 and generates
  `WALK_SPEED = 0.1F`, while an existing project carrying 0.4 still round-trips
  as 0.4 through `--resave`. One trap while wiring the UI: the helper draws the
  metric label AFTER the drag, so `IsItemDeactivatedAfterEdit()` at the call
  site would have queried the LABEL and the Properties edit would never have
  committed - the helper reports it through an out-param instead.
  Examples keep their own hand-tuned speeds (0.4/0.55/0.8) - they are stored
  values, not defaults, and codegen did not change, so nothing there drifts.

- (178) **Measuring tape + an object size readout - "how big is a box,
  really?"** Follow-up to (177), same user, immediately after: "mamy jakas
  mozliwosc zmierzenia czegos w edytorze? Taka wstawiona kostka prymityw jaki
  ma real life rozmiar?" The answer to the literal question is that every
  primitive is a UNIT shape (primmesh: a 1x1x1 box about the origin), so a
  stock box at scale 1 is one unit on a side - one metre in a metric project,
  20 cm at 5 units/metre. Nothing in the editor said so anywhere, which is
  the actual gap.
  Two readouts now do: a **Size** line under the Scale row in Properties
  (`App::objectWorldSize` - unit shape times scale, or a model's own bounds
  times scale; flat types report their zero axis, and which axis that is
  differs - Plane lies in XZ, decal/mirror/portal quads stand in XY), and a
  **measuring tape** in the viewport tool row (*Measure (7)*): click two
  points, read distance in units and metres plus the `dx dy dz` split, end
  following the cursor until the second click.
  The tape needed the one thing the viewport did not have: **the inverse of
  `camRay`**. `Viewport::projectToImage` (world point -> image coords of the
  last frame, ortho and perspective, mirroring camView's conventions exactly)
  is what lets an app-side ImDrawList overlay sit on world geometry under any
  projection - the same trick `materialPreviewProject` already plays for the
  UV panel. Both tape ends land through `placementRaycast`, so the tape
  measures the scene (object boxes + heightfield) rather than a plane through
  the origin.
  Two interaction traps, both hit while writing it: a tool that consumes
  clicks has to be added to EVERY branch that also consumes them (pick,
  rubber-band start, gizmo enable - the `!sculptMode_ && !paintMode_` chain),
  and a pending paste already owns both the click and the same screen corner,
  so the tape stands down while one is in flight.
  Verified: builds, editor starts clean. The overlay itself is user-verified -
  no input injection on this machine, so I cannot click a tape into existence;
  the projection math is an exact algebraic inverse of camRay and shows up
  instantly as a label sliding off the line if it is not.

- (177) **World scale: imports from reality land at the size the project
  actually uses.** User report, two symptoms that turned out to be one cause:
  a Mixamo character imports "kurduplata" (comically small), and a phone
  camera take needs five real metres walked to cover what looks like one metre
  in the editor. Neither importer was wrong on its own - both assume **1 unit
  = 1 metre** (ufbx normalizes FBX to metres, `.glb` is metres by spec,
  `CamTakeMapping::scale` defaults to 1) - the project simply was not metric.
  Worth writing down *why* projects drift off metric here: the stock FPP
  settings disagree with each other. `eyeHeight` 1.8, `gravity` 9.8 and
  `jumpSpeed` 4.5 are metric as written, but `walkSpeed` is units per 1/50 s,
  so the default 0.4 is **20 units/s** - 72 km/h for a 1.8-unit person. Tune a
  world until walking feels right and you land several times larger than
  metric, which is exactly where this user was (~5 u/m). Deliberately did NOT
  change that default: it would silently re-tune every existing project. The
  metric readout under the new setting exposes it instead.
  **What landed:** `ProjectSettings::unitsPerMeter` (default 1.0, so every
  existing project and every metric project is bit-identical in behavior) as
  the single conversion, host-side only - verified it reaches no generated
  file. Model imports now ask for the asset's real-world size (Meters /
  Centimeters / Inches / Custom, or "it is 1.8 m tall"), stored per asset as
  metres-per-file-unit in a new `"modelUnits"` manifest section
  (`Project::modelUnitMeters`, `Section::ModelUnits`, kSectionCount 13 -> 14);
  `addModelObject` inserts at `metersPerUnit * unitsPerMeter`
  (`Project::modelInsertScale`). Camera takes seed their scale AND their
  decimation tolerance (a world-unit distance) from the same number, and the
  modal prints the recording both ways ("4.80 m walked -> 24.0 units").
  Three deliberate non-choices: the asset file is never rewritten (the size
  lives in the manifest, so it can be corrected later and survives a
  re-export); an asset with no recorded size inserts at scale 1, which is why
  Tree Generator output - already authored in world units - is untouched; and
  objects already placed keep their scale unless you tick the dialog's
  "also rescale N object(s)" (the user explicitly did not want a scene-wide
  rescale tool).
  Verified: editor builds; `--new` writes the key; a hand-edited `.tyra` with
  `unitsPerMeter: 5` plus a `modelUnits` map round-trips through `--resave`
  with the deliberately invalid `0` entry dropped; `--refresh-gen` produces
  generated sources that mention neither key; GUI starts clean on the scratch
  project. The dialogs themselves have NOT been clicked through by me - no
  input injection on this machine - so the import flow is user-verified.
  Examples were left alone on purpose: the new keys are optional and codegen
  is unaffected, so their committed generated files cannot drift.

- (176) **The baked light was being clipped by the GS ALPHA TEST - it was never
  a resolution problem.** The user kept saying the lights looked square and
  "like the texture is just cut off", and proposed reworking the whole thing
  onto per-object textures. Measured before rewriting anything, and the
  measurement said the architecture was fine and something else was broken.
  **The forensics** (worth keeping - each step killed a hypothesis):
  the atlas region for the lit alley wall has **every one of its 10920 texels
  lit** and no hard edge anywhere in RGB or in alpha, so the "the lightmap cuts
  off" reading could not be about the data. A **1-texel checkerboard written
  over the entire atlas** then showed the wall correctly checkered *except* the
  same black V-shaped notch and isolated square - a hole that survives whatever
  is in the texture is a hole in the RASTERISATION, not the bake. It also
  measured the two things I would otherwise have guessed at: the atlas IS
  bilinearly filtered (the checker renders as a smooth dot pattern, not hard
  squares) and one texel is ~6 GS pixels wide at that distance. Dumping the
  region's **alpha == 0 mask** produced exactly the notch, pixel for pixel.
  **The bug:** StaPip sets the GS alpha test to `ATEST_METHOD_NOTEQUAL` against
  0 - the cutout rule that makes foliage and decals work. The scene lightmap
  carries occlusion in `A` and light in `RGB` and is read by two passes over
  the same texture, so **any texel with zero occlusion failed the alpha test
  and discarded the additive LIGHT pass with it**. Baked light was silently
  clipped to wherever the ambient occlusion happened to be non-zero. It has
  been that way since the atlas landed; it reads as blockiness because the
  clip follows the texel grid, which is why nobody chased it to the alpha
  channel.
  **The fix is one line:** floor every lightmap texel's alpha at 2
  (`aobake::kMinLightmapAlpha`). Not 1 - the engine's PNG loader scales alpha
  0..255 to 0..128 by integer division, so 1 lands back on 0. Two costs 1/128
  of darkening, under one framebuffer level. No engine change, no format
  change, no VRAM, no extra pass.
  **Result** (PCSX2, software renderer, frozen camera, bloom held fixed so the
  pulser cannot confound it): the notch, the isolated square and the whole
  blocky lit/unlit boundary are gone, and the wall is one continuous gradient.
  **26.4% of the frame gained light** (up to +30 levels) and nothing lost more
  than 4. 50 FPS unchanged.
  Also landed here: per-texel bakes now average over the texel's **footprint**
  (a 4×4 sub-sample grid, `kSuper`) instead of point-sampling its centre. A
  fixture 0.3 units off a wall throws a penumbra far under one texel, and a
  point sample of that aliases into the staircase bilinear then reconstructs.
  Worth 17% off the worst-direction step on the softest edges
  (`s3-pillar-lit-3` r3: 2.18 → 1.82) for host bake time only.
  **The rework was not needed, and the numbers say it would not have helped:**
  a per-object 128² texture is 16384 texels for a whole object, and that wall
  already gets 14080 from the shared atlas. The only real lever on texel count
  would be bits per texel (the atlas is RGBA32 because the *occlusion* needs a
  smooth alpha), and that is a much bigger change to reach for once the actual
  bug is fixed.

- (175) **Texel density per AXIS, and a measured dead end.** Half of this
  entry's original subject - baked light landing per texel on the TERRAIN - was
  implemented independently on `main` as (172) while this branch was in flight,
  and the merge kept that implementation; it is the better one (shadow casters
  pruned once per emitter rather than once per texel). What survives here is
  the atlas side.
  The alley wall in `examples/glow` was still visibly coarse along its length.
  The obvious fix - raise the flat 128-texel per-region cap that was binding on
  that axis - was tried first and **measured worse**: 42528 -> 32722 texels in
  use, and the worst-direction step on the lit face grew, because huge regions
  pack badly and the extra resolution went onto the axis with no gradient on
  it. The cap was not the problem; **isotropic density** was. The pre-pass grid
  went 4x4 -> 6x6 and now also measures how fast the signal moves along each of
  the region's two axes, splitting the area the peak earned between them (area
  still proportional to peak, only the aspect moves, clamped to 2x).
  **Numbers** (`examples/glow`, host harness, same 256 square atlas).
  Blockiness is the mean step between neighbouring texels in the region's
  *worse* direction: `s4-wall-left` r0 went **5.70 -> 2.53 -> 2.26** (original
  -> (174) -> now) at **6.0x6.0 -> 13.8x7.1 -> 15.6x7.8** texels per world
  unit; `s3-shadow-wall` r1 **7.80 -> 3.20 -> 2.99**. The last step uses FEWER
  texels than (174) did (39068 vs 42528) - the win is the axis, not the budget.
  *Testing lesson, and it cost me a bogus A/B:* freezing the camera with
  `walkSpeed`/`lookSpeed` 0 is **not enough** when the project has
  *Keyboard & mouse controls* on - PCSX2 binds the host pointer to an emulated
  USB mouse and any cursor movement turns the view. The first comparison
  reported "53% of pixels brighter"; with `keyboardMouse` off in the fixture the
  same pair is 6.3% brighter / 10.1% darker, mean **-1.35** - a redistribution,
  not a brightening. Turn the preference off in any screenshot fixture.

- (174) **Scene lightmap: soft shadows from area emitters, and a texel budget
  that follows the light.** Two independent improvements to the existing bake
  (`aobake.cpp` + its two twins), both measured rather than eyeballed.
  **(a) Soft shadows.** The shadow test cast ONE ray, to the emitter's nearest
  surface point, and the contribution was all-or-nothing - but an emitter is an
  AREA source (a lava plate, a neon strip), so that can only ever produce a hard
  rectangular edge where a real one has a penumbra. It now casts eight: ray 0 to
  the nearest surface point (so `samples = 1` reproduces the old behaviour bit
  for bit) plus seven spread over the emitter's silhouette as seen from the lit
  point - its extent projected onto the plane perpendicular to ray 0, sampled on
  a fixed Vogel disk (`kEmisShadowDisk`). **No RNG anywhere**: a bake has to be
  reproducible, and a spiral beats a grid here because a straight shadow edge
  resolves into as many levels as there are distinct offsets (8, not the 3 a 3×3
  grid gives). One subtlety worth keeping: rays aimed *below* the receiver's
  horizon are dropped from the vote rather than counted as blocked - counting
  them would darken a floor standing next to a big plate with no occluder
  anywhere, and leaving them out keeps the "nothing blocks ⇒ identical to
  before" invariant exact.
  **(b) Texel density where the light is.** Region sizing was pure world area,
  so on `examples/glow` the atlas spent 36% of a 256² image uniformly - pedestal
  undersides and wall backs at the same density as the lit walls. A 4×4 probe
  grid per region now estimates the peak signal it can carry (light received +
  occlusion cast on it), the density scales with `sqrt(peak)` so the AREA a
  region gets is proportional to what it receives, and a 20-step bisection then
  raises the density globally until the pack actually fills the image (the old
  halving ladder left whatever the power-of-two round-up wasted). The atlas
  DIMENSION is still derived from the *unweighted* area on purpose: weighting
  must not move a project's VRAM in either direction.
  **Numbers** (`examples/glow`, same 256² atlas both sides, both from a full
  `--build` - `--refresh-gen` does not run texbake, so an A/B across it compares
  a stale PNG with itself and silently proves nothing):
  texels in use 23572 → 42528 (36.0% → 64.9% of the image); texels on regions
  that receive light 20571 → 38944 (+89%) against 3001 → 3584 (+19%) for those
  that receive none; the lit face of `s4-wall-left` went 30×108 → 69×128, i.e.
  **6.0 → 13.8 texels per world unit** with the mean step between neighbouring
  texels along the gradient dropping 7.76 → 3.24 - that step size *is* the
  blockiness. Decoding the shipped `.res-baked/aoatlas/scene0.png` on both sides
  agrees: 22939 → 41328 lit texels, 20256 → 40384 occluded.
  For (a) in isolation (layout held fixed, so the comparison is per texel):
  185 texels darkened, 298 brightened, 65053 unchanged, total light +0.36%. Note
  the tempting invariant "a shadowing change must never brighten a texel" is
  **false** for this change and has to be - a penumbra darkens the lit side of
  the old hard edge and brightens the shadowed side; what does hold, and what
  was asserted, is that nothing outside the penumbra band moves at all.
  `s3-pillar-OUT-OF-REACH` stays at 0 lit texels, while `s3-pillar-lit-3` -
  which the wall put in full umbra - goes from 0 lit texels to 319.
  **The one deliberate divergence.** The generated game's per-vertex path
  (terrain, models, spawned clones, physics bodies, textured receivers) keeps
  the single hard ray, so the three implementations are no longer exact twins.
  Measured before deciding, per the brief: an EE timer around `loadScene(0)` in
  PCSX2 with the blocker loop repeated 8× cost **+200 ms of scene load**
  (1160 → 1360 ms) on this scene, and the same multiplier would land mid-frame
  on every runtime spawn and static-batch rebuild - to resolve a penumbra on a
  grid whose own resolution is 2 world units (33×33 heightmap over 64×64) or a
  box face's four corners. Written down in docs/emissive-materials.md, in the
  `emissiveLightAt` header comment, in the editor skill and in the glow README,
  because an undocumented twin divergence is a trap for the next person.
  **Verified**: PCSX2, software renderer, PAL - **50 FPS held** on both builds;
  before/after screenshots from a frozen camera (walkSpeed/lookSpeed 0 in the
  fixture - the FPP camera drifts, and two boots otherwise never line up), and
  the difference is confined to the shadowed faces (0.43% of pixels brighter,
  0.17% darker, everything else untouched). Editor viewport: shader compiles and
  renders the scene with the same softening - it could not be pixel-A/B'd,
  because moving its camera needs synthetic input this machine must not receive.
  Host side, the whole bake is exercised by a scratch harness that links
  `aobake.cpp` + `project.cpp` and dumps the atlas plus per-region statistics -
  the same trick treegen and matbake use, and far faster than clicking the GUI.
- (173) **Authoring ergonomics: orthographic/axis views, collision-aware
  placement, and a paste that follows the cursor.** Three requested editor
  features, one commit each in spirit but one branch in practice - they share
  the viewport's camera/ray plumbing.
  **(a) Orthographic projection on a chosen axis.** `Viewport::Projection` is
  now perspective / free ortho / the six locked axis views (Top, Bottom,
  Front, Back, Right, Left), persisted per project as `editor.viewProjection`.
  The front door is a **Blender-style axis gizmo** in the viewport's top-right
  corner (`App::drawAxisGizmo`): the three world axes as coloured balls that
  turn with the camera (positive ends stemmed and lettered, negative ends
  hollow), click one to snap to that ortho view, click the hub to toggle
  perspective. Drawn straight into the window's `ImDrawList` from the view
  matrix's columns - no GL, no extra pass - and painter-sorted by view-space
  z so the axis facing the camera is on top and wins the hover. It handles its
  own hit test (nearest ball, front-most first) instead of an `InvisibleButton`
  and returns "cursor is over me", which the pick / rubber-band / paste-commit
  branches consult - otherwise every click on the widget would also clear the
  selection, the way the pre-existing corner buttons quietly do. Backed by
  three other entry points on the same setting: the `Proj:` button, *View >
  Projection* and the numpad (`5` toggles, `1`/`3`/`7` + `Ctrl` pick a side -
  the number ROW stays on the transform tools). The enabling refactor was
  collapsing four hand-rolled cameras into one: `camView()` + `camRay()` now
  resolve eye/basis/extents once (including the Cutscene/look-through
  override), and `render()`, `pick()`, `terrainRaycast()` and the new
  `placementRaycast()` all consume them. Before this, three of those four
  rebuilt a **hardcoded 50-degree perspective ray**, so they already disagreed
  with the image while looking through a Camera entity with a different FOV -
  a latent bug the ortho work would have multiplied by six. Two deliberate
  choices: the ortho depth range **straddles the eye** (a parallel view is a
  slab through the scene - a Top view must not hide the roof it looks
  through), and orbiting a locked axis view **seeds yaw/pitch from that axis
  and drops to free ortho** instead of ignoring the drag or teleporting to a
  stale angle. `pan`/`fly` read the same basis, so flying in a Top view walks
  up the image (no view direction left to flatten onto the ground).
  **(b) Surface snapping.** New host-only `placement.cpp` (the
  decalproj/navmesh pattern): world AABB per object (rotation and real model
  bounds included), `isSupport()` for what counts as a surface, and
  `restOffsetY` - the offset that rests an object on the highest support under
  its *footprint* (terrain sampled corners+center, plus overlapping objects'
  tops). One `ceilingY` argument carries the whole behavioral difference
  between "insert on top of whatever is there" (`FLT_MAX` - three boxes added
  in one spot stack) and the `End` drop-to-floor ("nothing may lift it").
  Wired into every add path, the paste, and *Scene > Drop to floor*; toggled by
  *Surface snap* in the tool row / *View > Placement* (machine setting in
  `editor.ini`, on by default). Explicitly NOT a collision solver - no sweep,
  no penetration resolve; gizmo dragging stays free.
  **(c) Deferred paste.** `Ctrl+V` stages the copies instead of inserting
  them: they follow the cursor (outlined, surface-snapped, rendered from a
  scratch list so the scene model is untouched), and a left click *or* a
  second `Ctrl+V` commits them; `Esc` discards with no undo step. A group
  moves as one rigid arrangement, lifted by the largest offset any member
  needs. Pasting without ever passing over the viewport falls back to the old
  one-unit diagonal offset. The staged set is dropped on a scene switch and on
  project attach.
  **Verified.** Layer 0 (clean build) plus: a scratchpad host harness over
  `placement.cpp` covering all 11 placement rules (flat ground, sloped ground,
  insert-inside-a-table → on top, beside/flush footprints, drop-to-floor
  ceiling, skip list, lights and `collisionMode == 2` not being surfaces, a
  45-degree box resting on its corner at half a diagonal, group offset, model
  bounds with feet at the origin) - all pass; `--resave` round-trip of the new
  `editor.viewProjection` key; and window screenshots of the editor rendering
  `examples/showcase` in the **Top** view (terrain a perfect square, gizmo and
  overlays correct) and `examples/script-demo` in the **Front** view (terrain
  edge-on as a line, the box sitting on it), against a perspective capture of
  the same project as the baseline. The axis gizmo was screenshotted zoomed in
  (stems, letters, hollow negative ends, the hub) and the capture happened to
  catch a live hover - white ring plus the "Top view - orthographic along the
  +Y axis" tooltip - so the hit test and tooltip path are covered too. **Still needs a hands-on pass**: the
  interactive paste flow and the insert snap are mouse-driven, and synthetic
  input is off-limits on this machine - the math and the wiring are covered,
  the *feel* (does the copy land where you expect, is the numpad muscle memory
  right) is a human check.
- (172) **The GROUND goes per pixel too: baked emissive light + its shadows
  move onto the terrain lightmap.** (169) fixed the props and left the biggest
  surface in the frame behind. `examples/glow` measures it: *Terrain detail* 32
  over a 64-unit map is a 33x33 vertex grid, one lighting sample every **1.94
  world units**, and `shadeAt` in `buildTerrainChunk` ran the emitter response
  AND its shadow test per vertex - so every pool of light and every shadow edge
  on the ground was quantised into ~2-metre squares. In a dark scene where the
  ground fills most of the frame, that was the most visible artifact left.
  The fix is the exact twin of what objects already do, one level up: the
  terrain AO map (`.res-baked/aomap/scene<N>.png`, already RGBA32, already
  drawn as an extra chunk pass) becomes the terrain **lightmap** - `A` =
  occlusion as before, `RGB` = baked emissive light - and each chunk draws it
  twice, the occlusion multiply with BLACK vertex colors (it had grey ones,
  harmless while the RGB was zero, wrong the moment it is not) and then an
  additive pass through `lightAddInfoBag` whose vertex color carries the
  terrain's own base tint. `aobake::terrainAOMap` bakes both channels from the
  same heightmap normal `shadeAt` derives, reusing `collectEmitters` /
  `emitterLightAt` / `shapeBlocksRay`, with the same ordered 4x4 Bayer dither
  the object atlas uses (these are wide low-amplitude ramps; an 8-bit
  framebuffer bands them badly without it) and the shadow casters pruned ONCE
  per emitter instead of per texel. `SCENE_AO_MAP_LIT` is the terrain's
  `SCENE_AO_ATLAS_LIT`: with it set the chunk build stops adding
  `emissiveLightAt` to the vertex colors and skips the per-chunk emitter
  collect entirely, so the light cannot land twice; `SCENE_AO_MAP_OCC` gates
  the occlusion pass separately, so **each pass is drawn only when its channel
  has content**. The map is no longer gated on the ambient-occlusion
  preference either - the object atlas was ungated in (169) for the same
  reason, and `examples/glow` is exactly the case (AO off, five emitters).
  Along the way: every extra chunk pass now carries the BASE bag's
  `bboxVersion` instead of its own `++g_bboxStamp`. The engine's package-bbox
  cache is keyed by the vertex pointer, and all the passes share
  `ch.vertices`, so distinct stamps made each pass recompute the boxes the
  previous one had just built, every frame - the new pass would have been the
  fourth. Not previously noticed because it is invisible except in EE time.
  Known limits, documented rather than papered over: one 256² map for the
  WHOLE terrain (RGBA32 - palettising destroys the gradient through the
  engine's tRNS→CLUT path), so **0.25 world units per texel at 64 units**
  (~8x finer per axis than the vertex grid) but **0.75 at 192** like
  `examples/showcase` - better than per vertex, not dramatic. Shadows stay
  rectangular (analytic boxes/spheres); this makes them smooth-edged and
  correctly placed, not silhouette-accurate. And on a *textured* terrain the
  additive pass adds flat light instead of modulating the texture, so a hot
  pool reads slightly bright over dark texels - the opposite call from the one
  objects make (a prop is small and its texture hides the Gouraud seam; the
  ground is the whole frame and had nowhere to hide).
  **Verified in PCSX2 (software renderer), full `--build` on both sides** - not
  `--refresh-gen`, which does not run texbake and would have compared a stale
  PNG with itself (the trap (170) fell into). Decoded the baked PNG directly:
  before 256x256, alpha 10175 texels, **RGB 0 texels**; after, the identical
  10175 alpha texels and **11858 RGB texels**, max (255,203,146) - the lava's
  orange. Sampled the map along the ground: a smooth quadratic ramp away from
  the plate edge (124, 108, 87, 57, 34, 17, 5, 0 over 0.25-unit texels) and a
  hard cut to 0 behind `s3-shadow-wall`. Screenshots from a fixed spawn (a
  scratch copy of `examples/glow` with the player parked facing the lava pit,
  so the frames line up): before, the ground is a fan of ~2-metre facets and
  the wall's shadow is a visible polygon; after, both are smooth with a clean
  edge. **50.08 FPS** in steady state (before: 50.02), EE 34%. Also built the
  AO-off variant end to end: the map ships with **0 alpha texels** and 11858
  RGB ones, i.e. only the additive pass. `examples/showcase`, `large-terrain`
  and `script-demo` regenerate byte-identically apart from the two new flag
  tables, both all-zero - a scene with no emitters gains no pass. All 18
  example projects regenerated.

- (171) **GS VRAM: a real residency manager (order-independent free +
  coldest-first eviction), after measuring what the old one actually cost.**
  Upstream's allocator was a bump pointer whose `free(address)` was literally
  `pointer = address`, and `useTexture` deallocated the ENTIRE resident set the
  moment one texture didn't fit. Measured first, per the brief.
  **What the measurement said** (new `VRAMSTAT` counters in
  `RendererCoreTexture` - binds/hits/uploads/re-uploads/evictions/free-VRAM
  low-water, logged from `endFrame` on every evicting frame plus every 120
  frames; counters always compiled, logging debug-only since `TYRA_LOG` is a
  no-op under NDEBUG):
  (a) **A realistic scene never flushes.** `examples/showcase` - a whole
  village with streaming layers, particles, post-fx - holds **6 texture
  allocations and 0.87 MB of the ~1.08 MB heap free**, 0 evictions, 0
  re-uploads, forever. 4-bit palettization is doing all the work. So the
  headline claim ("the ceiling under per-model lightmaps") is real only for
  full-colour textures: one 256² 32bpp texture is 24% of the heap and a 512²
  is 93% of it.
  (b) **But it does not fall off a cliff gracefully.** A fixture just over
  budget (3×256² + 3×128² 32bpp, fixed camera so visibility is deterministic)
  re-uploaded **9-10 textures every frame** - the whole working set, because
  one over-budget bind dumped everything.
  (c) **And `free()` was memory-unsafe, not just wasteful.** Freeing anything
  but the newest allocation rewinds the pointer under still-live textures.
  Streaming layers hit this on every unload: a fixture that unloads a 3-object
  layer at t=6 s and reloads it at t=10 s logged 3 out-of-order frees, free
  space "gained" 0.11 MB out of nowhere, and on screen **two surviving boxes
  started drawing another box's texture**. This, not the flush count, is the
  reason the work was worth doing.
  **What was built.** `RendererCoreGSVRam` now splits VRAM into a *permanent
  region* (bump, filled by `allocateBuffer()` at init: frame/z buffers,
  post-fx scratch, noise, env-map + camera-feed targets, never released - and
  `free()` simply doesn't know those addresses, so nothing can reclaim them)
  and a *texture heap* above it managed by a coalescing best-fit free list, so
  `free()` is order independent. `RendererCoreTexture::makeRoomFor()` evicts
  one allocation at a time until the newcomer fits, victim chosen in two tiers
  (`pickVictim`): stale entries first (not bound this frame *or* the one
  before - LRU, ties to the bigger block), and when everything resident is in
  the live working set, the **most recently bound** one instead. That MRU tier
  is not a detail: with plain LRU a scene that re-binds in the same order every
  frame evicts exactly what it needs next, and the entire set cycles. The
  two-frame window matters for the same reason - "not bound yet this frame" is
  the tail of last frame's scan, not cold data. Both were measured, not
  assumed (single-tier LRU: 7-8 re-uploads/frame on the pathological fixture;
  a one-frame window: 8; the shipped policy: 3 on the realistic one).
  `RendererCoreTextureBuffers` gained a `lastUsedSeq` stamp for this.
  **Verified** (Layer 3 throughout - engine code only compiles in Docker;
  PCSX2 software renderer, PAL, same fixture and same fixed camera on both
  sides of every A/B, eviction policy isolated from the allocator with a
  temporary compile switch so the comparison is like-for-like):
  re-uploads/frame **9-10 -> 3** on the just-over-budget scene, output pixel
  identical, 5.51 -> 5.33 ms EE frame time with vsync off and PCSX2's GS
  thread (where emulated PATH3 transfers land) 39% -> 23%. The streaming
  fixture now round-trips exactly (free space 0.406 -> 0.617 -> 0.406 MB,
  largest free block unchanged at 416 KB, i.e. no fragmentation) and the
  post-reload frame is pixel-identical to the pre-unload one - the wrong-texture
  corruption is gone. `examples/showcase` is **byte-identical** before and
  after on every counter (bind/hit/up/res/freeMB at f=120..720), which is the
  point: realistic projects see no change at all. `examples/reflections`
  re-checked because the env map binds through `vramResident` - reflections
  still render, 50 FPS. Deliberate over-subscription (6×256² 32bpp, ~2.4× the
  heap) degrades rather than breaks: 10 -> 8 re-uploads/frame, scene still
  correct at 195 FPS uncapped. Honest limit: nothing fixes a working set that
  is 2.4× VRAM; the answer there stays palettization/atlasing. Not yet tested
  on real PS2 hardware. Docs: new [docs/gs-vram.md](docs/gs-vram.md) (budget
  table, what a texture really costs, the policy, the `VRAMSTAT` fields, the
  numbers) + docs/README + README + the `tyra-engine-dev` skill.

- (170) **Emissive light is blocked by solids (no more glow through a wall).**
  Owner spotted the obvious hole in (169): a lamp lit the ground on the far
  side of a wall. The occluder shapes were already there - `collectOccluders`
  reduces every *Cast shadow* object to an analytic box/sphere for the ambient
  occlusion - so all that was missing was a segment test. `shapeBlocksRay`
  (slab test for a box, quadratic for a sphere) drops a light contribution when
  the segment from the lit point to the emitter's nearest surface enters an
  occluder. Three twins as usual: host bake, generated game, viewport shader.
  Details that matter: the caster list is pruned by the EMITTER reach, not
  `SCENE_AO_RADIUS`, so it cannot share `g_aoLocal`; the receiver's own shape
  is excluded at collect time (the ray starts on it) and the emitter's own per
  emitter (it ends on it); a 0.02 bias off the surface stops a prop resting ON
  a floor from shadowing it with its contact face. The occluder table and the
  viewport's occluder uniforms are now emitted whenever emitters exist, not
  only when the AO preference is on - a scene can have glowing lamps casting
  shadows and no baked occlusion. Shadows are HARD and blocky by construction
  (a wall throws a rectangle, not its silhouette; a detailed mesh shadows as
  its bounding box) - documented, with *Cast shadow* as the per-object opt-out
  for railings and grates. `examples/glow` gained `s3-shadow-wall`, a slab
  between the lava pit and one pillar, so the example demonstrates it (the
  opposite pillar is the unshadowed control).
  **Verified, after first getting the measurement WRONG:** the ray/shape math
  was unit-checked in isolation (6/6: through a box, beside it, segment too
  short, sphere in front / beside / behind). Then three A/B "measurements" of
  the baked atlas showed byte-identical results - because `--refresh-gen` does
  NOT run `texbake`, so all three compared the same stale PNG against itself.
  Redone with full `--build` on both sides: shadows off 23875 lit texels /
  1350450 total light, shadows on 22939 / 1279260 - 1451 texels darkened, **0
  brightened** (shadowing can only subtract), 5.3% of the light removed. In
  PCSX2 at 49-50 FPS (EE 38%, GS 10%) the shadowed pillar is dark on its
  pit-facing side while the unshadowed one across the pit is lit. The
  refresh-gen/texbake trap is now written into the tyra-testing skill - it
  silently "proves" that an asset change did nothing.
- (169) **Emissive light goes PER PIXEL: the AO atlas becomes the scene
  lightmap - plus two lighting-model fixes it exposed.** (167)'s baked light
  landed on vertices, and a plain box face is two triangles, so a strong
  gradient showed the diagonal split as a hard seam (owner spotted it
  immediately on a pillar next to the lava pit). Measured the cheap fix first:
  Detail 5 on the receivers kills the diagonal for ~nothing (12 -> 300 tris,
  EE 37% -> 39%) but trades it for a visible subdivision lattice, and does not
  help imported models. So: the atlas route, which turned out CHEAPER than
  expected because the AO atlas already had free space. `SceneAoAtlas` ->
  `SceneLightAtlas`, `bakeSceneAoAtlas` -> `bakeSceneLightAtlas`: ONE 256^2
  RGBA32 image now carries **A = occlusion, RGB = emissive light**, read twice
  by two passes - texturing is MODULATE, so the vertex color of each pass
  selects which channels it sees (BLACK vertex color for the alpha-over
  multiply, which is also the one-line fix that stops the light in RGB leaking
  into the occlusion; WHITE for the additive pass, `fogDisabled` for the same
  reason the refl pass sets it). Zero extra VRAM. The atlas is no longer gated
  on the AO preference - a scene can have glowing lamps and no occlusion - and
  `SCENE_AO_ATLAS_LIT` tells the game per object whether its light came from
  the atlas, so `pushVert` never also puts it in the vertex colors. **Textured
  receivers deliberately keep the vertex path**: a flat additive add would blow
  out dark texels (the vertex path multiplies the texture), and texture detail
  hides the Gouraud seam far better than a flat surface does.
  **Two lighting-model bugs the sharper output exposed**, both mine from (167):
  (a) the emitted light COLOR was the resolved `Ke`, which has the white-hot
  core folded in - so a green lamp cast near-white light. It is now the
  AUTHORED glow color (`objparser` parses it out of the `# tyra-glow` hint,
  falling back to Ke normalized by its brightest channel for a hand-written
  `Ke`); the white-hot core is an exposure effect on the emitter's own surface,
  not a property of the light it emits. (b) the facing term was
  `max(0, N.L)`, which lights one face of a box fully and its neighbour not at
  all - a seam on the corner. Went through a linear `0.35 + 0.65*N.L` wrap
  (matching the occluder term) and then to **half-Lambert squared**, because
  the linear wrap still reaches zero at a finite angle and that angle reads as
  a hard shading edge in a dark scene; the squared form is smooth everywhere,
  zero only directly away, with a faint back-hemisphere fill where a bounce
  would be. Atlas coverage went 13.4% -> 30.4% of lit texels on that change
  alone. All three twins moved together (aobake / generated game / viewport
  shader). Also corrected a **stale engine comment**: `additiveBlendFix` claims
  it drains the pipeline with FINISH barriers "so keep it to a handful of
  meshes" - the implementation moved the equation in-band with the mesh tags
  and dropped the barriers long ago. It nearly steered this design away from
  the extra pass. Verified: editor builds clean; `examples/glow` (Detail back
  to 1) rebuilt and booted in PCSX2 software renderer at a full 50 FPS
  (EE 45%, GS 10%), no asserts - the pillars shade smoothly with no diagonal
  and no lattice, the neon strips cast saturated green/magenta pools instead of
  washed-out white, and the wall gradients have no hard edge. Atlas contents
  checked directly by decoding the baked PNG (max light RGB, lit-texel share).
  **Follow-up on "the wall gradients still are not smooth":** zoomed the
  framebuffer 5x instead of guessing, and it is NOT a filtering bug (the 3D
  path defaults to TyraLinear) - it is 8-bit BANDING. A pool of light is a wide
  LOW-amplitude ramp (~30 of 255 levels spread across half the screen), so
  every level is a broad plateau whose bilinear-magnified edges read as
  irregular blocks. The bake now dithers the light with an ordered 4x4 Bayer
  pattern keyed on the ATLAS texel (sub-level, deterministic, never crawls);
  measured on the baked PNG, non-zero plateaus dropped to median 1 texel /
  p90 3 and the wall reads visibly smoother at 1x. What is LEFT is budget, not
  a defect - one 256^2 atlas for the whole scene (bigger = RGBA32 VRAM the GS
  does not have) and an 8-bit framebuffer with no dither on the blend - so the
  doc says so plainly and points at the two real levers: a tighter emitter
  reach (steeper ramp spends its levels over less surface) and a little film
  grain, which is exactly what PS2 games shipped for this. NOT attempted:
  weighting atlas texel density by how much light a region actually receives -
  a real idea, but it wants its own measure-first pass.
  `docs/emissive-materials.md` + the example README updated; skills updated.
- (168) **`examples/glow` - the emissive showcase.** A first-person midnight
  tour, one station per axis of (166)+(167), because the feature only makes
  sense side by side: (1) two signs with IDENTICAL `Kd` where only one carries
  `Ke` - one is full cyan, the other a dark silhouette; (2) a white-hot ladder
  of three boxes at the same glow strength and 0 / 0.35 / 0.7 core, which also
  demonstrates that the bright pass is PER CHANNEL (the halos go red ->
  salmon -> white); (3) a glowing lava plate with reach 13 lighting four
  `concrete.mtl` pillars, plus a fifth identical pillar parked at x=19, just
  past the reach - same material, one orange one black; (4) a neon alley where
  four strips paint separate colored pools on the walls and overlap in the
  middle. A `bloom-pulse` Empty runs *Every 14s -> Set Bloom 0.45* in parallel
  with *Delay 7 -> Set Bloom 1.5*, so the halo collapses and blows back out
  unattended - a live demo of the new 0..2 bloom range (the sky-cycler pattern
  from `examples/reflections`). Scene lighting is one nearly-off cold
  directional (`ambient 0.06`, `diffuse 0.04`) plus fog to near-black at 70
  units: every warm pixel in the frame is a material. Verified: Docker build
  OK, booted in PCSX2 software renderer at a full 50 FPS (EE 40%, GS 13%) with
  9 static batches and no warnings in `bin/log.txt`; the opening frame shows
  all four stations at once, with the per-channel halo colors and the lit /
  unlit pillar pair both clearly readable. README written; listed in the
  root README's example section.
- (167) **Glow, part 2: it actually blazes now - white-hot core, bloom spread /
  over-add, and baked emissive LIGHT.** (166) shipped the emissive floor but
  "even at max it doesn't really glow" was the honest verdict, and the reason
  is structural: at glow 1 an untextured surface is ALREADY at the framebuffer
  maximum in its own hue, so there is no brighter orange to reach. Three
  answers, one per axis:
  (a) **White-hot core** (Material Editor): added to every channel, so the
  surface desaturates toward white the way an overexposed emitter does on
  camera - the only direction left, and it pushes every channel over the bloom
  threshold too. The `.mtl` now stores the RESOLVED emission in `Ke`
  (`glowColor x glow + white`, capped 1.99) with the authored controls in an
  extended `# tyra-glow <strength> <r> <g> <b> <white>` hint; the one-number
  form from (166) and a bare hand-written `Ke` both still load
  (`App::matEdKe` is the single definition of the resolved value, so the file
  and the previews cannot disagree).
  (b) **Bloom got a shape**: `bloom` now goes to 2 (its GS blend FIX is a whole
  byte, so the blur can be over-added - the load clamp and the Set Bloom node
  moved to 0..2 with it), and a new **Spread** setting maps 0..1 onto 1..4
  soften rounds over the quarter-res buffer, each with DOUBLED tap offsets, so
  the halo grows geometrically from a fringe into a corona (`setBloomSpread`,
  4 GS sprites a round, ping-ponging low0/low1). The postfx packet grew
  352 -> 512 qwords for the worst case; an undersized packet corrupts the GIF
  stream, so that bound is now spelled out in the comment.
  (c) **Baked emissive light** - the "wypalone jak AO swiatlo" step: tick
  *Lights up surroundings* (reach + strength, `# tyra-glow-light`) and the
  emitter's light is folded into the vertex colors of everything around it.
  Deliberately the AO machinery in reverse: `collectOccluders` and the new
  `aobake::collectEmitters` share one `objectShape()`, the game answers both
  with one `occShapeAt()` distance-to-shape query (templated over AoOccData /
  EmisLightData, which share the shape prefix), and the emitter list is pruned
  ONCE per object / per terrain chunk - the per-vertex-table-scan dcache
  disaster recorded above stays avoided. Falloff is quadratic from the
  emitter's SURFACE (a long neon strip lights evenly along its length) times
  N.L with no side-on floor, so light never leaks onto back faces; an emitter
  never lights itself. Table lands in `ao_data.gen.hpp` (`SCENE_EMIS`), gated
  on emitters existing, independent of the AO preference. Receivers: objects,
  static batches AND terrain. Viewport twins for all of it (`uEmis*`, capped
  at 8 nearest the camera; `collectEmitters` reads `.mtl` files so the viewport
  hands in a `GlowCache` member cleared by `invalidateAssets`).
  **Fixed in passing:** `rebuildStaticBatch` never re-pruned `g_aoLocal` per
  member, so every member of a batch was shaded with the FIRST member's
  occluder set - it now collects per member (and per member for the emitters,
  which is what surfaced it). Verified: editor builds clean; the (166) scratch
  project extended (white-hot 0.3, reach 9 / strength 1.6, bloom 1.4 /
  threshold 0.5 / spread 0.7) refreshed, Docker-built with the engine rebuilt,
  booted in PCSX2 software renderer at a full 50 FPS - the emitter is now a
  white-hot core inside a wide corona, the terrain under it carries a warm
  quadratic pool of light, and the matte box next to it (identical `Kd`, no
  `Ke`) is lit orange on the face turned toward the emitter where before it
  was a black silhouette; the editor viewport shows the identical picture.
  Material Editor's Glow panel (which now also reports the project's bloom
  setup and warns when bloom is off) still wants a human eyeball pass.
  `docs/emissive-materials.md` rewritten around the three axes; README, docs
  index and both skills updated.
- (166) **Emissive ("glowing") materials + a bloom bright-pass threshold.**
  Step 1 of the glow feature: a material can now light *itself*, so it keeps
  its own color in a pitch-black scene. Authored in *Material Editor > Glow
  (emissive)* (strength 0-2 + a glow color seeded from the material color,
  "Match material color" button), stored as the standard Wavefront `Ke`
  statement with the color x strength split riding in a `# tyra-glow` comment
  — the `# tyra-brightness` pattern, so hand-written `Ke` from any exporter
  still works (brightest component = strength; `Ke 0 0 0` = matte).
  Implementation is deliberately *not* a light: `pushVert` clamps the finished
  shade up to `Ke` as the LAST step, after Kd, AO and point lights, so the
  emissive floor ignores darkness on the way down and a brighter lit result
  still wins; the object tint multiplies on top and GS fog still applies. Cost
  on the console = three compares per vertex during the one-time geometry bake
  (spawned clones and static batches included — `g_primKe` is staged next to
  `g_primKd` in both `rebuildObjectGeometry` and `rebuildStaticBatch`), then an
  ordinary bag. The viewport is the GLSL twin (`uEmissive`, one-shot per draw
  so gizmos/wires/markers can never inherit it) and so is the Material Editor
  preview. Objects with an emissive material are dropped from the baked **AO
  lightmap atlas** (`materialGlows` in aobake.cpp): that pass darkens per pixel
  in a separate draw, which a floor baked into vertex colors cannot clamp back
  up — they keep the per-vertex AO path, where the floor wins. `Ke` parsing
  added to `objparser` (host) and `LeanObjLoader` (engine, both
  `LeanObjMaterial` and `LeanMtlMaterial`); the asset-import and texbake `.mtl`
  rewriters already pass unknown lines through verbatim, so nothing else moved.
  **The halo** is the second half: the engine's bloom got a real bright pass
  (`RendererCorePostFx::setBloomThreshold`) — one extra quarter-res sprite that
  subtracts a flat grey from the downsampled frame through
  `(0 - Cs)*128/128 + Cd`, which the GS clamps at zero, so everything below the
  cut drops out of the blur entirely. Without it bloom veils the whole picture
  (soft focus) and an emissive object does not read as glowing. Exposed as
  `ProjectSettings::bloomThreshold` (*UI Editor > Bloom + color grading >
  Threshold*, per-scene overridable under *Post effects*), 0 = the historical
  whole-frame behavior, so existing projects are untouched. `flatQuad` grew
  optional w/h for the low-res target. **Known gaps, by design for step 1:**
  animated `.glb`/`.fbx` models ignore `Ke` (the skeletal VU1 rig has no
  emission slot — the same reason `refl` is ignored there), terrain ignores it,
  and an emissive material does not illuminate its surroundings — the baked
  "emissive light" pass (the AO-style de-luxe version the feature was asked
  for) is the queued step 2. Verified: editor builds clean; scratch project
  (`glowtest`, near-black ambient 0.04 / diffuse 0.02, two identical-`Kd`
  boxes differing only in `Ke`) refreshed, Docker-built with the engine
  rebuilt, booted in PCSX2 software renderer at a full 50 FPS — the emissive
  box renders bright orange with a clean halo while the matte twin is a barely
  visible dark silhouette and the terrain/sky stay black (the threshold kept
  the bloom off them, and no wrap-around artifacts, confirming the GS clamps
  the subtract); the editor viewport shows the identical pair. The Material
  Editor's own Glow panel still wants a human eyeball pass (no synthetic input
  into the GUI here). New doc: `docs/emissive-materials.md`; README + editor
  and engine skills updated.
- (165) **Artist-authored mesh LOD meshes for static models.** Automatic
  decimation is not always what an artist wants (and the decimator refuses to
  touch small parts or cross uv seams, so some models barely shrink), so a
  model can now name its own levels: a new `modelLods` manifest section maps a
  model asset path to its tier files, and the bake folds those meshes into the
  `.tmdl` instead of decimating. Each level is validated - the same material
  set (same `usemtl` names, same order) and strictly fewer vertices than the
  level before it - and a failure warns with the reason and drops the WHOLE
  custom chain back to decimation, so a half-broken hand-authored chain never
  ships silently. Tier `.obj` files stay out of the bake (their geometry lives
  in the model's `.tmdl`). UI: a *LOD...* button per model in Assets picks the
  levels (candidate triangle counts shown, "(auto - decimate)" the default);
  clearing a level clears the coarser one; deleting a model drops it from
  every other model's chain. The per-object *Override mesh LOD* row now shows
  for static models too (without the animation-LOD row / yaw offset, which are
  skeletal-only). Also fixed a pre-existing staleness bug next door: the
  model-import path erased `modelInfoCache_` by the bare asset path while the
  cache is keyed `"<model>|<material override>"`, so a re-imported model kept
  its old triangle/material summary until a project reload. **Verified:**
  `--refresh-gen` + `--resave` round-trip on a scratch project - the custom
  chain lands at exactly the authored sizes (9216 -> 2304 -> 576 corners, vs
  4368/1938 from the decimator), a mismatched material set warns and falls
  back, the tier `.obj` files never reach `bin/`; in PCSX2 the 12-instance
  scene renders at 6.5 ms/frame with the hand-authored levels (11.5 ms with
  decimated, 27.1 ms with none). Docs: new docs/model-pipeline.md +
  animated-models.md / texture-atlasing.md / streaming-layers.md / README.
- (164) **Static mesh LOD: distance levels for `.obj` models.** Mesh LOD
  existed only for animated models because the `.tskl` had somewhere to put
  the decimated variants; now the `.tmdl` carries them and static objects
  switch on distance the same way skinned instances do (same hard `d` / `2d`
  thresholds, picked in `renderScene` next to `beyondDrawDistance`, which
  already needs the distance). The welding/quadric-collapse machinery moved
  out of `glbparser.cpp`'s anonymous namespace into `src/meshlod.{hpp,cpp}`
  with the tier policy (ratios, size floor, shrink slack) shared, so both
  bakes decimate to the same shape. **The trap that cost the first attempt:**
  a static mesh must NOT weld on normals. This pipeline derives a flat normal
  per face (`vn` is ignored), so every corner of a position carries a
  different normal, every position looks like a uv/normal seam twin, the
  collapse's position-twin lock fires on all of them and nothing decimates -
  the first version produced byte-identical "tiers". Static tiers weld by
  position+uv and recompute face normals afterwards, which is what flat
  shading wants anyway; the animated path still welds on normals (authored
  smooth normals are real data). Runtime: `GeoPart` grows per-tier baked
  buffers, shaded the first time an object renders that far away and kept, so
  a flip only re-aims bag pointers and each tier keeps its own frustum-bbox
  cache entry (that cache is keyed by vertex pointer). Invariants: collider
  and AABB stay model-level (collision never changes with camera distance), a
  rebuild drops every resident tier (a moved or Live-Link-patched object must
  not keep stale distant copies), highlight shells are invalidated on a flip,
  and physics fast-path bodies + texture-feed objects keep the full mesh since
  both depend on post-bake edits to the tier-0 buffers. **Verified** (PCSX2 SW
  renderer, PAL, COP0 around `renderScene`): 12 instances of a 9216-vertex
  model at 8 units went 27.1 ms -> 11.5 ms per frame, a hard 25 FPS -> a
  steady 50, VU 23% -> 15%; tiers came out at 4368 (47%) and 1938 (21%)
  corners and the `.tmdl` grows 295 KB -> 497 KB when they are baked (the gate
  keeps them out entirely when no distance is set). The refactor left the
  animated path bit-identical: `two-players`' `cat.tskl` bakes to the same
  SHA-256 before and after.
- (163) **Static models ship as a binary `.tmdl` - the EE stopped parsing
  `.obj`.** Static models were parsed as ASCII on a 300 MHz EE on every load:
  a `std::istringstream` per line, iostream float parsing, a `sqrtf` normal
  per face, a `std::map` lookup per `usemtl`, output vectors grown without a
  known count - and with streaming layers loading one asset per frame, that
  whole parse lands inside one frame. Everything it computed is a pure
  function of build-time inputs, so `templates::bakeStaticModels` (called from
  `refreshGenerated` next to the animated bake, so `--refresh-gen` produces it
  with no Docker and no GUI) resolves it once into `src/tmdl.hpp`'s format:
  triangulation, flat normals, the V flip, material assignment including a
  per-object `.mtl` override, texture-atlas UV rects folded into the UVs, and
  bin-relative texture paths. The PS2 side is a sequential read plus a memcpy
  per part, because the stored layout is exactly the interleaved 8 floats
  `GameModelPart::verts` already holds. Conventions copied wholesale from
  `.tskl` (4-byte magic, `u32` version accepted as a range, packed
  little-endian, fixed NUL-padded strings, bounds-checked sequential reader,
  soft-fail with `TYRA_WARN`); the engine's new `TmdlLoader` returns the same
  `LeanObjMesh` the `.obj` loader does, so the game keeps ONE geometry path
  and `LeanObjLoader` stays as the fallback. `texbake` stops mirroring an
  `.obj` whose `.tmdl` was baked, the Runner sweeps a superseded `.obj` out of
  `bin/` (the copy-back has no `--delete`, so a project built earlier would
  keep shipping the ASCII copy), and ISO export now claims the ARTIFACT name
  for the scene load group - static `.obj` as `.tmdl`, animated `.glb` as
  `.tskl` (the latter had the same pre-existing gap, landing every animated
  model in the "other" group). **Verified** (PCSX2 SW renderer, host: boot,
  COP0 Count around the load, 9216-vertex model): the loader call went
  286.4 ms -> 39.2 ms (7.3x), the whole `loadModelAsset` 306 ms -> 59 ms.
  Equivalence was checked by loading BOTH formats in one run and comparing on
  the console: positions and UVs are bit-identical (the atlas rect fold
  included), normals differ by at most 146 ulp because the cross product now
  runs on the host FPU instead of the EE's non-IEEE one - which also makes the
  console's normals match the editor viewport's. Two plan assumptions died
  here: the disc gets BIGGER (168 KB of indexed ASCII -> 295 KB of flat
  triangle list; RAM is unchanged, the `.obj` path built the same arrays), and
  screen-level pixel A/B is meaningless in these scenes - the orbit camera is
  at a different phase in every run. Design doc: docs/static-model-format-plan.md.
- (120) **Scripts panel cleanup: `src/scripts/` is exclusively the user's;
  generated sources moved to `src/gen/`; subfolders supported.** The Scripts
  list used to show the six engine-generated `*.gen.cpp` files (flow_graph,
  sequences, screen_fx, live_link, navigation, object_scripts) next to the
  user's own scripts — confusing ("what are these files I never wrote?").
  Now: (1) codegen writes them to `src/gen/` (registrations in
  `templates::generate` + the always-overwrite list in `refreshGenerated`;
  the engine's `Makefile.base` finds sources recursively, so no Makefile
  change), (2) `refreshGenerated` **deletes stale copies** from
  `src/scripts/` on the first build of an older project — critical, a
  leftover pair would be compiled twice into duplicate symbols, (3) the
  Scripts panel and the `TYRA_OBJECT_SCRIPT` attach-scan walk `src/scripts`
  **recursively** (subfolder scripts list as `ai\guard.cpp`, compile and
  scan like any other; `*.gen.cpp` filtered out for good measure), and (4)
  **New script...** accepts `ai/guard` to create `src/scripts/ai/guard.cpp`
  (segment-validated, class name from the basename). All 11 example
  projects regenerated (old copies pruned, new `src/gen/` committed); docs +
  ai-support skills + editor-skill path references updated. Verified:
  editor builds clean; `examples/script-demo` Docker build passes with the
  new layout (link line shows `obj/gen/*.gen.o`); a scratch project with a
  hand-made `src/scripts/ai/guard_brain.cpp` object script compiles and
  links via the recursive Makefile find (=== Build OK ===). The panel's
  visual list (nested rel-paths render, click opens VS Code) still wants a
  quick GUI eyeball pass.
- (162) **Scripts panel polish: subfolders render as a tree, the help
  blurb moves to a `(?)` tooltip.** Follow-up to (120)'s GUI eyeball note.
  The panel used to print a four-line explanatory paragraph under the list on
  every frame (object vs global scripts, "generated code lives in src\gen") -
  a wall of text that dwarfed a short script list. It's now a single `(?)`
  hover next to the New script... / Open in VS Code buttons. Subfolder scripts
  used to list as flat backslashed rel-paths (`sub\my_script.cpp`); the panel
  now builds a small `ScriptNode` tree from the recursive `.cpp` scan and
  renders folders as `DefaultOpen` `TreeNodeEx` nodes with the files nested
  under them (folders sorted first, files alphabetical). The per-folder
  TreeNode ID scope also fixes a latent ImGui ID collision - two files named
  the same in different folders (root `my_script.cpp` vs `sub\my_script.cpp`)
  now get distinct IDs instead of sharing one selectable. Click still opens
  the file in VS Code (the tree accumulates the `src\scripts\`-relative
  prefix for the path). Verified: editor builds clean. Pure editor UI, no
  codegen or generated-project change.
- (163) **Animation editor: non-destructive clip retiming/trim/rename +
  a project-wide animation fps.** Reported as "animations exported from
  Blender play too slow in game". Not an NTSC/PAL or a wall-clock bug:
  glTF and FBX store keyframe times in **seconds and no frame rate at
  all**, so a clip animated for 30 fps but exported from a 24 fps Blender
  scene simply *is* 25% too long in the file, and there is nothing for the
  importer to detect it by. Two layers of fix, both baked at build time so
  the console pays nothing and the source assets are never rewritten:
  (a) **`ProjectSettings::animSourceFps` / `animPlayFps`** (Preferences >
  Rendering, "exported -> should play at") give one project-wide speed
  ratio; equal values (the 24/24 default) are an exact no-op, so existing
  projects are untouched.
  (b) **`Project::animClipEdits`** - one `AnimClipEdit` per touched
  (model, SOURCE clip): rename, time scale, trim window, default loop.
  New `animedit.cpp` folds both into the parsed `glbparser::Skel` right
  before `writeTskl` in `bakeAnimAssets` - trim (interpolated boundary
  keys, rebased to 0) -> scale times -> rename. New manifest section
  (`Section::AnimEdits`, `kSectionCount` 11 -> 12) so it also travels the
  collaboration wire; the section is conditional, so an untouched project
  emits nothing.
  **Tools > Animation Editor** drives it: model picker, clip list, live
  animated preview (`Viewport::renderAnimPreview` + the new
  `uploadAnimPose` worker shared with the scene preview) with its own
  playhead, and the four fields. The scene viewport applies the same
  numbers via `Viewport::setAnimEdits` (pushed per frame by the app, the
  nav-overlay/decal pattern), so a placed object previews what will ship.
  Clip *references* (`SceneObject::animClip`, the Player locomotion
  clips, the Animation node's Clip param) store the EFFECTIVE name, so
  `App::effectiveClips` feeds every picker and `renameAnimClipRefs`
  retargets on rename - a node driving another object through an object
  link can't be resolved from there and is called out in the tooltip.
  Clip edits are build-time, so they also join `liveLinkContextHash`: the
  LIVE chip flips to amber (rebuild) instead of pretending a retimed clip
  reached the running game.
  Verified: exact numbers out of the baked binary. A scratch project with
  `wobbler.glb` (2 clips, 2.000 s each) baked, then fps 24->30 plus
  `Wiggle` timeScale 2, trim 0.5-1.5 s, rename "WiggleFast": a `.tskl`
  reader shows `WiggleFast` at **0.4000 s** (1.0 s trimmed / 2.5x, 68 -> 36
  keys) and `Twist` at **1.6000 s** (2.0 / 1.25, keys untouched) - both
  exactly the predicted values. `.tyra` round-trip through `--resave` is
  byte-identical; `--refresh-gen` on `examples/object-spawning` (an
  animated model, no edits) rewrites the `.tskl` byte-identically, i.e.
  the default ratio really is a no-op. Full Docker build of the scratch
  project = `Build OK`, boots in PCSX2 at a steady 50 FPS with the model
  rendering and no assert in `bin/log.txt`. The panel itself was eyeballed
  from a screenshot with the project's values loaded (list, preview,
  transport, "authored 2.000 s -> ships as 0.400 s (2.50x)"). All 17
  example projects resaved for the two new settings keys (which also
  picked up pre-existing drift: `textureAtlas`, `keyboardMouse*`,
  `camStyle`/`camPitch`/`camYaw`/`camRotate`). `examples/endless-scroller`
  could not be resaved - it has no `.tyra` at all on main, only committed
  build leftovers; left alone, flagged separately.
- (164) **Fix: animated models were tinted and point-lit in the editor
  only.** Found while investigating (163) - the user's third-person avatar
  had a blue face in the editor and looked right in game. The viewport
  multiplied `SceneObject::color` into every animated part and let the
  scene point lights add on top; the console does neither (a `SkelInstance`
  folds only the `.tskl` part color into its `litColors`, and point lights
  are baked into static vertex colors - which `docs/animated-models.md`
  already promised under "Point lights don't light the model"). The fpp
  project template gives its Player a cyan marker color `(0.15, 0.9, 0.9)`,
  so switching that Player to third person previewed the whole avatar cyan.
  All three animated draw sites (scene pass, mirror reflection, mirror
  player avatar) now go through one `drawAnimParts()` helper drawing with
  the neutral shade tint and suppressing the point-light uniform for the
  duration. The properties table's **Color** row claimed the opposite of
  what the runtime does - corrected. Verified: editor builds clean, and the
  divergence was confirmed by reading both sides (`viewport.cpp` uTint /
  `uLightCount` vs `templates.cpp` `setupAnimObject`, where `o.data.color`
  appears nowhere in the animated path while the static path at
  `pushVert` does fold it in). Flashlight and fog on animated parts are
  the same class of editor-only extra but were left alone - unlike the
  tint and point lights, whether the StaPip animated bags get them on the
  console is not settled, and guessing would trade one divergence for
  another.
- (151) **Edit an animated model's materials: create an override from its
  built-ins, and preview/paint it on the model in the Material Editor.** (150)
  let an animated `.glb`/`.fbx` take a Material (.mtl) override, but you still
  had to hand-author that .mtl blind - the Material Editor could only preview a
  material on a primitive or a static `.obj` (a `.glb` has no sibling `.obj`),
  and Properties showed a read-only, confusing "Materials" list of the built-in
  colors with no way to act on them. Three changes close the loop:
  (a) **the Material Editor previews on animated models too** - the preview/bake
  mesh path (`Viewport::buildMatPrevAnimated`, `meshInputFromBaked` in app.cpp)
  now builds from a `.glb`/`.fbx` **bind pose** (frame 0) with the assigned .mtl
  resolved exactly as the console bakes it into the `.tskl` (name-matched full
  replace, textures off disk so live painting updates through the shared
  texCache_), the shape picker lists animated models, and *Properties > Material
  > Edit...* now hands the animated model to the editor instead of suppressing
  it; (b) **a "+ New material from this model..." entry** in the Properties
  material combo (`App::createMaterialForModel`) extracts the model's built-in
  materials - part names as `newmtl`, base colors as `Kd`, embedded/referenced
  textures written next to the .mtl - into a fresh `res/materials/<model>.mtl`,
  assigns it and opens it previewed on the model (the answer to "can I import
  the built-in material?" - yes, that IS the seed); (c) the read-only built-in
  **Materials list in Properties is gone** (it only confused - the picker is how
  you act on materials now). An unnamed material can't be name-matched by an
  override, so seeding reports that instead of writing a dead file. Verified
  headlessly: a harness reusing the real `glbparser`+`objparser` seeded a valid
  .mtl from `wobbler.glb` (1 part `WobblerBody`) and `spider2.glb` (3 parts
  `spider`/`spider.legs`/`spider.tee`) and round-tripped each through
  `applyMaterialOverride` (all matched); `cat.glb`'s unnamed material was
  correctly refused. Assigning a seeded .mtl to a wobbler in a scratch showcase
  copy, `--refresh-gen` emitted a distinct `wobbler__ovr3a65.tskl` variant
  alongside the base `wobbler.tskl`; editing that .mtl's `Kd` to magenta re-baked
  the variant `.tskl` to carry `(1,0,1)` (teal gone). Editor builds clean. The
  in-editor GL preview + paint-on-the-animated-mesh and the combo button are a
  GUI path (no synthetic-input automation here) - they need a hands-on look.
- (150) **Animated models honor a Material (.mtl) override, as an extra option
  on top of the built-in materials.** Until now an animated `.glb`/`.fbx` model
  drew only with the materials baked into the file (the docs said so explicitly);
  a static `.obj` model could already take an assigned `.mtl` as an override that
  replaces its own libraries by `usemtl` name, but the animated path dropped the
  override on the floor (`collectAnimModelPaths` keyed the model identity on the
  path alone, the properties UI suppressed the Material combo with an
  `!animatedModel` guard, and nothing consumed `materialPath` at bake). Now the
  same `SceneObject::materialPath` field drives both: the Material combo shows
  for animated models too (the built-in materials still list read-only above it,
  so it is an *option besides*, not instead - empty = the model's own), and the
  override is resolved into the `.tskl` **at bake time** - no engine/runtime
  change. Mechanically: the animated-model identity became the pair
  `{modelPath, materialPath}` (`collectAnimModelKeys`, mirroring the static
  `collectModelKeys`), so the same `.glb` with two different overrides bakes to
  two distinct `.tskl` files (`animBakedTsklRel` derives a stable
  `__ovr<fnv16>` suffix, shared verbatim by the emit in `modelDataHeader` and
  the bake in `bakeAnimAssets`); the override textures extract next to that
  variant `.tskl` with a variant-unique prefix so they never clobber the base
  model's. The remap itself is a new shared `objparser::applyMaterialOverride`
  template (instantiated for both `glbparser::Baked` and `Skel`): it matches each
  part's material NAME against the library, a hit replacing the part's baseColor
  (Kd) + texture, a miss falling back to plain white/untextured - **identical**
  to how `objparser::load`'s `overrideMtl` resolves a static `.obj` (a full
  replace, not a merge; `refl` has no skeletal slot and is ignored). The viewport
  preview calls the very same helper on its baked model (cache re-keyed by
  `path|mtl`), so what you see matches what the console bakes. Verified headlessly
  on a copy of `examples/showcase`: gave one of six `wobbler.glb` objects an
  override `.mtl` named `WobblerBody` (magenta Kd + a `map_Kd`), `--refresh-gen`
  then emitted `ANIM_MODEL_COUNT = 2` with `wobbler.tskl` (base, untextured - the
  other five) **and** `wobbler__ovre8c2.tskl`; the override `.tskl` contains the
  magenta baseColor (absent from the base) and references
  `models/wobbler__ovre8c2_ground.png`, and `scene_data.hpp` maps the overridden
  object to `animModel` index 1 while the five untouched ones stay 0. Editor
  builds clean.
- (128) **Third-person camera styles — top-down and isometric games.** The
  third-person Player grows a **Style** picker (Properties > Third-person
  camera): **Orbit (behind)** is the unchanged free-look rig; **Top-down**,
  **Isometric** and **Fixed angle** pin the camera to an authored **Angle**
  (elevation above the horizon, 10–85°) and **Direction** (world heading =
  which way is "up" on screen), with an optional **Right stick rotates**
  switch that lets the player orbit the yaw while the pitch stays pinned.
  Top-down (80°/0°) and Isometric (35°/45°) are just presets of Fixed angle —
  picking them seeds the angles, which stay editable. Runtime cost is a
  couple of compares in the shared player walker: the fixed styles write
  `P.pitch = -PP_CAM_PITCH(pi)` every frame and skip the right-stick reads
  (scene load also starts the player's yaw on the authored heading instead of
  behind the avatar); everything else — camera-relative left-stick movement,
  the spring arm, locomotion clips, Distance/Height/Shoulder — is the existing
  third-person machinery, which is exactly why a fixed steep angle "just
  works". Full chain: `playerCamStyle/Pitch/Yaw/YawRotate` on `SceneObject`
  (+ `==`, live-link recipe hash), `camStyle/camPitch/camYaw/camRotate` in
  the `player.thirdPerson` JSON block, per-player `PLAYER_/PLAYER2_ CAM_STYLES/
  _PITCHES/_YAWS/_YAW_ROTATES` scene_data tables (angles baked in radians) +
  `PP_CAM_*` accessor macros, the gated stick block in the game template, the
  Style combo + Angle/Direction/rotate controls in Properties, README +
  docs/animated-models.md. (Rebased onto the two-player refactor: the camera
  style is per Player object, so P1 and P2 can each carry their own style.)
  **Verified:** editor builds clean; scratch fpp project flipped to
  `thirdperson`+`topdown` round-trips the new keys through `--resave`;
  generated `scene_data.hpp` carries `PLAYER_CAM_STYLES = {1}` /
  `PLAYER_CAM_PITCHES = {1.309F}` (75°) and the game compiles in Docker;
  **PCSX2 boot (emulog "is executing", no TYRA banner in `bin/log.txt`) with
  F8 snaps of the SAME scene in two styles**: top-down 75° (avatar dead
  center seen from above, landmark boxes flat on the checkerboard) and
  isometric 35°/45° (horizon visible, avatar centered, boxes framing the
  diagonal) — the camera holds the authored angle instead of orbiting.
  Editor GUI screenshot shows the new Properties section loading those
  values (Style=Isometric, 35/45, rotate on). Stick *feel* (right-stick
  rotation in fixed styles, camera-relative walking) still wants a hands-on
  pad test — keyboard pad bindings cover buttons, not analog sticks.
- (161) **AO reshaped: per-object "Cast shadow", textures only, model AO
  parked** - owner feedback on (159/160): the shadows only look right in the
  textured version, control belongs on the object, and per-vertex model AO
  reads as triangulated shading on authored meshes. So: the "AO quality"
  switch is GONE (`aoTextured` removed from settings/preset/serialization) -
  the texture path is the only one; the terrain per-vertex grid
  (`TERRAIN_AO_TABLES` + the shadeAt multiply + the chunk occluder staging)
  is deleted; a new `SceneObject::castShadow` (default on, Properties >
  "Cast shadow" + the multi-select row, serialized only when false, folded
  into liveLinkRecipeHash) gates `aobake::collectOccluders`, so casting is
  per object while receiving stays automatic. Model receive/self-AO is
  DISABLED (g_aoOff staged for type 5 in the generated rebuild; texbake no
  longer writes .aov sidecars and sweeps stale ones; the viewport stops
  baking model self-AO and skips model fragments via a new uAoReceive
  uniform) - the whole pipeline (aobake::modelAO, the sidecar format, the
  LeanObjLoader reader) stays in-tree with comments pointing at a future
  per-model lightmap-unwrap. Batching got smarter: an object whose atlas
  regions come out fully lit is dropped from the atlas (firstRegion -1) and
  stays batchable; covered objects render solo (the same deterministic bake
  reused in the scene-table emitter for the eligibility bit). Verified in
  PCSX2: the box casts onto terrain AND onto the wall, the sphere keeps its
  contact blob, the wall with Cast shadow OFF darkens nothing while still
  receiving the box's shadow, 50 FPS. Possible future win (backlog-worthy):
  merge the AO passes into static-batch bags (they share one atlas texture)
  to win batching back.

- (160) **Textured AO quality mode (experimental)** - the follow-up to (159)
  after the owner's PCSX2 check: per-vertex AO on the sparse terrain grid and
  on 2-triangle primitive faces shows its Gouraud diamonds. New **AO quality**
  switch on the ambience preset (`aoTextured`): the same occlusion bakes into
  **per-pixel AO textures** - a terrain AO map (heightmap self-occlusion +
  occluder contact, `aobake::terrainAOMap`, ≤256²) and a per-scene **primitive
  lightmap atlas** (`aobake::bakeSceneAoAtlas`: shelf-packed regions per
  builder UV layout - box 6 faces / sphere 1 / cylinder 3 / cone 2 / plane 2 -
  rasterized on the host with the same occluder+ground formulas, now also
  host-implemented as `aobake::occluderOcclusionAt`). Both draw as extra
  alpha-blended passes (black texture + GS alpha-over = exact per-pixel
  `Cd*(1-a)` multiply): the terrain pass after base+layers per chunk, the
  object pass per part right before the additive env pass, reusing the
  layer-blend info bag; pushVert emits atlas STs (builders bump `g_aoRegion`)
  instead of multiplying the shade. texbake writes the PNGs into
  `.res-baked/aomap|aoatlas/`; codegen emits the matching rects from the same
  deterministic bake. Covered objects leave static batching; models/physics/
  pickable/save-state/clones keep the vertex bake. Two dead ends worth
  remembering: (a) the engine's palettized tRNS→CLUT path loses the smooth
  alpha gradient - a pngquant-quantized AO map renders as NOTHING in PCSX2
  (an untextured red-probe pass proved the blend pipeline itself fine), so
  the AO textures ship as RGBA32, capped at 256² for VRAM; (b) the first
  "no AO on screen" was a stale-ELF screenshot - verify the camera pose
  before debugging pixels. Verified in PCSX2 both ways: textured mode shows
  smooth per-pixel contact shadows (sphere blob, box-on-wall shadow, no
  triangle edges) at 50 FPS; flipping the combo back reproduces the vertex
  look exactly. Editor viewport previews per fragment in both modes (its
  usual look ≈ textured); GUI screenshot still blocked by the machine's
  white-window quirk - see (159).

- (159) **Baked ambient occlusion** (docs/ambient-occlusion.md). Soft contact
  shadows folded into the same per-vertex colors the directional light bakes
  into - zero PS2 per-frame cost. Three bakes: **terrain self-occlusion**
  (host, `aobake::terrainAO` 8-direction horizon scan → `TERRAIN_AO_TABLES`
  in terrain_heights.gen.hpp; the viewport multiplies the identical grid),
  **contact darkening** (host reduces solid objects to oriented-box/sphere
  occluders → `inc/ao_data.gen.hpp`; the EE evaluates the response per vertex
  at scene load in pushVert/shadeAt - `aoOccluderAt`/`aoShadeMul`, pruned per
  object/chunk per the point-light dcache lesson - plus a ground-contact term
  off the bilinear heightmap), and **raycast model self-AO** (host,
  `aobake::modelAO`, 24 deterministic cosine-weighted rays per obj position
  with an XZ-grid accel; texbake writes a `<model>.aov` sidecar into
  `.res-baked/models/` that the engine's `LeanObjLoader` quietly picks up -
  grazing hits are rejected instead of excluding "triangles containing the
  vertex", because on low-poly models that exclusion removes entire adjacent
  walls and no interior corner ever darkens; the first bake proved that with
  an all-255 sidecar). The occlusion response formula is twinned in the
  viewport fragment shader (per fragment, live - the same pattern as the
  point-light preview); occluder SHAPES and both grid/model bakes are
  single-source in aobake.cpp. Settings `aoEnabled/aoStrength/aoRadius` on
  ProjectSettings + AmbiencePreset (Ambience Editor block, tooltips + a
  static-bake caveat note); new projects enable it on their Default preset,
  pre-AO projects read as off. Animated models neither cast nor receive
  (they relight dynamically, like with baked point lights); a runtime-moved
  object re-bakes its own shading on rebuild but its cast shadow stays where
  the scene was built (documented). Verified: editor build clean; headless
  fixture (boxes + wall + sphere + an open-front hut .obj) - occluder table,
  per-scene constants and AO grid inspected in the generated sources; full
  Docker build compiles the generated EE code + the LeanObjLoader fork; PCSX2
  A/B screenshots (AO off vs on) show contact blobs under the sphere/boxes,
  wall-base darkening and the hut's interior-corner gradient, and the .aov
  sidecar bytes match expectations (dark back corners 134-152, open front
  217-236). The ground term got a 0.7 damp after the first A/B (full
  half-hemisphere read too muddy on wall bases). Editor-viewport visual
  parity could not be screenshotted this session - the GUI presents a white
  window on this machine even on a pre-change baseline build (AMD GL
  present/compositing quirk, PCSX2's D3D window captures fine) - the shader
  compiles clean (no stderr) but a human should eyeball the live preview
  against the PS2 output.

- (158) **Reflection examples split into three focused levels.** The
  combined examples/raytraced-mirror had grown to carry the VU0 raytracer
  AND the texture feeds; per owner it is now three single-topic showcases:
  **raytraced-mirror** keeps only the RT mirror (glass wall + balls +
  textured crate + animated wobbler + floor/pillars); **texture-feeds** is
  new (CCTV camera feed on one monitor, a raytraced mirror streamed onto
  another - both live in one frame, and the camera feed's terrain vs the
  mirror stream's terrain-less sky make the two systems visibly distinct);
  **probe-aim** (157) is the third, unchanged. Verified in PCSX2 (SW
  renderer): all three boot clean and show their effect. Authoring note
  worth keeping: in the generated game's view, +X world maps to SCREEN
  LEFT - a monitor at -X shows on the right; two rounds of "which monitor
  is which" confusion traced to that, not to any feed bug. Example
  generated files were regenerated in this same change.

- (157) **Reflection probe aim: reflected ray (Preferences > Rendering,
  docs/reflective-materials.md).** The @sky dynamic env map's camera can
  now aim along the REFLECTED central ray instead of the classic GT3
  level-forward: each refresh, a camera ray is intersected with the
  dynamic-reflective objects themselves (detected by their bound env
  target - no new flags), with analytic normals: OBB face tests in the
  object's own frame for boxes/save points/planes (live rotation
  honored), spheres for curved shapes, bounding spheres for models. The
  probe then renders from the hit point along the reflected direction, so
  the map shows what the surface actually mirrors. The design went
  through THREE cuts, each driven by the owner feeling the previous one:
  (1) crosshair-anchored shared probe with constant alpha-0.25 smoothing
  - reflections trailed the camera by ~20 frames (alpha per
  every-2nd-frame refresh compounds); (2) adaptive smoothing (same hit
  object = instant tracking, cross-fade only on switches) - fixed the
  trailing but the aim still decayed to classic whenever the object left
  the screen center ("ucieka jak sie mocno na boki patrzy"); (3) FINAL,
  owner's own idea: PER-OBJECT probes anchored to the eye->center ray -
  renderObjectProbe re-renders the shared 128x128 target right before
  EACH reflective object draws (interleaving works on one VRAM target
  because the env bracket's begin() drains PATH1, so the previous
  object's draws sample THEIR map before it is overwritten). The
  eye->center pose depends only on positions, never on view rotation -
  reflections stay put when looking around, the pose is continuous per
  object, NO smoothing exists at all, and side-by-side reflective objects
  show genuinely different simultaneously-correct reflections (verified:
  the example's ball and monolith mirror different prop subsets in one
  frame - the monolith honestly showed pure sky until its rotation was
  aimed so its reflected cone actually contains the props). Cost scales
  with reflective object count (one full probe render per object per
  frame - "10 objects = your own funeral", per the owner); probes skip
  inside split halves (raster bracket rule). The proximity self-skip keys
  on the probe eye so the mirror-er never swamps its own map. One more
  owner-caught bug closed the loop: reflections came out HORIZONTALLY
  MIRRORED because the VU1 matcap sampled every map with the MAIN
  camera's right/up - a probe looking back at the player has its left on
  the player's right, so each probe now stores ITS camera basis on the
  object geometry and the env pass samples with it (renderEnvPass takes
  the owning ObjectGeometry; classic mode untouched - its probe shares
  the player's heading, so the flip never showed there). Chain: ProjectSettings::envProbeReflected (JSON, ==,
  Preferences > Rendering checkbox, {{ENV_PROBE_REFLECTED}} constant in
  both game hpp templates), all-runtime aim block in the env pass. OFF by
  default - existing projects keep their look. Verified in PCSX2 (SW
  renderer) A/B on examples/reflections (temporary local flip, example
  NOT committed - its generated files would drift wholesale): same
  viewpoint, classic shows the red cube as a small washed smudge in the
  chrome, reflected aim shows it as a large round ball placed differently
  per sphere - the probe now renders from the surface's vantage. Ships
  with a dedicated sample, **examples/probe-aim**: a chrome "crystal
  ball" whose equator sits at eye height (a level view ray reflects
  straight back) mirroring a red crate / yellow ball / blue pillar
  standing BEHIND the spawn - two authoring lessons baked into its
  layout: props must stand in the reflected half-space (first draft put
  them in front and the chrome showed pure sky), and a tall ball makes a
  level ray hit below the equator and reflect into the ground (the env
  map has no terrain, so that reads as empty horizon). In-game
  motion (smoothing feel, crosshair slides) remains a hands-on pad test.

- (156) **Live texture feeds: camera-to-texture (CCTV) + raytraced-mirror
  streams (docs/texture-feeds.md).** Any surface can show a live feed via
  *Properties > Texture feed*: a Camera entity with "Render to texture"
  renders its view - sky (+resident terrain) + an explicit object list,
  the Mirror philosophy - into a NEW second instance of the env-map
  redirect bracket (`RendererCore::camFeed`, 128x128 + own z, ~128 KB of
  VRAM permanently below every texture, Clamp wrap) every frame from the
  camera's LIVE transform (+Z lens, Cutscene Director convention) at its
  baked FOV; or a raytraced Mirror's traced image re-streams onto any
  other object. Feeds draw EMISSIVE (colors flatten to the object tint at
  texture scale) through plain surface UVs. One feed camera per scene
  (first enabled wins, extras warn at codegen); feed surfaces are
  excluded from static batching; renames remap "camera:<n>"/"mirror:<n>"
  refs and camera view lists. Chain: SceneObject::camFeed/camFeedTerrain/
  camFeedObjects + textureFeed (+==, JSON, recipe hashes, properties UI on
  the Camera + a Texture feed combo in the shared material picker),
  CAM_FEEDS/CAM_FEED_VIEWS/OBJECT_FEEDS side tables, renderCameraFeed()
  before all main-frame 3D + the binding in rebuildObjectGeometry.
  Two raster lessons paid for: the target samples UPSIDE DOWN through
  plain UVs (GS rows run top-down vs texture V down from row 0 - the env
  map/portals never showed it; the binding V-flips the surface sts), and
  Repeat wrap bleeds opposite-edge rows into the border (feed texture is
  Clamp). Verified in PCSX2 (SW renderer) on examples/raytraced-mirror:
  the billboard above the mirror shows the CCTV camera's aerial view
  (textured crate, balls, wobbler, terrain horizon - right side up, clean
  edges) while the floating monitor streams the VU0-traced mirror image,
  BOTH live in one frame together with the raytraced mirror itself; boot
  clean. Walk-around (the feed showing the player moving) remains a
  hands-on pad test.

- (155) **Raytraced mirror reflections on a VU0 microprogram (experimental
  PoC).** A Mirror object gained *Properties > Mirror > Raytraced (VU0,
  experimental)*: instead of re-submitting reflected geometry, the game
  ray-traces the reflection per pixel, per frame, on VU0 — the first (and
  only) VU0 MICROMODE program in the codebase. Traced scene = sphere
  proxies of the target list (+ player) against the sky gradient; per-user
  feedback the generated game draws NO synthetic ground (the kernel's
  optional checker plane exists but stays off — authors place real floor
  geometry), and the traced image edge is a per-mirror **Reflection
  resolution** option, 32/64/128/256/512 (`mirrorRtSize`/
  `MirrorData::rtSize` through the whole chain; rows wider than one VU0
  batch trace in 64-texel chunks — `Vu0Raytracer::trace`; cost scales with
  edge^2, so 128 is ~4x the default and still a frame rate while 256/512
  are labeled photo modes in the UI — 512 also costs 1 MB of the ~1.33 MB
  GS texture budget). Ships with a playable sample level,
  **examples/raytraced-mirror** (glass wall at 128, four balls +
  reflectPlayer, thin-box floor + pillars deliberately NOT in the mirror
  list). Engine: `Tyra::Vu0Raytracer`
  (`vendor/tyra/engine/{inc,src}/renderer/rt/`, exported by `<tyra>`) +
  `vu0_rt_kernel.vclpp`, built through the same vclpp/vcl/dvp-as pipeline as
  the VU1 programs but uploaded by the EE to VU0 micro memory (0x11000000)
  and kicked per image row with `vcallms 0`, params/results through VU0 data
  memory (0x11004000), sync by polling VPU STAT. The traced scene is a
  stylized proxy: curved targets as spheres, FLAT targets (boxes, save
  points, planes, decals) as axis-aligned slab proxies (`Vu0RtBox`, ray-vs-
  AABB per-axis fold, face normal from entry-axis masks — added when a
  user-listed floor never showed: a flat object as a bounding sphere
  engulfs the glass, ray origins start inside and the entry distance dies
  on the eps mask; rotation is ignored on both), and — per user request,
  "jazda na całego" — static .obj model targets as REAL TRIANGLE MESHES
  WITH TEXTURES (`Vu0RtTriangle`, up to 2 groups / 36 tris per mirror):
  codegen decimates the model's textured submesh by vertex clustering
  (under-budget meshes pass through exactly), bakes it model-local
  (`RT_PROXIES`/`RT_PROXY_VERTS` in scene_data.hpp), and the game
  re-transforms by the live object transform each frame — triangle proxies
  DO honor rotation. The kernel runs a dual-basis Moller-Trumbore (no
  cross products, rational nearest-hit compares by cross-multiplication,
  one division for the winner) behind per-model bounding-sphere early-outs
  (exit-distance test — the inside-origin lesson again), and returns
  (record, barycentric u/v, shade); the EE samples the model part's
  texture in RAM while packing (nearest; 32/24bpp linear, 8bpp with the
  CSM1 CLUT rotation undone, 4bpp nibble-swapped — every PNG-loader
  format) and modulates by the shade. UVs never enter VU0. ANIMATED
  models (.glb/.fbx) reflect LIVE: codegen picks a connected coarse mesh
  of VERTEX indices from the rest pose (medoid clustering - each grid
  cell represented by the real vertex nearest its centroid, after a
  first triangle-sampling attempt rendered as disconnected confetti) and
  the game reads the live skinned vertices at those indices each frame
  (the same buffers the model renders from; renderMirrors runs after
  skinning) lifted by animMat - the reflection plays the clip; untextured
  parts fall back to the material base color. All proxies
  carry live position + tint + single-bounce lambert, sky-gradient
  misses — traced into an rtSize^2
  RGBA32 texture the glass quad samples, re-uploaded over PATH3 into its
  existing GS allocation each frame (`updateTextureInfo`; re-allocates
  automatically after an eviction flush). Two key tricks: the EE mirrors the
  CAMERA across the glass plane once (Householder on the point), so every
  texel's reflected ray is just normalize(P - eyeMirrored) — zero per-texel
  reflection math; and nearest-hit selection is fully BRANCHLESS — VU floats
  saturate instead of producing inf/nan, so clamp(x*1e38, 0, 1) is an exact
  step(0, x) and masks fold the winner (the only branches are the two loop
  back-edges). Editor chain: `SceneObject::mirrorRaytraced` + `mirrorRtSize`
  (+==, JSON `"mirror": {"raytraced", "rtSize"}`, Mirror properties checkbox
  + resolution combo, live-link recipe hash), `MirrorData::raytraced`/
  `rtSize` columns, game runtime `buildRtMirrors` / `renderRtMirror`
  (textures created at scene load, GS-freed + deleted on scene switch). Docs: docs/raytraced-reflections.md + README bullet +
  engine-skill notes (incl. two new VCL traps paid for here: `r`/`q`/`i`/`p`
  are reserved register names, and broadcast fields are only legal on the
  second source operand). Verified: full Docker build clean (kernel = 1152
  bytes = 144 instructions, comfortably inside VU0's 4KB;
  `Vu0RtKernel_CodeStart/End` symbols confirmed with nm); e2e in PCSX2 on
  the SOFTWARE renderer with a scratch FPP scene (8x4 wall mirror,
  raytraced + reflectPlayer, three colored spheres): boot log prints "VU0
  ray tracing kernel uploaded (1152 bytes)", no asserts, and F8 screenshots
  show the traced image on the glass - round correctly-lit sphere
  reflections on the physically correct sides, the player proxy, sky fade
  (the checker plane was verified too, before the no-ground follow-up
  removed it from the generated game). A/B on the same scene: classic
  mirror EE 38% / VU 4% / GS 9% vs raytraced EE 36-37% / VU 2% / GS 7%,
  BOTH locked at 50 FPS - the RT mirror trades the copy re-submission for
  VU0 trace time and comes out cost-neutral in this small scene; the same
  scene renders correctly at rtSize 128 AND 512 (chunked rows, no seam at
  any 64-texel chunk boundary; 512's frame rate was not measured - the
  ~64x cost figure is analytic, the image is verifiably right). The
  example level boots clean and its F8 screenshot shows all four balls +
  the player proxy reflecting on the correct sides, the listed floor
  slab reflecting as a floor under them (an earlier floor made of the
  Plane primitive z-fought its own two coplanar faces at this scale -
  patchy dark wedges; the thin box has no coplanar pair and rendered
  clean), the textured crate model (12 tris, exact pass-through)
  reflecting as a real wood-and-rivets crate at its live 25-degree yaw,
  and the animated wobbler's coarse green mesh visibly CHANGING POSE
  between two F8 screenshots taken 4 s apart while correctly occluding
  the ball reflection behind it - the glass plays the Twist clip.
  The triangle kernel cost THREE VCL failures, all recorded in the engine
  skill: "ERROR: no opt table .. for <loop>" is the REGISTER ALLOCATOR
  running out (31 VF ceiling), not syntax - fixed by reloading
  fixed-address params at use sites and lane-packing the fold state
  (the group-loop unroll and the cross-product-free dual-basis rewrite
  made along the way were kept: simpler CFG, 4-qword records). Kernel is
  now 3872 of 4096 bytes - at ~95% of VU0 micro memory, the next feature
  needs a diet first. Walk-around
  feel (reflection tracking the camera) remains a hands-on pad test. Third
  kernel iteration fixed a subtle one: mix-with-sentinel-BIG selection
  cancels catastrophically in single floats (t - 1e10 + 1e10 == 0), which
  cut every reflection off at the checker horizon - the nearest-hit state
  is comparison-mask-folded instead (see the kernel header note).
- (154) **Per-object sleep delay: "Sleep after (s)" on the Physics block
  (default 3 s, was a hard ~0.5 s).** User ask: relax the sleep timing and
  give it a per-object override. New `SceneObject::physSleep` runs the whole
  chain - project.hpp field + operator== -> objectJson/parse (emitted only
  while `physics` is on, like the rest of the material block; clamped
  0.1-60 s on load) -> Live Link recipe hash (baked table data, a stale
  value must not force a rebuild on non-physics objects - it sits in the
  same `if (o.physics)` group) -> Properties drag (Physics section, with a
  hint line) -> `SceneObjectData::physSleep` column in scene_data.hpp ->
  runtime. The game-side counter changed shape: the fixed
  `PHYS_SLEEP_FRAMES = 24` constant is gone; the countdown length is
  `everyFrames(physSleep)` (wall-clock true under disableVsync, same as
  every other timer since (114)), `restFrames` widened signed char -> short
  (3 s at 50 fps = 150 > 127), and on completion the counter pins to a
  `PHYS_ASLEEP = 0x7FFF` sentinel - the asleep test (`physAsleep(o)`, new
  helper used by pass 1/pass 2/portal-latch/carry sites) never re-derives
  the threshold, so a measured-dt wobble can't flap a sleeping body awake.
  "Write restFrames = 0 to wake" stays the contract for scripts/nodes.
  Verified (probe repro, PCSX2): three bodies with physSleep 5 / 3 (the
  default, materialized by --resave round-trip) / 0.5 settle from the same
  throw and pin to 32767 at rest counts 250 / 150 / 25 frames - exactly
  5 s / 3 s / 0.5 s at PAL 50; scene_data.hpp carries the column; editor +
  Docker PS2 builds clean.

- (153) **Settle-flatten v2 (finish the fall) + mesh collision holds for
  tumbled/rotated models (world-space steepness, side-aware push).** Two
  follow-ups. (a) The (152) flatten waited for the tumble to die to the
  rest gate (spin < 0.75 deg/frame) and re-picked "nearest 90deg step"
  every frame - a crate still tipping forward could get yanked BACK to the
  face it was leaving, and the tumble's rolling-without-slipping kept
  re-deriving spin from the residual slide under the ease (overshoot-and-
  return, 270.49 -> 270.00). Now the flatten engages while the tumble is
  still dying (PHYS_FLATTEN_SPIN = 2.5 deg/frame, speed gate 4x rest),
  picks each target ONCE with a ~20-frame momentum lookahead
  (roundf((rot + spin*20)/90)) latched in RuntimeObject::flatTgt (reset
  whenever the gate fails), zeroes the residual spin (the ease drives from
  there) and suppresses the tumble's spin re-derivation while latched. The
  sleep rule itself is unchanged and now documented here: a body sleeps
  after 24 consecutive frames (~0.5 s) of grounded + speed under ~0.8 u/s
  + spin under 0.75 deg/frame, with flatten-in-progress resetting the
  countdown. (b) `CollisionMesh::resolveSphere` judged "steep wall vs
  walkable floor" on the LOCAL normal.y - a mesh-collision physics model
  lying on its side (tumbled bodies rest with 90deg pitch/roll now) has
  world-walls whose local normal reads as floor, so the player walked
  straight through; AND the push itself was two-sided along
  (center - closest), which for a step landing PAST a wall's plane points
  INTO the volume - with the FPP step (~0.4 u/frame) longer than the
  player radius (0.35) a fast walker crossed the plane and got sucked
  inside (this also affected unrotated meshes). The engine gained a
  resolveSphere overload taking the up direction in mesh-local space
  (classification = dot(normal, up), world-up rides in via invRotated)
  and the pre-move position: the sphere is ejected to prev's side of each
  face, crossings are caught within a radius+0.6 capture band, gated on
  the plane distance dominating the gap (sCur^2 > 0.5*d2) so crossing a
  wall's PLANE near its top edge while walking ON the mesh doesn't yank
  the walker off the top. Verified (probe repro, PCSX2): a walk-step sweep
  into a rz=90 mesh cube - approach stops at the face (-5.35 = face -
  radius), steps landing 0.2-0.4 INSIDE eject back to -5.35 (pre-fix they
  pulled in deeper); flatten traces are monotone with no overshoot (Crate
  engages at spin 2.52, eases 64->73->85->90.00 exactly; Target
  134->...->180.00), the sphere's trace stays byte-identical. Editor +
  Docker PS2 builds clean (engine lib rebuilt from the bind-mount).
  In-game pad feel remains the hands-on check.

- (152) **Physics upgrades: hits wake sleeping bodies, tumbled boxes ride
  their rotated bound (no more sinking), and near-rest bodies settle flat.**
  Three user asks in one pass over `updateObjectPhysics`. (a) *A thrown
  body never woke the body it hit*: pass 1 treats sleeping bodies as static
  solids and resolved the mover OUT of contact, so pass 2 - the one that
  wakes sleepers and trades momentum - never saw the pair overlap (its
  `ph <= 0` early-out hit every time). Now a mover faster than
  `PHYS_WAKE_SPEED2` (~2.5 u/s; rest is ~0.8) skips the wall treatment for
  a sleeping body and lets the impulse pass handle the hit - wake + mass-
  split impulse, the existing math; near-rest contacts keep the wall
  treatment so settled stacks stay cheap and stable. (b) *Boxes/models sank
  into the terrain "as if they had sphere physics"*: `physExtents` is an
  unrotated AABB, so a rolled box supported itself on its half-height while
  its corners visibly pierced the ground. The mover's contact extents are
  now the support of the ROTATED bound per world axis (sum of |basis
  column| x half extent, center offset rotated too) - a tumbling box rides
  its corners (center height breathes with the roll), and yaw-only authored
  rotation leaves the vertical extent unchanged, so placed blocks rest
  exactly as before. Spheres skip it (rotation-invariant; their box corners
  would overestimate the radius). Statics as the OTHER side of a contact
  keep the plain AABB - the (149) known limitation, unchanged. (c) *Settle-
  flatten*: a near-rest tumbled body (grounded, under the rest thresholds)
  eases pitch/roll at 3 deg/frame to the nearest 90deg step instead of
  sleeping on an edge; the rotated support extent lowers it onto its face
  as it tips. Euler-order trap (the (150) family): `rotated()` composes
  Rz*Ry*Rx, and with the roll on an ODD 90 step the pose is only flat when
  the yaw sits on a step too - so the yaw joins the easing exactly then.
  Spheres skip flattening (orientation invisible; easing would visibly
  roll the baked shading). Sleep waits for the easing to finish
  (flattening resets the countdown); `flatMoved` joins the rebuild
  condition so slow-path bodies re-bake the eased pose. Verified with the
  (151) probe repro + a sleeping Target crate in the thrown crate's path
  (PCSX2, physlog.txt): Target sleeps (rest=24), the crate arrives at ~10
  u/s, Target wakes with momentum (flies ~3 u, tumbling), the thrower
  hands off its speed (0.20 -> 0.012 u/frame); mid-tumble center heights
  match the support math (y=0.690 at rz=58.7deg = 0.5(|cos|+|sin|));
  both crates ease to exactly 90.00/180.00 and y returns to 0.500; the
  sphere's trace is byte-identical to the pre-change run (skip paths
  hold). Editor + Docker PS2 builds clean. In-game pad feel remains the
  hands-on check.

- (151) **Settling physics bodies no longer "snap their rotation back":
  sleeping fast-path bodies stay on objMat (no settle re-bake).** User
  report: a thrown ball that stops tumbling "freezes and its rotation
  resets". The data said otherwise - an in-game probe (owned
  terrain_game.cpp printing every body's pos/rot/spin/restFrames/matrixMode
  to a host file twice a second) showed `data.rotation` is PRESERVED through
  sleep (a rolled ball rests at rz=259.84deg and keeps it forever). What
  actually snapped was the (114) fast path's settle step: on wake a body
  bakes local vertices with shading FROZEN at the wake pose (the light
  pattern rides along as it tumbles - baked colors can't re-light per
  frame), and on sleep the old code set dirty for one world re-bake that
  re-shaded the rest pose. That discrete wake-shading -> rest-shading jump,
  on a sphere whose shade gradient is the only orientation cue, reads
  exactly as "the ball rotated back" - and it fired at the very frame the
  body froze, welding the two complaints into one artifact. (The freeze
  itself is the intended sleep; by the time restFrames hits the threshold,
  friction has decayed the spin to ~0.002deg/frame - imperceptible.) Fix:
  drop the settle re-bake - a sleeping body keeps its local bake + objMat
  (one matrix refresh per frame, same as awake). Safe because every
  fast-path consumer reads objMat or o.data (mirrors compose reflection *
  objMat, env pass and portal views take the bag's matrix, split band / use
  targeting / collision read o.data), and the two vertex-array consumers
  (usable-highlight hull, matcap env normals) were already excluded by
  physFastPathEligible. Trade-off, documented in the code: a resting body
  keeps the shading baked at wake - the same shading it showed all flight -
  instead of snapping to a freshly lit rest pose; a retint / Live Link edit
  still re-bakes via dirty. Verified with the probe repro (fpp scratch, a
  pickable+physics sphere and box impulse-launched by an OnStart->Delay->
  Apply Impulse graph, PCSX2 boot, physlog.txt over 30 s): before -
  matrixMode flips 1->0 at the sleep frame (the re-bake = the visible pop);
  after - the identical deterministic trajectory settles at the same
  rz=259.84deg with matrixMode still 1 at rest (nothing re-bakes, so
  nothing can pop). In-game pad-throw eyeball is the remaining hands-on
  check.

- (150) **Thrown-object teleport/stuck regression: the box-collision horizontal
  frame is yaw-only again.** Day-one regression from (149): the new OBB
  footprint test built its local frame from the object's FULL 3D rotation and
  dropped the Y component in both directions (`invRotated({dx,0,dz})` in,
  `rotated({lx,0,lz})` out). For a yaw-only rotated placed block that is an
  exact isometry - but physics bodies TUMBLE (`spin[0]`/`spin[2]` write
  pitch/roll into `rotation` while sliding), and for a pitched/rolled box the
  XZ projection is a contraction: part of the horizontal offset escapes into
  local Y and is discarded, so (a) a player metres away from a tumbling thrown
  crate read as "inside" its footprint (a 90deg-pitched box collapsed the
  whole Z axis - `lnz ~ 0` no matter the distance), (b) the blocked-branch
  commit re-projected the contracted coordinates and *pulled the player to the
  box center* (the reported "throw teleports me into the object"), and (c)
  once inside, every frame's full-stop re-commit contracted again - the
  reported "you can get stuck inside a physics object" (landed tumbled bodies
  keep their pitch/roll at rest, so walking into one triggered it too).
  `collidePlayer`'s box mode now builds the horizontal frame from
  `rotation[1]` alone (one cos/sin pair, inverse = transpose of `rotated`'s Y
  block): the local<->world round trip is the identity for ANY rotation,
  identical to (149) for yaw-only blocks (pitch/roll were already documented
  as "collide upright; mesh mode is the escape hatch") and reduces to the old
  AABB at zero rotation. The world box *center* still uses the full rotation
  (a real 3D point, no projection involved). The (149) camera spring-arm
  sweep is NOT affected - its slab test keeps all three components, a true
  isometry. Verified: a standalone numeric check reproduces both symptoms
  against the (149) math (player at Z=5 from a 90deg-pitched box reads INSIDE
  and commits to the center; arbitrary-tumble round trip drifts 0.31u/frame)
  and confirms the fix (same player reads FREE, round trip exact to 1e-5,
  yaw-45 block behavior byte-identical to (149), zero rotation = plain AABB);
  editor builds clean; a scratch `--new` fpp project's regenerated
  `terrain_game.cpp` carries the yaw-only code and the Docker PS2 build
  compiles + links. In-game throw feel is the remaining hands-on check.

- (149) **Rotated box collision: the player now collides with a block's real
  (rotated) faces, not its unrotated bounds.** Box-mode player collision
  (`collidePlayer`, shared by both walkers) built the blocker box straight from
  `scale` on the world axes and ignored the object's rotation entirely - so a
  yaw-rotated block blocked the player along a phantom axis-aligned box (an
  invisible wall jutting past the visual corners) while letting them walk
  straight through the block's actual rotated faces. The footprint test now runs
  in the box's OWN horizontal frame: the player's swept XZ is taken into local
  space with `invRotated`, the inside/wall-cancel logic runs against the local
  half-extents, and the resolved slide is mapped back with `rotated` - so the
  residual slide follows the rotated wall. Vertical (top/bottom, ground/ceiling)
  stays world-space: a yaw does not tilt the box, and box mode never modeled a
  tilted top, so a pitched/rolled box still collides upright as before (mesh
  collision is the escape hatch for those). The math reduces exactly to the old
  AABB test at zero rotation, and the model-AABB center offset now rotates with
  the object too (was added on the world axes; identity for a primitive, whose
  offset is 0). NOTE: this was reported as a copy/paste bug ("the copy has no
  collision"); it is not - paste preserves every field (verified: the pasted
  row is byte-identical in the generated `scene_data.hpp`, and the Live Link
  recipe hash matches so a live-spawned copy clones a colliding template). The
  real trigger was rotating the block. The **camera spring arm**
  (`sweepSphere`) got the same treatment: its boom ray is now cast in each
  box's local frame (broad phase uses the OBB's own world AABB, so it no longer
  rejects a rotated block's protruding faces) - previously the camera sailed
  straight through rotated primitives, because the boom tested an unrotated
  scale-box the real faces stuck out past. Physics-**body**-vs-solid collision
  (`physExtents`) still uses the axis-aligned bound - a known remaining
  limitation (the solver's resting/bounce-normal/momentum contacts are all
  built on AABB faces, a larger separate change). Verified: editor builds clean; a scratch `--new`
  fpp project's generated `terrain_game.cpp` carries the new local-frame code
  and the Docker PS2 build compiles + links (=== Build OK ===); a standalone
  numeric check confirms a point inside the old phantom AABB but off a
  45deg-rotated wall now reads FREE (old: blocked in empty air) while a point on
  the real rotated face reads BLOCKED (old: walked through), and a second check
  confirms a camera boom crossing a rotated wall near its tip now blocks where
  the old scale-box missed it entirely. The in-game *feel* (walking a rotated
  wall on a pad, and orbiting the camera behind one) is the remaining hands-on
  check.

- (117) **Third-person spring arm: whisker anticipation instead of a raw
  snap-in.** The camera boom used to jump the instant the straight boom ray got
  blocked (`P.boom = want` hard snap) — correct (never clips) but visually
  violent when walking past a wall edge. Now two extra **whisker casts** splayed
  ~20° to either side of the boom (same `springArm` query, same AABB broad
  phase) detect walls the camera is about to sweep behind and pull a *target*
  length partway toward the whisker's hit (60% weight — an off-axis hit is a
  hint, not the true obstruction); the boom eases toward that target briskly
  (0.30/frame) on the way in and gently (0.06/frame, as before) on the way out.
  The old guarantee is intact as a hard clamp: `P.boom = min(P.boom, want)`
  every frame, so a wall that appears between whiskers (fast camera spin) still
  clamps instantly rather than ever showing a clipped frame — but it now
  usually fires from a boom that anticipation already pulled most of the way
  in, so the residual correction is small. The boom state is the per-player
  `P.boom` (each split-screen player smooths independently). Cost: +2
  `springArm` casts per frame in third-person mode only. Verified layer 0-3:
  editor builds clean, whisker code lands in the generated `terrain_game.cpp`
  of a scratch `--new` project, Docker build of that project compiles and links
  (=== Build OK ===). The actual camera *feel* (wall graze, corner sweep) needs
  a hands-on pad test in PCSX2 — the math guarantees no-clip, the tuning
  constants (20° splay, 0.4 retention, 0.30 in-rate) are first-guess values a
  human may want to nudge.
- (118) **Collaboration polish: mid-session file refresh, session prefs,
  docs.** Closes out remote-collaboration v1. **Refresh project files**
  (client, Session window): re-runs the join-time manifest diff mid-session -
  the host rescans its disk, the client fetches only new/changed files through
  the same chunk pipeline (hash cache makes an unchanged project a no-op),
  then drops every disk-derived cache (`Viewport::invalidateAssets`, model/
  wav caches) and rescans assets. This is how assets the host imported
  mid-session reach clients - scene edits never need it (they stream live);
  clients' own disk-writing edits (Material Editor paint) stay a documented
  v1 limitation. **Prefs (editor.ini):** `displayName=` (name shown to peers;
  seeded from USERNAME, remembered from the last session modal) and
  `sessionCacheDir=` (remote-project cache root override) - both editable in
  *Edit > Preferences > Collaboration sessions*. **Docs:**
  `docs/collaboration.md` (usage, sync/conflict semantics, cache layout,
  trust model, v1 limitations), README feature bullet + docs index, testing
  skill gains the headless-session + two-instance recipes. **Verified:**
  headless harness - a file written into the host's res/ mid-session arrives
  at the client via requestRefresh (exactly 1 file fetched) and the
  `Refreshed` event fires; all earlier session/convergence harnesses re-pass;
  editor builds clean.

- (117) **Session presence + client-mode UX.** The "who is doing what" layer
  and the participant-facing polish. **Presence:** every editor broadcasts its
  selection (stable object ids + the scene index) as a `presence` frame,
  throttled to 5 Hz and only on change; the host relays to everyone else.
  Remote selections render as **wire outlines in each peer's color** in the
  viewport (drawn under the local amber so local always reads on top;
  `Viewport::setPeerSelections`, ids resolved to indices per frame), as
  **colored dots** on the object rows in the Project panel, and as "- <scene>"
  next to each participant in the Session window. **Client-mode gating:** a
  joined client's Save is disabled everywhere (File menu with an explanatory
  tooltip, Ctrl+S, the toolbar floppy) - the HOST owns saving/committing; the
  title bar shows `[joined]` while in a session and drops it on leave/kick/
  close. Presence state resets on session start and clears on end. **Verified**
  (two editor instances over 127.0.0.1, synthetic input + screenshots): the
  client's selection shows on the host as a blue dot on that object's row and
  the Session window lists "papaj - main <ip:port>" with a Kick button;
  kicking pops the client's "You were removed from the session by the host /
  the project stays open as a local copy" modal, the `[joined]` title marker
  disappears and the synced project stays open; the client's title showed
  `sesstest [joined]` and its participants list exactly two entries.

- (116) **Live model sync - simultaneous editing with per-object last-write-
  wins.** The heart of the collaboration feature: everyone in a session edits
  at once and every editor converges on the same model. Engine
  (session.hpp/.cpp, pure `Project&` in / frames out - fully headless-
  testable): `ModelShadow` is the last-broadcast view of the model;
  `diffModel()` compares the live project against it and emits one frame per
  changed unit - `obj-upsert` (the objectJson body; emitted BEFORE the
  layout), `scene-layout` (the whole scene table: names/meta/ordered id
  lists - covers scene add/remove/rename/reorder + object add/delete/move/
  reorder in one LWW unit; `project::scenesLayoutJson`/`applyScenesLayout`
  re-home objects BY ID so a move keeps its live body), `heights` (raw float
  grid in the binary trailer) and `section` (the Phase-113 blobs).
  `applyEdit()` folds an inbound frame into the project AND the shadow, so
  the echo of your own edit re-diffs to nothing. **Convergence rule: the host
  is the total order** - it applies every client frame and rebroadcasts it to
  ALL peers including the origin; TCP preserves that order per client.
  Editor integration: `modelEditSerial_` bumped in `commitChange`,
  `applySnapshot` (undo/redo broadcasts!) and `setDirty(true)` (the UI-Editor
  / layout paths that bypass commitChange) - **any new mutation path must hit
  one of those or the session silently misses it**; `sessionTick` diffs when
  the serial moved and applies inbound batches (then: selection prune,
  viewport push, one history anchor per batch so undo rewinds remote edits
  batch-wise, host marks dirty + refreshes the joiner snapshot via
  `setModelFiles` so a late joiner gets the CURRENT model, not the
  host-start state). Two subtle bugs found by the property test and fixed:
  (a) `applyScenesLayout` fabricated an empty placeholder for an unknown id -
  a delete-vs-keep race then diverged; unknown ids are now skipped (the body
  upsert always precedes the layout in a batch); (b) applying a remote
  `scene-layout` used to copy `p.scenes` into the shadow wholesale, which
  captured this peer's not-yet-broadcast local edits as "already sent" - a
  reorder from one peer silently swallowed a concurrent recolor from the
  other; the shadow now mirrors the structural change onto its OWN bodies.
  Also fixed: the client duplicated itself in the participants list (the
  host's welcome already includes the joiner). **Verified.** Headless
  property test: host+client replicas, 6 seeds x 6000 rounds of concurrent
  random edits (add/delete/move/recolor/rename/reorder objects, scene
  add/remove/rename, cross-scene moves, terrain sculpts, section edits)
  through the real engine + relay rule -> byte-identical models after every
  round (whole-model FNV hash over layout+bodies+heights+sections), plus a
  shadow-vs-fresh-shadow drift probe each round; all Phase 113-115 harnesses
  re-pass. Interactive (two editor instances over 127.0.0.1, driven by
  synthetic input, screenshots): host adds an Empty via Scene>Add -> it
  appears in the client's object list + viewport within a second; client
  adds one -> it appears on the host (auto-named `empty-2` against the
  synced state) and the host titlebar gains the dirty `*`; participants
  list shows host + client with address and a Kick button.

- (115) **Collaboration session: host / join / full transfer + local cache
  (src/session.hpp/.cpp).** The connection layer of the live sessions.
  `Session` owns one worker thread (Runner idiom: `std::atomic` state +
  mutex-guarded queues; the UI thread drains `drainEvents()` once per frame in
  `App::sessionTick()` and is the ONLY place session data meets `project_` /
  ImGui). Host: hashes/scans the project (excludes bin/ obj/ .git/ .res-baked/
  *.history; the .tyra + objects/*.json + terrain-*.heights come from the LIVE
  in-memory model via `manifestFiles()`), listens, and on each join sends
  `welcome` + a content-hash `manifest`; the client diffs against its cache,
  `need`s only the misses, receives chunked `file` frames (256 KiB, per-peer
  backlog-capped so one slow peer can't balloon host RAM) and `sync-done`,
  then opens the materialized project. Remote projects live under
  `%LOCALAPPDATA%\tyra-editor\remote-cache\<projectId>\project`; `cache.json`
  (size+hash+mtime) makes a re-join of an unchanged project fetch **zero**
  files and a one-asset change fetch **exactly one**. Host-side hashing is
  memoized across sessions (`hash-cache.json`) so hosting a big project never
  rehashes unchanged assets twice. Handshake gates: protocol-version and
  6-digit join-code mismatch → `deny`, session-full → `deny`, 5 s ping /
  15 s timeout keepalive, host `kick` and `close` broadcast `bye`. Path safety:
  the client rejects any manifest path that is absolute / has a drive / climbs
  `..`. `wire::Transport` stays the swappable seam (LAN TCP today).
  UI: a **Session** top-level menu (Host / Join / Session Window / Close-Leave),
  the Host and Join modals (display name, port, join code, local host IPs, a
  firewall hint; the Join modal shows a live transfer progress bar and inline
  errors), a Session window (participants with per-peer color dots + Kick), a
  session-ended modal, and a toolbar **SESSION chip** cloned from the LIVE chip
  (green "SESSION (n)" hosting / blue "JOINED" / amber "SYNC"). A project
  switch (`attachProject`) tears the session down, except the join handoff
  which keeps it alive. **Verified.** Headless harness (host+client `Session`
  in one process over 127.0.0.1, real sockets): a join transfers the whole
  scratch project (40 files / 615 KB incl. a 300 KB binary asset) and the
  client's loaded model is byte-identical to the host - `scenes ==`, every one
  of the 11 sections' `sectionJson` equal, asset bytes equal, `projectId`
  equal; a re-join of the unchanged project fetches 0 files; changing one asset
  fetches exactly 1 and its new bytes arrive; a wrong join code is denied with
  the code-specific message; a kicked client sees the removal message; the host
  sees PeerJoined / PeerLeft. GUI (editor, screenshots): the Session menu, the
  Host modal (name=USERNAME, port 7797, generated join code, three LAN IPs),
  the green SESSION (0) toolbar chip, and the Session window (participant
  "papaj (host)" + Close button) all render; starting the host raised the
  Windows Firewall prompt the modal warns about.

- (114) **Collaboration wire transport (src/wire.hpp/.cpp).** The byte layer
  under the upcoming live sessions, deliberately independent of the project
  model. Frames are `[u32 jsonLen][u32 binLen][json][bin]` (LE): JSON carries
  the message, the raw binary trailer carries bulk payloads (file chunks,
  heightmap grids) so bytes never pass through json.cpp (which collapses
  `\u` escapes). Hard caps (4 MiB json / 16 MiB bin per frame) kill a
  malformed/hostile connection instead of ballooning memory; the incremental
  `FrameDecoder` survives arbitrary short reads. The `wire::Transport`
  interface (`listen/connect/poll/send/sendBacklog/kick/close`, single-thread
  contract, `Event` stream of Connected/Disconnected/Frame) is **the seam a
  future internet transport plugs into** (WebSocket-through-tunnel etc. -
  session code never sees sockets); `makeTcpTransport()` is the LAN
  implementation: Winsock2 non-blocking sockets + `WSAPoll`, TCP_NODELAY,
  no SO_REUSEADDR (a second host must get "port is already in use", not
  steal the socket), per-peer send queues drained on poll. Plus
  `wire::fnv1a64`/`hashFile` (streamed content hash for the transfer cache)
  and `localIPv4()` for the host UI. CMake links `ws2_32`. **Verified**
  (headless harness, single process pumping host+client transports on
  127.0.0.1): codec reassembles frames from 1-byte feeds and round-trips
  empty json/bin; oversized header latches error; 1000 small frames arrive
  in order; an 8 MiB binary round-trips byte-exact; client close surfaces
  Disconnected on the host and kick() surfaces it on the client; connect to
  a dead port errors; double-listen and port-in-use report cleanly;
  hashFile == fnv1a64 on known bytes and false on a missing file.

- (113) **Collaboration groundwork: manifest sections, projectId, in-memory
  model files, objectJson escaping fix.** The serialization layer learns the
  shapes the upcoming live-session wire format needs, with the .tyra byte
  layout unchanged. `save()`/`load()` are recomposed from per-section
  writers/readers (`project::Section`: Settings / Hud / Audio / TexQuality /
  SaveData / Gradings / Ambience / LoadingScreens / Splash / Sequences /
  Menus); `project::sectionJson()` / `applySectionJson()` expose each group of
  manifest keys as one standalone JSON blob (apply is total-replace with
  reset-to-defaults, not a patch - the LWW unit for project-wide data).
  `project::objectJson()` / `parseObject()` are now public - one object as a
  wire string and back (the objects/<id>.json body). `Project::projectId`
  (16-hex, `ensureProjectId`; stamped at create, backfilled on load, omitted
  from the manifest while empty) gives the remote-project cache a stable key.
  `project::manifestFiles()` returns byte images of the .tyra + every
  objects/<id>.json + terrain-*.heights straight from the live in-memory
  model (a dirty host must ship its live state, not the last save).
  Fixed in passing: `objectJson` wrote name/layer/model/material/sound paths,
  mirror-target names, script names and anim clips **without `jsonEscape`** -
  a `"` in an object name corrupted the saved file; likewise music/sound
  paths and textureQuality keys in the manifest. **Verified** (headless
  harness vs .obj files, all 10 examples/): golden byte-diff of load->save
  output pre/post refactor is identical after id canonicalization except the
  intended `projectId` line; per-section `sectionJson -> applySectionJson ->
  sectionJson` string-equal both onto a copy and onto a field-clobbered
  project; `manifestFiles()` bytes == the files `save()`/`saveHeights()`
  write; every object round-trips `objectJson -> parseObject` (`operator==`),
  plus an escaping regression case with quotes/backslashes/newlines.
- (148) **Seeded example script no longer recolors the sky on every X.**
  Owner: the scaffolded `src/scripts/example_interaction.cpp` toggled
  `ctx.skyColor` whenever Cross was clicked (near the box in FPP, anywhere in
  orbit) - and Cross is the jump button, so in an FPP project every jump
  flipped the sky orange, reading as a glitch (the same reasoning that had
  already neutered the two-players demo in 110). Both creation-time templates
  (`TPL_EXAMPLE_SCRIPT_FPP` / `TPL_EXAMPLE_SCRIPT_ORBIT`, templates.cpp) now
  ship a minimal hello-world instead: `init()` logs one line, `update()` is
  empty but carries the old box+X sky-toggle verbatim as a ready-to-uncomment
  comment block, so the teaching value stays without the surprise. The file is
  still user-owned / written only at creation (not in refreshGenerated's list),
  so existing projects keep their edits; the 12 committed examples that carried
  the old script were rewritten by hand to the new variant (10 FPP + 2 orbit -
  mirror-room, video-modes; two-players already had its own no-op stub). Docs:
  script-demo/README.md rewritten to describe the hello + commented example.
  Verified: editor builds clean; `--new ... fpp` and `--new ... empty` scratch
  projects emit the new script for both variants; Docker PS2 build of
  examples/script-demo (which exercises the hand-edited FPP file) compiles
  clean.

- (147) **Region-aware default display mode: the "PAL picture" preference +
  a DEFAULT menu option.** Owner follow-up on (146): a project should ship
  ONE build that boots the right mode per region - 480i on NTSC, the
  author's chosen PAL flavor (letterboxed NTSC-size vs full-height 576i)
  on PAL - and the in-game display row should offer "default" as a
  first-class option next to explicit overrides like 480p/1080i. Two
  pieces, no engine change: (1) `ProjectSettings::palFullHeight`
  ("palFullHeight" JSON, a "PAL picture" combo under Preferences > Display
  mode, shown for the region-following "interlaced" mode): the generated
  main.cpp promotes Interlaced -> Pal576i before engine init when the
  effective region is PAL (forced videoSystem, or `graph_get_region()` on
  auto), so the whole boot already runs full-height. (2) The display row's
  optionModes gained a **-1 sentinel** = "project default": the game
  latches `g_defaultDispMode` from the engine settings at init (the boot
  mode IS the resolved default - nothing can have switched yet) and
  `displayOptionMode` resolves -1 to it, so APPLY on the DEFAULT option
  returns the player to the per-region default. Menu Editor: the dropdown
  gained "Default (project)" (combo index = mode + 1), the "+ Option
  block" DISPLAY preset is now DEFAULT/480p/1080i (modes -1/1/2, the spec
  carries the table), clamps widened to -1..4 (load/emit/UI). Verified:
  editor builds clean; scratch project (videoSystem pal + interlaced +
  palFullHeight + a DEFAULT/480p/1080i row) emits the main.cpp promotion
  guard, `MENU_0_E0_MODES[3] = {-1, 1, 2}` and the g_defaultDispMode
  latch/resolve; Docker build links; PCSX2 boots it in PAL with a native
  512x512 F8 screenshot (the promotion path, since the BIOS region is
  NTSC and videoSystem is forced pal). Auto-region promotion on a real
  PAL BIOS + the pad-driven menu pass remain hands-on checks.

- (146) **True PAL: DisplayMode::Pal576i, the full-height 512-line frame.**
  Owner follow-up on (145): our "PAL" was the NTSC-sized picture (512x448
  buffer) output at 50 Hz - the letterboxed port look. The new mode renders
  a 512x512 frame and scans it as the classic 576i FIELD signal (512 of
  the raster's ~576 visible lines - what full-PAL European releases did).
  Engine (vendor/tyra): enum value appended (serialized - append only),
  `RendererSettings::updateGeometry` 512x512 case, `getRefreshRate` pins it
  to 50 Hz like the DTV modes pin 60, `programDisplay` reuses the stock
  interlaced default case with the signal forced to GRAPH_MODE_PAL (512
  lines is ps2sdk's own full PAL frame - `graph_set_screen` copes, no
  setDtvDisplay needed), flicker filter kept (`presentFrameBuffer`).
  Projection aspect needs NO change: the formula is buffer-shape-agnostic
  (4:3 window baseline). Cost: ~380 KB more GS VRAM (three 512-line
  buffers), texture budget ~1 MB. Editor: `displayMode` "pal576"
  (Preferences combo + tooltip), {{DISPLAY_MODE}} -> Pal576i, Set Display
  Mode flow node mode 4 (combo + desc), Menu Editor display-row dropdown
  gained "576i" (clamps/seeds 0..3 -> 0..4 in project.cpp load, the
  menu_data emitter and app.cpp). Verified: editor builds clean; scratch
  project with "pal576" + a 4-option display row (modes 0/2/1/4)
  round-trips and emits `Tyra::DisplayMode::Pal576i` in main.cpp +
  `MENU_0_E0_MODES[4] = {0, 2, 1, 4}`; full Docker build (libtyra rebuild
  included) compiles and links; PCSX2 boot: emulog logs "Mode Changed to
  PAL" on an NTSC-region BIOS (the forced-PAL path is live), the scene
  renders a clean full 4:3 frame, and an F8 screenshot with
  `ScreenshotSize = 2` (uncorrected internal size) on the software
  renderer is exactly **512x512** - the full-height buffer on screen
  (stock interlaced is 512x448). That ScreenshotSize=2 trick is the way
  to read the real GS buffer size; the default window-size screenshots
  are DAR-corrected and hide it.

- (145) **Display-mode menu row: stage-then-APPLY + a scan-mode dropdown per
  option.** Owner reports: cycling the in-game "Display mode" option block
  switched the scan mode on every press (VRAM rebuild, menu force-closed,
  confirm prompt armed), so the option list could not even be browsed; and
  the Menu Editor edited the row's options as free text while their meaning
  was silently positional (option index == Tyra::DisplayMode - no way to
  offer e.g. just 480i + 1080i, and a mislabeled option lied). Two changes:
  (1) **Apply video mode row** (`MenuEntry::ApplyVideo`, action 9,
  serialized "apply-video"): while any menu in the project has one
  (codegen'd `MENU_HAS_APPLY_VIDEO`), the bind-5 row only stages its save
  value and the APPLY row commits it (`updateGameMenu` case 9 → the same
  scriptCtx video request + 8 s keep-or-revert net); with no menu on screen
  the row **snaps back to the live mode** each frame, so a browsed-but-
  unapplied selection or a reverted confirm never lies, and the boot seed
  aligns the row to the compiled mode so a title-screen menu opens honest.
  Projects without the row keep the classic switch-on-change behavior
  (MENU_HAS_APPLY_VIDEO=false compiles the old path). (2) **Explicit
  option→mode table** (`MenuEntry::optionModes`, "optionModes" JSON,
  `MenuEntryData::optModes`, null = legacy positional): the Menu Editor
  edits each display option as a dropdown of the four scan modes + a
  free-text label (rename "480i" to "576i" for PAL), so any subset in any
  order works. The "+ Options menu" scaffold's DISPLAY page and the option-
  block popup gained the APPLY row. Verified: editor builds clean; scratch
  project with a shuffled 3-option row (480i/1080i/480p → modes 0/2/1) +
  APPLY round-trips through --resave, --refresh-gen emits
  `MENU_0_E0_MODES[3] = {0, 2, 1}`, `MENU_HAS_APPLY_VIDEO = true`, the
  deferred bind-5 branch and updateGameMenu case 9; full Docker PS2 build
  of the project compiles clean (Build OK, ELF present). A pad-in-hand
  PCSX2 pass (browse the row, APPLY, confirm/revert) is pending - the
  harness has no pad automation. Gotcha logged for next time: PROLOG
  globals sit BEFORE `namespace {{NAME_UPPER_NS}}` opens - a helper
  touching generated types (MenuEntryData) must go after it (first Docker
  build failed exactly there; GCC's error recovery made it look like the
  param type collapsed to `const int&`).

- (144) **Portal crossing: stop the full-screen mask erasing the mounting
  wall.** Owner: at the crossing the wall vanishes and reveals the trick.
  Cause: the full-screen crossing mask (repaints the WHOLE screen with the
  destination at the nearest depth, so nothing redraws over it) fired
  whenever the quad clipped the near plane (`nearClipped`), which off-axis
  triggers while the opening does NOT yet fill the view - erasing the still
  visible wall. It is now used ONLY as a last resort, when the clipped fan
  has fully **degenerated** (`!carved` - the eye is on the surface, no
  valid opening polygon exists), i.e. the single unavoidable frame right at
  the plane (the walker teleports the same instant). Every approach frame
  keeps a valid fan, so the crisp carved WINDOW is drawn and the wall stays
  around it. `nearClipped` removed. Known residual: a free-standing portal
  (no wall) can still show the world just past it in that degenerate frame -
  inherent to the single-render PS2 portal (no oblique near plane). Verified:
  editor builds clean, portals example regenerated + Docker build exit 0.
  Owner pad test next.

- (143) **Two-sided carried-object rendering through a portal (owner's
  architectural call).** The owner reasoned the fix out: don't just NOT
  draw the object - draw it, and clip out only the part inside the portal
  frame. That is exactly right, and it is how a real portal renders. The
  previous approach mapped the whole carried object to the far side and
  drew it ONLY in the through-view, so with a wall it read as clipped to
  the opening (the parts that would fall over the wall vanished) and looked
  like it stalled. Now the object rides straight ahead at its REAL position
  and is drawn NORMALLY in the main pass - the portal's z-cap clips the
  half past the surface inside the opening, a wall around the opening
  occludes the rest - while that portal's through-view draws a COPY mapped
  to the exit (`carryPortalPi` + a save/`portalMapPoint`/restore around the
  view-object loop in renderOnePortalView). The two halves meet at the
  plane, so the object physically straddles the portal, near half this
  side and far half coming out the other - no pin, no bend, no vanish, wall
  or not. renderViewObject's exit-plane dead zone keeps the mapped copy
  hidden until the object's centre reaches the surface, so it appears only
  as it emerges. Verified: editor builds clean, portals example regenerated
  + Docker build exit 0. Owner pad test next.

- (142) **Carried object no longer pins on a portal's mounting wall.**
  Owner narrowed it perfectly: a free-standing portal carries the object
  through fine, a wall-mounted one stops it. The doorway that makes the
  carry sweep ignore the mounting wall works for a wall fully behind the
  plane, but the example wall's front face is flush WITH the portal plane,
  so the sweep still clipped `want` a hair short - and any shortfall below
  `bendT` stops the portal-bend from triggering, leaving the object pinned
  on the surface. Fix: when the carry ray aims through a portal that will
  render the object on the far side (`bendShows` - viewAll or view list),
  `want` is now FORCED to the full carry reach, overriding the sweep, so
  `d > bendT` always holds and the object flies through regardless of the
  wall. Teleport-only portals (can't show the object past the plane) still
  clamp to the surface. Known remaining nit: standing right at a portal and
  looking to the SIDE shows the between-portals dead zone (inherent to the
  single-render PS2 portal - no oblique near plane); looking through it is
  clean. Verified: editor builds clean, portals example regenerated +
  Docker build exit 0. Owner pad test next.

- (141) **Portal crossing mask: physical near-plane test, not a distance
  threshold (fixes "objects vanish near a portal").** (139)/(140) forced
  the full-screen crossing mask whenever the eye was within a fraction of
  the crossing-zone depth of the plane (`zoneClose`, ~1 m out). That
  repaints the WHOLE screen with the destination, so standing that close to
  a portal erased every near-side object (owner report + screenshots). The
  mask now fires only when the quad actually **clips the near plane**
  (`nearClipped` - a corner projects behind `wMin`), which is the literal
  "eye a breath from the surface" moment the mask exists for; every frame
  before that shows the crisp carved window with the near scene intact.
  Also reverted (140)'s behind-the-plane selection band (it let a portal
  render its back face and ghost/erase geometry as you stood behind it);
  the loop-order fix (carried object positioned after the portal teleport)
  is kept. Verified: editor builds clean, portals example regenerated +
  Docker build exit 0. Owner pad test next.

- (140) **Carrying through a portal, the dead-centre take two: keep the
  through-view alive across the plane.** (139) still left the object
  snapping at the exact centre and a "between the portals" flash (owner
  screenshots). Root cause: `renderPortalView` only selected a portal
  while the camera was strictly in FRONT (`rel·n > 0`), so at the plane the
  portal dropped out entirely - no through-view, so the bent carried object
  (skipped in the main pass) had nowhere to draw and the crossing zone's
  full-screen mask never engaged, exposing the wall behind. Fixes: (a)
  portal selection now keeps a portal live for a short band JUST behind the
  plane while the eye is inside the opening rectangle (the crossing frames
  before the walker teleports) - outside the rectangle the back face still
  shows nothing; (b) the crossing-zone test's lower bound drops to
  `lz > -0.6` to match, so `zoneClose` forces the full mask through the
  exact centre; (c) `updateCarriedObject` moved AFTER `updatePortals` in
  both loops - on the teleport frame the camera is already rebuilt to the
  arrival side, so the object anchors there instead of holding one frame at
  the departure side and blinking. Verified: editor builds clean, portals
  example regenerated + Docker build exit 0. Owner pad test next.

- (139) **Carrying through a portal, the dead-centre polish.** With the
  portal-aware carry (138) working, the owner found the object still
  snapped onto the mounting wall at the exact CENTRE of the opening, and
  the "two portals at once" flash returned there for a frame. Both are the
  eye sitting right ON the plane: (a) the carry sweep's doorway
  (`armSweepPass`) only arms when its probe starts in FRONT of the plane,
  so dead centre it failed, the sweep caught the wall and yanked the object
  onto it - the probe now starts backed up behind the eye (-1.2 along dir)
  so it always straddles; the bend detection likewise tolerates the plane
  sitting slightly behind the eye (`t` down to `-(r+0.6)`) so the object
  stays mapped through instead of un-bending for a frame. (b) The
  crossing-zone full-screen mask (from round four) engaged only once the
  quad's screen BBOX stopped covering, but the quad POLYGON stops reaching
  the corners a touch earlier once the near plane clips it - a new
  `zoneClose` (eye within the last half of the crossing zone) forces the
  full mask there, closing the corner-peek. Verified: editor builds clean,
  portals example regenerated + Docker build exit 0. Owner pad test next.

- (138) **Carrying through a portal, take two: the object flies through
  instead of pinning.** (137) clamped the carried object's center to the
  portal plane to stop it vanishing - but any clamp PINS the object's
  forward motion, so it froze on the surface while the player walked the
  last stretch (owner: still stops like a wall, half-in slice or not). The
  clamp was the wrong model. Now the carry is **portal-aware**: if the
  carry ray pierces a portal whose through-view renders the object
  (`portalShowsObject` = viewAll or on the view list), the object flies on
  THROUGH - its placement is mapped to the far side (`portalMapPoint`, the
  teleport isometry) in front of the target, where that portal's
  through-view already draws it, so you see it just beyond the opening as
  it crosses. It is skipped in the near main pass
  (`carryMappedThroughPortal`) so it doesn't also show as a distant double
  at the target. On-screen the hand-off is continuous: near-side (main
  pass) and far-side (through-view) both land the object in the portal
  opening. A teleport-only portal (no through-view of the object) can't
  show it on the far side, so there it still clamps to the plane as the
  best available. Verified: editor builds clean, portals example
  regenerated + Docker build exit 0. Owner pad test next.

- (137) **Carrying an object through a portal (owner's fourth live test).**
  Throwing was "perfect"; carrying had two faults. (1) **The carried
  object vanished at the seam.** Once its center passed the portal surface
  plane it rendered BEHIND the portal - renderPortalView carves the
  opening and caps it at the surface depth, so anything past the plane
  z-fails and disappears (it re-appeared only after the player crossed,
  via the through-view). Fix: `updateCarriedObject` now clamps the carry
  reach so the object's center rides at any portal plane the carry ray
  pierces (rectangle + slack) - half-in / half-out, the classic "entering
  the portal" slice (an earlier revision clamped it SHORT of the plane and
  it pinned flat against the surface like a wall; owner follow-up) - and
  it re-anchors to the new camera the instant the player teleports
  through. (2) **The player couldn't walk through while carrying.** The
  carry whisker (pushes the walker back when the object no longer fits in
  front of the face) re-derived its portal doorway from a FORWARD probe,
  which stops piercing the moment the eye reaches the plane - so the
  doorway slammed shut exactly at the crossing and the whisker bounced the
  player back out. The whisker now takes `portalPassOn`/`portalPassPlane`
  (already published by `updatePortalPass`: "the body column is in the
  opening") as the authoritative doorway, falling back to the forward
  probe only for the approach; the three walkers reset `portalPassOn`
  AFTER the whisker instead of before (so its internal re-collide keeps
  the wall open too). Verified: editor builds clean, portals example
  regenerated + Docker build exit 0. Owner pad test next.

- (136) **Portal crossing, round four (owner's third live test): only
  backwards worked, the thrown sphere "freaked out", and the residual
  standing-in-the-opening pop.** Three fixes:
  (1) **Forwards carry was blocked by the carry whisker.** Walking a
  carried object toward a wall portal, the whisker (which pushes the
  walker back when the object no longer fits in front of the face) read
  the mounting wall as solid and shoved the player off the portal - so it
  could only be entered backwards (the whisker probes forward only). The
  carry sweep AND the whisker now arm the same portal doorway
  (`armSweepPass` factored out of the thrown-arc code): obstacles behind
  the aimed portal's plane stop blocking while carrying into an opening.
  (2) **The thrown rigid body careened between the portals.** The physics
  object teleport mapped only the VERTICAL velocity through the pair and
  wrote only `velocityY`, dropping horizontal entirely - fine for a
  straight-down faller, but a thrown sphere exited with world-space X/Z
  that no longer matched the rotated target and shot off sideways. It now
  maps the full velocity vector (the vertical keeps its position-delta
  fallback for the fall loop). Added a 6-frame per-object hop cooldown
  (`portalHopCool`) so rect-edge jitter / resolution kicks can't re-hop
  every frame (the example's legit fall re-crosses every ~13 frames).
  (3) **The residual crossing pop.** The full-screen crossing-zone mask
  triggered on distance alone, flipping the screen corners wall->
  destination a frame before the quad grew to fill them. It now fires only
  when the carved quad no longer covers the whole screen (near-plane
  clipping ate it) - the fan hands off to the mask with nothing visibly
  changing.
  Verified: editor builds clean, portals example regenerated + Docker
  build exit 0. Owner pad test next.

- (135) **Portal crossing, round three (owner's second live test): the
  radius bug, the visibility rule, 30 u/s, and the carry-whisker wall
  tunnel.** Four changes:
  (1) **The doorway never opened - the radius bug.** (134)'s aim test
  armed the wall exclusion only when the CENTER's motion segment pierced
  the plane in that same frame - but the collision (sweep or AABB
  resolution) stops the body half an extent BEFORE the plane, so the
  center never got there and every throw bounced off the mounting wall
  (owner repro). The aim segment ends are now padded by the body's extent
  (+0.1), in both the thrown arc and physics pass 1 - the doorway opens
  the frame contact WOULD happen, which is exactly when it must.
  (2) **The visibility rule (owner's design):** whatever a portal SHOWS
  can also go through it. New `portalCanCross(p, oi)`: teleportObjects
  OR viewAll OR view-list membership OR the player-released latch. Used
  by the updatePortals object loop, the pass-1 aim and the floor-swallow
  suppression; `portalCarryAim` takes the object index (-1 =
  unconditional, the released-flight path). The example's wall portals
  are viewAll, so every rigid body crosses them now - flag not needed.
  (3) **Terminal fall 15 -> 30 u/s** (owner: 15 too floaty, x2 request).
  (4) **Carry whisker could shove the walker through a wall** (owner
  find): the whisker's pushback runs AFTER collidePlayer and was never
  collision-checked, so carrying an object toward blocking geometry while
  a wall stands at your back pushed you clean through it. The pushback
  now re-runs collidePlayer from the pre-push position (signature gained
  feetY/eyeHeight; all three walker call sites updated).
  Verified: editor builds clean, portals example regenerated + Docker
  build exit 0; generated code shows the padded aim segments, the
  portalCanCross wiring and the whisker re-collide. Owner pad test next.

- (134) **Throw-through-portals rework after the owner's live test: flag
  semantics + terminal velocity tuning.** (133) shipped but the owner's
  test failed on both counts, for two distinct reasons. (1) The test
  sphere is a *physics* pickable, so it never touches the thrown-arc code
  - it rides the rigid-body path, and every portal hop there was gated on
  the portal's Teleport-physics-objects flag, which the wall portals in
  the example do not set (the player teleports through any linked portal;
  gating a deliberate throw on an ambient-objects flag was the wrong
  semantic). New rule: a **player-released body (throw OR drop) is
  "portal-free"** - crosses any linked portal - until it settles to
  sleep. Implemented as `thrownFreeIndex`, stamped in `releaseCarried`
  (index from `&o - runtimeObjects.data()`), cleared on sleep at the top
  of `updatePortals` and on scene reset; `portalCarryAim` gained a
  `needFlag` param (ambient physics keeps requiring the flag, the thrown
  arc and the freed body do not), and the updatePortals object loop,
  physics pass-1 aim plane + floor-swallow suppression all honor the
  latch. Carried objects are now explicitly skipped by the object
  teleport (`oi == carryIndex`) - the carry owns their motion. (2) The
  "cube accelerates to superluminal" report survived the (133) cap
  because the cap was working exactly as the pre-#97 one did: 50 u/s.
  The old loop was constantly hitching and never sustained it; the
  unhitched loop does, and 50 u/s across a 7.7-unit column is a 6.5 Hz
  strobe. Terminal fall is now **15 u/s** in both integrators (~0.5 s per
  column leg - fast, readable). Verified: editor builds clean, portals
  example regenerated + Docker game build exit 0; generated code shows
  the latch wiring and both 15 u/s caps. The owner's pad test is the real
  verdict.

- (133) **Throws fly through portals + the lost terminal velocity.** Owner
  request ("could a thrown object fly through a portal?") plus an owner
  report: the infinite-fall cube now accelerates absurdly - and indeed the
  old portal-branch 50 u/s terminal-fall cap died in the #97 sim rewrite
  (PHYS_MAX_SPEED alone allows 3 u/frame = 150 u/s, and after (131)
  unhitched the loop nothing ever slowed the cube down). Changes, all in
  the game template:
  - **Terminal fall restored**: `vel.y` capped at `50 * g_frameDt` per
    frame in `updateObjectPhysics` (real-time-correct on PAL and NTSC) and
    the same cap on the thrown arc, which previously had no clamp at all.
  - **Thrown objects hop through**: `portalCarryAim` (which linked,
    teleport-objects portal does the motion segment pierce front-to-back?)
    + `portalCarryCrossing` (position + FULL velocity vector mapped by the
    same flip-about-local-Y isometry updatePortals uses). Wired into the
    non-physics thrown arc in `updateCarriedObject`; thrown rigid bodies
    already ride `updatePortals`' physics path.
  - **Doorway rule for objects**: while a throw or a falling body is aimed
    into an opening, obstacles fully behind that portal's plane are
    excluded from `sweepSphere` (new `sweepPass*` state) and from the
    physics static-solid resolution - without this the mounting wall
    stopped/bounced the object ~r short of the plane and the crossing
    never fired (the walkers' `updatePortalPass` rule, applied per body).
  - Thrown arc also skips the terrain ground-rest inside a swallowing
    floor portal's zone (swept test, same as the walkers/physics).
  Verified: editor builds clean; regenerated portals example compiles in
  Docker (exit 0); generated code shows the cap, the carry-crossing calls
  and both doorway filters. Throw feel + wall-portal crossing want a
  hands-on PCSX2 pad test (owner has the live session).

- (132) **Portal viewAll: the mounting wall's backside filled the
  through-view (hotfix on main).** Owner report right after (131) merged:
  a portal mounted on a wall showed that wall through itself. (131)'s
  viewAll path submitted the **merged static-batch bags** with an
  exit-plane test per batch AABB - but a batch AABB spans its whole
  grouping cell (min 48 units), so the test never rejected anything and
  the wall behind the target portal - batched together with half the map -
  painted its backside across the view. The per-object dead zone in
  `renderViewObject` was the already-solved twin ((113)'s "wide thin wall"
  fix); batches bypassed it. Fix: never submit batch bags into a
  through-view - a batched member instead gets a **one-time solo bake**
  inside `renderViewObject` (`objectGeometry` parts empty + not dirty →
  `rebuildObjectGeometry`), after the dead-zone check so a wall behind the
  plane costs nothing. A DIRTY batched member is deliberately left alone:
  `rebuildObjectGeometry` clears the flag `renderStaticBatches` keys its
  demotion on and the portal pass runs first in the frame - the demotion
  rebuilds the solo bag the same frame, the next live view picks it up.
  Cost honesty: a live viewAll view pays pre-(122)-style solo submits for
  batched decor it can see (bake is once, then cached); the main pass
  keeps full batching. Verified: editor builds clean; regenerated portals
  example compiles in Docker (exit 0); generated code shows the solo-bake
  branch and no batch submit in `renderOnePortalView`. Eyes-on PCSX2 pass
  on the example map pending (owner has a live session).

- (131) **Portals vs the merge wave: empty through-views + the infinite-fall
  hitch.** Owner report after #110/#118/#120 landed: through a portal only
  particle effects were visible, standing in the opening briefly read as
  "looking through two portals at once", and the falling cube in the portal
  map sometimes stopped dead. Two independent regressions, neither in the
  portal code itself (byte-identical since #113):
  (1) **Static batching (#120) ate the through-view.** `renderPortalView`
  re-submits view objects via their per-object solo bags, but a batched
  member's geometry lives only in the merged batch bags — no solo bag, so
  every batchStatic primitive silently vanished from the view (particles
  survived on their dedicated redraw path; the missing wall around the
  target portal is what read as seeing through two portals). Portals are the
  same reference kind as mirror lists and were missed when #120 built
  `batchBlockedNames`: portal view lists now block batching for their
  members (codegen), and the **All objects in view** mode — which has no
  list to block by — re-submits the merged batch bags themselves in
  `renderOnePortalView`, with the exit-plane dead zone applied per batch
  AABB (one-frame lag on a scene's very first frame: batches bake in
  `renderStaticBatches`, which runs after the portal pass).
  (2) **The rigid-body sim (#97) can tunnel the floor-portal swallow zone.**
  The zone spans 2.0 units above the plane, the old portal-branch fall code
  was capped at 1 u/frame, but PHYS_MAX_SPEED is 3 u/frame — a
  terminal-velocity faller can step clean over the zone between two frames,
  the point-sampled test misses, the terrain clamp fires and kills the
  fall, and the cube visibly parks on the ground over the portal until
  gravity re-accelerates it into the plane. `portalSwallowSwept` now tests
  both frame endpoints plus the segment's plane-crossing point (exact for
  the vertical fall that is the only motion fast enough to tunnel).
  Docs: portals.md (batching interplay + the stale "50 u/s terminal
  velocity" claim), README's batching bullet. Verified: editor builds
  clean; scratch project with two linked portals + a batchable box on the
  view list emits `batchStatic=0` for the listed box (and 1 when unlisted);
  the generated game compiles the swept test + viewAll batch submit.
  PCSX2 eyes-on pass on the portal map still pending.

- (123) **Live Link: the physics material is part of the recipe hash.**
  `liveLinkRecipeHash` mixed the `physics` flag but not the four material
  fields (113) added next to it - `physMass`, `physBounce`, `physFriction`,
  `physTumble`. All four are compile-time constants in `SCENE_OBJECTS` (see
  the `SceneObjectData` rows in templates.cpp), the live snapshot record
  carries only id/template/position/rotation/scale/color, and a live-spawned
  clone copies its whole row from the template - so retuning bounciness on a
  running game silently did nothing while the chip stayed green LIVE, and a
  clone could inherit a template's physics instead of its own. They are now
  hashed next to `drawDistance`, but only **while `physics` is on**: every
  runtime read is guarded by `data.physics`, so stale values left behind by
  toggling physics off must not force a spurious rebuild. No other field
  (113)/(114) introduced touches `SceneObject`. Verified: editor builds
  clean; the hash of an object with physics off is unchanged by editing its
  (hidden) mass, while turning physics on or retuning a physics object's
  mass/bounce/friction/tumble changes it - i.e. the chip now flips to amber
  "rebuild" for exactly those edits.
- (129) **Pickable review fixes: "PICK UP" prompt + no more inserting the
  carried object into walls (PR #116 comments).** Two owner reports. (1) A
  pickable target now shows a **PICK UP** prompt instead of USE: new built-in
  `res/hud/pickup.png` (128x32, style-matched to use.png, written when
  missing like the other built-in HUD sprites; `pickPromptPng` in
  templates.cpp), a second sprite at the same UI-Editor placement, picked per
  frame by the target's `pickable` flag; `PICK_PROMPT_PATH` baked into
  hud_data.gen.hpp. (2) The carried object could still be parked *inside* a
  wall by pressing the face against it: the sweep correctly found the wall
  but the old `minReach` clamp then pushed the object back OUT past it. Now
  the carry reach follows the third-person boom's policy (springArm, PR
  #114): the sweep is the law — **snap in** when blocked (down to a
  `PICK_MIN_DIST` floor that keeps the object's near face off the clip
  plane), **ease back out** when the wall clears (`carryDist`, seeded with
  the object's real distance on grab so a close grab reels out instead of
  popping). On top, a **carry whisker** (`applyCarryWhisker`, called by all
  three walkers after `collidePlayer`, carrying player only): the same
  sphere sweep run horizontally from the eye along the yaw pushes the walker
  back when the carried object no longer fits at its comfort reach in front
  of the face — pressing "ryjem" into the wall while carrying is simply
  blocked (the probe is yaw-only on purpose: with pitch in it, looking down
  would read the terrain as a wall and freeze the walker). Also from the
  origin/main merge review: `staticBatchEligible` now excludes pickable
  objects (they move at runtime; demotion-on-dirty would have caught it, but
  build-time exclusion skips the first-pickup rebuild hitch). Verified:
  editor builds clean; scratch fpp project with a pickable crate + usable
  lever emits the right rows (`pickable=1` vs `usable=1`), pickup.png lands
  in res/hud, whisker call sites in all three walkers, Docker game build
  compiles. The wall-press feel still wants a hands-on PCSX2 pad test.
  Second merge of origin/main afterwards brought **rigid-body physics
  (#97)**, which rewrote `updateObjectPhysics` into a two-pass sim — the
  carried/thrown skip was re-applied to BOTH passes (pass 1 world
  integration and pass 2 body-vs-body impulse exchange; the carry owns
  those positions, so a crate in your hands must not be shoved by a
  falling one), and the empty-scene placeholder row was reconciled against
  the merged struct: physics params after `physics`, pickable/pickThrow
  after `usable` — the documented (113) trap, checked this time by
  counting columns against the struct (50 = 50 for both real rows and the
  placeholder). Also from that merge: `vendor/ufbx` is a new dependency
  (`setup.ps1` re-run needed after pulling #119) and the Properties
  physics checkbox is now main's "Physics (rigid body)" label.

- (130) **Pickables vs the rigid-body sim: released objects hung in mid-air
  and throws ignored physics.** Owner report right after the #97 merge: drop
  a carried crate in the air and it just hangs there; throw it and it flies
  a flat, lifeless arc. Root cause is the sim's **sleep contract**, which
  did not exist before #97: a body with `restFrames >= PHYS_SLEEP_FRAMES` is
  asleep and skips simulation entirely, and a crate picked up off the ground
  is asleep *by definition* (that is how it was resting). The carry path
  moved it by writing `data.position` directly and never touched
  `restFrames`, so on release the sim kept skipping it — it hung exactly
  where the hands opened. Fix: a single `releaseCarried(o, vx, vy, vz)`
  hand-off used by every exit from the hands (drop, throw, despawn/hide
  mid-carry) that sets the velocity and **wakes** the body (`restFrames =
  0`). The throw is now handed to the real sim instead of the hand-rolled
  arc, so a thrown crate bounces, rolls and tumbles with its authored
  mass/bounce/friction — the old manual integration survives only for
  pickables *without* Physics, which have no sim to hand off to (and, as
  documented, hover when dropped). Also: carrying zeroes all three velocity
  components and the spin (the old code zeroed `velocityY` alone — the
  pre-#97 field), and catching a body mid-flight kills its tumble instead of
  leaving it spinning in your hands. Verified: editor + Docker game build
  clean, generated `releaseCarried` wakes on all three exits, scratch crate
  authored as a real rigid body (`physics=1, pickable=1, pickThrow=1`).
  Drop/throw *feel* is the hands-on pad test the owner is running.

- (128) **Pickable objects — pick up, carry in front of the face, drop,
  experimental throw.** New per-object flags `pickable` + `pickThrow` (solid
  geometry only, save points excluded). Pressing USE on a pickable object
  grabs it; each frame it rides `PICK_CARRY_DIST` in front of the eye (in
  third person: in front of the *avatar's head*, the camera pivot — not the
  camera floating meters behind), positioned by a **sweep** of its own
  bounding radius against the world, so the carried object keeps colliding
  with walls/props and can neither be pushed through geometry nor parked
  behind it — a blocked reach just brings it closer to the face. The sweep is
  the old camera `springArm` generalized into `sweepSphere(pos, dir, maxDist,
  radius, skipIndex)` (AABB slab tests + terrain march, unchanged math);
  `springArm` is now a thin wrapper passing `CAM_RADIUS` + the carried index,
  so the boom ignores the box hovering at the face. The carrier stops
  colliding with its cargo both ways (`collidePlayer` skips `carryIndex` —
  otherwise the player wedges against their own crate) and `updateObjectPhysics`
  leaves carried/thrown objects alone. USE drops it in place (already a swept,
  legal spot; with Physics on it falls and rests via the normal path);
  `BTN_THROW` (Circle) launches it if **Can throw** — integrated under gravity
  with a per-frame sweep, stopping on the first hit and handing `velocityY`
  off to regular physics. Picking eats its own USE press (`carryGrabbed`
  latch — otherwise the same click reads as an instant drop), use-targeting
  is disabled while hands are full, a pickable+usable object still fires On
  Used on the grab press, scene switches open the hands, and a
  despawned/hidden carried object just releases. Tunables as **#defines** in
  `controls.hpp` (`PICK_CARRY_DIST`/`PICK_THROW_SPEED`/`BTN_THROW`) with
  `#ifndef` fallbacks in the game cpp so user-owned `controls.hpp` copies
  from before this feature still build — and their tuning wins when present
  (that's why defines, not constexpr: `#ifndef` can't see a constexpr).
  Full chain: fields + `operator==` + JSON save/load + `liveLinkRecipeHash`
  bits, Properties + multi-select UI, `SceneObjectData` columns (struct doc,
  row emission AND the empty-scene placeholder row — the documented (113)
  trap), both loop call sites. Verified: editor builds clean; scratch project
  with a pickable+throwable crate round-trips `--resave`, row emits
  `usable=0, pickable=1, pickThrow=1`, Docker build compiles (=== Build OK
  ===). The grab/carry/throw *feel* needs a hands-on pad test in PCSX2 (not
  run this session — a PCSX2 instance from a parallel session was live and
  the Runner would have killed it).

- (113) **Terrain splat painting - paint a blend of terrain layers, drawn as
  two-pass GS splatting.** Terrain used to wear a single tiled material; now a
  scene can carry extra **terrain layers** (each an existing `.mtl`, so they
  inherit texture + Kd tint + tiling) and you **paint their blend straight onto
  the terrain in the 3D viewport** with a brush (a paint mode alongside sculpt,
  sharing the same raycast + ring; Shift or the Erase toggle removes). A
  unified **Terrain Editor** window (Tools > Terrain Editor) hosts BOTH terrain
  brushes - Sculpt and Paint as switchable tools (viewport toolbar + keys 4/6;
  grabbing a tool opens the window; one brush in hand at a time) - plus the
  layer stack (Photoshop-style: top row paints over those below, "+ Add layer"
  at the top drops the new layer on top, base at the bottom;
  add/rename/pick material/reorder/remove, active-layer radio, and
  a per-layer **Size** = how big that layer's texture pattern looks on the
  ground, a multiplier on its material tiling) and the per-tool brush settings;
  compact brush sliders float in the viewport while a tool is active (same
  variables, never disagree) and `[`/`]` resize the brush from the keys.
  **Brush ranges scale with the map** (radius up to half the map, sculpt
  strength up to dim/100, logarithmic sliders): the old fixed 30/0.5 caps made
  the brush useless on a 2000x2000 world (verified by GUI script - radius
  reaches ~1000 on a 2000-map, overlay and window stay in sync).
  **Runtime = era-correct two-pass vertex-alpha splatting** (the first cut
  baked the blend into ONE whole-terrain composite - zero runtime cost, but the
  GS's 512-texel texture cap made it embarrassingly blurry up close, dead end
  documented in docs/terrain-painting.md): weights live per VERTEX on the
  heightmap grid (`SceneData::splat`, sidecar `terrain-<scene>.splat`, resample
  policy identical to heights), codegen bakes them into
  `terrain_heights.gen.hpp` + layer descriptors into `texture_data.gen.hpp`,
  and `buildTerrainChunk` adds one StaPip bag per layer present in a chunk
  (shared vertices, tiled layer STs, shade-lit colors with alpha = weight)
  under a blending-enabled info bag - the in-band per-mesh ALPHA qword (105)
  already defaults to alpha-over, so no engine change was needed. The editor
  viewport draws the same two passes (particle shader, 9-float mesh, LEQUAL
  no-depth-write blend after the base chunks) - editor and PS2 agree by
  construction. **Verified end-to-end**: headless harness (30 checks: grid
  coupling, round-trip, undo equality, layer column ops, detail-change
  resample, codegen tables incl. the no-layers null case); generated game
  compiles clean in Docker; PCSX2 SW-renderer boot shows the tiled dirt path +
  rock zone crisply blended over textured grass with soft Gouraud edges, no
  TYRA asserts; **A/B benchmark** (same scene, layers stripped): 50 FPS / EE
  36% / VU 2% unpainted vs 50 FPS / EE 36% / VU 3% with two painted layers -
  the extra passes only exist where painted. Editor GUI screenshot confirms
  the viewport twin matches the PS2 output. **Follow-up fix caught by the
  owner's first real map**: layer textures ship to the game directly now, so a
  1024x1024 material texture hit the engine's "512x512 max" assert at load
  (v1's composite had masked oversize imports; a 1280x720 fog texture in the
  same project was a pre-existing landmine on ANY object). texbake now resizes
  every res/models|materials|textures PNG with non-PS2-valid dimensions
  (power-of-two, max 512) into the bake, exactly like HUD sprites - sources
  stay full-res for the viewport. Verified on the owner's project: three
  textures auto-resized (2x 1024x1024, 1x 1280x720), game boots with zero
  asserts.
- (114) **Stochastic tiling (texture bombing) for terrain - kill the
  tiled-grid "checkerboard".** A tiled terrain texture repeats on a visible
  grid the moment the camera pulls back; PS2 has no pixel shaders to randomize
  it per-fragment, so the randomization happens **at build time, in pixels**.
  New per-base / per-layer **Stochastic** toggle in the Terrain Editor: the
  build bakes that texture into one larger, still-perfectly-tileable
  "supertile" (up to 512x512) whose interior scatters randomly rotated /
  flipped / offset, feathered patches of the source, wrapped on the torus so
  it tiles seamlessly. The game tiles the supertile like any texture - **same
  single pass, zero runtime cost** - but the repetition period is 2-8x longer
  (by source size), so the grid leaves the visible range. New host module
  `src/stochtile.{hpp,cpp}` is the single source of truth (`generate` +
  `factorFor` + `bakedBinPath`), deterministic from the source path: texbake
  generates the supertiles into `.res-baked/stoch` (never mirrored from res/,
  regenerated wholesale, exempt from the vanished-source sweep) quantized like
  the source, and the editor viewport uploads the same pixels - so preview ==
  build. Codegen points the terrain texture table + tiling at the supertile
  (repeats-per-unit divided by the factor so the on-ground size is unchanged).
  Best on organic textures; off by default; a scene without it is byte-for-byte
  unchanged. **Verified**: headless harness (13 checks: factor math, 512²
  output, torus wrap-seam not a hard discontinuity, bombing actually perturbs
  the tiled base, determinism, bakedBinPath sanitize, codegen path + divided
  tiling); PCSX2 SW-renderer A/B on a 256x256 map with a deliberately
  grid-heavy 128px source - OFF shows identical blobs locked to a perfect grid,
  ON scatters them at varied positions/sizes; both 50 FPS / EE 37% / GS 7%
  (zero runtime cost confirmed). texbake logs "baked N supertile(s)".
  New files `src/stochtile.{hpp,cpp}`.
- (115) **Macro ground variation - light/dark patches at the group-of-tiles
  scale.** The stochastic supertile (114) still repeats every 2-8 tiles (the
  GS 512 texture cap is hard); this adds an *unbounded* third scale: a
  per-scene **Variation** (Amount + Patch size, Terrain Editor) multiplies
  deterministic world-position value noise (two smoothstepped octaves,
  integer-hash lattice, no trig) into the terrain vertex shade while chunks
  bake. Zero runtime cost (vertex colors are computed at build anyway),
  infinite period, and it tints base + painted layers TOGETHER (all shading
  flows through shadeAt), so patches read as ground lighting, not an overlay;
  Gouraud keeps edges smooth. Twin formula in the generated game
  (templates.cpp `tintNoise2` above buildTerrainChunk) and the viewport
  (viewport.cpp) - identical inputs, kept in sync. The supertile generator
  also gained a few large low-amplitude brightness blotches (mid scale), so
  micro/mid/macro compose. Fields on SceneData (in undo, manifest + history
  JSON, emitted as TERRAIN_TINT_VARIATIONS/SCALES). **Verified**: harness
  round-trip + codegen checks (34 total now); PCSX2 SW renderer shows soft
  multi-tile light/dark patches over the stochastic scatter at 50 FPS / EE
  35% (same as without - zero cost); editor GUI shows the Variation section
  and the viewport crop shows the same patches over the checker (twin
  confirmed both sides).
- (116) **Terrain Editor polish: base material combo + stochastic no-op hint.**
  Two friction points from real use. (1) The base terrain material could only
  be set in Scene Preferences, away from where you paint - now there's a
  material combo on the base row of the Terrain Editor's layer stack; it edits
  the scene's own material when the scene overrides the project default,
  otherwise the project default (so a single-scene project just sets it in
  place). (2) "Stochastic tiling did nothing" - because it scrambles a texture,
  and the base (or a layer) with no texture assigned had nothing to work on,
  silently. The Stochastic toggles are now disabled (greyed) whenever the
  base/layer has no texture, with a tooltip saying to assign one first;
  codegen was already a no-op there, so this is purely communicative. Verified
  by GUI: assigned a base material from the Terrain Editor combo (flat green ->
  tiled ground), the checkbox re-enabled, and ticking it visibly broke the
  tiled grid in the viewport preview.

- (122) **Model yaw offset (content-forward correction) + FBX orientation
  investigation on real user content.** Owner's imported cat
  (`character.fbx`) faced 90 deg sideways as a third-person avatar.
  Diagnosis chain, each step measured: (1) the RAW file (before any
  importer conversion) already has its content long along +-X while
  declaring front=+Z - the import preserves orientation byte-faithfully;
  (2) the repo's working `cat.glb` is the same rig whose root was
  hand-wrapped into Z-forward back in the two-player work - same disease,
  same source convention (models authored facing Blender's red +X axis;
  both exporters map Blender's -Y to the engine's forward); (3) a host
  replica of TsklLoader's full validation passes the fbx-baked .tskl, and
  in-game instrumentation showed the model loading, skinning and animating
  correctly - the "invisible avatar" red herring during verification was
  the idle clip resolving to the fbx's `EmptyAction` (a REAL animated take
  on the Armature that flings the cat off-camera; the rest pose is
  `reference|EmptyAction`, matching how the .glb rig is authored).
  Fix shipped: **`modelYawOffset`** on SceneObject (degrees around the
  model's own Y, applied between scale and rotation in the generated
  game's anim-matrix build AND the viewport's `modelMatrix` - the two are
  documented twins), so an X-forward model renders turned while the
  walker's faceYaw, AI turn-to-face and authored rotation stay
  convention-pure. Full chain: field + `==`, JSON (`modelYaw`, omitted at
  0), `liveLinkRecipeHash`, `SceneObjectData` column + placeholder row,
  band-cull rotated-object check includes the offset, Properties UI row
  (with the Blender-habit tooltip) on animated models and avatars.
  Also switched the ufbx load to `SPACE_CONVERSION_ADJUST_TRANSFORMS` +
  `GEOMETRY_TRANSFORM_HANDLING_HELPER_NODES` (the geometry-modifying
  variants are documented as animation-lossy; sausage-rig regression
  identical). Verified in PCSX2: the fbx cat renders sideways at offset 0
  and tail-to-camera at +90 (screenshots), sausage harness byte-identical,
  scratch project codegen + Docker build clean. Root-motion note for
  authoring: the fbx walk take carries ~1.3 m of real root travel - as an
  avatar clip that reads as sliding; export locomotion in place.



- (121) **FBX import for animated models (.fbx next to .glb).** Feasibility
  answered with a yes: the vendored [ufbx](https://github.com/ufbx/ufbx)
  single-source reader (MIT; `vendor/ufbx`, cloned by setup.ps1 like the
  other deps, compiled into the editor) reads binary+ASCII FBX from
  Blender/Maya/Max. New `src/fbxparser.cpp/.hpp` fills the SAME
  `glbparser::Baked`/`Skel` structures the .glb path produces, so
  everything downstream — `.tskl` serialization, LODs, viewport preview,
  import validation, codegen, the third-person locomotion mapping — is
  untouched and format-agnostic; call sites now go through a tiny
  `animimport::bake/parseSkel` extension dispatch. Design choices: axes/
  units normalized to the glTF convention (right-handed Y-up, meters -
  Maya centimeter rigs import at the right size), geometry transforms
  (pivots) baked into vertices, FBX animation curves NOT translated but
  **resampled at 24 Hz and RDP keyframe-reduced per channel** (sidesteps
  rotation orders/pre-post rotations/pivot curves entirely; quaternion
  hemisphere continuity enforced for the runtime's lerp), take names
  `Armature|Walk` shortened to `Walk` (full name kept on collision),
  weights capped to the 4 strongest and renormalized to 255, external
  texture files copied next to the imported .fbx (a .glb embeds them, an
  .fbx usually does not; non-PNG transcoded). `isAnimatedModelPath` now
  accepts .fbx; import dialog, model combos (via a merged
  `listAnimatedModelFiles`) and UI texts updated. Verified: editor builds
  clean; a scratchpad harness on ufbx's skinned test rig
  (`blender_279_sausage_7400_binary.fbx`) shows 3 named clips, 1728 verts,
  3-bone palette, all weight sums == 255, real vertex motion across baked
  frames, 126 keys after reduction, 58 KB .tskl; composing the exported
  node TRS hierarchy reproduces ufbx's own `node_to_world` to 2.4e-7;
  full e2e: scratch project with the .fbx as a Model object `--refresh-gen`
  bakes `res/models/sausage.tskl` and the Docker game build compiles
  (=== Build OK ===). Pending: an in-PCSX2 visual pass of an .fbx model
  animating (blocked this session by a parallel PCSX2 instance) and a
  GUI import-dialog walkthrough.
- (114) **Physics perf: moving bodies render through a VU1 model matrix
  (28-body bench 14 → 156 FPS) + frame-counter timers made wall-clock true
  under disableVsync.** Profiling the (113) physics with bodies scattered
  showed the frame dying not in the solver (VU0, trivial) but in
  `rebuildObjectGeometry`: every awake body re-tessellated and re-shaded its
  whole mesh on the EE every frame it moved. Now an awake body takes a
  **matrix fast path**: one local-space bake on wake (scale baked into the
  vertices, shading frozen at the wake pose - the light rides along while it
  tumbles, corrected by a world re-bake on sleep) and from then on only
  `ObjectGeometry::objMat` (rotation basis via the same `rotated()` the bake
  uses + translation) is refreshed per frame; every `part.infoBag->model`
  points at it, so **VU1 applies the motion** inside the transform it already
  does, frustum classification uses the engine's object-space-planes path
  (proven by animated models, which have always rendered model-space vertices
  under `animMat`), and the bbox cache stays valid (no per-frame bboxVersion
  bump). Mirrors compose `reflection * objMat` exactly like the animated
  path; the dynamic-env-map base pass needs nothing (bags carry their
  matrix). **Exclusions** (legacy re-bake path): usable objects (the
  highlight hull/apron reads world-space vertex arrays), reflective-material
  objects (matcap env normals bake in world space), animated models (already
  matrix-driven). Impulse-pass separations and player shoves stopped setting
  `dirty` (the matrix refresh in renderScene picks the moved positions up);
  the Apply Impulse node emits no `dirty` at all now (velocity-only).
  **Bench** (tyra-testing layer 3, PCSX2 software renderer, debug + FPS
  overlay + vsync off + vu1 clipping, 28 high-bounce bodies dropped from
  8-20 units): before 14-16 FPS all-airborne / 33 part-settled; after **156
  FPS all-airborne / 137 FPS**, VU 4% → 41-45% - the work measurably moved
  to the VUs; no asserts, tumbled boxes render visibly rotated. Bonus bug
  found by the unlocked frame rate: `everyFrames()` counted frames at the
  NOMINAL vsync rate, so with disableVsync every frame-counter timer (Every N
  Seconds, Delay, splash holds, sound retriggers) ran as much too fast as the
  FPS exceeded 50 - the physics-playground kicked ball got re-kicked every
  ~1.1 s real and climbed into the sky. `everyFrames` now divides by the
  measured `g_frameDt` (bit-identical at vsync - the clock snaps to nominal),
  **Every N Seconds** compiles to a per-node countdown instead of
  `frame % everyFrames(s)` (a modulo against a divisor that tracks measured
  dt can skip its ==0 frame), and the loading-screen holds compare against a
  `loadingTotal` snapshot instead of re-evaluating `everyFrames(0.7F)` in a
  `==` (which could now miss and never load the scene). Verified: example
  telemetry back to sane pacing (ball lands between kicks, rests at
  terrain + radius, descends the terraces, wall-bounces at ±23.5) with the
  scene still uncapped >130 FPS.

- (113) **Object physics upgraded from "falls straight down" to a
  rigid-body-lite simulation (bounce, slide, tumble, stacks, shoves,
  impulses).** The old `updateObjectPhysics` was Y-only gravity that stopped
  dead at the terrain height. The new one gives every `physics` body: full 3D
  per-frame velocity; restitution bounces off the terrain using the **real
  slope normal** (central differences on the heightfield), so bodies kick
  sideways off hills and slide/roll downhill; per-contact friction; **tumble**
  (ground contact converts slide into roll-without-slipping spin, integrated
  into the Euler rotation - visually right, era-appropriate); reflecting
  world-edge walls; AABB contacts against static solids resolved along the
  least-penetration axis (crates rest on platforms, land on each other's
  tops with ground friction); an **impulse pass** between bodies
  (upright-cylinder contacts, momentum split by relative mass, restitution =
  max of the pair); and **player shoves** (`pushPhysicsBodies`, called from
  both walkers before `collidePlayer` with the attempted step - push scales
  with 1/mass). Perf: near-rest grounded bodies **sleep** after 24 frames
  (`RuntimeObject::restFrames`) and cost one branch per frame until an
  impulse/shove/collision/support-loss wakes them - a support-loss check wakes
  riders when the body under them slides away; the vector work (integrate,
  normal decompose, reflect, dot/normalize) runs on **VU0** via `Tyra::Vec4`'s
  macro-mode ops; geometry rebuilds only on frames the transform actually
  changed. Authoring: per-object physics material - **Mass / Bounciness /
  Friction / Tumble** (`physMass/physBounce/physFriction/physTumble`,
  serialized only while `physics` is true, defaults keep old projects loading
  clean), edited under the Properties *Physics (rigid body)* checkbox. Scripts
  see `velocityX/Z` + `spin[3]` + `restFrames` next to the kept `velocityY`
  (legacy scripts compile unchanged); save-restore and Set Position / Move
  Object By wake the body so it re-settles. New **Apply Impulse** flow node
  (`PushObject`: X/Y/Z in units/s, converted to per-frame velocity at codegen,
  wakes the body); Spawn Object clones start with fresh physics state. New
  `examples/physics-playground` (README-documented): superball vs dead-thud
  vs medium materials dropped on a terraced slope, a sleeping crate stack the
  player can topple, and a flow graph that kicks a ball every 3 s while
  logging its position. Verified per tyra-testing layer 3: scratch FPP
  project, Docker build, PCSX2 **software renderer** - `bin/log.txt`
  telemetry shows the kicked ball resting at exactly terrain + radius
  (y = 3.1 = 2.5 plateau + 0.6), flying on each impulse, descending the
  terraces to the low plain (y = 0.6) and ping-ponging off the ±23.5 walls;
  screenshots show both balls mid-air then settled and the crate stack
  upright; steady state (all bodies asleep) holds **50 FPS, EE ~35%** - same
  as before the feature; a transient 24 FPS dip appears only while several
  bodies rebuild geometry mid-flight (the pre-existing moving-object rebuild
  cost, not the sim). The walk-into-shove path needs a hands-on pad test by a
  human (no pad in the harness). Dead end for the record: the physics helpers
  were first emitted as file-`static` functions - `GameModel` is a nested
  type of `TerrainGame`, so they must be static members (the PS2 gcc error
  cascade "cannot convert GameModel* to const int*" means exactly this).

- (122) **examples/two-players: two cats, the sample-man avatar removed, sky-toggle
  defused; static batching (#120) merged into the branch.** Owner request
  after the profiling session. P1 is now `player-cat-ginger` - the same
  `cat.glb` avatar as P2 at scale 3 with the P2 rig (cat-sized boom, Idle
  mapped to the `EmptyAction` rest pose so the walk cycle no longer plays
  in place while standing - the root cause of the "avatar turns wrong,
  camera-dependent" report: unmapped idle fell back to clip 0 = the walk
  cycle, whose root motion swung the body) and a ginger tint vs P2's gray
  (object color multiplies the model texture - two distinct cats from one
  .glb). the old P1 avatar model + its extracted texture deleted from the repo;
  README/docs mentions rewritten (the 14k-vertex history note in
  docs/multiplayer.md stays as context). `example_interaction.cpp` no
  longer registers the press-X-sky-toggle script - the FILE stays as a
  comment-only stub because refreshGenerated recreates missing files
  write-if-missing, so deleting it would resurrect the behavior on the
  next build. Showcase settings restored after the owner's profiling edits
  (release, vsync on, FPS/MEM HUD off, animLod 0, meshLod 4). PR #120
  (static batching, stacked on this branch) merged via GitHub +
  fast-forward pull; owner's uncommitted map edits stash-preserved through
  the merge and folded into this commit (terrain heights included).
  Verified: editor rebuilds clean post-merge, example regenerated (both
  scene rows are cat animModel 0, zero references to the removed model in generated
  code), Docker build OK, PCSX2 boot clean at 50 FPS with no asserts.
  The 2P visual pass (two distinct cats in split) still wants a pad.


- (122) **Static batching for scene objects - the lever (121) called for.**
  The generated game now merges non-moving primitive objects that share a
  material into combined world-space StaPip bags at scene load, so a map of
  small decor pays the ~0.7-1.5 ms fixed per-bag EE submit cost once per
  batch instead of once per object (twice over in split screen - (121)
  measured the two-players map's static loop at 11-17 ms for 8 tiny
  objects). Pieces: build-time eligibility as a new
  `SceneObjectData::batchStatic` column (`staticBatchEligible` in
  templates.cpp: geometry primitives only, no physics / usable / save-state
  / reflected / draw-distance / streaming layer / own graph or attached
  scripts, and not referenced by name from any same-scene flow node with an
  ObjectName param, mirror target list, or cutscene track / camera shot -
  over-excluding is safe, so readers count too); game-side grouping by
  material within a coarse world cell (quarter-map, min 48 units, anchored
  at the map corner - a finer or origin-straddling grid split the
  two-players decor into single-member batches worth nothing) with a
  reflective-material opt-out at load; one shared info bag (Precise
  frustum culling - never raw submission - full clip checks), bboxVersion
  bumped on every rebuild per the bbox-cache rule; per-batch world AABB
  wired into the split-screen band cull like terrain chunks. Runtime
  mutation channels that build time cannot see (Live Link records, Raycast
  / custom-node latches fed into object actions, global scripts writing
  ctx.objects) are caught per frame: a dirtied member is DEMOTED to the
  solo path (batch rebuilds once without it - a per-frame-animated member
  would otherwise re-bake the batch every frame), a visibility/residency
  flip only rebuilds in place (caught by a shown-snapshot, since hide/show
  can skip dirty). New Preferences > Rendering toggle `staticBatching`
  (default on; the A/B lever), baked as STATIC_BATCHING into
  terrain_config.hpp; boot logs "Static batching: N objects in M batches".
  Docs: README bullet, docs/multiplayer.md budget rule updated (N_bags,
  not N_objects), batching invariants added to the tyra-editor-dev skill;
  all 12 example projects regenerated. Verified: editor builds clean;
  two-players codegen flags exactly the 6 primitives (players 0) and
  merges them into 1 batch; Docker builds clean for the FPP (two-players)
  and orbit (scratch) variants; PCSX2 software-renderer boots show the
  title scene and the split halves pixel-plausible at 50 FPS / 100% with
  no TYRA asserts; a PCSX2 harness (owned scratch copy dirtying one box at
  frame 300 and toggling another's visibility every 200) logged the exact
  expected sequence - initial bake of 3, in-place rebuild on the flip,
  demotion of the mutated member, rebuilds with 2 members after - and kept
  rendering all boxes. **Real-PS2 A/B still pending**: the measurement
  copy is staged in %TEMP%\tyra-editor-test\batchab (fresh codegen + the
  (121) PERF frame/sub-phase instrumentation and teleport sweep, release +
  vsync off; flip `"staticBatching": false` in the .tyra for the B leg),
  but ps2link on the console answers neither reset nor execution (pings
  fine - the same wedged state (121)'s ops note ends with) and needs a
  power-cycle before `--build <abs> --run-ps2 192.168.100.150` with the
  MAIN checkout's ps2client can run the sweep.

- (121) **Real-PS2 split-screen profiling: VIF raster switch validated on
  hardware; the "35 FPS on an empty map" mystery solved (per-bag submit
  overhead, not a bug).** Owner hit ~35 FPS in split on the two-players map
  and asked for profiling. Method: scratch copy +
  padless split harness (title screen off, `opt_players` default 2P) +
  a teleport sweep script sampling five viewpoints (3 s each, min/avg/max
  frame dt logged over the `[ps2]` stream), deployed to the real console
  (192.168.100.150) via `--run-ps2`, then two rounds of owned-copy COP0
  instrumentation (loop segments, then renderScene sub-phases). Results
  (PAL, vsync off): config A (owner's debug + meshLod 44) worst view
  29.2/32.1 ms avg/max = the reported 35 FPS; release + meshLod 4 was THE
  SAME (29.8 ms - profile and avatar LOD irrelevant here); 1P on the same
  build = 13-15 ms per view -> split is exactly 2x, no hidden overhead.
  Sub-phases per frame (both halves): sky 0.5 ms, terrain 0.8-1.0 ms
  (terrainDetail 16 -> 8 changed ~1.7 ms - not the sink), skeletal avatars
  4.5-5.7 ms, **static-object loop 11-17 ms = the sink: ~0.7-1.5 ms fixed
  per-bag submit cost per object on the real EE** (the map's 8 primitives
  + pillars each pay it, x2 halves; PCSX2's fast EE hides it, which is why
  (118)'s emulator numbers said 50 FPS locked). Runtime overheads all
  healthy on hardware: split brackets 0.04-0.14 ms (**the (120) VIF-queued
  switch validated on the real console** - correct halves, no hang, the
  brackets are near-free), beginFrame 0.55 ms, endFrame 0.53 ms, 2D/HUD
  0.005 ms. Conclusion: not a bug - small maps made of many separate
  primitive objects are the pathological case for per-bag overhead;
  documented a hardware budget rule in docs/multiplayer.md
  ((0.5 + N_objects x ~1 ms + anim) x 2 <= 20 ms). The lever worth
  building next: a static-batching pass (merge non-moving primitives
  sharing a material into one bag at scene load). Ops note: rapid
  redeploy cycles (each kills the previous ps2client host) wedged ps2link
  once - owner power-cycled; the deploy chain otherwise ran A->G unattended
  over the worktree with the MAIN checkout's ps2client (firewall rules are
  path-scoped).

- (120) **Split raster switch without CPU stalls + per-object LOD
  overrides (P1/P2 avatars tune independently).** Two pieces. (1) The
  review's remaining item, done the era-correct middle way instead of the
  full GS-second-context rebuild: `RendererCoreSplitView::begin()` no longer
  costs a `dma_channel_wait` + `draw_wait_finish` round-trip - the per-half
  XYOFFSET/SCISSOR shift rides the **VIF1 stream** as a prebuilt immutable
  4-qword packet `[VIF FLUSH, VIF DIRECT -> A+D giftag]`: the FLUSH makes
  the VIF itself wait for the previous half's microprogram + PATH1/PATH2
  transfers, DIRECT streams the register writes through PATH2 in-band, and
  the EE moves straight on to culling/packaging the next half (the wait
  overlaps real work instead of blocking). The full second-context variant
  (CTXT bit in PRIM) stays future work - every VU1 GIF tag and each texture
  send would need a _2 twin. `end()` deliberately keeps its CPU handshake:
  the HUD/post-fx after it arrive over PATH3, which a VIF-queued restore
  cannot order against. Giftag NLOOP double-checked against the documented
  stall pitfall (2 A+D rows, NREG 1, DIRECT counts 3 qwords incl. tag).
  (2) **Per-object LOD overrides**: the project-wide Animation/Mesh LOD
  distances (Preferences > Rendering) can now be overridden per object -
  new `animLodOverride`/`meshLodOverride` on SceneObject (-1 = preference,
  0 = off for this object, >0 = custom distance; full chain: `operator==`,
  JSON emit-at-non-default, `liveLinkRecipeHash`, `SceneObjectData` columns
  + the empty-scene placeholder row, `updateAndRenderAnimObjects` reads the
  effective per-instance values, and `bakeAnimAssets` bakes .tskl LOD
  chains when ANY object referencing the model overrides mesh LOD > 0 even
  with the preference off). Properties UI (`drawLodOverrides`) appears on
  animated models and on Player avatars - the **two Player objects of a
  two-player scene each carry their own set**, giving independent main/
  second-player categories. Verified: editor builds clean; a scratch copy
  of examples/two-players with overrides on the cat (`animLod 12`,
  `meshLod 0`) round-trips `--resave` and emits `..., 12.0F, 0.0F, ...` in
  its object row; Docker build compiles the new engine + game; PCSX2
  padless split harness (a script flips the menu-bound `opt_players` save
  value; plus `titleScreen` off and `opt_players` defaulting to 2P so no
  pad is needed) runs the VIF-queued raster switch every frame: the
  software-renderer screenshot shows both halves correctly cropped and
  scissored (P1's FPP view up top, the cat avatar idling in P2's half),
  full-screen HUD on top, 100% speed, no asserts - a mis-ordered register
  write would bleed the halves, and the documented undercounted-NLOOP
  pitfall would hang the GIF at boot. Real-PS2 validation of the VIF path
  still pending (PCSX2's VIF/GIF model is permissive).

- (119) **Split-screen optimization pass (review follow-ups): band culling,
  two-focus streaming, per-half particle billboards.** Three findings from
  the optimization review of the two-player PR, implemented: (1) **Band
  culling** — the split raster crops via XYOFFSET+scissor and keeps the
  projection full-height, so the engine's frustum classify let each half
  transform ~2x the geometry it can show; `computeSplitBand` now derives two
  extra planes bounding the half's visible vertical band (0.62 margin over
  the exact 0.5 for the clipper's guard band; disabled on degenerate
  straight-up/down views) and terrain chunks + static objects wholly outside
  skip submission before the engine sees them. Chunks got a build-time world
  AABB for the test; rotated objects fall back to a bounding-sphere cube so
  the cull can under-cull but never over-cull. (2) **Two-focus terrain/layer
  streaming** — worse than the review's "streaming ignores P2": with both
  split passes calling `updateTerrainChunks` under different cameras and one
  shared pool, the P1 pass evicted P2's chunks and vice versa - permanent
  rebuild churn once the players walked apart. The chunk ring now streams
  once per frame around BOTH foci (P1's look-at + P2's avatar; a chunk near
  either survives, build picks the nearest-to-its-focus across both rects),
  the pool doubles in scenes that can host P2, and auto-streamed layers use
  the min distance over both players (load when either enters, unload when
  both leave). (3) **Particle billboards per half** — quads were built
  camera-facing once (P1's view), so P2 saw fire/fog sprites edge-on; the
  quad build is split out of the simulation (`orientParticleQuads`, shape
  stored per particle) and the second half re-faces the same particles for
  its own camera. The fourth review item - replacing the split brackets' three
  CPU stalls with the GS's second drawing context (CTXT bit in PRIM) - is
  deliberately NOT done: Tyra's VU1 microprograms hardcode context 1 in
  their GIF tags, so it needs microcode changes + a real-PS2 pass. Verified:
  editor builds clean; `examples/two-players` regenerated + Docker build
  compiles; a fresh 1P scratch project also compiles (the paths fold away
  without a second player); PCSX2 boot of the example is clean (menu +
  scene render, 50 FPS, no TYRA assert in bin/log.txt). The split-specific
  paths (band culling actually kicking in, two-focus streaming under two
  pads) still want the hands-on two-controller session (117)/(118) used.

- (118) **Split-screen perf + correctness: 25 -> locked 50 FPS, cat faces
  forward.** Owner playtest findings on (117). The real BUG: renderScene
  runs twice per split frame and the animated path advanced playback AND
  re-skinned every avatar in BOTH halves - animations played at 2x speed
  and the P1 avatar's 14k verts were skinned twice. Fixed with
  `splitSecondPass` (generated game): the second half re-submits the
  frame's skinned buffers under its own camera/frustum, no advance, no
  re-skin (a mesh-LOD tier switch still forces the other tier's buffers).
  Also dropped the split brackets' per-half clear sprite - beginFrame's
  full-screen clear covers both halves and the scissor clips z-writes, so
  the copied-from-env-map clear was two half-screen GS fills + FINISH
  stalls per frame for nothing (engine splitView.begin loses the clearColor
  param). Profiler-driven (debug Show frame profiler + vsync off, PCSX2 SW,
  PAL): 1P scene 5.3 ms / frame ~10 ms; split scene was 19.2 ms and the
  whole frame ~21.5 ms - 1.5 ms over the 20 ms vsync budget = halved to 25.
  After skin-reuse: scene 16.4. The rest is content: demo tuned with mesh
  LOD distance 4 (third-person cameras sit ~5 units out -> both avatars
  render the 50% baked variant nearly always; the old P1 avatar was a PC-grade
  mesh) + terrain detail 16 -> scene 13.2 ms, frame 18.4 ms, **FPS 50
  locked with vsync** (F8-verified counter). The cat: the committed
  cat.glb's root wrap flipped to -90 deg Y (the +90 guess in (117) made it
  run backwards - owner caught it with two pads); AABB z-range flip
  verified headlessly, in-game the camera now sees its back. Docs:
  multiplayer.md Performance section, example README, engine skill
  (no-clear contract + splitSecondPass).
- (117) **examples/two-players + oversized-glb-texture clamp.** The committed
  demo for (116): a 14k-vertex sample humanoid (P1) vs a cat (P2) in a box arena, title menu
  picks 1P/2P (Player-count option block), pause menu switches mid-game,
  split-screen third-person cameras per player, Start-on-pad-2 hot-join.
  Two authoring finds baked into the pipeline/docs: **(a)** the humanoid's
  1024x1024 embedded texture hit the engine's hard `TYRA_ASSERT` (512 max)
  and quiet-halted the game on load - glbparser's image extraction now
  box-downscales oversized embedded textures to <=512 (power-of-two factor,
  POT sources stay POT) with a build warning, so any Blender-textured
  avatar Just Works; **(b)** the owner's cat.glb (an FBX re-export) parsed
  fine but was authored X-forward, which the avatar drive (faceYaw expects
  Z-forward) would render as a crab-walk - fixed in the committed asset by
  wrapping the glb scene root in a +90deg-Y rotation node (BIN untouched;
  the AABB flip confirmed the axis swap headlessly before any boot).
  Verified: Docker build clean; PCSX2 e2e drives the title menu from the
  keyboard - 1P full screen, then 2 Players + START = live top/bottom split
  with both avatars standing and animating (F8 screenshots; earlier
  frames caught P1 visible in P2's half). PCSX2 launches were flaky
  post-reboot (Vulkan swapchain / parallel-session clobbering) - the
  driver script now relaunches and re-verifies per pass.
- (116) **Two-player games: shared screen + split screen, runtime join/leave.**
  `ProjectSettings::multiplayer` ("off"/"shared"/"split", *Preferences >
  Multiplayer*) + `p2JoinOnStart`; player 2 is the scene's **second Player
  object** (scene order picks the slots; the Player properties panel says
  which is which). The single-player walker state (`entX/entYaw/...`,
  `camBoom`, clip indices) is hoisted into a per-player `PlayerCtl` struct
  and the walker is parameterized (`updatePlayerWalker(PlayerCtl&, pi,
  Tyra::Pad&)`) - all three modes (walk/noclip/third person) work per
  player, with per-player tuning from new `PLAYER2_*` scene tables +
  `PP_*(pi)` selection macros in scene_data.hpp. **Shared screen**: one
  camera orbits the pair's midpoint (P1's right stick), boom stretched by
  separation, spring-armed like the solo boom. **Split screen**: new engine
  `RendererCoreSplitView` (env-map-style raster bracket: PATH1 drain +
  XYOFFSET shift + SCISSOR + per-half color/z clear - a vertical *crop* of
  the unchanged full-screen projection, so proportions are exact), camera
  swapped between halves via `renderer3D.update()` (frustum follows per
  mesh). Engine `Pad` gains `initOptional(port)` - padInit is once-global,
  a missing controller no longer blocks/asserts (upstream `update()`
  busy-waited forever), and it keeps polling: **hot-join**. Runtime switch
  both ways: Start on pad 2 joins; a new **Player count (1P/2P)** menu
  option block (bind 7, edge-triggered + write-back so the row and pad-2
  joins never fight) toggles anytime; scene switches keep P2 while the new
  scene has a second Player. Cutscene overrides suspend the split; the env
  map pauses refresh during split halves (its bracket restores a full-screen
  raster); HUD/menus/post-fx stay full-screen (documented v1 limits in
  `docs/multiplayer.md`). ScriptContext gains `player2Active/player2Position`
  (the "nearest player" seam for the NavMesh PR). Verified: editor builds
  clean; headless harness (scratch project with two Players, split mode, the
  menu block) round-trips save/load incl. the new fields and
  `refreshGenerated` emits the PLAYER2 tables / split render path / bind 7
  row; full Docker build (engine + game) compiles; PCSX2 boots and the
  keyboard-driven pad-1 menu toggle flips full-screen 1P <-> top/bottom
  split with both cameras live (F8 screenshots). Pad-2 hot-join and shared
  mode's feel still want a hands-on two-controller test. Docs: README,
  `docs/multiplayer.md`, editor + engine + testing skills.
  *(While here: PROGRESS.md carried a committed, unresolved merge conflict
  from c96caaa - markers removed; the two (104)-(107) runs below came from
  parallel branches, both kept as written.)*
- (127) **Portals: particle emitters show through (VU1 billboard re-render).**
  Merged main's particle-billboard-VU1 work (their (117)/PR #118: the EE
  now submits particle CENTERS and a VU1 `billboard` program expands each
  into a camera-facing quad from a `right`/`up` basis on `StaPipBillboardBag`)
  and used exactly the seam it was designed for — "swap the basis, re-render
  the same centers for another view". Portal through-views previously
  skipped particles (billboards are view-dependent). Now `renderOnePortalView`
  computes the virtual camera's right/up basis and `renderViewObject` handles
  emitters (type 7): for each emitter reached (listed, or any with All
  objects in view), it swaps the bag's basis to the virtual one, renders the
  same live centers, and restores the saved basis immediately so the frame's
  final main-pass particle render is untouched. Rain (kind 4) keeps world-up
  like the main pass. Centers are the sim's own arrays (no copy) and the VU1
  program does the second expansion, so the added cost is one extra on-VU
  expansion per visible emitter per live view — no EE vertex work. **Verified
  (Layer 3, PCSX2 D3D11 HW):** a fire emitter placed at the tower base in
  examples/portals renders correctly INSIDE portal-a's opening, camera-facing
  for the virtual camera and animating across frames, at a locked 50 FPS /
  100% speed; docs/portals.md's "particles don't show through" limitation is
  gone. Post-merge with main also re-verified (editor + Docker game build
  clean, boots). Real-PS2 pass pending like every GS-level change.

- (126) **Portals: doorways open in collision — walk through the mounting
  wall.** The (125) wall-mounted pair looked right but could not be
  entered: the wall's box collision blocked the walker before the
  crossing plane. General rule, per the owner's "epic collisions" ask:
  `updatePortalPass` (called by all three walkers right before
  collidePlayer, cleared right after) publishes the plane of the linked
  portal whose opening the body column currently sits in (feet + waist
  probes, rect +0.25 margin, -0.6..+1.2 around the plane, any
  orientation); collidePlayer then skips objects fully BEHIND that plane
  - the exact same OBB-projection extent as the through-view dead zone.
  Net effect: the mounting wall opens up like a doorway exactly where the
  portal is (it still blocks beside the opening - the zone requires the
  column inside the rectangle), geometry in front of or poking through
  the surface still collides, and an unlinked portal's wall stays solid
  (the zone requires a live target). Physics objects never collide with
  objects, so they need no equivalent. **Verified (Layer 3, PCSX2 D3D11
  HW):** compiles + boots clean on the wall-mounted map, doorway view and
  infinite fall intact at locked 50 FPS; the actual walk-through is
  pad-only - that check stays with the owner.

- (125) **Portals: wall-mounted portals — exact OBB extent in the
  dead-zone test.** Owner mounted both walk-through portals flush on gray
  wall boxes (their "update portal map" commit) and the opening filled
  with the far wall's backside — the object dead-zone test used a crude
  max-axis bounding radius, so a WIDE thin wall (1.98×4.16×1.0) "reached
  through" by half its WIDTH (needed sd < -1.81 to drop; actual sd was
  -0.53). The extent along the exit-plane normal is now the exact OBB
  projection (sum of |dot(normal, object axis)| x half-scale per axis),
  with 0.1 slack so a flush-mounted wall (the quad sits 0.02 in front of
  it) classifies as behind; geometry genuinely poking through the plane
  still renders. Also reconciled examples/portals: the owner's commit
  carried only generated files, so the source objects/manifest were
  reconstructed to match their map (walls behind both portals, scripted
  anchor removed - the demo is pad-driven now, portal-floor terrain view
  on) and everything regenerated consistently. **Verified (Layer 3, PCSX2
  D3D11 HW):** looking at the wall-framed portal-a, the opening shows the
  destination (tower/terrain/sky) with no gray backside anywhere - the
  Portal look proper; 50 FPS / 100% locked.

- (124) **Portals: entry-side arrow in the editor viewport.** Owner
  request: the tinted quad alone didn't say which face is the entrance.
  A new `portalArrow_` line mesh (shaft + 4 head barbs along +Z) draws at
  every portal, rotated with the object but at a fixed 1.2-unit length
  (quad-scale-free, like the camera frustum wedge), tinted the portal
  color brightened toward white. It marks the +Z front - the side that
  shows the through-view and accepts the crossing. **Verified:** editor
  builds clean; GUI opened on examples/portals and zoomed in via
  synthetic wheel input - the arrow reads clearly against the tinted
  surface (screenshot).

- (123) **Portals: exact chunk extents kill the last "gleba", floor
  portals swallow.** Round 3 of hardware feedback. (1) (122)'s dead-zone
  test still let the terrain backside into the ceiling view ("dalej
  pizdeczka... gleba w górnym"): the corner-sampling used a 1-unit slope
  margin, and the demo's flat terrain sits only 0.8 under the exit plane
  — sd = -0.8 never beat the -1.0 cutoff. Lesson recorded: compute, don't
  guess margins. TerrainChunk now carries its exact minY/maxY (filled in
  buildTerrainChunk from the heightmap) and renderTerrain does a precise
  AABB-vs-plane p-vertex test with a 0.05 epsilon - the flat-map chunks
  drop at any portal height. (2) Owner's own suggestion implemented: a
  body touching a linked FLOOR portal stops colliding with the terrain
  ("może w momencie, jak obiekt dotyka portalu, na ten czas nie koliduje
  z terenem?") - `portalSwallowZone` (floor portals only, front normal
  up, rectangle footprint, -0.6..+2.0 around the plane) suppresses the
  ground clamp in updateObjectPhysics (floorY = -inf) and in all three
  walkers (ground = -inf; feet AND waist probed so the clamp cannot snap
  the body back mid-straddle). A portal lying ON the ground now swallows
  the cube (and the player - you drop in like a pit); the demo's floor
  portal moved from 0.8 down to 0.3 to prove it. **Verified (Layer 3,
  PCSX2 D3D11 HW):** screenshots catch the cube mid-sink INTO the
  ground-level portal (center below its old rest height - the clamp is
  off) and back at the ceiling next shot - the loop closes through a
  ground portal; both column portals run terrain+sky ON with no backside
  anywhere; 50 FPS / 100% locked. Pad checks (walking into a ground
  portal, hardware feel) stay with the owner.

- (122) **Portals: terrain joins the dead-zone test — floor/ceiling pairs
  keep their sky.** Follow-up to (121)'s "turn the terrain off" caveat,
  which the owner rightly disliked ("fajnie, jakby w portalach było widać
  teren"): renderTerrain now honors the through-view's exit plane too.
  renderOnePortalView publishes the target plane in `portalExitPlane[4]`
  (+ flag) around the destination render, and renderTerrain drops chunks
  whose rect corners + center (at their heightmap heights, 1-unit slope
  margin) all sit on the virtual camera's side — the same "invisible
  through a real hole" rule the view objects use. A floor→ceiling pair
  now keeps **Terrain + sky in view** ON: the opening shows the sky-dome
  gradient and the falling cube instead of the terrain's backside; a
  chunk straddling the plane still renders whole (cliff-edge caveat in
  docs/portals.md). Demo ceiling portal flipped back to terrain+sky on.
  **Verified (Layer 3, PCSX2 D3D11 HW):** with terrain enabled on both
  column portals, the floor portal's surface shows sky + the cube
  mid-fall inside it, no ground backside anywhere, walk-through/infinite
  fall/four views intact at locked 50 FPS.

- (121) **Portals: hardware-feedback round 2 — the doorway moment + the
  dead zone.** Two more owner reports from the pad. (1) "Skok widać, gdy
  się jest ryjem dokładnie w centrum portalu, jakby się przez dwa naraz
  patrzyło": with the eye closer to the plane than the near distance, the
  quad's frustum-clipped fan shrinks and the world behind the
  free-standing surface peeks around the opening for a frame or two. Fix:
  a **crossing zone** — eye within ~near·2+0.45 of the plane, inside the
  rectangle (+0.3 margin), looking INTO the surface → the carve becomes
  the WHOLE screen at the nearest depth, so the destination fills the
  view until the hop lands (renderOnePortalView short-circuits the clip
  path). (2) "Górny portal ma teksturę ziemi i nie widać jak kostka
  wpada": the floor↔ceiling pair's isometry puts the ceiling view's
  virtual camera ~5 units UNDERGROUND looking up, and with no oblique
  near plane the terrain between the camera and the exit plane renders
  (double-sided) and occludes everything — the "ground texture". Fix: a
  general **dead-zone test** — view objects entirely on the camera side
  of the exit plane are skipped (through a real hole they are invisible;
  bounding radius like the env-map self-skip) — plus the demo ceiling
  portal's terrain toggle turned off (terrain has no per-chunk plane
  test; documented in docs/portals.md). **Verified (Layer 3, PCSX2 D3D11
  HW):** the ceiling surface no longer shows terrain backside and the
  cube drops out of it cleanly; walk-through + infinite fall + four live
  views intact at locked 50 FPS. The doorway-zone carve compiles into the
  demo but only a pad walk-through exercises it — that check (and whether
  the hardware pop is gone) stays with the owner.

- (120) **Portals: owner-feedback round — multi-view, jump-in, seamless
  hop.** Three fixes from playing the demo on hardware: (1) **up to four
  portal views per frame** instead of one (nearest qualify; carved
  FARTHEST-first so overlapping openings resolve like occlusion would —
  `portalMaskBegin` gained a bbox z-clear so an earlier portal's z-cap
  can't reject a later view's geometry; NLOOP trap re-paid: the new
  begin-packet giftag said 8 with 7 register writes and the GIF wedged
  exactly as the engine skill warns — FPS: N/A, frozen frame; count the
  qwords). The infinite-fall pair now runs viewAll, so standing under the
  ceiling portal you SEE the cube approaching inside it instead of it
  "spawning" at the surface. (2) **Feet probe**: the player crossing test
  runs a second segment at the feet - jumping/dropping into a floor portal
  teleports (the waist probe alone never dipped below a knee-height
  plane; "I can't jump into it"). (3) **No exit offset**: the +0.2 arrival
  nudge read as a one-frame camera pop at the crossing moment on hardware
  (owner: "ekran delikatnie skacze") - removed; the pair transform is an
  isometry, the crossing overshoot maps to the same overshoot past the
  target plane, so the hop is now mathematically continuous (the reverse
  link can't re-trigger anyway - the arrival moves away from the plane).
  The player velocity mapping also carries the actual per-frame motion
  (same ground-clamp race the objects had). **Verified (Layer 3, PCSX2
  D3D11 HW):** four live views at once (the floor portal's sky-view
  visible beside the walk-through pair), demo loop + walk-through intact,
  locked 50 FPS / 100% speed; manual jump-in and the hardware
  no-pop check want the pad test.

- (119) **Portals: experimental "All objects in view".** Owner request: a
  per-portal switch (`portalViewAll`, Properties > Portal) that renders
  EVERY scene object in the through-view instead of the explicit list
  (ignored while on). Runtime: the viewAll branch walks all runtime
  objects — the pushed frustum planes classify each bag against the
  VIRTUAL camera (off-view geometry drops EE-side before packaging) and
  `beyondDrawDistance` measures from the virtual eye, so the practical
  cost is what the destination actually sees; mirrors are skipped (glass
  only — their copies are a main-pass trick) and portals stay excluded
  (no recursion). Documented squarely as experimental: big scenes pay a
  second submission pass while the portal's view is live, particles still
  don't show through, the authored list stays the shipping default. Full
  chain: field + serialization (`viewAll` in the portal block) + recipe
  hash, UI checkbox that gates the list UI with a cost warning, a
  `viewAll` column in PortalData. examples/portals flipped portal-a to
  viewAll with an EMPTY list (portal-b keeps the classic list) — the demo
  proves both modes. **Verified (Layer 3, PCSX2 D3D11 HW — Vulkan
  presentation still wedged):** the tower shows through portal-a with
  nothing listed, the walk-through and infinite-fall demos unchanged,
  locked 50 FPS / 100% speed.

- (118) **examples/portals — the Portal object demo.** A committed example
  for (117): a two-way pair across the map (portal-a in front of the FPP
  spawn ↔ portal-b by a red landmark tower 25 units away), an Empty with a
  small flow graph (On Start → Delay 6 s → Spawn Player At) that walks the
  player through the surface unattended, and the classic **infinite fall**:
  a floor portal on the ground linked up to a downward-facing ceiling
  portal, with a physics cube endlessly dropping through the pair in plain
  view of the spawn (its fall speed carries through every hop by the portal
  velocity mapping). **Verified in PCSX2 (locked 50 FPS / 100%, EE ~33%):**
  the through-view shows the tower at full resolution with correct parallax
  and no visible boundary (screenshotted); the scripted crossing lands the
  player exactly where the view promised (post-teleport screenshot: same
  tower, close up, level camera, correct yaw); timed screenshots caught the
  cube at different column heights — including above its own spawn height,
  proving it had already looped — and the loop was still running at
  t=45 s; no TYRA banners in `bin/log.txt`; editor GUI opens the project
  (viewport + object list screenshot). Two physics fixes fell out of
  watching the loop: object physics gained a **50 u/s terminal velocity**
  (updateObjectPhysics — without it a portal infinite-fall accelerates
  until the cube clears the whole column in one frame and the smooth loop
  turns into blinking; also era-authentic), and the crossing test now
  carries the object's **actual per-frame motion, not just `velocityY`**:
  on the very frame a cube crossed a near-ground floor portal, the physics
  ground clamp could zero `velocityY` *before* the portal test ran, so the
  cube arrived at the far end with v=0 and visibly hung before re-falling
  (the owner spotted the hitch); the position delta still holds the real
  fall, so the larger of the two maps through the pair.

- (117) **Portal objects — a linked pair of surfaces with a live
  through-view and a seamless walk-through teleport.**
  `PrimitiveType::Portal` (16): a rectangle (decal quad, +Z = front) that
  names another Portal in the scene as its target (one-way by design; a
  "Link back" button makes pairs two-way). Rendering is a real second view,
  budgeted the PS2 way: each frame the game picks ONE portal (nearest
  linked one the camera is in front of) and renders sky + terrain
  (per-portal toggle) + an explicit view-object list (the Mirror
  philosophy) **in-place, at full resolution, straight into the
  framebuffer** — right after the frame clear, before any main-scene 3D,
  scissored to the quad's screen bbox. The GS has no stencil, so the
  shaped opening is carved with reversed-z ops
  (`RendererCore::portalViewBegin/End` → `RendererCorePostFx::portalMask*`,
  both draining PATH1 without latching the post-fx drain gate): re-far the
  bbox depths, cap the quad interior with a z-only ALWAYS triangle fan at
  the surface depth (the 4 corners frustum-clipped on the EE,
  Sutherland–Hodgman, ≤9 verts — walls in front still occlude the view,
  the wall behind loses, DoF/particles see a solid surface), then repaint
  the spilled ring outside the opening via a GEQUAL sprite at z=0 that
  hits exactly the pixels the reset left at far. The virtual camera is the
  player camera mapped through the pair (source local frame → 180° flip
  about local Y → target frame; VU0-macro Vec4/M4x4 math, geometry through
  the normal VU1 static pipeline) with the SAME projection as the screen —
  only the view matrix swaps (`RendererCore3D::pushPortalView`) — so the
  destination lands exactly where the opening sits: correct parallax, no
  per-pixel work, and the opening is pixel-for-pixel as crisp as the scene
  around it. (Dead end recorded: v1 rendered the view into a second
  128×128 env-map-style VRAM target and projected it onto the quad with a
  screen-locked-UV textured fan — it worked, but the bilinear upscale read
  as a visibly soft "window" against the crisp scene, the exact seam the
  in-place render eliminates; the RTT variant also cost +64 KB VRAM.)
  Every other portal (and unlinked ones) draws as a tinted translucent
  quad (`rebuildObjectGeometry` case 16, skipped in the main loop like
  mirrors, blended after them in `renderPortals` — the live portal skips
  its tint so nothing washes the opening). The **teleport** (`updatePortals`,
  called from both loop flavors after the physics step) probes the walker's
  waist segment against the front face each frame and maps position, view
  yaw/pitch and vertical velocity through the same transform the camera
  uses — what the surface showed is exactly where you arrive; the frame
  camera is rebuilt on the hop so no frame renders from the departure side.
  Physics objects cross too (per-portal switch; per-object prev-pos table,
  `velocityY` mapped, `dirty` set). Full chain: model + `.tyra`
  serialization (`portal` block) + live-link recipe/unspawnable rules,
  Insert > Gameplay > Portal, Properties block (target picker with
  two-way link button, view-object list, terrain/objects toggles), rename
  remap, viewport preview (translucent tinted quad + link line to the
  target via a new unit-segment mesh), PORTALS/PORTAL_VIEW_OBJECTS side
  tables in `scene_data.hpp` (name → index resolution at codegen), docs
  (`docs/portals.md`, README, live-link list, engine/editor skills).
  Limits by design, documented: one live view per frame,
  no portal-in-portal recursion, tilted pairs carry only vertical velocity
  (walkers keep no horizontal velocity state), view-listed animated models
  show their last skinned pose. **Verified (Layer 3):** editor + engine
  compile clean; `--resave` round-trips the portal block; generated tables
  correct for a 2-portal scene; in PCSX2 at a locked 50 FPS / 100% speed
  (EE ~33%): live through-view with correct parallax and NO visible seam
  (the opening is indistinguishable from the surrounding scene — only the
  destination landmark gives it away), scripted walk-through arriving
  exactly where the view promised with the camera rebuilt, and a physics
  cube teleporting through a flat (floor) portal — the tilted-pair
  rotation path — all screenshotted; no TYRA banners in `bin/log.txt`.
  Honesty note on renderers: the RTT rounds ran on the SW renderer; the
  final in-place build was verified on D3D11 HW because the Vulkan
  presentation layer wedged mid-session (the known rapid-relaunch
  swapchain failure) — an SW-renderer pass on the final build plus the
  hands-on pad test (walking through manually, strafing past the surface
  edge, portals partially off-screen) still want a human; real-hardware
  pass pending like every GS-level change. Also fixed in passing: a
  committed merge-conflict marker pair left in PROGRESS.md by an earlier
  merge.

- (104) **Gamepad vibration (DualShock rumble) — flow node + scripts.** The
  engine fork's `Pad` gains `setActuators(smallMotor, bigPower)` (`Modified by
  TyraX` in pad.hpp/pad.cpp): act-direct control of the two DualShock motors —
  the on/off buzz engine and the 0–255 heavy motor — using the actuator slots
  `initPad()` was already aligning (it logged "# of actuators: 2" and then
  never drove them). On top of that: `ScriptContext` carries a rumble request
  (`rumble` −1 = leave / 0–255 big-motor power, `rumbleSmall`, `rumbleSec`)
  plus a `padVibrate(ctx, big01, small, seconds)` helper in the generated
  `script.hpp`; both game loops (orbit + FPP) apply the request and run a
  `g_rumbleTimer` auto-stop countdown (Seconds > 0 — it ticks even while a
  menu pauses the scripts, so a timed rumble always ends; Seconds 0 = vibrate
  until the next request). A **Vibrate Pad** flow node (Player category; Big
  0..1 slider, Small checkbox, Seconds; defaults big=1, 0.5 s) compiles to the
  same request in `actionCode()`. **Verified:** editor builds clean; a scratch
  FPP project with an injected `On Start → Vibrate Pad` graph emits
  `ctx.rumble = 204 / ctx.rumbleSmall = 1 / ctx.rumbleSec = 1.5F` in
  `flow_graph.gen.cpp`; engine + game compile in Docker and boot in PCSX2
  (pad init logs "# of actuators: 2", the game runs at 50 FPS through the
  on-start rumble and its 1.5 s auto-stop — no hang, no assert). The actual
  rumble *feel* needs a physical controller (PCSX2 forwards vibration to the
  host pad); that hands-on check stays with a human.

- (117) **Particle billboards move from the EE to a VU1 program family.**
  `updateParticles()` used to both simulate AND expand every particle into a
  camera-facing quad on the EE — 6 verts + 6 colors (+ 6 STs) written per
  particle per frame, then pushed through the stock StaPip path. Now the EE
  keeps only the simulation: each particle is submitted as ONE center vertex
  (the sim's own `pos` array, no copy), one qword of 2×2 basis weights
  `(m00,m01,m10,m11)` riding the ST channel, and one color — and a new
  StaPip **billboard** VU1 program family (`billboard/stapip_billboard_{c,t}`,
  94/106 instructions) expands each center into 2 triangles in clip space:
  the camera right/up basis (uploaded per mesh at `VU1_BILLBOARD_BASIS_ADDR`,
  carried on the new `StaPipBillboardBag`) is transformed by the MVP once per
  mesh, corners are `C ± (R·m00+U·m01) ± (R·m10+U·m11)` — so rain's world-up
  streaks (independent half-height) and the fog swirl (per-particle 2D
  rotation) fold into the same four weights, perspective-exact. Culling is
  per QUAD on VU1 (one `clipw` judgement per corner against the GS raster
  window / depth range; any corner out → ADC on all 6 emitted verts), which
  makes `frustumCulling = None` safe for these bags — the one legitimate
  use, the program itself is the wrap protection. Micro memory was the
  design constraint: the VU1-clipping program set measures **2036/~2042**
  instructions (nm over the .o files), so the two billboard programs are NOT
  resident — they live in their own prebuilt packet swapped in on demand
  (`StaPipQBufferRenderer::ensureProgramSet`, the same MPG upload a
  StaPip↔DynPip switch already does) and the resident set is lazily restored
  by the next non-billboard bag. The prim giftag NLOOP is built EE-side at
  6× the input count (`gsVertexCount` override) — the GIF-stall trap from
  the portal work, dodged by construction. The texture bag is now mandatory
  for particle bags (it carries the params channel) with a nullable image:
  a real map selects the textured program (corner UVs are constants in the
  microcode), no map the untextured one. Verified in PCSX2 (SW renderer,
  debug + Show FPS + profiler + vsync off, scratch scene with fire 200 /
  smoke 200 / fog 120 / sparks 200 / rain 256 / custom fountain 256 = 1232
  particles): **A (EE quads): FPS 137–139, FRAME ~7.03 ms, PART ~1.10 ms;
  B (VU1 centers): FPS 192–214, FRAME ~5.05 ms, PART ~0.31 ms** — the
  particle phase's EE cost drops ~72% and the whole frame gains ~2 ms; all
  six kinds render correctly (vertical rain streaks, swirling fog, fire
  ramp). Designed for the portal branch to pick up: swapping
  `billboardBag->right/up` and re-rendering the same bags draws the same
  centers for a virtual camera, which is exactly what portal through-views
  need (today they skip particles entirely). Real-PS2 pass pending.

- (116) **NavMesh + NPC AI — Patrol / Chase / Flee / On Player Seen.** NPCs
  can finally go somewhere on their own. Three layers, all
  pay-for-what-you-use (a project without AI nodes carries zero nav data or
  code): (1) a **host-side bake** (`src/navmesh.cpp`, shared by codegen and
  the editor) rasterizes each scene into a walkable-cell bitmap — terrain
  slope from the same bilinear heightmap the game samples, blockers
  mirroring `collidePlayer`'s box mode (AABB, step-onto/walk-under rules,
  mesh-collision objects never block — they're ramps) inflated by an agent
  radius; grid capped at 128×128 so the PS2 arrays stay static. Tunables in
  *Preferences > AI navigation* (`navCellSize`/`navMaxSlope`/
  `navAgentRadius`), live preview via *View > Nav mesh overlay* (green
  quads, signature-cached recompute like the projected decals). (2) A
  **generated runtime** (`nav_data.gen.hpp` + `navigation.gen.cpp`): A* on
  the EE (8-connected, no corner cutting, octile heuristic, expansion cap,
  **one pathfind per frame round-robin**, unreachable goals path to the
  closest reachable cell), string-pulled paths, one agent state per runtime
  object (spawn-pool clones included), terrain snapping + shortest-arc
  turn-to-face (the avatar's convention, so walk clips line up). (3) Five
  **flow nodes** (category "AI"): Patrol Waypoints (waypoints = objects
  named `<prefix>1..n`, resolved at codegen with natural sort), Chase
  Player (stop distance, give-up), Flee From Player (sideways fan-out when
  the straight-away is blocked), Stop AI Movement, and the On Player Seen
  trigger (range + FOV cone around facing + optional terrain LOS;
  edge-fired exec like Near Object, plus a bool output for the gates). Two
  design traps hit and fixed during verification: the tick script's
  scene-generation reset ran *after* an On Start command in the same frame
  and wiped it (fix: lazy shared `navSyncGeneration` called from both the
  commands and the tick), and the patrol arrival radius was tighter than
  the grid raster (a path ends at a cell *center*, a waypoint can sit on a
  cell *corner* — 0.75 cells deadlocked; now 1.1× cell). Verified on PCSX2
  (layer 3) with position telemetry logged from the graph itself
  (`Every N Seconds → Get Position → Position To Text → Log`): patrol
  cycles wp1→wp2→wp3 with a clean **A\* detour around a wall** (passes at
  the obstacle's inflated edge), On Player Seen → Chase closes in and holds
  at exactly Stop Dist, Flee runs to exactly Safe Dist then idles; steady
  50 FPS. Stop AI compile-verified only (trivial `mode = 0`); LOS terrain
  march and the editor overlay rendering still want a hands-on eyeball
  pass. New example `examples/nav-ai` (guard + rabbit, boots clean) and
  `docs/navigation-ai.md`; `examples/script-demo` regenerated (picks up the
  new gen-file stubs + stale id hashes from before this change).
  Follow-up fix (owner repro on the example: "the guard patrols fine but
  never chases me"): `navPlayerPos` read the Player OBJECT's position, which
  the game syncs to the live player **only in third-person mode** — in FPP
  walk/noclip the object keeps its authored spawn position forever, so every
  NPC watched the spawn point while the real player walked free (the
  original PCSX2 chase pass couldn't catch it: with no pad input the player
  never left spawn, so stale == live). Now the object position is used only
  when `PLAYER_MODES[scene] == 2`; otherwise the camera is the player (eye
  minus a nominal 1.5 to approximate the feet). Re-verified padlessly with a
  teleport repro: `On Start → Delay → Spawn Player At` a marker on the
  patrol route — before the teleport the guard patrols past the (stale)
  spawn without reacting under the old code; with the fix the post-teleport
  live position enters the cone and Chase fires. Also narrowed the AI-node
  hint lines in the graph UI (they were wider than the field rows and
  stretched the nodes out of alignment).

- (113) **Font Manager + the Display Text node (runtime text), on top of a new
  multi-exec-pin primitive that merged six node types away.** Three layers, one
  feature.

  **(1) Multi-exec pins.** `FlowLink` grew a `toPin` (serialized as `"pin": N`,
  omitted at 0) and `FlowNodeType` an `execInCount` + `execInLabels`, so one
  action can expose several labeled exec inputs instead of the codebase's old
  convention of a *pair of node types* per show/hide. Pin ids needed no
  widening: slot 2 stays the primary exec-in and the spare slots 10..15 hold the
  rest (`flowExecInPin`/`flowExecInIndex`). Codegen threads the pin into
  `actionCode(n, pad, pin)`, and `emitExec`'s cycle guard is now keyed on
  **(node, pin)** — one trigger legitimately driving two branches of the same
  node is not a cycle. Six types retired into five merged ones:
  `Show/Hide/Toggle Object` → **Set Object Visible** (show/hide/toggle),
  `Show/Hide/Toggle HUD` → **Set HUD Visible**, `Show/Hide Text` → **Set Text
  Visible**, `Load/Unload Layer` → **Set Layer Loaded**, `Play/Stop Animation` →
  **Animation**. `Play/Stop Music` and `Play/Stop Sequence` were deliberately
  *not* merged: their Stop is global (no param), so a "stop" pin would visually
  imply it stops the track named in the field next to it, which it does not.
  `readFlowGraph` migrates pre-merge graphs (`flowLegacyNodes`): it rewrites the
  node type and retargets every exec link landing on it to the branch's pin, so
  old projects keep their logic instead of silently losing the nodes (unknown
  types are dropped on load).

  **(2) Font Manager** (*Tools > Font Manager*, `Project::fonts`). Fonts are now
  first-class named entries; `HudText`/`GameMenu` reference one **by name**
  instead of each carrying a raw TTF path (`migrateFontRefs` folds each distinct
  legacy path into an entry on load). `fonts[0]` is the fallback every unset
  reference resolves to and cannot be deleted; a stale name falls back to it
  rather than failing a bake. Also fixed a real leak found on the way: imported
  `res/fonts/*.ttf` were being mirrored into `.res-baked/` and swept onto the
  ISO, despite nothing on the PS2 ever reading a TTF (texbake's `editorOnly`
  now excludes them — the tooltip claiming "nothing ships but pixels" was
  aspirational).

  **(3) Display Text** — the actual ask: a node whose string is a *runtime*
  value, so it cannot be a pre-baked sprite like every other text here. Fonts
  it uses bake a **glyph atlas** (`menubake::atlasLayout`/`bakeAtlasPNG` →
  `res/fonts/atlas-<name>.png` + metrics in `inc/font_data.gen.hpp` from the
  same layout call, so pixels and metrics cannot drift); the runtime blits cell
  by cell (`drawFontText`), the trick the engine's debug font already used.
  Glyphs bake **white** and are tinted per-font at runtime, so one atlas serves
  any color and the drop shadow is just a second dark pass. One slot per node
  (`dynTextSlots`, walked identically by the header and the script); the wired
  text is re-read every frame *only while the slot is on*.

  **VRAM.** The premise of the request ("fonts shouldn't sit in VRAM all the
  time") turned out to be **already true, and the real risk is the opposite**:
  `RendererCoreTexture::useTexture` DMAs a texture to GS on its *first render*,
  so a font nobody displays costs 0 B of VRAM — but once drawn it is **pinned
  forever** (no LRU; the only eviction is an all-or-nothing flush when the next
  texture doesn't fit), and there is only ~1.33 MB of texture VRAM after the
  frame/z buffers, with an 8 KB tax per allocation. So: the atlas is added to
  the repository lazily on first draw and `useTexture()` is deliberately never
  called eagerly (unlike the streamed model textures), and atlases default to
  **4-bit** (white glyphs the runtime tints — 16 levels is plenty, ~8x cheaper).
  Explicit unload-on-hide was considered and rejected: `RendererCoreGSVRam::free`
  is `pointer = address`, a bump-pointer stack pop, so freeing anything that is
  not the newest allocation rewinds past still-live textures. The Font Manager
  shows each atlas's measured VRAM cost rather than hiding this.

  Atlas sheet size picks the smallest area, tie-broken toward square: pow2
  rounding makes 64x512 and 128x256 cost the identical 32k pixels for the
  default font, and the squarer sheet leaves headroom before the 512px cap
  starts dropping glyphs.

  **Verified**: editor builds clean; scratch project → atlas baked (128x256,
  4-bit, 13.6 KB → 4.5 KB in `.res-baked`) and `font_data.gen.hpp` tables match
  the node; generated `flow_graph.gen.cpp` shows *On Start* → the show pin and
  *Every 3 Seconds* → the **hide pin of the same node** (the whole point of the
  primitive), with the refresh guarded by `dynTextOn`; Docker/PS2 compile +
  link clean, and **PCSX2 (software renderer) shows "Score: 0"** centered with
  its shadow at 50 FPS, `bin/log.txt` free of asserts. Migration verified by
  `--resave` on a hand-written pre-merge graph (`HideText` → `SetTextVisible` +
  `"pin": 1`, `ToggleObject` → pin 2, `StopAnimation` → pin 1) and on the three
  affected examples; a project with **no** Display Text (`FONT_COUNT = 0`)
  compiles and links too. Not covered: pad-driven interaction and real PS2
  hardware — both still want a human. *(Numbering: this entry landed on its
  branch as (113) while main was already at (115) — kept as committed.)*

- (115) **examples/video-modes: a `480I FIELD RENDER` menu row.** The
  display-mode test bed gains the fourth scan mode from (114): a new VIDEO
  OPTIONS entry firing a `video-480i-field` flow event, consumed by the
  aspect-ball graph's On Menu Event -> Set Display Mode(Mode 3, confirm
  8 s) - same pattern as the other three rows. README updated (menu table,
  intro, real-hardware notes: field rendering is the same 480i/576i signal,
  so any cable works; judge the motion on a CRT, PCSX2's deinterlacing
  hides most of it). Committed generated files regenerated with a Docker
  build in the same commit (the example-drift rule). **Verified** (Layer
  3): the example boots in PCSX2, the baked menu panel shows the new row
  (F8 snapshot), regenerated `flow_graph.gen.cpp` carries
  `ctx.requestDisplayMode = 3` under the `video-480i-field` event, exit 0.
  Actually selecting the row with a pad (menu navigation + the confirm
  prompt) stays a hands-on test, like the other rows.
- (114) **True field rendering: the `interlaced-field` display mode.**
  Question from the owner: does the engine render once and scan the frame
  out over two fields, or does each field get a fresh image? Finding: the
  stock interlaced mode renders full 512x448 frames into a FIELD-scanned
  (FFMD=0) buffer and `endFrame` waits on `graph_wait_vsync()`, which fires
  per FIELD - so at full speed each field already shows a new frame, but
  every one of those images pays full-height fill/geometry cost and the GS
  scans out only half its lines. The new **InterlacedField** mode
  (`DisplayMode::InterlacedField`, project pref `"displayMode":
  "interlaced-field"`) is the classic retail recipe: half-height 512x224
  frame/z buffers scanned with SMODE2.FFMD=FRAME (every buffer line, every
  field), so the same 50/60 distinct-images-per-second now cost half the
  fill - and the three screen buffers shrink from ~2.6 MB to ~1.3 MB of
  VRAM, roughly doubling what's left for textures. Engine details: the
  DISPLAY window is IDENTICAL to the stock mode (ps2sdk's
  `graph_set_screen` mis-programs DY/DH for the interlaced+FRAME case, so
  the registers are written directly via `setDtvDisplay(652/50 NTSC,
  680/72 PAL, 2560, 448, 5x MAGH, 1x MAGV)`); no flicker filter (nothing
  to blend); the game-facing coordinate space stays 512x448 - the
  projection is built at the render height (raster scale only; the
  world-space frustum comes from fov+aspect, so culling and frustum planes
  are untouched), 2D sprites squeeze y by half in `RendererCore2D`
  (upstream's own commented-out "interlacing" scaffolding, finally lit
  up), and clears/post-fx/env-map restores use the new
  `RendererSettings::getRenderHeightF()`. Per-field half-line alignment:
  `flipBuffers` reads CSR.FIELD after the vsync, flips it (the frame being
  rendered shows one field LATER) and appends an XYOFFSET write (+8 = 0.5
  px on odd fields) to the flip packet - without it static geometry bobs a
  full scan line at 25/30 Hz. Editor chain: 4th value in the Preferences
  combo + tooltip, `SetDisplayMode` flow node Mode 3, menu option block
  `480i FIELD` (bind idx = enum value), codegen `{{DISPLAY_MODE}}` ->
  `InterlacedField`; enum values are serialized, appended only.
  **Verified** (Layer 3, PAL BIOS): fresh `--new` scaffold with
  `interlaced-field` boots in PCSX2, terrain scene shows correct 4:3
  proportions from the 512x224 buffer (F8 snapshot vs stock-interlaced
  rebuild of the same project - same framing, slightly softer static
  edges, as expected); debug HUD "FPS 49" text sprite renders at the right
  position/aspect through the 2D squeeze at PAL field rate; no TYRA
  asserts in `bin/log.txt`, ELF confirmed in emulog. Pending a human pass:
  runtime switches into/out of field mode (same `setDisplayOutput` reinit
  path the video-modes example exercises for 480p/1080i), NTSC region, the
  field-phase sign on a real CRT/PS2, and real-hardware A/B like every
  display change.
- (113) **Build break: empty-scene placeholder row in `scene_data.hpp`
  lost a field.** The reflective-models change added `reflected` to
  `SceneObjectData` and to the per-object row emitter but missed the
  placeholder row emitted for scenes with zero objects (it keeps the array
  non-zero-sized) - so `--new` + `--build` of a FRESH project failed in
  `scene_data.hpp` ("invalid conversion from 'const char*' to 'int'" at
  the animClip column) while every populated example kept building, which
  is why it slipped through. One `0` in the reflected slot restores
  alignment; the row now carries a comment anchoring it to the struct.
  Also removed the `<<<<<<<`/`=======`/`>>>>>>>` conflict markers that the
  #89 merge had committed into this very file (both hunks were distinct
  entry sets from parallel branches - the union is the correct log, so
  only the marker lines went; the historical duplicate entry NUMBERS from
  parallel branches stay as they are). **Verified**: Layer 3 - the fresh
  scaffold compiles and boots in PCSX2 again.
- (112) **Rounded reflection normals - flat surfaces stop reflecting "one
  pixel".** Owner's observation on the console: the mirror monolith showed
  a single uniform patch of the env map per face while the spheres "reflect
  like RTX ON" - inherent to matcap math (UV comes from the normal; a flat
  face has ONE normal -> one sample stretched across it). New per-material
  **Rounded normals** checkbox (Material Editor > Reflection; stored as the
  TyraX `-rounded` flag in the `refl` statement, placed before the filename
  so last-token parsers stay compatible): the env pass swaps the captured
  face normals for directions radiating from the part centroid
  (`normalize(vertex - centroid)`, recomputed at geometry rebuild), so every
  corner of a flat face gets a different UV and the face sweeps a gradient
  of the map that pans with the camera - the curved-lacquer look. Spheres
  are unchanged by construction (their true normals are already radial);
  lighting/geometry untouched; zero runtime cost (different data in the env
  ST slot). Full chain: both .mtl parsers (LeanObjLoader + objparser),
  MatEd UI + writer + import rewrite (option tokens before the filename
  already survive), viewport GLSL twin (`uReflRounded`/`uReflCenter`,
  world-space part centroid), codegen rebuild override. The showcase's
  `chrome-dyn` material flipped to rounded (the monolith demos it; the
  chrome-live spheres share the material - no visual change for them).
  **Verified** (Layer 3, PCSX2 SW renderer): the monolith face sweeps the
  sunset gradient AND shows the reflected prop instead of one flat patch;
  spheres identical; no TYRA banners; editor GUI opens the showcase and the
  viewport twin shows the same gradient on the monolith (GUI screenshot).
- (111) **Real-hardware follow-ups: the 2D ALPHA leak (vanishing HUD font
  outline) + GT3-cadence env map (95 -> 107 FPS).** The owner's console run
  of the reflections showcase surfaced two things. (1) The debug HUD font
  lost its black outline on reflective frames: the in-band per-mesh ALPHA
  (105) leaves the GS `ALPHA` register holding whatever the LAST 3D mesh
  set - after an additive env pass everything drawn through the 2D sprite
  path (which never touched ALPHA, it inherited state) blended additively,
  and black texels add nothing. `RendererCore2D::render` now pins the
  standard source-alpha equation in every sprite packet - in-band on
  PATH3, no syncs. (2) The FPS dip when large reflective spheres cross the
  screen edges (SCENE 18.35 ms on hardware vs ~8 in PCSX2 - the emulator
  undercounts the clip programs' per-triangle cost) is trimmed with the
  other GT3 trick: the dynamic env map now re-renders every SECOND frame
  (`envMapTick` in renderScene; the VRAM target persists between hits, and
  a 25/30 Hz refresh of a blurry 128px reflection is imperceptible - the
  first frame always renders, fresh VRAM). **Verified** (Layer 3, PCSX2 SW
  renderer, debug + vsync off): bench envmap phase 1.6 -> 0.7 ms avg,
  frame 9.75 ms (95 -> 107 FPS); the HUD font keeps its dark outline over
  the bright sunset sky (the exact condition that exposed the leak on
  hardware); dynamic spheres reflect the current sky phase with no visible
  update lag. The edge-crossing clip cost itself is the remaining lever on
  that spot - authoring-side (hero-sphere detail) or a future LOD; noted,
  not attempted here.
- (110) **Reflections-map perf: 46 -> 95 FPS by putting cull_tce back in the
  VU1-clipping program set.** The user's report (an average map hits ~100
  FPS with vsync off, the reflections showcase ~25) profiled to the env
  second pass: an owned-copy COP0 breakdown of renderScene on the showcase
  (debug, SW renderer, vsync off) read envmap 1.7 / dome 0.13 / terrain
  1.43 / objects 16.5 ms - of which the reflective env pass was **14.2
  ms**, ~6x its own base geometry (2.3 ms). Cause: (109)'s micro-memory
  compromise force-routed every env-bag package through clip_tce at 1/5
  occupancy with per-subpackage copies, and the EE then waited on the much
  heavier per-triangle clip program for geometry that was 95% fully
  in-frustum (A/B: the same scene on the EE clipper ran the env pass in
  2.75 ms through cull_tce). Fix, two parts: (a) all five clip programs now
  share ONE fan-emitter instance in a rotating 3-iteration loop
  (`fanEmitLoop`; the corner pointer walks srcBase -> fanPtr -> fanNext)
  instead of three inlined emit copies - frees 116 instructions; (b)
  upstream's `createProgramsCache` padded every program with "+1"
  micro-memory word although `getProgramSize()` is already even-rounded
  (MPG uploads in 64-bit pairs) - packing back to back frees 10 more. Both
  together fit the full 10-program vu1 set (2036 <= the 2042 ceiling; the
  overflow assert fired correctly at 2046 during bring-up), so env bags now
  route like any textured bag: in-frustum -> cull_tce, crossing ->
  clip_tce, and StaPipCore's forced-clip branch is deleted. **Verified**
  (Layer 3, PCSX2 SW renderer, debug + vsync off): showcase env pass 14.2
  -> 2.56 ms, whole frame 21.2 -> 11.35 ms (46 -> 95 FPS), reflections
  visually intact; the close-up screen-edge repro (clipped reflective
  sphere) renders identically clean after the emitter rewrite; and a fresh
  98k clipbench (fpp, terrain detail 128, vu1) still reads **120 FPS** -
  the fan loop's few extra instructions per *clipped* triangle don't move
  the general-path baseline.
- (109) **M4: VU1 clipping is the default + the clip_tce env program.**
  Owner decision after in-situ testing: the close-up/screen-edge corruption
  on reflective geometry (107/108 saga) does not occur on the VU1 clipping
  path, so the hidden `"clipping": "vu1"` mode graduated to the default
  ahead of the originally planned hardware-perf gate (that pass - clipbench
  PERF + the ADC check on a real console - is still owed before the EE
  clipper can be deleted). New projects scaffold with `"clipping": "vu1"`;
  the Preferences combo (project + per-scene override) now shows three
  options: *Precise clipping on VU1 (default)* / *Precise clipping on EE
  (legacy)* / *Fast culling*; a `.tyra` WITHOUT a clipping key still loads
  as `precise`, so existing projects keep their exact behavior until opted
  in. To make reflective materials work there, the fifth clip variant
  **clip_tce** (`stapip_clip_tce_vu1.vclpp`) computes the matcap ST from
  the ST-slot normal BEFORE the Sutherland-Hodgman pass (it then lerps
  through cuts like a regular texture coordinate; `CalculateTyraEnvStq`'s
  rsqrt runs before any edge/emit div, so the shared Q register stays
  safe), and the codegen's EE-computed-ST fallback (`envSts`) is deleted -
  the env pass is VU1-only in both clipping modes. Micro-memory lesson:
  cull_tce + clip_tce on top of the 8-program vu1 set measured 2162
  instructions against the ~2032 ceiling (nm on the .o files - the
  createProgramsCache assert is compiled out in release), so the vu1 set
  carries ONLY clip_tce (9 programs) and StaPipCore force-routes every
  env-bag package through the clip program at clip occupancy
  (`envForceClip` + `renderSubpkgs(forceClip)`; a full-occupancy package
  can expand past the buffer half when the 0.9w guard band cuts triangles
  the EE classified as fully inside, so cull-routing was not an option).
  `examples/reflections` flipped to vu1 and regenerated. **Verified**
  (Layer 3, PCSX2 SW renderer): fresh `--new` project scaffolds with vu1 +
  `CLIP_VU1S={true}`; the close-up repro (chrome sphere at the player,
  crossing the screen edge) renders clean through clip_tce - no
  punch-through, no wedges, no smeared polygons; the reflections showcase
  boots and reflects in vu1 mode; no TYRA banners in the game logs.
- (108) **Env matcap: normalize the normal on VU1 + close-up artifact
  post-mortem.** Follow-up on the user's screen-edge report (with an FPS
  dip - that part is the known EE-clipper cost for PARTIALLY_IN_FRUSTUM
  bags, paid twice by reflective objects). `CalculateTyraEnvStq` now
  RE-NORMALIZES the ST-slot normal (inlined rsqrt; the macro must run
  BEFORE the position's div q - rsqrt shares the Q register) because the
  EE clipper lerps normals across clip cuts and a lerped normal is short.
  Honest post-mortem: on the edge repro this changed zero pixels - the
  visible smudges turned out to be the DOCUMENTED flat-facet patchwork
  magnified by a screen-filling sphere (detail 24 -> 48 visibly cleans it;
  facet patches scale with tessellation, confirmed by test), and the hard
  hatched blocks were already fixed by (107). The normalization stays: it
  makes clipped strips sample correctly (they are a thin screen-edge band,
  hence the zero-diff on this repro) and unties the matcap from any
  non-unit normals (scaled models). Docs updated with the up-close facet
  guidance and the screen-edge FPS note. **Verified** (Layer 3, software
  renderer): edge repro pixel-diffed before/after fixes; detail-48 variant
  visibly smoother; no TYRA banners.
- (107) **Close-up punch-through fix: the env pass drops the TestOnly
  trick.** User-reported (third close-up artifact): standing at a static-map
  chrome sphere, LATER objects (its pedestal) punched through the sphere as
  a solid block plus dithered fringes, and the whole surface showed z-fight
  moire dots. Isolated with a scratch FPP scene (sphere right at the
  player): no-reflection variant clean, so the env pass was the trigger -
  specifically its `PipelineZTest_TestOnly` (ATEST all-fail + AFAIL
  keep-zbuffer, color-only writes): correct at distance (cull programs) but
  on the close-up EE-clipped path it corrupted the depth relationships of
  everything drawn after. The env pass now uses the standard GEQUAL test -
  the two passes are coplanar, so re-writing identical depths is benign -
  and the punch-through is gone. Residual: a subtle sampling moire on very
  magnified spheres (the 128px map's bands under STQ precision) - cosmetic.
  WHY TestOnly misbehaves there is not yet root-caused; the highlight hull
  shells still use it (pre-existing, unchanged). **Verified** (Layer 3,
  software renderer, scratch close-up scene): pedestal correctly occluded,
  sunset reflection intact; distance rendering unchanged.
- (106) **Self-reflection fix: near-camera reflected objects skip the env
  pass.** User-reported (second close-up artifact after 103): a reflective
  object that is ALSO marked "Show in reflections" sampled its own body
  from the env map up close - the object fills most of the wide-FOV env
  view when the camera stands at it, so chrome showed big dark hatched
  patches of itself ("siet"). Mid-distance mutual reflections (the red
  box's blob in a neighboring sphere) look great - the degenerate case is
  only the object the camera is hugging. Fix in the generated env pass:
  skip a reflected object when the camera sits within ~1.9x its bounding
  radius (0.87 * max scale, the unit-cube half-diagonal); it pops back in
  a step away. **Verified** (Layer 3, software renderer): the screen-
  filling marked "@sky" sphere from the repro scene is uniformly clean up
  close; the reflections showcase keeps its marked paint spheres and the
  red-box blob in neighboring chrome.
- (105) **Objects in reflections: the per-object "Show in reflections"
  flag.** The dynamic ("@sky") env map reflected only the sky dome; now an
  object marked `reflected` (Properties checkbox, `"reflected": true` in the
  object json) is rendered into the map too - chrome mirrors it, the second
  half of the GT3 trick. Engine: `RendererCoreEnvMap` gained a dedicated
  128x128 z-buffer (begin() clears color+depth in one all-pass sprite,
  ZBUF_1 points at it with writes ON) so marked objects occlude each other
  inside the map; the dome still draws first with an AllPass test. Codegen:
  `SceneObjectData.reflected` (struct field + row column - keep them 1:1),
  and the renderScene env pass submits marked objects' BASE bags after the
  dome (no env-in-env), depth-tested in the env target. Full editor chain
  per tyra-editor-dev: SceneObject field + operator==, save/load (default
  false stays implicit), single- and multi-select Properties checkbox. The
  editor viewport's @sky approximation still shows the sky only (noted in
  the tooltip + docs) - object reflections are checked in the game.
  **Verified** (Layer 3, software renderer): in examples/reflections the
  matte control box and the red/blue paint spheres are marked - the dynamic
  chrome spheres show the red box's blob (and paint-sphere dots) exactly
  where the scene places them, while static-map spheres are unchanged; log
  clean. Cost note: each marked object = one extra small render per frame.
- (104) **examples/reflections rebuilt as a first-person chrome showroom.**
  The original orbit-camera five-object demo (its res/ assets initially
  missed the repo - see 102) is now an FPP scene: an avenue of pedestals
  pairing static-sunset-map chrome against dynamic "@sky" chrome, a tall
  mirror monolith, three car-paint spheres and a matte control - plus a
  flow-graph **sky cycler** (Every 14 s -> Set Sky sunset, parallel
  Delay 7 s -> Set Sky day) that makes the dynamic mode's point in one
  glance: only "@sky" surfaces follow the retint. Flow-graph codegen
  gotcha worth remembering: built-in action nodes do NOT chain exec onward
  (emitExec recurses only through custom exec_out nodes), so
  trigger->SetSky->Delay silently drops the Delay - wire the Delay to the
  trigger in parallel. **Verified** (Layer 3, software renderer): two
  screenshots 7 s apart show the day and sunset phases with the dynamic
  spheres/monolith tracking the sky while static-map spheres keep their
  sunset bands; boot log clean.
- (103) **Dynamic env map fix: the clear sprite never painted (feathery
  reflections).** User-reported: up close, "@sky" surfaces showed grey
  feathering, as if geometry bled through. Diagnosis via a probe material
  (black base, strength 1.0 - the sphere becomes a monitor for the env map):
  the map's sky half was clean but the below-horizon half was BLACK - the
  begin() clear sprite rasterized against the MAIN window's XYOFFSET (the
  offset switch sat after it in the packet), landed outside the target's
  0..127 scissor and never painted, so below-horizon samples hit VRAM
  garbage. Matcap STs crossing the horizon line picked up that black =
  feathering along facet boundaries. Fix: write the target-centered
  XYOFFSET before the clear sprite. **Verified** (Layer 3, software
  renderer): a screen-filling @sky sphere is now uniformly clean - the
  below-horizon half reflects the horizon color, no dark streaks.
- (102) **Scaffold fix: res/ assets are tracked by git.** `--new` wrote the
  keep-empty-dir `.gitignore` (`*` + `!.gitignore`) into `res/` - correct
  for `bin/`/`obj/` build output, but it silently excluded every AUTHORED
  asset from the repo, defeating the whole collaboration format (a teammate
  pulling the project got "material file missing" on every object; that is
  exactly how examples/reflections first shipped without its .mtl files).
  New `TPL_RES_GITIGNORE`: everything under `res/` is checked in except
  build-regenerated bakes (`/menus/`, `/models/*.tskl`, `/models/*.tanm`).
  `hud/` is deliberately NOT ignored - user-imported HUD images land there
  next to the baked text sprites, and losing imports is worse than
  committing regenerable bakes. NOTE: projects created before this fix keep
  their old `res/.gitignore` (create-time file, never refreshed) - replace
  it by hand. examples/reflections aligned to the new template. **Verified**
  (Layer 1): editor builds clean; a fresh `--new` scaffold emits the new
  `res/.gitignore`.
- (101) **Dynamic environment map: GT3-style live-sky reflections
  ("@sky").** Phase 2b of (99)/(100). A material's sphere map can now be
  `<dynamic - live sky>` (Material Editor; stored as the filename token
  `@sky` in the `refl` statement): the game re-renders the scene's SKY DOME
  into a 128x128 VRAM texture every frame and reflective materials sample
  that - reflections track the live sky, script retints included. Engine
  fork: `RendererCoreEnvMap` (render target allocated at init below the
  texture region so FIFO vram frees can never reclaim it; begin()/end()
  bracket = PATH1 drain + FRAME/SCISSOR/XYOFFSET redirect with MASKED z
  writes + clear sprite, restore + TEXFLUSH), `Texture::vramResident` (a
  texture whose pixels live only in GS memory - `useTexture` binds its
  texbuffer directly, no PATH3 upload, never evicted),
  `RendererCore3D::pushEnvView/popEnvView` (square 110-deg projection along
  the camera's level forward + frustum planes widened 1.4x for the
  screen-aspect mismatch - overly wide planes only cost clipping work,
  never wrongly cull). Generated game: `@sky` materials bind the engine
  target (`g_dynamicEnvUsers` refcount gates the per-frame dome pass at the
  top of renderScene, AllPass z-test swapped in for the dome). Editor: the
  combo entry + `@sky` guards in texbake/import flows/missing-file warning;
  the GL twin approximates the dome with the analytic horizon/zenith
  gradient (uReflOn == 2). New pitfall recorded in tyra-engine-dev, cost a
  debugging round: a GIF A+D giftag whose NLOOP undercounts its register
  writes (begin()'s clear sprite made it 8, tag said 7) stalls the GIF
  forever - eternal loading screen, no assert, clean log. **Verified**
  (Layer 3): PCSX2 software renderer, scratch scene with BOTH modes side by
  side - the `@sky` sphere reflects the scene's blue sky gradient (top half
  sky-blue, pale horizon line) while the static-PNG sphere keeps its
  white-band/brown look and the matte control stays flat; log free of TYRA
  banners; "Dynamic env map initialized (VRAM at 700416)" confirms the
  init-time allocation. A live Set-Sky-Color retint of the reflection still
  wants a hands-on pad test.
- (100) **Reflective materials on VU1: TCE matcap programs + in-band GS
  ALPHA.** Phase 2a of (99). The env pass's sphere-map STs now come from a
  new StaPip VU1 program family: `stapip_cull_tce_vu1.vclpp` +
  `stapip_as_is_tce_vu1.vclpp` (`CalculateTyraEnvStq` in `tyra_macros.i`) -
  the env bag's texture bag sets `coordinatesAreNormals`, the world-space
  normals ride the vertex stream's ST slot, and VU1 computes
  `st = (.5+.5(n·r), .5-.5(n·u))` from a per-mesh camera basis uploaded at
  `VU1_ENV_BASIS_ADDR` (the free lights-matrix area; program selection via
  `StaPipVU1TextureEnvColor`, EE-clipper set = 10 resident programs). The
  blend equation moved IN-BAND: every StaPip mesh's tag block gained a GS
  ALPHA A+D pair (`VU1_ALPHA_ADDR` = 21, `StoreTyraGifTags*Alpha` 9/7-qword
  variants, `getMaxVertCount` -7 -> -9, clip programs' NLOOP patch offsets
  6/4 -> 8/6) - alpha-over by default, additive for env bags - so BOTH
  `sync.align3D()` FINISH barriers and the PATH3 `setAlpha` bracketing in
  `StaPipCore::render` are gone; reflective mesh count is no longer
  bottlenecked. dynpip keeps the original 7/5-qword macros (its C++ knows
  nothing of the ALPHA qword). Generated games fall back to EE-computed STs
  only in hidden `"clipping": "vu1"` scenes (no clip-family env variant;
  asserted engine-side). Two NEW vclpp pitfalls recorded in tyra-engine-dev:
  a `;` comment inside a `#macro` body makes vclpp SILENTLY swallow every
  call site (the speckled-sphere debugging session: the .o.vcl in the
  compiler container is the ground truth), and `#define` aliases expand only
  one level (dvp-as "unresolved expression") - VU1 defines must be literals.
  **Verified** (Layer 3): Docker build compiles all 14 StaPip programs;
  PCSX2 software renderer boots clean and the zoomed screenshot shows the
  chrome sphere's crisp horizon band + per-face box reflections identical in
  character to the EE version; matte control box unaffected. Real-hardware
  micro-memory headroom computed, not measured (~1.35k of 2k instr).
- (99) **Reflective materials: sphere-mapped "chrome" (the NFS/GT car-paint
  trick).** New `refl -type sphere -mm 0 <strength> <file>` statement in
  `.mtl` files, authored in the Material Editor's new **Reflection** section
  (sphere-map picker + strength slider, live in both previews). The PS2 side
  is the period-correct technique: at geometry build the generated game
  captures world-space normals for reflective parts (`pushVert` /
  `g_envNormals`); each frame `renderScene` derives the camera basis and
  rewrites a per-part env ST array on the EE (`u = .5+.5(n·right)`,
  `v = .5-.5(n·up)`), then submits the part a SECOND time as its own
  StaPipBag - same vertex array + bboxVersion (shared frustum-bbox cache
  entry; all-white "many" colors keep the VU1 program shape identical to the
  base bag), sphere map as texture, `PipelineZTest_TestOnly`, `fogDisabled`
  (GS fog would ADD the fog color through the additive equation). The
  additive blend itself is a new engine-fork feature: per-bag
  `PipelineInfoBag::additiveBlendFix` - `StaPipCore::render` drains PATH1
  (`sync.align3D()`), switches the global GS ALPHA register to
  `Cs*FIX/128 + Cd` via the new `RendererCoreGS::setAlpha` (preallocated
  PATH3 packet, `setFogColor` pattern), and restores alpha-over after the
  bag's own drain - placed after the frustum early-out so a culled bag never
  flips global state. Both `.mtl` parsers extended in sync (engine
  `LeanObjLoader` + editor `objparser.cpp`); the GL twin samples the map in
  the viewport FS from `dFdx/dFdy` flat normals + the same camera-basis
  formula (both sides faceted - the loaders' per-face normals, like base
  lighting). Also threaded through: texbake quality claims, model/material
  import rewrites (`refl` filename remapped, options preserved), Material
  Editor round-trip (no longer falls into `extra`). `gl_loader` gained
  `glActiveTexture`/`GL_TEXTURE0/1` (the sphere map rides texture unit 1).
  v1 limits: static primitives + .obj models only (no .glb/terrain), EE ST
  math + two FINISH barriers per reflective mesh - the planned phase 2 moves
  UV-from-normal into the StaPip VU1 programs (GT3 style) and can smooth
  normals. **Verified** (Layer 3): editor builds clean; `--new` + overlay
  scratch project (red chrome sphere detail 24 + chrome box + matte box,
  128px sky-gradient sphere map) `--resave` round-trips the `refl` line;
  generated `terrain_game.cpp` carries the env pass; full Docker build
  (engine + game) compiles; PCSX2 **software renderer** boots clean
  (`bin/log.txt` free of TYRA banners) and the F8 screenshot shows the
  horizon flash across the sphere/box while the matte box stays flat; the
  editor viewport shows the matching matcap on the same scene (GUI
  screenshot). The Material Editor preview shares the verified shader path;
  its interactive feel (picker, slider) still wants a hands-on pass. Docs:
  README, `docs/reflective-materials.md`, engine + editor skills.

  *(Numbering note: the reflective-materials series above landed as its own
  (99)-(112) while the Live-Link/mirror series below already used (99)-(107).
  Old entries keep their numbers; the sequence continues from (113) up top.)*

- (107) **Live Link v2 — per-project on/off + live add/delete of objects.**
  *(Numbering note: entries 104-107 appear twice — the reflections marathon
  above and the Live Link/mirror line below landed from parallel branches
  with the same numbers; both are kept.)*
  Two follow-ups to (106). First, the on/off is now a **project setting**
  (`ProjectSettings::liveLink`, default on; *Project > Preferences > Build*,
  *Build > Live Link*, and the toolbar **LIVE chip itself is the switch** —
  click to toggle): off = `live_link.gen.cpp` is an empty TU even in debug
  builds and the Runner deletes `livelink.sig`, so the game carries no poller
  at all for anyone who doesn't want debug builds patched from outside (the
  short-lived editor.ini flag from (106) is gone — the setting travels with
  the `.tyra`). The chip is always visible in the debug profile with four
  states: gray "LIVE off", dim "LIVE (build)" (no poller-capable build yet),
  green "LIVE", amber "LIVE (rebuild)". Second, the protocol moved from
  index-addressed to **id-addressed records** (v2: 64 B per object = FNV-1a 64
  of the editor object id + a spawn-template index + the 12 live floats;
  `SCENE_*_OBJECT_ID_HASHES` tables baked into scene_data.hpp, binary-searched
  on the EE), which buys: renames/reorders are non-events, **adding an object
  live works** — the game clones an equal-recipe authored template through the
  existing runtime spawn pool (`ctx.spawnObject`, ≤32 clones) and patches the
  clone — and **deleting live hides** the object (undo restores; spawned
  clones despawn). `bin/livelink.sig` became an as-built record (per-object
  id + recipe hash in built order + a context hash) the editor evaluates
  per tick: recipe drift on a built object, a new object with no template or
  one that can't be faithfully spawned (point lights / projecting decals /
  mirrors / objects carrying flow graphs or scripts), or layer-table changes
  → amber chip, zero writes. Trap fixed along the way: the editor seeds its
  snapshot sequence from the clock at project attach — a restarted editor
  starting again at seq=1 collided with the previous session's seq=1 that the
  still-running game remembered, and the (different) snapshot was deduped
  away. **Verified in PCSX2 (SW renderer, 50 FPS steady):** live ADD — a
  box added to the project files spawned in the running game via template
  index 1 and took its own color/rotation; live DELETE — removing the pillar
  from the manifest hid it in the running game while the spawned box
  survived; all four chip states screenshotted (off/build/live/rebuild);
  recipe change (box detail 1→4) flipped amber with `livelink.bin` untouched;
  `liveLink: false` build emitted the stub TU and removed the sig. All nine
  examples regenerated/rebuilt clean in Docker. Real-PS2 pass still pending
  (as in (106)).

- (106) **Live Link — edit the running game.** Scene edits (object position /
  rotation / scale / color) stream into the running game with **no rebuild**:
  drag a gizmo in the editor and the box slides across the PS2 screen. No
  socket and no new protocol — the transport is the host filesystem the game
  already loads assets from (PCSX2 Host Filesystem, or the ps2link/ps2client
  file server on a real console), so one mechanism covers both targets. The
  editor (`App::liveLinkTick`, ~10 Hz) writes `bin/livelink.bin` (little-endian
  `TXLL` blob: seq + scene + 12 floats per authored object + a seq-echo footer,
  written atomically tmp→rename) whenever the live-patchable state changed; a
  generated global script (`templates::liveLinkScript` →
  `src/scripts/live_link.gen.cpp`, **debug profile only** — release emits an
  empty TU) polls it every 6 frames (25 under ps2link, where each fopen is a
  network round-trip), rejects torn/stale reads (magic + exact size + footer,
  seq dedupe), and patches `RuntimeObject.data` + `dirty` only for objects
  whose values really changed — the same dirty-rebuild path the Move/Set Color
  flow nodes use, so physics/collision/shading follow. Index-mapping safety:
  `project::liveLinkSignature` (FNV-1a over scene/object order, ids, types,
  model/material, prim detail, layers, and the build-baked cases — point
  lights, projected-decal projectors) is stamped into `bin/livelink.sig` by the
  Runner at build start (which also deletes any stale `livelink.bin`); the
  editor streams only while the project still hashes identically, and the
  toolbar shows **● LIVE** (green) / **● LIVE (rebuild)** (amber) accordingly —
  a structural edit can pause, an Undo or a rebuild resumes automatically.
  Master switch in *Build > Live Link* (`editor.ini`, default on). Docs:
  `docs/live-link.md`. **Verified in PCSX2 (SW renderer, 50 FPS steady):**
  (1) game-side poller — scratch debug FPP project booted, a hand-crafted
  `livelink.bin` moved/rotated/stretched/recolored the box on the live game
  (screenshots A/B); (2) full editor→game loop — object JSON edited, editor
  GUI opened on the project, it auto-wrote seq=1 and the running game showed
  the new pose/color with no rebuild (screenshot C); (3) structure guard —
  adding a third object flipped the toolbar to amber and blocked writes (seq
  unchanged), a headless rebuild refreshed `livelink.sig` and streaming
  resumed on its own (fresh 3-object snapshot). Release codegen verified to
  emit the stub. Real-PS2 pass (ps2link cadence) still wants a hands-on test.

- (105) **examples/mirror-room — the Mirror object demo.** A committed example
  for (104): a gray wall built in three pieces **around an opening**, a Mirror
  filling the opening, crate/ball/pillar props, and a third-person wobbler
  player with **Reflect player** on. The wall pieces are themselves on the
  mirror's list, so the glass shows a furnished room, and the README spells
  out the load-bearing detail (a solid wall behind the glass would z-occlude
  the copies — the opening IS the mirror). The terrain needs no list entry:
  it extends behind the wall and doubles as the mirror room's floor.
  **Verified:** authored inline, `--resave`d to the split layout, built in
  Docker and booted in PCSX2 (software renderer) from a short-path copy —
  props, walls and **the live avatar** all reflect through the opening at a
  locked 50 FPS, no asserts in `bin/log.txt`; the verified project was then
  copied into `examples/` minus the gitignored build outputs. This is also
  the first in-PCSX2 proof of the Reflect-player path (a real `.glb` avatar
  reflecting its live pose).

- (104) **Mirror objects — the PS2-era mirror as a scene object type.**
  `PrimitiveType::Mirror` (15): a rectangle (the decal quad, +Z face) that
  fakes a real mirror by **physically drawing its listed objects a second
  time**, reflected across the glass plane — no render-to-texture, no
  stencil. The parameters follow the "hard list beats a radius" call: an
  explicit **Reflected objects** list (names; renames remap alongside the
  sequence tracks, dangling names drop silently at codegen), **Reflect
  player** (third-person avatar only — an FPP player has no body), **glass
  opacity** (the shared color field is the tint) and the usual
  collision/draw-distance controls. Codegen keeps `SceneObjectData` a fixed
  POD by emitting a flat `MIRRORS`/`MIRROR_TARGETS` side table into
  `scene_data.hpp` (the `OBJECT_SCRIPT_ATTACHES` pattern), names resolved to
  scene-table indices at generation. The runtime trick is the highlight-hull
  one, generalized: `renderMirrors()` builds a Householder reflection about
  the live plane (normal = rotated +Z) and **re-submits each target's
  existing bags with the info bag's model pointer swapped onto it** — static
  parts are world-space-baked so the matrix reflects world coords; animated
  targets (and the avatar) compose `reflection * animMat` — so VU1 does all
  the per-vertex work, the EE never copies a vertex, and moving/animated
  targets reflect their **live pose** for free. Winding flips under a
  reflection but the GS draws both faces, so no reordering. Draw order is
  the load-bearing part: mirrors skip the main static loop entirely (the
  quad would z-write the plane and z-reject the copies behind it), then
  after `updateAndRenderAnimObjects` the copies draw first and the tinted
  quad alpha-blends over them (vertex alpha = opacity, patched after
  `addDecal` in `rebuildObjectGeometry` case 15). The viewport previews the
  same illusion (reflection matrix pass after the scene, constant-alpha
  `uOpacity` uniform added to the shader — reset at frame start so the
  sky/outline draws don't inherit it). The copies are real geometry on the
  far side of the plane, so the docs/tooltips say it plainly: build the
  mirror into a wall — the wall hides the mirror world outside the frame.
  **Verified:** editor builds clean; scratch project (box + mirror listing
  it, `reflectPlayer: true`, opacity 0.4) round-trips through `--resave`
  (legacy inline → split objects keep the `mirror` block); generated
  `scene_data.hpp` carries `MIRRORS[1] = {{0, 1, 0.4F, 1, 0, 1}}` +
  `MIRROR_TARGETS[1] = {0}` and both header templates (orbit + FPP) the new
  members; the game compiles in Docker and **boots in PCSX2 (software
  renderer): original box, translucent glass and the mirrored copy on the
  opposite side all render at a locked 50 FPS, no asserts in `bin/log.txt`**;
  editor-viewport screenshot shows the same scene (copy + tinted glass).
  Player reflection compiles through the shared anim-bag path but wants a
  hands-on test with a .glb avatar; real-hardware A/B pending like every
  perf-adjacent change.

- (103) **Over-the-shoulder camera offset.** `SceneObject::playerCamShoulder`
  (Properties > Third-person camera > **Shoulder**, default 0 = unchanged
  behavior) slides the third-person rig sideways, completing the camera offset:
  **Distance** is the offset back, **Height** the offset up, **Shoulder** the
  offset sideways — the three together are the rig in the camera's own frame.
  The key detail is that Shoulder slides the **whole rig — eye AND look-at
  alike** — along the camera's right vector, so the avatar sits off-center in
  frame; offsetting only the eye would merely angle the camera back at the
  player and keep them centered, which is *not* an over-the-shoulder shot. The
  right vector `(-cos yaw, 0, sin yaw)` is derived from `cross(forward, up)` and
  matches the walkers' own strafe convention, so "+ = right" means the same
  thing to the camera as it does to the stick. The lateral offset is **itself
  spring-armed** (a second `springArm` cast along the right vector, clamped to
  the requested offset) so a shoulder cam cannot slide into a wall the player is
  hugging — and that cast is skipped entirely when the offset is 0, so the
  default centered camera pays nothing. **Verified:** editor builds clean;
  scratch third-person project with `camShoulder: 0.8` generates
  `PLAYER_CAM_SHOULDERS = {0.8F}`, compiles in Docker and boots in PCSX2 — the
  avatar sits visibly **left** of center (a right-shoulder cam looks past the
  avatar's right side, exactly the RE4/Gears framing), shifted ~90 px from a
  450 px half-width, which matches the ~77 px predicted for 0.8 units at a
  6-unit boom under the ~75° horizontal FOV. 50 FPS, no assert. Wall-hug
  clamping of the offset still wants a hands-on pad test.

- (102) **Third-person camera spring arm — the camera stops at geometry instead
  of punching through it.** (101) left the third-person boom naive: it only
  clamped the eye above the terrain height *at the eye point*, so any wall,
  prop or ridge between the avatar and the camera was simply passed through
  (and the avatar disappeared behind it). `TerrainGame::springArm` now casts
  the boom from the pivot (the avatar's head) toward the desired eye and
  returns the first blocked distance; the camera stops there. Classic spring
  behavior: **snap in** on a hit (easing in would show a clipped frame) and
  **ease back out** when clear (`camBoom`, ~2 s to re-extend a full boom), so
  leaving cover doesn't pop. `CAM_RADIUS` (0.3, deliberately > the 0.15 near
  clip) keeps the eye off the surface and inflates the boxes so corners keep
  clearance; `CAM_MIN_DIST` (0.6) stops the camera collapsing into the head.
  **The shape is entirely budget-driven** (it runs every frame): **AABBs only,
  even for mesh-collision models** — camera collision needs no triangle
  precision (stopping a few cm early is invisible) and a slab test is a few
  compares with no sqrt, vs walking a collider's triangle list; the boom is
  short, so a **6-compare broad phase** (boom-segment AABB vs object AABB)
  rejects nearly every object before a single division; markers/decals/emitters,
  objects set to collision **none** (the author's opt-out) and **the avatar
  itself (type 6 — it must never block its own camera)** are skipped outright;
  and the terrain is a **fixed 8-step march + 4 bisections over the distance
  that survived the object pass** (constant cost, and shorter once an object
  already pulled the camera in). Boxes containing the pivot are ignored rather
  than collapsing the camera (the player brushing a wall). The old
  `terrainHeightAt` eye clamp stays as a cheap safety net for ridges falling
  between march samples. Object AABBs are sized exactly like box-mode player
  collision (real mesh / baked anim AABB when present), so the camera agrees
  with what the player collides against. **Verified:** editor builds clean; the
  scratch third-person project + a 12x4x0.5 wall 3 units behind the player
  compiles in Docker and boots in PCSX2 — **A/B in-engine**: wall at z=-3 pulls
  the boom 6 → ~2.45 (stops 0.3 in front of the wall face; the avatar fills the
  frame and is never occluded), wall moved to z=-22 returns the boom to the full
  6 (identical to the no-wall baseline) — i.e. **it pulls in only when actually
  blocked, no false positives**. Both at a locked 50 FPS with EE% unchanged
  within noise (35% → 36-37% *with* the extra wall to render), so the arm's cost
  does not register. Camera feel while running along walls still wants a
  hands-on pad test.

- (101) **Third-person player + cutscene "Hide player".** Adds a third
  `playerMode` (2 = third person) alongside Walk (FPP) and Noclip, plus a
  per-sequence **Hide player** flag in the Cutscene Director. **Design goal was
  max flexibility / min boilerplate** — the whole feature *reuses the animated
  `.glb` pipeline instead of building a new one*: the avatar is the Player
  object's OWN model (`SceneObject::modelPath`, must be an animated `.glb`), and
  a new `hasAnimBody()` predicate routes a mode-2 Player through the exact same
  collection/bake/setup/render/LOD/pose-share path as an animated Model
  (`collectAnimModelPaths`/`animModelIndexOf`, `setupAnimObject` now accepts
  type 6, `updateAndRenderAnimObjects` draws it for free). Each frame the
  third-person branch of `updatePlayerEntity` moves the player relative to the
  orbit camera, turns the avatar toward its movement direction (shortest-arc
  yaw lerp → `entFaceYaw`), rides a camera boom behind/above it (pulled above
  terrain), writes the entity pose into the avatar's runtime object and calls
  `drivePlayerAnim`. **The "2002 would faint" bit** = `drivePlayerAnim`: a
  trivial idle/walk/run(/jump) clip mapping auto-selected from the player's
  *actual* planar speed with 0.18 s cross-fades and foot-speed-matched playback
  — drop in a model, name three clips, get a fully animated character, no
  scripting. Escape hatch: a non-locomotion clip fired by a script/flow
  **Play Animation** one-shot plays to the end (`animFinished`) before
  locomotion resumes, so waves/attacks compose with zero new API; attached
  scripts see the avatar as `self`. New fields: `playerIdle/Walk/Run/JumpClip`,
  `playerRunThreshold`, `playerCamDist/Height/TurnRate` (+ `operator==`, JSON
  `player.thirdPerson` block, per-scene `PLAYER_*` scene_data tables + macros),
  and `Sequence::hidePlayer` (JSON, `Seq`/`kSeqs`, `ScriptContext::hidePlayer`
  written by the sequence player and cleared on release, applied post-scripts in
  both the orbit and FPP game loops). Editor UI: the Player *Properties* mode
  dropdown gains **Third person** with a `.glb`-only model picker, idle/walk/run/
  jump clip combos, run threshold and camera tunables; the Cutscene Director
  header gains a **Hide player** checkbox; the viewport previews a mode-2 Player
  as its avatar model. **Verified:** editor builds clean; a scratch project
  (Player → third person, `wobbler.glb`, idle=Wiggle/walk=Twist, + a
  hide-player cutscene) round-trips through `--resave`, generates correct
  `scene_data.hpp` (`PLAYER_MODES={2}`, clip tables, player row `animModel=0`)
  and sequence codegen, **compiles + links in Docker (PS2 toolchain, `tpp.elf`
  built, `wobbler.tskl` baked)** and **boots in PCSX2 at full 50 FPS** with the
  avatar rendered behind the orbit camera playing its idle clip, no assert.
  Interactive movement feel / turn blend / run threshold and the in-game
  hide-player trigger still want a hands-on pad test by a human.

- (100) **Engine perf audit: EE→VU0/VU1 work-distribution pass over the render
  hot path (47→73 FPS precise / 120 FPS with VU1 clipping on the 98k
  benchmark).** A deep audit of `vendor/tyra` looking for EE work that belongs
  on the VUs found the math library (Vec4/M4x4), skinning (entry 34) and the
  EE clipper's per-vertex transforms already on VU0, and clipping itself
  already ported to VU1 (M0–M3, hidden mode) — the remaining EE hog was the
  **per-package frustum classification** feeding both clip paths, plus three
  smaller leaks. Four fixes, all in the StaPip render path: **(1)**
  `RenderBBox::clipFrustumCheck` re-ran the entire 8-corner/6-plane check a
  second time with an all-zero guard band for every PARTIALLY_IN_FRUSTUM
  box — upstream's "crappy guard band xd" placeholder; a zero margin cannot
  change the answer, so the re-check (a full duplicate sweep on exactly the
  expensive boxes) is deleted. **(2)** Classification moved from
  transform-8-corners-then-dot-each-against-6-planes (up to 8 VU0
  matrix-vector transforms + 48 dots per package, plus building a merged
  8-corner RenderBBox per straddling subpackage) to **object-space planes +
  the p-vertex/n-vertex AABB test**: `StaPipCore::render` transforms the 6
  frustum planes into the bag's object space once (`CoreBBox::
  computeObjectSpacePlanes`, exact same math factored per object — the
  d*column.w terms are dropped precisely because the corner path ignores the
  transformed w), and every package/subpackage check is now 6 planes × 2 dot
  products on min/max corners (`CoreBBox::frustumCheckAABB`,
  `StaPipBagPackagesBBox::getMergedMinMax` merges part min/maxes without
  materializing a box). Main-bag check uses the same test. **(3)** The
  `CoreBBox(const Vec4*, count)` min/max scan — per frame for skinned-mesh
  bbox recalculates and DynPip bags — went from six compare-branches per
  vertex to a **VU0 macro-mode `vmini.xyz`/`vmax.xyz` loop** (bit-identical
  results, ~4 ops/vertex, no branches). **(4)** `StaPipQBuffer::fillByCopy*`
  (every clip-classified subpackage, both EE and VU1 clip paths) copied
  vertices via a non-inlined per-element `Vec4::copy` call plus four branch
  checks per vertex — now four `memcpy`s per package. Verified per
  tyra-testing layer 3: fresh 128×128 terrain @ detail 128 (~98k verts) FPP
  scene, debug profile + FPS HUD, vsync off, PCSX2 **software renderer** —
  baseline 47 FPS (3 identical samples) → **73 FPS** (+55%) with
  **pixel-identical output** (0/4.27M pixels different outside the FPS
  counter, byte-exact across samples); the same scene in the hidden
  `"clipping": "vu1"` mode runs **120 FPS** (2.55× baseline; 0.047% pixel
  diff vs the EE-precise baseline — the known M3 LSB texel shifts on clipped
  edges, not a regression), confirming the classification cost was what kept
  the VU1-clipping mode from paying off (the M5 "companion work" from
  docs/vu1-clipping-plan.md). detailtest (spheres + orbit camera) boots clean,
  no asserts, geometry intact. PCSX2 numbers are directional (it undercounts
  EE clip cost 15–20%); a real-PS2 clipbench re-run is still wanted before
  flipping the M4 preference. DynPip's per-render heap churn
  (`new DynPipBag*[]`, per-bag texture/lighting bag allocations) was audited
  and left alone — generated games render exclusively through StaPip, so it
  only gains the shared VU0 min/max constructor.

- (100) **DoF review-feedback round: authored in the UI Editor, HUD rectangle
  fix, node modes, pin-label alignment.** Four fixes from testing the Raycast
  / Set Depth Of Field entry (94, PR #90) on a real project. (a) **Crosshair
  punched a sharp rectangle into the blur**:
  2D sprites stamp z = max across their whole rect (transparent margins
  included - see the Renderer2D drain comment), so a DoF pass composited at
  the bloom slot / endFrame z-failed under every sprite's rectangle. DoF now
  composites via `applyPostFx(PassDof)` immediately after `renderScene()`,
  before any 2D, in both game templates - the HUD can never interact with it
  again. (b) **DoF is now authored like bloom/grain**: `dofAmount`/`dofFocus`
  /`dofRange` on `ProjectSettings` (postFx override category, per-scene in
  Scene Preferences), serialized with the other post effects, edited in
  *Tools > UI Editor* as a `[ Depth of field ]` stack entry pinned under the
  whole stack (see (a) for why it cannot be dragged above sprites), codegen'd
  to `POSTFX_DOFS`/`POSTFX_DOF_FOCUSES`/`POSTFX_DOF_RANGES` in scene_data.hpp
  and applied at boot + every scene (re)load like bloom. (c) The **Set Depth
  Of Field node got a Mode combo**: *Set custom* (Focus/Range/Amount as
  before, position link = live distance), *Off*, and *Scene setting* - a new
  `ctx.dof = -2` request the game answers by re-applying the scene's authored
  values. (d) **Output pin labels ragged**: `rightLabel()` right-aligned each
  label by a text-width-dependent `Indent()`, and text rendering truncates
  the pen start to whole pixels - per-label fractional indents put the ">"
  arrows on different pixels at some DPI/zoom combos. The arrow is now its
  own item in a fixed column (same x every row, cannot drift); labels
  right-align against it. Verified: codegen harness asserts the mode
  emissions (`ctx.dof = 0` / `-2` / custom), the scene_data arrays, the
  authored apply and the PassDof-before-HUD ordering; full Docker + PCSX2
  e2e with an authored DoF (no flow node) and a 64x64 crosshair HUD with
  transparent margins shows the cross crisp with NO rectangle seam over the
  blurred horizon, far terrain blurred, near sharp, raycast still logging
  the analytic hit. Alignment could not be reproduced on this machine
  (pixel-scans at uiScale 2.5 x zoom 100%/121% all read a shared right edge
  +-1px), so the fix removes the mechanism rather than chasing the combo;
  GUI screenshots after the change still line up.
- (99) **Projected decals (rzutowanie na model) — decals that conform to the
  receiver geometry.** The flat `Decal` (62) only worked as a sticker on a flat
  wall; this adds a **"Project onto surfaces"** mode (`SceneObject::decalProject`)
  that wraps the decal texture onto whatever it covers — angled/curved walls,
  models, terrain — for wall text/graffiti and **fake blob shadows**. **Key
  architectural choice, driven by the EE budget:** projection is *pure host-side
  geometry* and runs entirely at build time. The decal object's oriented unit
  cube is a projector volume; `decalproj::project` (new `src/decalproj.cpp`)
  gathers the receiver triangles overlapping it (terrain + every solid object,
  auto), transforms them into decal-local space, keeps only front-facing
  (`+Z`-local) surfaces, Sutherland–Hodgman-clips each to the unit cube,
  fan-triangulates, computes a projected UV and nudges the result along the
  surface normal to sit in front. The output is a finished **world-space triangle
  list** baked into `inc/decal_data.gen.hpp` (per scene, per object index). The
  PS2 just uploads and draws it through the existing per-object static-pipeline
  path (`rebuildObjectGeometry` case 13, identity model matrix since `pushVert`
  already bakes world-space verts; alpha + z-fight offset come for free like the
  flat decal) — **no projection, clipping or CPU transform ever runs on the EE**;
  it rides the same VU1 path as all geometry, built once per scene, not per
  frame, capped at 4096 tris. Receiver tessellation was extracted into a shared
  host module (`src/primmesh.cpp`, box/sphere/cylinder/cone/plane as raw
  pos+normal+uv) so the projector uses *exactly* the geometry the viewport draws
  and the game generates; the viewport bakes shade on top (identical output).
  Live editor preview: the app computes the projected mesh (`updateProjectedDecals`,
  recomputed only when a scene signature changes) and pushes it to the viewport
  (`setProjectedDecals`), drawn via the existing decal-alpha path. **Also fixed a
  latent decal UV-handedness bug** shared with the flat decal: a decal faces `+Z`
  and is viewed from the `+Z` side where world `+X` is to the viewer's left, so
  `u = x+0.5` ran textures right-to-left (text mirrored). Switched to the
  slide-projector convention `u = 0.5−x` in the projected decal, `unitDecal` and
  `addDecal` so signs/text read correctly and flat/projected stay consistent (the
  old test images were symmetric, so nobody had noticed). **Verified end-to-end**:
  a standalone geometry harness (wall + floor cases, footprint/UV/offset asserts,
  all pass); clean editor build; a scratch project (box wall + projecting "TYRA"
  text decal + a floor blob-shadow decal on flat terrain) built through Docker +
  PS2 `make` (`=== Build OK ===`, generated code compiles on the ee-gcc
  toolchain) and **booted in PCSX2** (software renderer, PAL 50 FPS, no asserts):
  the shadow conforms to the terrain and the "TYRA" text wraps onto the wall
  reading correctly (after the UV fix). `decal_data.gen.hpp` emits the baked
  meshes; `decalProject` load/save round-trips (omitted at its default). The flat
  decal (`decalProject=false`) is unchanged apart from the shared UV fix.
  Surface-matched vertex lighting for projected decals (they ship unlit/flat-tint
  today) and texture-baking into receiver textures are noted as future follow-ups.
- (94) **Window layouts (per-project, switchable, editable).** The editor now
  keeps a **named collection** of docking arrangements instead of a single dump.
  A new top-level **Layout** menu lists every layout (radio-checked active one),
  and switching re-docks the windows. Three built-ins ship with every project:
  **Default** (the classic Project/Properties/Output+Debug/Viewport arrangement),
  **Director** (Viewport centre + Cutscene Director dopesheet along the bottom),
  and **Material Designer** (Material Editor filling the window, core panels as
  background tabs). Each layout is edited by just rearranging windows and is
  saved per project; the menu also has *Save current arrangement*, *Reset to
  built-in arrangement* (recipe-backed layouts only), *New layout…* (captures the
  live arrangement under a new name), *Rename…* and *Delete* (disabled when only
  one layout remains — a project must always keep at least one; even the
  built-ins can be deleted). A layout also carries the set of optional editor
  windows it wants open (Cutscene Director, Material Editor, …), so switching
  opens/closes them to match.
  - **Data/format.** `Project::windowLayout` (one ImGui ini string) →
    `std::vector<WindowLayout> windowLayouts` + `int activeLayout`
    (`project.hpp`). A `WindowLayout` is `{name, ini, recipe, openWindows}`; a
    built-in starts with an empty `ini` and a `recipe` id (`LayoutRecipe`
    Default/Director/Material) so it can be seeded at `--new` time with no ImGui
    context — `App::buildLayoutRecipe` arranges it via DockBuilder the first time
    it's shown, and the resulting dump is captured on save. The `.tyra` now
    writes `"layouts": [...]` + `"activeLayout"`; the reader **migrates** the old
    single `"layout"` dump into a seeded built-in set (the dump becomes Default's
    `ini`, so old projects gain Director/Material while keeping their exact
    arrangement) and guards against an empty/out-of-range set.
    `project::seedBuiltinLayouts` is the shared seeder (used by `create` and the
    migration path). Layouts are editor state — not game data, not in undo.
  - **Timing.** A switch is applied at a frame boundary: a saved-`ini` layout
    loads via `LoadIniSettingsFromMemory` in the run() loop (can't run
    mid-frame); a recipe layout rebuilds in `drawUI` before any panel is
    submitted (DockBuilder must precede the windows it docks). After either, the
    layout's headline panel is brought to front (`pendingFocusWindow_`).
  - **Verified.** Editor builds clean (`build.ps1`). Headless `--new` writes the
    three seeded built-ins with empty `ini` + recipe ids; `--resave` on a copy of
    `examples/showcase` (a real legacy project with a `"layout"` dump) migrates it
    to `layouts`/`activeLayout` with the original docking data preserved verbatim
    inside Default's `ini` and Director/Material appended. GUI switching itself
    wasn't machine-driven (no synthetic input on the dev box); the DockBuilder
    recipes reuse the exact pattern of the previously working default-layout code.

- (94) **Raycast + Set Depth Of Field flow nodes.** Two new built-in nodes.
  **Raycast** (Player category) casts a ray from the player's eye along the
  view direction when its exec fires and latches the results into runtime
  members exactly like a C++-backed custom node's outputs (`objOut<id>` /
  `posOut<id>`): the position output is the hit point, the object output the
  hit object (-1 = none; downstream built-in actions are handle-guarded like
  Spawn Object clones), and its "after" exec fires immediately after the cast
  so wired actions read fresh values. The runtime helper (`flowRaycast`,
  emitted into `flow_graph.gen.cpp` only when a graph uses the node) tests
  object bounding spheres (same marker-type skip list as the USE picker, the
  player entity excluded) and marches the terrain heightmap
  (`terrainHeightAtScene` + bisection); the ray origin/direction come from a
  new `ScriptContext::playerLook` set next to `playerPosition` in both game
  templates. **Set Depth Of Field** (Scene category, Focus/Range/Amount)
  blurs the image progressively past Focus (full blur at Focus+Range,
  Amount 0..1, 0 = off); a wired position replaces Focus with the distance
  from the player to that point at fire time (e.g. keep an object in focus
  via Get Position). Engine side (`RendererCorePostFx`, TyraX fork): a new
  `PassDof` (composited right after the 3D scene since entry 100's HUD fix)
  reuses the bloom blur chain, then blends the blur
  back through three full-screen sprites drawn at real GS depths with the
  pass's ordinary GEQUAL z-test (writes masked) — the world-distance → GS-z
  mapping is solved from the shared perspective matrix
  (`z(d) = 0xFFFFFF·near·(far−d)/(d·(far−near))`), so the sharp/blurred split
  follows actual scene depth per pixel at zero EE cost; 2D sprites stamp
  z = max, so HUD/menus never blur. `blit()` grew an optional z param and the
  postFx packet grew to 352 qwords (every pass at once now fits).
  Verified: codegen harness (scratch main() against the build .obj files)
  over a graph exercising every wiring — static + position-wired SetDof,
  Raycast → Set Position (pos link) / Hide Object (object link, guarded) /
  Log via PosToText — then full e2e in Docker + PCSX2: an FPP scene with a
  2×2×2 box at (0,1,6) logged `ray: (0, 1.8, 5.4)` every 2 s (exactly the
  analytic sphere hit 6−√(1−0.8²)), Set Object Color driven by the raycast's
  object output painted the box red in-game, and F8 screenshots on BOTH the
  HW and software renderers show the near checkerboard sharp and the far
  terrain/horizon blurred past the 5-unit focus. Interactive feel (walking
  around with the pad while DoF is on) still deserves a hands-on test.
- (93) **Rebrand to TyraX.** Our fork — the editor, the repo, the docs and the
  VS Code plugin — is now **TyraX**; the upstream engine we fork keeps the name
  **Tyra**. What changed: the executable / CMake target `tyra-editor` →
  `tyrax-editor` (`build\tyrax-editor.exe`, CLI usage strings, `resources/app.rc`,
  build scripts, testing/PR skills, `-ProcessName`); the window title and all
  user-facing prose "Tyra Editor" → "TyraX"; the generated- and engine-fork
  markers `Generated by tyra-editor` / `Modified by tyra-editor` /
  `Added by tyra-editor` / `tyra-editor guard band` → the `TyraX` equivalents
  (154 `Modified` + 14 `Added` sites across `vendor/tyra`, every `.gen.*` and
  ownable file in `examples/`, and the `templates.cpp` writers); the VS Code
  extension `tools/vscode-tyra` → `tools/vscode-tyrax`, package `tyra-flownode`
  → `tyrax-flownode`, publisher `tyra` → `tyrax`, id `tyrax.tyrax-flownode`,
  languages/scopes `tyra-{flownode,screenfx}` → `tyrax-*`, display names →
  "TyraX …", and the committed VSIX rebuilt (`tyrax-flownode-0.1.0.vsix` via
  `npx @vscode/vsce package`); the generated `.vscode/extensions.json`
  recommendation follows to `tyrax.tyrax-flownode`.
  - **Kept as-is on purpose** (internal/on-disk state, or upstream): the `.tyra`
    project-file extension (user decision — no migration, examples untouched);
    the engine and everything under `vendor/tyra` stays the **Tyra** engine
    (`libtyra`, `Tyra::`, `TYRA_*` macros, `tyra-engine-shared`, the `h4570/tyra`
    Docker image); `%LOCALAPPDATA%\tyra-editor` (editor.ini + ps2sdk cache — a
    rename would silently drop user prefs and force a multi-hundred-MB re-extract);
    the `%TEMP%\tyra-editor-test\` scratch convention; the `.claude/skills/tyra-*`
    folder identifiers (harness-referenced) — their prose was rebranded, the
    slugs left; and the GitHub URL `doctorspider42/tyra-editor` (per request —
    the repo itself isn't renamed here). PROGRESS/plan history left unrewritten.
  - **Compat**: the ownership-marker check in `project.cpp` now accepts **both**
    `Generated by TyraX` and legacy `Generated by tyra-editor`, so `.tyra`
    projects authored before the rebrand keep syncing their ownable files.
  - **Verified**: clean `build.ps1 -Clean` links `tyrax-editor.exe`; headless
    `--new rebrandtest` scaffolds a project whose generated files carry
    `Generated by TyraX` ×21 with **zero** stale `tyra-editor` markers, and whose
    `.vscode/extensions.json` recommends `tyrax.tyrax-flownode`; engine tokens
    (`h4570/tyra`, `libtyra`, `Tyra::`, `TYRA_OBJECT_SCRIPT`) confirmed intact.
    Docker game-build round-trip not run (no engine/codegen logic changed, only
    marker text).

- (98) **Brush opacity settings: labeled slider + per-dab Vary.** Follow-up
  on (97). The opacity slider was an unlabeled 70px stub next to Size - now
  a properly labeled **Opacity** on its own row, and a new **Vary** slider
  (0-100%) randomly reduces each dab's opacity by up to that fraction
  (shared LCG with the rotation roll; the Live-dab ghost pass is exempt so
  the preview shows the base strength). Brush controls reflowed into stable
  rows - mode+color/picker, Size+Opacity, Spacing+Vary, (brush) Angle+Random
  - after the first cut clipped the Opacity label off the pane edge.
  **Verified** (Layer 2, GUI harness): the cropped control rows show
  "24 px Size | 1.00 Opacity" and "300% Spacing | 100% Vary"; a stroke at
  Spacing 300% + Vary 100% left dabs ranging from full-strength red to
  barely-there pink along one drag. Editor builds clean.
- (97) **Dab rotation (manual + random) and the Live dab ghost.** Follow-up
  on (96). Brush-image dabs gained an **Angle** slider (0-360 deg; the
  stamp offset is rotated back into image space, loop radius padded by
  sqrt(2) so corners don't clip) and a **Random** toggle re-rolling the
  rotation per dab from a member LCG (Angle disabled while on) - bricks can
  be laid deliberately, splats scattered organically. New **Live dab**
  toggle (default on): every hovered frame the paint pane composites ONE
  uncommitted stamp under the cursor - the active layer is backed up,
  stamped, composited, restored, so undo snapshots/stroke starts/saves
  (which all recomposite first) only ever see clean layers; the ghost pass
  never re-rolls the random angle (a spinning preview reads as noise) and
  the composite is wiped when the cursor leaves. **Verified** (Layer 2, GUI
  harness): a rectangular brick.png brush ghost-previewed on the crate
  while hovering with the texture PNG's hash untouched; a click stamped the
  brick rotated by the dialed 67 deg; with Random + 300% spacing a drag
  left bricks in clearly different orientations; the Angle slider greys out
  while Random is checked. Editor builds clean.
- (96) **GIMP-style brush dabs + Spacing.** User feedback on (95): the Brush
  mode painted a texture-space *tiled pattern* ("revealing" an image through
  holes); now each dab **stamps the whole brush image** scaled to the brush
  Size - the PNG's alpha is the dab shape, its RGB the paint (an irregular
  splat PNG paints organic blotches). The Tile slider is gone; in its place
  a **Spacing** slider (5-300%, % of brush diameter, applies to every mode)
  with the GIMP residual-distance algorithm - the leftover distance carries
  across mouse samples (`matEdStampResidual_`), so low spacing draws one
  continuous line and >=100% drops exactly-spaced separate stamps regardless
  of mouse speed; the stroke seeds a dab at the click and restarts across UV
  seams. **Verified** (Layer 2, GUI harness): with a 64^2 splat.png (alpha
  blob) at Spacing 300% a drag left a row of separate orange blotches on the
  crate; at 5% the same drag drew a continuous ribbon; the splat brush was
  picked from res/brushes in the picker; layer stack from (95) reloaded from
  the sidecar across an editor restart. Editor builds clean.
- (95) **Paint layers with blend modes + project-global brushes.** The paint
  tool grew a per-texture **layer stack**: Background + N transparent layers,
  each with Normal/Multiply/Add/Overlay, an opacity slider and a visibility
  eye; strokes land on the active layer, `+`/`-`/Up/Down manage the stack.
  The **flattened composite** is rebuilt after every change and written to
  the texture PNG - the PS2 pipeline still sees a plain texture. The stack
  persists in a `<texture>.png.layers/` sidecar (layerN.png + layers.json,
  read via the in-tree json parser; any inconsistency falls back to a single
  Background - never fails a load); one Background = sidecar removed, so
  untouched textures stay plain files. texbake skips (and scrubs from stale
  bakes) the sidecars AND the new `res/brushes/` dir, so neither ships in
  .res-baked/bin/ISO; Duplicate copies a texture's sidecar along. The old
  "texture brush" was renamed to **Brush** and its sources moved from
  ad-hoc PNGs to **project-global brushes** in `res/brushes/*.png` with an
  "Import brush from PNG..." item in the picker; a third **Eraser** mode
  removes paint from the active layer (or punches decal alpha on the
  Background). Stamps now compose with straight-alpha "over" so strokes on
  transparent layers have no dark fringe. Undo gained typed steps (paint =
  layer snapshot, layer add/remove restore the structure, props as before);
  paint/layer steps apply only while their texture is loaded. **Verified**
  (Layer 2, GUI harness): + added "Layer 1", a red stroke on it wrote the
  sidecar (layers.json + layer0/1.png listed on disk); Multiply visibly
  darkened the blobs (grid shows through); the grass brush appeared in the
  picker from res/brushes and painted a continuous checker; Eraser chewed a
  hole in a red blob; the visibility eye toggled the composite PNG hash and
  restored it exactly; a headless --build's .res-baked contained neither
  brushes/ nor crate.png.layers/ while the composite crate.png shipped.
  Editor builds clean.
- (94) **Material Editor follow-ups: big preview, own Ctrl+Z undo, Delete.**
  User feedback on (93). (a) The preview pane now takes ~48% of the window
  width (floor `scaled(260)`) and the default window grew to 1020x600 - the
  paint surface is the point of the window. (b) **Ctrl+Z, scoped**: the window
  keeps ONE undo stack of paint strokes and committed property edits
  (`MatEdUndoStep` - pixel snapshot or a pre-edit copy of the staged entries,
  `matEdPrevMats_` baseline, cap 16). While the Material Editor is focused
  (`matEdFocused_`, same focus-scoping as the Flow Graph's Ctrl+C/V), Ctrl+Z
  runs `matEdUndoLast()` instead of scene undo (Ctrl+Y is inert there); an
  Undo button sits next to Duplicate. A paint step restores + rewrites its own
  texture even if the entry/texture switched since; a property step restores
  the entries and saves the file. (c) **Delete...** button routes into the
  existing asset-delete confirm flow (usage count, object/terrain fallbacks);
  the Material case now also clears the editor's staged state when the open
  file is the one deleted, and invalidates viewport caches. **Verified**
  (Layer 2, GUI harness): stroke -> PNG hash changed -> Undo button restored
  the pixels (0 brush-colored samples); Brightness drag wrote Kd 0.3 to the
  .mtl -> Undo restored Kd 1.0; Delete showed the confirm modal, removed
  crate-copy.mtl from disk and the list, cleared the pane, status "Deleted".
  Ctrl+Z routing verified end-to-end with a temporary debug chord (Ctrl+U ->
  "Material Editor: nothing to undo" with the window focused, F/T focus flags
  in the title) because synthetic keybd_event Ctrl+Z is swallowed by
  something machine-global in the test environment while Ctrl+S/Ctrl+U pass -
  real-keyboard Ctrl+Z uses the identical `IsKeyChordPressed` path that scene
  undo always used. Editor builds clean.
- (93) **Material Editor: preview on real models, texture painting, duplicate,
  new-texture canvas.** Four additions in one coherent pass. (a) The preview
  shape combo now lists every `res/models/*.obj` next to the four primitives;
  a model renders with the open `.mtl` as its override (usemtl-matched, same
  rule as the game) through a new `Viewport::MatPrevModel` cache that keeps Kd
  OUT of the vertex colors (it rides the tint uniform) so the selected entry's
  staged, uncommitted slider values preview live; camera orbits by drag
  (LMB, or RMB while painting), wheel zooms, AABB-framed. Opening the editor
  from a Model object's Edit... passes the model as a hint; opening a
  res/models `.mtl` auto-picks the sibling `.obj`. (b) **Painting**: a Paint
  toggle raycasts each stroke sample against a CPU copy of the displayed
  triangles (`materialPreviewPick`, Moller-Trumbore + barycentric UV),
  splats a soft round brush into a CPU RGBA copy of the entry's texture
  (segment interpolation between samples, wrap-around repeat, a seam-jump
  guard that breaks the segment instead of smearing), live-uploads the shared
  GL texture (`updateTexturePixels` - scene viewport updates too, same
  texCache_ id) and writes the PNG on mouse release - painting IS the bake,
  texbake quantizes it at build like any PNG. Brush modes: color, and a
  **texture brush** sampling a pattern PNG in texture space (tiled, density
  slider) so strokes reveal one continuous image. Per-stroke undo stack
  (12 snapshots; assets stay outside project undo, like imports). Only faces
  whose usemtl matches the edited entry take paint. (c) **Duplicate** copies
  the open `.mtl` under a `-copy` name plus every referenced texture (once
  each, `<newbase>-<tex>` in place, map_Kd rewritten) so repainting the copy
  never bleeds into the original. (d) **New paintable texture...** in the
  texture combo: a blank pow2 PNG (64-512, fill color) written next to the
  `.mtl` via stb_image_write, assigned as map_Kd and Paint auto-enabled.
  **Verified** (Layer 2, GUI harness): scratch project with a UV'd cube .obj +
  128^2 grid texture; synthetic-input session confirmed - preview auto-landed
  on crate.obj, a red stroke appeared on the mesh and changed the PNG hash on
  disk ("Painted res/models/crate.png" status), Undo stroke reverted the hash,
  Duplicate produced crate-copy.mtl + crate-copy-crate.png with rewritten
  map_Kd, the grass texture brush painted a continuous checker pattern across
  faces, RMB-orbit worked mid-paint, and the New texture modal created a 256^2
  canvas and assigned it. Editor builds clean. Known UV property (documented,
  not a bug): faces sharing texels (the test cube maps all six faces to the
  same 0..1 square) all show a stroke painted on any one of them.
- (89) **VS Code extension for `.flownode` / `.screenfx` files (the "full deluxe
  package").** The two text formats a project uses for custom logic — custom
  flow nodes and custom screen effects — were plain text in VS Code. They now
  get a real language extension (`tools/vscode-tyra`, id `tyra.tyra-flownode`).
  Both formats share the same shape (a `key = value` header, a `---` line, then a
  C++ body with `{placeholder}`s), so the extension defines two languages over
  one design.
  - **Highlighting**: TextMate grammars colour the header (keys / enum values /
    `#` comments) and, after `---`, inject `source.cpp` (`contentName:
    meta.embedded.block.cpp`) for **full embedded C++** highlighting with the
    `{obj}`/`{num0}`/`{p0}`… placeholders overlaid. The body is a begin/end
    region that starts at `---` and never ends (`"end": "(?!)"`), so it runs to
    EOF regardless of what the C++ looks like.
  - **Language features** (`extension.js`, plain JS, no build step): diagnostics
    (unknown/duplicate keys, bad `string`/pin/`exec_out` enums, non-contiguous
    `num*`/`param*`, missing/empty body, `call = fn` with a stray inline body,
    unknown or undefined `{placeholder}`), hovers for every key/placeholder, and
    key/value/placeholder completion. A `SPEC` table drives all of it and is
    kept in sync with `src/flownode.cpp` / `src/screenfx.cpp`.
  - **Delivery**: `templates.cpp` now also emits `.vscode/extensions.json`
    (recommends the extension; written-if-missing in `refreshGenerated` so it
    never clobbers user recommendations, unlike the always-overwritten
    `c_cpp_properties.json`). The editor installs the extension by running
    `code --install-extension` on the **prebuilt `tools/vscode-tyra/*.vsix`**
    (committed to the repo, found next to the exe), once per session inside
    `App::openInVSCode`, plus an explicit **Custom nodes… ▸ Install VS Code
    extension** menu item; the outcome is reported in the status bar. New doc
    `docs/vscode-extension.md`; corrected the stale "there is no dedicated
    `.flownode` extension" line in `docs/custom-flow-nodes.md`.
  - **Install mechanism — a dead end and the fix.** The first cut *copied*
    `tools/vscode-tyra` into `~/.vscode/extensions/tyra.tyra-flownode-<version>`.
    It compiled and looked fine on the dev box only because the extension had
    *also* been installed there by hand via `code --install-extension` during
    verification — the copy path itself was never exercised. On a second machine
    it did nothing and printed nothing. Root cause, then confirmed empirically
    (`code --list-extensions` after a manual folder drop → not listed): **modern
    VS Code (≥ 1.74) loads only the extensions in its own manifest cache and
    ignores folders dropped into `~/.vscode/extensions`.** Fix: ship a prebuilt
    `.vsix` and install through the `code` CLI (which updates that cache), run
    synchronously so the real exit code drives a status message — no more silent
    failure. Lesson: verify the *product's* install path, not a hand-installed
    stand-in.
  - **Verified**: grammars tokenized with the real `vscode-textmate` engine —
    header scopes correct, unknown key → `invalid`, bad pin →
    `invalid.illegal.pin`, `---` region runs to EOF with placeholders overlaid;
    `extension.js` against a mock `vscode` module (17/17 diagnostics/hover/
    completion assertions, clean files = zero diagnostics); the **exact
    `cmd.exe /S /C "code --install-extension "<vsix>" --force"` string the editor
    builds** was run and confirmed to install + register the extension
    (`code --list-extensions` → `tyra.tyra-flownode`, exit 0), and the negative
    case (manual folder drop → not listed) proved the old path was broken; the
    editor builds clean (before and after merging `origin/main`); headless
    `--new` confirms `.vscode/extensions.json` is generated and valid. The C++
    wrapper runs that verified string via the same `CreateProcessA` mechanism
    `openInVSCode` already uses; the GUI button itself was not clicked headlessly.
- (93) **Options-menu editor: ready-made setting blocks + paged category menus.**
  Building an in-game options screen used to mean hand-wiring every row to a
  save value and a flow graph (the `video-modes` example does this by hand).
  The Menu Editor now scaffolds it directly. Two additions:
  - **Insert option block** (per-menu "+ Option block" popup): appends a fully
    configured stateful row bound to a built-in engine setting - **Music
    volume**, **Sound volume** (master SFX), **Controller deadzone**, **Aim
    response curve**, **Display mode** (480i/480p/1080i) and **Widescreen**
    (4:3/16:9). Each is an ordinary Toggle/Choice entry (restyle/relabel it like
    any other), backed by an auto-created save value, plus a new
    `MenuEntry::settingBind`. A **Bind** combo on any Toggle/Choice row exposes
    the same binding for manual use.
  - **+ Options menu** (menu list): scaffolds a whole paged options screen - an
    `OPTIONS` root whose rows open `AUDIO` / `CONTROLS` / `DISPLAY` submenus
    (Triangle backs out), each pre-filled with the matching blocks. Categories
    are plain submenus, so all existing styling (accent, fonts, images, layout)
    applies.
  - **How it drives the game:** codegen adds a `bind` column to `MenuEntryData`
    and a `TerrainGame::applyMenuBindings()` run every frame (both orbit and fpp
    loops, before `applyVideoRequests`) that maps a bound row's option index -
    held in its save value, so it persists and previews like any stateful row -
    onto the setting, spread evenly across the row's options (5 volume options
    -> 0/25/.../100 %; deadzone -> 0..0.4). Volume/deadzone/curve are idempotent
    (re-applied each frame); master SFX rides on `ScriptContext::sfxVolume`
    (0..100), multiplied into every Play Sound one-shot and sound-emitter sample.
    The deadzone block adds runtime globals `g_deadzoneL/R` (seeded in
    `buildScene` from the compile-time `ANALOG_DEADZONE_*` Preferences constants,
    so with no block the sticks behave exactly as before) that the shared
    `stickAxis()` reads. The **Aim curve** block reuses the per-stick response
    curve landed in parallel on main (entry 88, "Analog stick sensitivity
    curves"): it drives the same `g_stickCurve*`/`g_stickExp*` runtime globals
    the Set Stick Curve flow node uses (Linear / Smooth = S-curve / Precise =
    exponential), so there is one curve mechanism, not two. Display/widescreen
    rebuild VRAM + arm the keep-or-revert confirm, so they fire only on change
    through the existing `scriptCtx.requestDisplayMode`/`widescreen` path (seeded
    at boot so a saved choice does not re-switch the picture on startup).
  - **Verified**: editor builds clean; a scratch orbit project with an OPTIONS
    menu carrying all six block types round-trips through `--resave` (every
    `bind` string preserved); `--build` regenerates `menu_data.gen.hpp` with the
    correct `bind` column (music=1 .. widescreen=6, 0 on the non-stateful AUDIO
    row) and `terrain_game.cpp` with `applyMenuBindings` + the buildScene
    deadzone seed + the `scriptCtx.sfxVolume` scale + the curve mapping onto
    `g_stickCurve*`. The full PS2 toolchain compiled + linked the ELF in Docker,
    and it **booted in PCSX2**: the OPTIONS panel renders at a steady 50 FPS with
    the bound rows' current values shown (MUSIC/SOUND 100 %, DEADZONE Medium via
    the baked value strip), no assert in `bin/log.txt`, and no spurious display
    switch at boot (the applyMenuBindings-every-frame path is crash-free). Known:
    with many value-rows crammed into one menu the shared value strip hits the
    512 px texture cap and the overflow rows show no current-value label
    (pre-existing limit; the paged scaffold keeps each submenu well under it).
    (The pre-merge verification ran against a single stick-curve exponent; the
    curve block was then rebased onto main's per-stick curve and the editor
    rebuilt clean - the interactive re-check rides on the note below.) The
    **interactive path** - cycling a row with the dpad and hearing/seeing the
    volume / deadzone / display actually change - still needs a hands-on pad
    test by a human (established convention for pad-driven behavior). Docs:
    README "Game menus"/"Options menus" bullets, `tyra-editor-dev` skill (menu
    chain + applyMenuBindings). Follow-up worth proposing: a dedicated
    `examples/options-menu` demo project.
- (88) **Default projects folder (machine-global editor setting).** New Project
  used to always propose `~/TyraProjects` as the location. Now the folder is
  configurable in *Edit > Preferences > New projects* (a "Default folder" text
  field with Browse.../Clear), stored in `editor.ini` under `%LOCALAPPDATA%` as
  `defaultProjectsDir` alongside the emulator path / PS2 IP - a per-installation
  setting, not project data. The **New Project** modal re-seeds its Location
  field from this value every time it opens, so a mid-session preference change
  takes effect on the next `File > New Project` without a restart; an empty
  setting falls back to `~/TyraProjects` (`defaultNewProjectLocation`). Follows
  the documented machine-global-setting chain: `EditorConfig` field +
  load/save in `editor.ini`, an `App::globalDefaultProjectsDir_` member seeded
  at startup and funnelled through `saveGlobalConfig()`, staged in the prefs
  modal via `prefDefaultProjectsDir_`. **Verified** (Layer 1): editor builds
  clean with the new field; traced the round-trip by inspection - the value is
  written by `saveEditorConfig`, re-read by `loadEditorConfig` on next launch,
  and copied into `newLocation_` both at startup and on each modal open.
- (92) **Logo colour + Loading Screens window polish.** Follow-ups: (a) the
  splash logo's X read too cyan on the PS2 - `banner_data.cpp` regenerated with
  a cyan→azure-blue shift (green pulled down on clearly-bluish pixels, factor
  0.5; brightest X texel R0 G72 B254, was R0 G143 B253); (b) the Loading Screens
  preview was tiny - the preview strip grew from 200 to 360 px tall and the
  window default from 720x560 to 780x760, so the 512:448 preview is much larger;
  (c) trimmed the wordy boot-splash blurb (dropped the "Tyra logo always shows"
  note). Editor builds; game relinks (banner is engine data).
- (91) **Boot splash logo swapped to the TYRAX artwork.** The engine's Tyra
  splash (`vendor/tyra/engine/src/info/banner.cpp` + `banner_data.cpp`) drew a
  128x32 RGBA logo; replaced its embedded pixel data with `resources/tryaX.png`
  (576x190) converted to a 256x128 RGBA texture (fit-to-width on black,
  regenerated packed `R+G<<8+B<<16+A<<24` array - the file header notes it's
  generated, not hand-edited) and enlarged the splash sprite to 384x192,
  centered. Gotcha caught in PCSX2: `Sprite` defaults to `MODE_REPEAT`, so a
  sprite bigger than its texture **tiles** the logo (the old 128x32==texture
  size hid it); set `MODE_STRETCH`. **Verified** (Layer 3): booted in PCSX2 -
  the new white "TYRAX" + cyan X shows once, centered and crisp on black during
  the ~2s logo hold, then the splash/loading sequence proceeds as before.
- (90) **Boot splash screens (images) with configurable duration.** On top of
  (88)/(89): *Tools > Loading Screens* gained a **Boot splash screens** section
  (collapsing header) - a list of images shown in order at startup, **after the
  always-present Tyra logo and before the loading screen**, each for its own
  duration (0.1-10 s). Images only for now (studio / "presents" cards). New
  `SplashScreen` model (a `HudImage` + bgColor + duration, reusing the HUD
  import/pow2-quant bake), `Project::splashScreens`, JSON save/load, texbake +
  ISO-startup registration, a `SPLASHES[]` table in `inc/loading_data.gen.hpp`,
  and `loadingscreen::renderSplash`. Splashes are **independent of the
  loading-screen master toggle** - they always play when defined. The boot state
  machine grew a phase 0 (step through splashes by frame-counted duration, one
  `renderSplash` per frame) before the existing load phase; both run from the
  loop so each splash is shown for its full vsync-paced time (same reason the
  boot loading screen had to move out of `init()`). Editor: add / reorder
  (*Move up*/*Move down*) / delete, per-splash image (import + `hudBakeControls`),
  duration, background color, size/position. **Verified** (Layers 2-3): codegen
  emits the `SPLASHES` table (fullscreen size, GS-range bg, seconds); the PS2
  game compiled; a boot with two 1.5 s splashes booted in PCSX2 and
  window-screenshots caught the sequence **Tyra logo → splash image (a teal
  "PRESENTS" card, full-screen, 50 FPS) → loading screen (bar + segments) →
  terrain**, in that order. Exact per-splash seconds confirmed from the code
  path (`everyFrames` + vsync), not a stopwatch (PCSX2's window is capturable
  only partway through boot). Editor interactions (the new section's buttons)
  not click-tested - no synthetic input while the user is at the machine.
- (89) **Loading screen at boot: Tyra logo (~2s) → loading screen → scene.**
  Follow-up to (86): the loading screen fired on scene switches but was
  invisible at the very first boot - you saw the Tyra logo, then the scene. Two
  causes. First, the initial `loadScene(0)` ran inside `init()`, before the main
  loop; frames presented there aren't vsync-paced (`endFrame` only waits on
  vsync once `isFrameLimitOn` is honored under the running loop), so the boot
  loading frames flashed by faster than the display. Second, the engine's splash
  (`banner.show`) drew the Tyra logo for just 2 frames and relied on framebuffer
  persistence, so its on-screen time was incidental. Fixed both: the first scene
  now loads from the loop's new boot state machine (`bootPhase`/`bootFirstScene`,
  which also moved the scripts' `init()` there so `onStart` still sees scene 0),
  so its progress bar is vsync-paced and visible; and `vendor/tyra`'s
  `banner.show` now re-renders the logo in a COP0-timed (~2s) loop - re-drawing
  matters because `beginFrame` clears, and a *rendering* loop is frame-limited so
  real time tracks wall time (an earlier no-draw busy-wait raced ahead of the
  display and finished in a fraction of a second - the dead end that proved the
  vsync/`graph_wait_vsync` dependency). Boot with loading screens off is
  unchanged (scene loads on the first frame). **Verified** (Layer 3, PCSX2 SW
  renderer): a 256-terrain-chunk boot scene, dense window-screenshots - caught
  the Tyra logo, then the loading screen (dark background, baked "LOADING", the
  full cyan bar and five lit orange segments) at 50 FPS, then the terrain, in
  that order; the boot loading screen never appeared before this change. Note:
  the ~2s logo hold is now engine-wide (every generated game), and PCSX2's window
  only becomes capturable partway through the hold, so the exact 2s was confirmed
  from the code path (COP0 rate + `graph_wait_vsync`), not a stopwatch on the
  screenshots.
- (88) **Loading screen editor: user-defined loading screens with real progress
  bars.** The old "loading screen" was a single project-global bool that flashed
  a hardcoded 256×64 `hud/loading.png` on black for ~0.7s per scene switch (and
  nothing at boot — a black screen). Now *Tools > Loading Screens* authors
  **named** loading screens, each with a background color, image + baked-text
  elements (the HUD pipeline, reused verbatim) and **progress bars** — continuous
  (track + growing fill) or quantized (N segments lighting up one per 1/N step,
  as colored rects or an optional PNG tinted on/off). Scenes pick a screen in
  *Scene > Preferences* (empty = the project default; `defaultLoadingScreen`),
  and with none defined the built-in `loading.png`-on-black fallback is shown —
  so existing projects are byte-for-byte unchanged. `ProjectSettings::loadingScreen`
  stays the master enable. New model: `LoadingBar` + `LoadingScreenDef` (project.hpp,
  reusing `HudImage`/`HudText`), `Project::loadingScreens`/`defaultLoadingScreen`,
  `SceneData::loadingScreen` (added to `operator==` so undo of the per-scene
  assignment works). The **progress is real**: `loadScene` in the generated game
  was refactored to count its work up front (streamed assets + objects + in-view
  terrain chunks via a new `countPendingChunks`), pump a loading-screen frame
  every ~1/24 of the way through the asset drain / object rebuild / batched
  terrain build, and the same screen now also covers the boot `loadScene(0)` and
  the 0.7s scene-switch hold. The shared `loadingscreen::renderFrame` (lazy-init,
  in the game-cpp prolog) draws bars as tinted quads over a shipped white 8×8
  sprite (`res/hud/loading-white.png`) — GS colour modulation (128 = 1.0), the
  same trick as `sequences::renderOverlay`. Codegen emits `inc/loading_data.gen.hpp`
  (element tables + a scene→screen map resolved with `loadingScreenIndexFor`);
  loading images + segment textures register in the texbake `hudBake` map,
  loading texts bake under screen-index-mangled names (`text-ls-<i>-<name>.png`),
  and all of it joins the ISO startup group. Editor: the *Loading Screens* window
  (screen list / element stack / property editors / 512×448 preview with a
  *Preview progress* slider), a *Scene > Preferences* screen combo, and an *Open
  Loading Screens editor* button in Project Preferences; like the other
  project-wide preset collections it saves immediately (outside undo).
  **Verified** (Layers 0–3): editor builds clean; `--new` + a hand-authored
  `.tyra` regenerated `loading_data.gen.hpp` with correct tables (GS-range bar
  colours 0.2→25.6/0.8→102.4, background 0..255, mangled text path, `LS_DEFAULT`
  + per-scene map); the full PS2 game **compiled and linked** in Docker; a
  two-scene project that ping-pongs every 2s via *Every N Seconds → Switch Scene*
  booted in PCSX2 (software renderer) and window-screenshots caught the loading
  screen rendering exactly as authored — dark-blue background, baked "LOADING"
  text, the cyan continuous bar full and all five orange quantized segments lit —
  at a steady 50 FPS with no assert, confirming the init()-time frame presentation
  (the one runtime unknown) works. The editor window itself wasn't click-tested
  (no synthetic input while the user is at the machine); its data path is the same
  JSON the hand-authored fixture exercised.
- (88) **Analog stick sensitivity curves (per stick, live-settable from the
  flow graph).** The stick handling only had a per-stick deadzone (feature 31);
  above it the response was linear. Now each stick carries a **response curve**
  applied after the deadzone rescales the magnitude to 0..1: **Linear**,
  **Exponential** (`pow(mag, exp)` - gentle near center, snappy at the edge, the
  classic aiming curve) or **S-Curve** (smoothstep - soft center + firm cap),
  with an **exponent** (>=1) tuning curves 1/2. Independent per stick (left =
  movement, right = camera).
  - **Model / serialization**: `ProjectSettings` gains `stickCurveL/R` (int) and
    `stickExpL/R` (float), in `operator==`, saved/loaded in `project.cpp`
    (defaulting to Linear / exp 2 on older projects, clamped on load).
  - **Preferences UI** (*Project > Preferences > Input*): a curve combo +
    exponent slider per stick with a live **PlotLines preview** of the response
    (deflection → output), under the existing deadzone sliders.
  - **Codegen**: `terrain_config.hpp` bakes `STICK_CURVE_L/R` + `STICK_EXP_L/R`;
    a single shared `stickAxis(raw, dz, curve, exp)` free function (deadzone +
    curve) replaces the duplicated `axis` lambda / `axisValue` in both player
    paths (`updatePlayerEntity` and the FPP `updatePlayer`), reading runtime
    globals `g_stickCurve*/g_stickExp*` seeded from the constants in `init()`
    (namespaced constants can't initialize the global-scope defs directly).
  - **Flow graph**: a new **Set Stick Curve** node (Player category) writes the
    curve/exponent for the left / right / both sticks through `FlowScriptCtx`;
    the game loop copies it into the runtime globals and resets the request, so
    a change persists across scene switches (options menu / sniper mode / etc.).
  - **Verified**: editor builds clean; codegen inspected on fresh `--new` fpp
    and orbit fixtures (constants, globals, shared `stickAxis` in both paths,
    scriptCtx apply block); an `On Start → Set Stick Curve` graph compiles and
    the emitted `flow_graph.gen.cpp` sets both sticks; **both templates build on
    the PS2 toolchain in Docker**; the fpp ELF **boots in PCSX2 at a steady
    50 FPS with no assert** (the node fires on start); save/load round-trips the
    four fields and the new node type via `--resave`. The actual on-hardware
    *feel* of each curve with a real DualShock still wants a hands-on pad test.

- (86) **Custom screen effects: user-authored full-screen post effects, drop-in
  files, positioned on the UI.** The editor shipped exactly two full-screen post
  effects (bloom, film grain), hard-coded in the engine's `RendererCorePostFx`.
  Now a project can add its own — **per project, no editor rebuild** — as a
  `screen-effects/*.screenfx` text file, the screen-effect analogue of custom
  flow nodes. There are no pixel shaders on the PS2, so an effect is written
  **low-level** (raw GS framebuffer blits, the same way bloom/grain are), and it
  is **positioned in the UI Editor screen stack** like the built-ins.
  - **Engine** (`vendor/tyra`): `RendererCorePostFx::applyCustom(build, user)`
    wraps the exact frame-state setup/teardown + DMA kick `apply()` uses and
    calls a user build callback that appends GS primitives; `blit()`/`flatQuad()`
    are now public, plus accessors for the framebuffer geometry, the shared
    noise texture, the two quarter-res scratch buffers and a PRNG.
    `RendererCore::applyCustomPostFx` fronts it with the same PATH1 drain barrier
    as `applyPostFx` so the pass composites over finished 3D.
  - **Editor**: `src/screenfx.{hpp,cpp}` load `.screenfx` files (manifest: title
    + up to four numeric-param sliders; `---`; raw C++ body with `{p0}..{p3}`
    placeholders) into a `customScreenEffects()` registry, mirroring
    `flownode.cpp`. A `Project` holds `ScreenFxPlacement`s (key + stack `layer` +
    `enabled` + params); placements whose file is missing on load are dropped
    (like unknown flow nodes). The **UI Editor** screen stack gained N reorderable
    `[ FX: … ]` entries alongside the bloom/grain markers (generalized
    `buildStack`/`rebuild`), a properties panel (enable + param sliders + jump to
    file), and a management block (add to stack / new starter / reload from
    folder). Codegen emits `src/scripts/screen_fx.gen.{hpp,cpp}` (one
    `screenFx_<n>` build callback per enabled placement, params baked as a local
    array) and injects `applyCustomPostFx` calls into both the ORBIT and FPP
    frame loops at each effect's slot (in-loop for a concrete layer, after the
    loop for topmost).
  - **Verified** (codegen + full PCSX2 e2e, orbit scratch project with a
    "Color Wash" effect at the top of the stack, params R=0.1/G=0/B=0.4,
    amount 0.6): the editor builds clean; `screen_fx.gen.cpp` emits
    `customFx`-style `screenFx_0` with the params baked (`0.6F, 0.1F, 0.0F,
    0.4F`) and `terrain_game.cpp` calls `applyCustomPostFx(&screenFx_0, nullptr)`
    after the HUD loop; the Docker build compiled the new engine API + the
    generated effect in the PS2 toolchain (`screen_fx.gen.o` linked into the
    ELF) and booted in PCSX2. A/B screenshots: with the effect **on**, the whole
    frame is washed toward blue (the default green checker terrain reads
    blue-gray, the sky darkens toward the tint); with it **off**
    (`enabled:false`), the effect is dropped from codegen entirely (no
    `applyCustomPostFx`, no `screenFx_0`) and the scene renders the plain green
    terrain / blue sky. Docs: new `docs/custom-screen-effects.md` (format, the
    GS draw API + blend cheat-sheet, packet budget, "file must travel with the
    project", limitations), plus README / docs index / both dev skills. Not
    built (documented as follow-ups): per-scene param overrides, flow-graph
    "Set <effect> param" runtime control, and custom VRAM / uploaded textures.
    The **editor UI was driven with synthetic clicks** (Tools > UI Editor):
    screenshots confirm the `[ FX: Color Wash ]` entry sits in the screen stack
    between the pinned USE prompt and the built-in film-grain / bloom markers,
    and selecting it shows the properties panel with the four manifest sliders
    reading the placement's values (Amount 0.600 / Red 0.100 / Green 0.000 /
    Blue 0.400) plus Jump-to-file / Remove-from-stack. Fine-grained
    drag-reorder and live slider dragging weren't scripted; the stack model and
    param round-trip they drive are the same paths the load/codegen exercised.
- (87) **Missing assets no longer kill the game - they degrade gracefully.** A
  failed asset load used to be a fatal assertion (game stops). Now the engine
  recovers and keeps running: a **missing/empty texture** returns a visible 8x8
  magenta-and-black checkerboard placeholder (`makePlaceholderTexture` in
  `png_loader.cpp`) instead of trapping on `fopen`; a **missing music WAV** is
  skipped (`audio_song.cpp` `load()` returns early, `play()` no-ops when nothing
  loaded) so the game runs on in silence. The fork's other loaders were already
  non-fatal - `LeanObjLoader`/`TanmLoader`/`TskLoader` return `nullptr` on a
  missing file and the generated game null-checks (`if (!mesh) return; // renders
  nothing`), `AudioAdpcm::load` already returns `nullptr` + `tryPlay` guards - so
  models, animations and one-shot sfx already skipped cleanly; textures and music
  were the last two fatal spots reachable from generated games. (Legacy
  `md2_loader` / TinyObjLoader `obj_loader` still assert, but the editor's games
  use LeanObj + tanm/tskl, never those.) The failures are still surfaced: a new
  **`TYRA_SOFT_ERROR`** macro (`debug/debug.hpp`, marked `Modified by
  tyra-editor`) logs the *same* delimited `====== TYRA ======` block a fatal
  assert does - so the editor's tail/parse path catches it identically - but with
  a `Non-fatal error (game keeps running)!` header and **no halt / no screen
  takeover**. The editor's error dialog reads that header and switches wording
  from "hit an assertion and stopped" to "reported an error but kept running"
  (`drawErrorModal`), still copyable, still flashes the window. **Verified**
  (Layer 3): rebuilt `libtyra` with the engine changes (compile-checked in the
  PS2 toolchain after fixing a first-cut `getTextureSize` scope error - it's a
  `TextureLoader` member, so the placeholder computes `w*h*4` for bpp32 inline);
  booted `error-test` in PCSX2 with `hud/use.png` deleted - the game **rendered
  the full scene at 50 FPS** (previously it halted) and `bin/log.txt` held the
  `Non-fatal error … using a placeholder` block at `png_loader.cpp:92`; the host
  parser harness extracted that exact 304-byte block and confirmed the
  `Non-fatal` marker the dialog keys off. (One gotcha worth remembering: a failed
  engine build leaves the *old* `libtyra.a` in place and the checksum marks the
  sources "synced", so the next `--build` links the stale lib and looks like the
  change did nothing - force it by `rm`-ing `/tyra/engine/bin/libtyra.a` in the
  compiler container.)

- (86) **Errors no longer crash full-screen: engine halts quietly, the editor
  catches them.** A failed `TYRA_ASSERT` / `TYRA_TRAP` in the running game (e.g.
  a missing texture: `TYRA_ASSERT(file != nullptr, "Failed to load ", …)` in
  `png_loader.cpp`) used to seize the whole screen with the kernel debug console
  (`init_scr()` + an infinite `scr_printf` loop in `TyraDebug::trap`) - one
  missing PNG blew the game away with a full-screen dump. Now the trap keeps
  writing the assertion to the console / host `log.txt` (unchanged) and then
  halts quietly with `for(;;) SleepThread()`, leaving the last frame on screen;
  the upstream on-screen dump is gated behind a new `Tyra::Info::drawAssertScreen`
  (default **off**, marked `Modified by tyra-editor` in `info.{hpp,cpp}` and
  `debug/debug.hpp`) for standalone hardware debugging with no console attached.
  The editor now tails the game log every frame (`App::pollGameError`,
  throttled 0.5 s) - `bin/log.txt` for PCSX2 runs, the `[ps2]` runner-log stream
  for network deploys - and on a newly seen assertion raises a **copyable** error
  dialog (`drawErrorModal`, read-only selectable dump + Copy) and pulls the
  editor to the foreground + flashes its taskbar entry
  (`glfwRequestWindowAttention` / `glfwFocusWindow`) since PCSX2 holds the
  foreground when the game dies. It parses the stable
  `====== TYRA ======` … `==================` delimiters
  (`extractLastTyraAssert`), dedupes by block text (`errorSeenSig_`) but
  **forgets that signature when a log source shrinks** - the Runner deletes
  `bin/log.txt` before every launch, so a new run drops the size to 0 and an
  *identical* error (same missing file, same line) pops again on the next run
  instead of being deduped against the stale text; the size is baselined on
  project attach so a stale dump present at open neither pops nor looks like a
  shrink. (A first cut deduped purely by text and re-baselined at each build/run
  - it missed the second, identical run's error; fixed with the shrink reset.)
  A **debug toggle** silences the
  dialog: `EditorConfig::errorPopup` (default on, persisted in `editor.ini`),
  flipped from a "Pop up on errors" checkbox in the Debug window or an "Only log
  to console" checkbox in the dialog itself - off = errors go only to the console
  / Debug window. **Verified** (Layer 3 + Layer 2): editor builds clean; a scratch
  `fpp` project built in Docker (engine change compiles in the PS2 toolchain -
  `libtyra` rebuilt, ELF linked) and booted in PCSX2 fine (terrain+sky, 50 FPS);
  deleting the boot-loaded `hud/use.png` then relaunching the ELF produced the
  assertion in `bin/log.txt` (`Failed to load … use.png … png_loader.cpp:61`)
  while the screen **kept the last frame** (Tyra logo, FPS 0, EE idle) instead of
  the kernel dump; a host harness ran `extractLastTyraAssert` over that real log
  and 4 synthetic cases (no-assert, two-blocks-last-wins, partial-block, `[ps2]`
  line-prefixed) - 10/10 checks passed. The live GUI modal pop (assert arriving
  while the editor is running a build/run) wasn't automated - no synthetic input
  while the user is at the machine - but the detection + parse path it depends on
  is exercised above.
- (87) **Merge-friendly project format: one file per object + collaboration
  scaffolding (steps 2-4, building on the ids in entry 86).** The whole point of
  the ids was this: the monolithic `<name>.tyra` is now a **manifest** that
  keeps project-wide data (settings, hud, menus, sequences, gradings, ambience,
  save data, editor state) and, per scene, an **ordered list of object ids** -
  each object's body moved out to its own `objects/<id>.json` file (Unreal-5
  One-File-Per-Actor style). Two people editing *different* objects now touch
  *different* files, so git merges with zero conflict; the only residual
  collision is two people *adding* an object to the same scene at once (a
  one-line conflict in that scene's id list). The dir is flat (not per scene)
  because ids are project-global - a scene rename never moves a file and an
  object can change scenes without touching its body. Order is preserved by the
  manifest list (it is significant: first Player / SpawnPoint wins, draw order);
  the object array is not reorderable in the UI, so the list only ever gets
  appends/removes. `save()` writes every live object then prunes
  `objects/*.json` whose object no longer exists; `load()` dispatches per scene -
  an `objects` array of *id strings* loads the split files, an array of *object
  bodies* is the legacy inline format (so old projects and the committed
  examples keep loading unchanged and migrate to split on the next save /
  `--resave`). The history file stays monolithic (inline bodies) - it is
  gitignored churn, not the tracked source of truth. **Collaboration
  scaffolding (Level 2):** because a real game map is its own repo (separate
  from this editor's `examples/`), `create()` now drops a `.gitattributes` and a
  `COLLABORATION.md` into every new project. `.gitattributes` marks the files
  that *cannot* be auto-merged - `terrain-*.heights` and the imported `res/`
  assets - as `lockable` so a team can `git lfs lock` them before editing (uses
  only LFS's lock registry, no LFS storage/server; inert until `git lfs install`,
  so it never breaks a non-LFS workflow). Both are written once at creation and
  never regenerated (not in `refreshGenerated`'s lists), so users can edit them.
  **Verified** (Layer 0/1, headless, no Docker needed - `--resave` exercises the
  full load+save round-trip): `--new fpp` writes a manifest with `"objects":
  ["<id>"]` and an `objects/<id>.json` body, plus `.gitattributes` +
  `COLLABORATION.md`; a legacy inline `.tyra` (no `objects/` dir) migrates to the
  split layout on `--resave` and a second `--resave` is byte-identical (stable,
  no churn); a stray `objects/deadbeef.json` is pruned on the next save; an
  `empty`-preset project round-trips with `"objects": []`; a copy of the
  `script-demo` example (3 objects incl. a flow graph) migrates to 3 object files
  with the graph intact. Codegen is untouched - the in-memory `Project` is
  identical whichever layout it loaded from, so no example needed regenerating.
  Next (optional): deterministic key ordering in the manifest for even smaller
  diffs, and a live/real-time collab layer (CRDT) if the team ever outgrows
  lock-based coordination.

- (86) **Stable object ids (step 1 of the merge-friendly project format).**
  Groundwork for multi-user collaboration on one project: today every edit
  touches the single monolithic `.tyra` file and objects are identified only by
  their array position, so two people editing the same scene collide badly in
  git. `SceneObject` gained an opaque `id` (16 hex chars from a 64-bit random
  value) that is generated once and never changes, even across renames - a
  stable merge/persistence key. Object *references* (flow graphs, sequences,
  layers) still resolve by name; the id only exists to give the coming
  file-split + merge machinery something stable to anchor on. `id` is the first
  key in each object's JSON (and part of `SceneObject::operator==`, so undo
  captures it). A single choke-point `project::ensureObjectIds()` stamps a fresh
  unique id on any object that lacks one and reissues accidental duplicates; it
  runs on `create`, at the end of `load` (before the caller snapshots for undo -
  so pre-id projects migrate transparently) and inside `commitChange()` (so
  freshly inserted / pasted objects get ids before they hit an undo snapshot or
  disk). Paste clears the copied id so a paste is always a new identity. Added a
  headless `--resave <projectDir>` (load + save) as the one-shot way to migrate
  an existing project to the new format without the GUI. No codegen change - ids
  are editor-side persistence metadata, never emitted into the game, so the
  example projects did not need regenerating (they gain ids the next time they
  are opened and saved, or via `--resave`). Note: opening a *pre-id* project with
  an existing `.history` discards that undo history once (the id-less snapshot no
  longer equals the freshly-migrated state - the normal stale-history path), then
  a fresh history is written. **Verified** (Layer 0/1, headless): built clean;
  `--new … fpp` writes a player object whose JSON starts with a 16-hex `id`;
  stripped the id from that `.tyra` to simulate a legacy project, ran `--resave`
  and confirmed an id was stamped, then ran `--resave` again and confirmed the id
  was byte-identical (stable, no churn). Paste id-clearing + the multi-object
  dedup path are code-reviewed but not GUI-automated (no synthetic input while
  the user is at the machine). Next steps toward multi-user: deterministic
  minimal-diff serialization, then splitting the `.tyra` into one file per object
  (OFPA-style), then LFS-lock tooling for the non-mergeable assets.

- (86) **Editor application icon.** The editor exe/window used the default blank
  Windows icon. Added `resources/icon.ico` (multi-size 16→256, generated from the
  new `resources/icon.png` brand mark via PIL) and `resources/app.rc`, wired into
  CMake behind `WIN32` with `enable_language(RC)` and a `-I resources` flag so
  windres finds the `.ico`. The resource is deliberately named `GLFW_ICON`: GLFW's
  Win32 backend loads a resource by exactly that name into the window class
  (`vendor/glfw/src/win32_window.c`), so this doubles as the runtime title-bar and
  taskbar icon with no C++ code, and — being the exe's only icon — is what Explorer
  shows for the file. **Verified**: clean reconfigure + build compiles the RC object
  and links; `[System.Drawing.Icon]::ExtractAssociatedIcon` on the built exe returns
  the blue T✕ mark (32×32), confirming the icon is embedded under the right name.
- (84) **examples/script-demo: drop the dangling `house-1` model reference.**
  The scene's `house-1` object referenced `res/models/house.obj`, but the
  example ships no model assets (its `res/` and `.res-baked/` hold only a
  `.gitignore`, and there was no `res/models/` at all), so the model was a
  dangling reference the example could never build. The house was never part of
  the documented story either — README (repo + example) only promises "walk up
  to the orange box, press X, the sky changes color." Removed the stray object
  from `script-demo.tyra` and regenerated the committed generated files via
  `--build examples\script-demo`: `scene_data.hpp` drops from 3 objects to 2
  (`SCENE_OBJECT_COUNTS`/`SAVE_OBJECT_MAX` now 2), and `model_data.gen.hpp` now
  has `MODEL_COUNT = 0`. **Verified end-to-end**: the Docker build now completes
  (`=== Build OK ===`, exit 0) and produces a full `bin/script-demo.elf`
  (2.4 MB scene binary), where before the missing asset left the example
  unbuildable. No `house` references remain anywhere in the example tree.
- (83) **Layers panel: stop the delete button overlapping the size readout.**
- (85) **Documentation review pass: closed feature/example gaps and added the
  first editor screenshots.** A full read-through of the docs surfaced several
  gaps against `tyra-docs`: the README's feature list had **no Animated models
  entry** despite `docs/animated-models.md` being the largest guide, and its
  *Example projects* section **omitted `large-terrain` and `object-spawning`**
  (both already had good READMEs). Added the animated-models feature bullet, a
  new *Documentation* section linking the `docs/` index, the two missing example
  bullets, and a `camera-takes.md` entry in `docs/README.md` (it was referenced
  from the README but not indexed). Wrote the missing `examples/script-demo/README.md`
  (the only example without one). Captured the **first three editor-UI
  screenshots** with the GUI harness (`.claude/skills/tyra-testing/scripts/screenshot-window.ps1`,
  DPI-aware GDI capture + synthetic click/scroll/pan helpers) and embedded them:
  `docs/img/editor-overview.png` (README hero, cutscene-demo scene),
  `docs/img/flow-graph.png` (the custom-nodes graph, in `custom-flow-nodes.md`)
  and `docs/img/cutscene-director.png` (the dopesheet, in the cutscene-demo
  README). *Verified* by launching the editor GUI on each example project and
  eyeballing every capture; the docs were re-read after editing. **Two findings
  worth recording:** (a) the checked-in `build/tyra-editor.exe` was **stale** —
  it predated the *Tools > Cutscene Director* menu item (`app.cpp:688`), so the
  flagship window was missing until a rebuild; screenshotting current UI needs a
  fresh `build.ps1`. (b) `examples/script-demo` has a **dangling model
  reference**: `house-1` points at `res/models/house.obj`, but unlike the other
  examples script-demo ships no `res/` assets (only `res/.gitignore`), so it
  likely can't build — flagged as a separate task, the new README documents only
  the working box/sky interaction. Screenshot scope was editor-UI-only per the
  request (no Docker/PCSX2 in-game shots this pass); the UI-scale nuance (the
  flow-graph node layout is authored for 1× and overlaps at the user's 2.5×
  scale, so that shot was taken at 1× + canvas zoom) is captured in the
  editor-gui-screenshot-harness memory.

- (84) **Layers panel: stop the delete button overlapping the size readout.**
  Each layer row lays out `[eye] [name input] [start] [N | X.X MB] [x]` on one
  line, but the name `InputText` reserved a fixed `-118px` on the right while the
  real right-hand content — the "start" checkbox + the variable-width
  "N | X.X MB" readout + the "x" `SmallButton` — runs wider than that, so the
  MB text and the delete button drew on top of each other (visible as
  `start 20 |x0.` with the size clipped). Now the row measures the actual
  widths up front (`CalcTextSize` on the formatted count string, "start", and
  "x", plus `ItemSpacing`/`FramePadding`) and passes that sum as the negative
  `SetNextItemWidth`, so the name field shrinks to exactly fit and the readout +
  button always sit clear; the "x" button is anchored at `ContentRegionMax.x`
  minus its measured width instead of a hardcoded 22px. The per-row object count
  is now computed once and reused for both the reservation and the label.
  **Verified** (GUI A/B): built clean, opened a scratch project with four
  auto-streamed layers (forest/village/ruins/weather) and window-screenshotted
  the editor — the readout reads in full (`start 0 | 0.0 MB`) with the `x`
  flush right and no overlap, versus the pre-fix build where the same rows
  clipped the MB under the button.
- (83) **Edit > Preferences: machine-global editor settings (emulator path +
  dev-PS2 IP).** The PCSX2 path and the ps2link IP are per-machine, not
  per-project (the emulator lives at a fixed path on this PC; the dev PS2 has a
  fixed LAN address), yet they used to be stored in each `.tyra` under Project >
  Preferences - so moving a project to another machine carried a wrong path.
  They now live in the existing global editor config (`editor.ini` under
  `%LOCALAPPDATA%\tyra-editor`, alongside UI scale and navigation), edited in a
  new **Edit > Preferences** modal. The Edit menu is enabled without a project
  open so the emulator can be pointed at before creating one. `EditorConfig`
  gained `emulatorPath`/`ps2LinkIp`; all `saveEditorConfig` sites funnel through
  a new `App::saveGlobalConfig()` so a UI-scale or nav change never drops them.
  `Project::emulatorPath`/`ps2LinkIp` stay only as the Runner's runtime
  transport (not part of undo, no longer serialized to `.tyra`); `attachProject`
  copies the global values in on every open and **migrates** any legacy value
  from an older `.tyra` into the global config on first open (the reader still
  accepts the old fields; the writer no longer emits them). Project Preferences
  lost its Emulator / Real PS2 sections; every "Set … in Project > Preferences"
  hint (menus, toolbar tooltips, Debug window, Runner log) now points to Edit >
  Preferences. **Verified** (GUI + headless): built clean; `--new` writes a
  `.tyra` whose `editor` block no longer contains `emulatorPath`/`ps2LinkIp`;
  hand-injected legacy fields (`C:\legacy\pcsx2-qt.exe`, `192.168.1.77`) into an
  old-style `.tyra`, launched the editor on it, and confirmed `editor.ini` gained
  `emulatorPath=C:\legacy\pcsx2-qt.exe` / `ps2LinkIp=192.168.1.77` - backslashes
  and the IP round-tripped intact - with UI scale / nav lines untouched. The
  interactive Save in the modal wasn't automated (no synthetic input while the
  user is at the machine); the migration path exercises the same load/save code.

- (82) **View menu: toggle the distance-fog preview in the editor.** The
  scene's distance fog (Preferences/Ambience > Distance fog — the GS hardware
  fog that fades geometry toward the camera far plane, *not* the particle "Fog"
  emitters) is now suppressible in the viewport without touching the scene's
  own fog settings or the generated game. *View > Preview > Distance fog* (on by
  default) flips a session-only `App::showFog_`; both `setFog` call sites
  (`applyProjectToViewport` and the Ambience Editor live preview) `&& showFog_`,
  and the menu item re-runs `applyProjectToViewport()` so it takes effect
  immediately. Editor-only, like the TV-safe-frame overlays — not persisted in
  the `.tyra` file. **Verified** (Layer 0 + GUI A/B): built clean, launched on a
  scratch fpp project with fog forced bright-cyan / start 3 / end 22; with the
  toggle on the terrain fades into cyan and distant boxes vanish, with it off
  the terrain shows its natural material and the far geometry stays visible
  (screenshots `fog-on.png` / `fog-off.png`). The interactive menu click itself
  wasn't automated (no synthetic input while the user is at the machine); the
  A/B swapped the default instead.

- (71) **Phone camera takes — record a real 6DoF camera move on an iPhone
  (ARKit) and import it as Cutscene Director camera keys.** "Walk around a
  room looking around" becomes a PS2 cutscene camera move; the PS2 runtime
  needed zero changes (imported takes are ordinary free camera shots).
  **Module** (`src/camtake.hpp/.cpp`, new — no ImGui deps, links into host
  harnesses like project/templates): `CamTake` (samples: time, position in
  meters, quaternion, optional FOV; canonical space = the ARKit convention,
  right-handed Y-up, camera looks local −Z — which matches the game world's
  axes, so mapping is scale/yaw/origin only). Deliberate two-stage split for
  phase 2 (live Wi-Fi/USB pose streaming): *acquisition* (loaders → CamTake)
  vs *bake* (`bakeCamTake`: pure function of take + mapping, callable on a
  growing buffer). **Loaders:** CamTrackAR `.hfcs` (FXhome iPhone app; plain
  XML read with a minimal in-file XML subset reader — positions are HitFilm
  "pixels" / 2.8352 / 1000, orientation eulers negated back and applied ZYX,
  FOV from the zoom channel `2·atan(0.5·H/zoom)`, times = frame/FrameRate,
  semantics from FXhome's official Blender importer) and an app-agnostic CSV
  (`t,px,py,pz,qx,qy,qz,qw[,fov]` — spec in `docs/camera-takes.md`, a direct
  ARKit `camera.transform` dump). **Bake:** eye = origin + yaw(pos−pos₀)·scale,
  look-at 2 m ahead along the sample's view direction (roll has no
  representation in eye/look-at keys and is dropped), then time-parameterized
  Ramer–Douglas–Peucker decimation over the (eye, look-at) curve with a
  world-units tolerance — the PS2 tables in `sequences.gen.cpp` grow with
  every key, so a 60 Hz take must shrink ~100×. All keys linear easing; FOV =
  take average. **UI:** "Import take..." (button by the Cutscene Director
  transport + the camera-lane context menu) → file pick → modal with
  scale/yaw/origin (defaults to the preview camera, "From view")/
  start-at-playhead/tolerance controls, a live samples→keys readout,
  replace-vs-append radio; one `commitChange()` per import; camera track
  auto-enabled, sequence duration extended to fit. **The empirical
  gotcha** (the reason the axis mapping was tested against a real take): the
  Blender importer's `axis_conversion(from_forward='Z', ...)` suggests
  HitFilm cameras look +Z, but the script assigns the rotation to a *Blender*
  camera, which films down local −Z — so the decoded orientation is already
  the canonical −Z-forward rotation, no flip. First build used the +Z reading
  and PCSX2 showed only sky: the take pitched 37–65° *up*; with −Z it pitches
  37–65° *down* (a phone picked up off a desk — the only physically possible
  reading, a face-down phone can't track). Verified: a scratch harness
  (linked against the editor's objects) loads the real user-recorded take
  (`sample-take.hfcs`, 395 samples @ 60 Hz, 6.57 s, FOV 56.4°) → **12 keys**
  at the default 0.05 u tolerance with the interpolated path staying within
  tolerance of every original sample (also: a synthetic walk-+X-looking-+X
  CSV take comes out walking +X and decimates to exactly 2 keys; a 20 s
  60 Hz circle-walk take with sinusoidal look-around → 96 keys, error bound
  holds; scale/yaw/origin/time-offset mapping asserts; a hand-written
  mini .hfcs pins px→m, FOV and the identity orientation). E2E: injected the
  baked take + landmark boxes + OnStart→Play Sequence into a scratch project
  (`%TEMP%\tyra-editor-test\camtake`), `sequences.gen.cpp` stayed at 13 KB
  (12-key table), Docker build OK, **PCSX2 booted it** and screenshots at
  three moments show the camera panning across the boxes/terrain along the
  handheld path. The import modal itself compiles + is wired, but its
  click-through (file dialog → sliders → Import) still needs a hands-on
  human pass.

  **Import into a chosen Camera entity + post-import adjust** (user request).
  The import modal's new **Import as** selector targets either *free camera
  shots* (as before) or a **Camera entity**: the take is baked into that
  entity's transform track (position = eye, rotation = the Euler whose +Z lens
  points along the recorded view — `seqEulerFromForward`, the exact inverse of
  the runtime `seqCameraForward`), the entity's FOV is set from the take, and a
  bound camera-lane key is added so it dollies along the path. So **two cameras
  in one scene each carry their own recording** and you cut between them on the
  camera lane. No PS2 runtime change — an entity track filmed by a bound shot
  already runs on hardware (cutscene-demo's cam-dolly). After importing, the
  take + mapping stay loaded and an **Adjust imported take** section (Start
  point / Start yaw / Scale) re-bakes the same target in place, so the whole
  path can be re-positioned and re-oriented without re-importing.
  `App::applyCamTake()` is the shared apply used by both Import and the live
  adjust. Verified: editor builds clean; a host harness loads the **real**
  sample `.hfcs` (395 samples → 15 keys, FOV 56°), bakes it and confirms the
  Euler round-trip `seqCameraForward(seqEulerFromForward(dir))` matches the
  recorded view within **0.02°** across every key, with the first eye landing
  exactly on the chosen origin — so a bound shot films precisely along the
  take. The modal + adjust widgets compile and mirror the verified free-import
  flow; their click-through still wants a human pass.

  **Preview polish** (user request). (a) The camera you preview *through* now
  hides its own model + FOV frustum, so its body no longer sits on the near
  plane and fills the frame: `Viewport::setHiddenCameras()` skips them in both
  the scene pass and the frustum loop, driven from the two preview paths (the
  single look-through camera, or every camera the active cutscene shot films
  from). (b) **Space** toggles play/stop while the Cutscene Director is focused
  (gated on `!WantTextInput && !IsAnyItemActive` so it doesn't double-fire with
  a focused button or fight a text field). Verified: editor builds clean and
  runs on cutscene-demo with the three cameras still drawing normally in
  "View: Free" (no regression); the hide-on-preview and the Space toggle are
  interactive, so their live feel wants the user's hands-on pass.

  **Three fixes** (user report). (a) **Preview off now still animates:**
  `cutscenePosedObjects()` gated *all* posing on `seqPreview_`, so unchecking
  "Preview in viewport" froze the scene. Posing is now unconditional while the
  Director is open on a sequence; `seqPreview_` only gates driving the viewport
  camera + the bars/fade overlay - so with it off, a Camera entity still dollies
  along its track and you watch from a free vantage point. (b) **Keyframe
  dragging works:** the dopesheet's per-lane hit rectangle was submitted before
  the key diamonds without `SetNextItemAllowOverlap()`, so ImGui's
  `ItemHoverable` blocked the keys (`HoveredId` claimed by the lane) and they
  never became active - dragging did nothing. Marking the lane allow-overlap
  lets the diamonds on top catch the mouse (drag to retime, as the cursor +
  tooltip already advertised). (c) **Ctrl + mouse wheel zooms the timeline**
  over the dopesheet (mirrors the flow-graph zoom), consuming the wheel so it
  doesn't also scroll; the Zoom slider tooltips it. Verified: editor builds
  clean and runs on cutscene-demo; the three are interactive, so their live
  feel wants the user's pass.

  **"From view" now aims, not just positions** (user request). The take-import
  "From view" button (import modal + Adjust section, and the on-load default)
  set only the mapping origin; now `App::takeOriginAimFromView()` also sets the
  mapping yaw = viewHeading − `camTakeInitialYawDeg(take)`, so the recorded
  path's first sample looks where the editor camera looks - frame the shot in
  the viewport, hit From view, the take is aimed there. Verified with a host
  harness on the real sample `.hfcs`: for five chosen view headings the baked
  first key's heading matches the target to **0.000°**.

  **Two import fixes** (user report). (a) **Scale was ~8× too small:** the
  `.hfcs` px→m conversion divided by `PixelsPerMM·1000` where FXhome's own
  Blender importer *multiplies* by `PixelsPerMM/1000` — so a real metre walked
  came out a few centimetres in the game. Now `meters = px * 2.8352 / 1000`
  matching FXhome, so *Scale* 1 ≈ 1 unit per real metre; the sample take's
  travel goes from ~0.14 m to a plausible **1.13 m**. (b) **Sudden 180/360
  whip after import:** baking a take into a Camera entity's rotation track
  emitted Euler angles that wrap at ±180, so a pan crossing 180° (170 → −175)
  made the linear rotation interp spin the long way. The baker now unwraps each
  Euler channel to stay continuous with the previous key — a harness on the
  real take confirms the max consecutive delta drops from a possible ~345° to
  **14.5°**. Both verified by host harness; editor builds clean.

  **Camera shots always bind to a Camera entity** (user request - the free vs
  bound choice muddied "what do I use?"). The camera lane no longer authors
  "free" shots (a snapshot of the editor view): the key inspector's *Shot
  from* is a cameras-only picker, adding a shot ([+] / double-click / menu)
  binds to the active camera (looked-through → selected → the scene's only
  camera) and no-ops with a hint when the scene has none, and *Import take* is
  *Into camera* (the free option removed, Import disabled until a camera is
  picked). So the workflow is: place + aim Camera entities in the scene, then
  the lane just says which camera films when. Legacy free keys from older
  projects still play (the runtime keeps the stored eye/look-at fallback) and
  show as diamonds, but the UI nudges you to bind them. Editor builds clean;
  interactive, so the feel wants the user's pass.

  **cutscene-demo upgraded to a full showcase** (all shots now camera-bound to
  match the model). Grew from 3 to **5 camera entities**; the two ex-free
  shots became `cam-hero` (low angle) and `cam-crane`, and `cam-crane` now
  **cranes on its own object track** (pos + rot, staying aimed) — so the demo
  has *two* moving cameras (dolly + crane), a Step-cut montage plus one
  **Smooth blend** (cam-hero → cam-crane), and a new `obelisk` **scale +
  colour** track (it swells gold→orange) alongside the existing pos/rot and
  visibility tracks — every object-track channel is now exercised. Bars use a
  visible 0.6 s/1.0 s slide. Verified: headless Docker `--build` of the
  hand-edited `.tyra` compiled (JSON + codegen valid) and **PCSX2 ran it at 50
  FPS** — screenshots show the orange swollen obelisk, firing sparks, risen
  hero and cinema bars.
- (79) **Tool windows scale their layout with the UI scale (fix 250% clipping)** —
  the floating Tools windows (Menu Editor, Material Editor, Color Grading,
  Ambience, UI Editor, Disc Layout, Cutscene Director) and the modal dialogs
  baked their widget widths, child-region sizes, `SameLine`/`Indent` offsets and
  preview sizes as raw pixel literals. `applyUiScale()` scales fonts (`FontScaleMain`) and style
  spacing (`ScaleAllSizes`) but not code literals, so at 250% (a 4K laptop the
  user actually runs) combos and inputs were too narrow for the 2.5×-tall text —
  "apply a preset…" showed "apply a", "256 px" showed "25(", the entry action
  combos clipped to "Close"/"Continu", and the panel preview was a postage stamp.
  Added `App::scaled(px)` (= `px * uiScaleApplied_`) and routed every literal
  layout size in those windows through it (window `SetNextWindowSize`, the
  `##*_list` child widths, all `SetNextItemWidth`, absolute `SameLine`/`Indent`,
  the menu 1:1/TV preview sizes and clamps, the disc table column widths, and the
  short-label dialog buttons `ImVec2(120/140, 0)`); `gradingWheel()` took a
  `scale` param so the two Resolve-style trackballs scale too. The Cutscene
  Director (which merged in from main mid-PR) got the full treatment including
  its hand-drawn dopesheet canvas - the lane height / ruler height / label-column
  width and the per-element offsets (key-diamond radius + hit padding, playhead
  triangle, pinned-label text positions, channel-letter spacing, tick-label
  crowding threshold) all route through `scaled()`. This finishes the job the UI
  Editor had already done inline (its 3 sites now use the helper).
  Verified at the user's real 250% scale by scripting the GUI (DPI-aware screen
  capture + synthetic clicks/scroll): the Menu Editor shows every field in full
  and a properly sized baked-panel preview (MENU / Continue / Sound·Off /
  Difficulty·Low / Start Game); Color Grading renders its quick-look buttons and
  both hue wheels side by side; the Cutscene Director lays out its whole
  transport row, dopesheet (ruler ticks, "[*] Camera (2)" lane label, key
  diamonds + retime tooltip, playhead) and Key inspector without clipping.
  Editor builds clean.
- (79) **StaPip partial-frustum cost: save points tessellated as detail-16
  boxes (9.2k verts) + pooled packager arrays** — follow-up to the PCSX2
  observation from the usable-highlight session: even with highlighting off,
  the showcase spent 12-14 ms of EE per frame inside `StaPipCore::render`'s
  PARTIALLY_IN_FRUSTUM branch (~160 EE-clipper calls) on views with the save
  shrine + trees near the camera. Measured first (recipe from the clipbench
  notes: COP0 counters in the engine around the partial/full branches,
  `packager.create` and clip/cull submissions; owned `terrain_game.cpp` in a
  scratch copy of examples/showcase with a deterministic orbit camera parked
  between the campfire and the shrine; PERF line per 50 frames to
  host:perf.txt; PCSX2 software renderer). **Baseline at the worst angles:
  66 ms/frame (15 FPS), 36-38 ms in the partial branch = ~285
  `packager.create` calls producing ~1340 packages (16-17 ms of allocation +
  per-package bbox classification) plus ~385 EE-clipper submissions
  (19-20 ms), ~12 partial bags/frame — nearly all of it one object.** Root
  cause: `SceneObject::primDetail` defaults to 16 and **SavePoint** was never
  made "box-like" when (61)'s follow-up gave Box its 1/16 default/cap — but
  the generated game builds SavePoint geometry through the
  `default: addBox(...)` case, so every save shrine silently tessellated
  16x16 per face = 3072 tris / **9216 verts** (while the editor viewport drew
  it as a plain box - the preview never showed the cost). Fix (a), editor:
  SavePoint is box-like across the chain — `primDetailIsBoxLike()` in
  project.hpp (default 1, clamp 1..16, box triangle readout), the viewport
  draws it from the box mesh cache honoring detail, and the Detail slider now
  shows for SavePoint. Old `.tyra` files carry no `"detail"` key for save
  points (16 was the emit-suppressed default), so they all load as 1; a
  shrine's baked point-light gradient flattens — set Detail explicitly if the
  look is wanted (it is authorable now). Fix (b), engine:
  `StaPipBagPackager::create` returns pointers into two grow-only pools
  (bag-level / split-level — the parent array is alive during a split;
  callers no longer `delete[]`, absent sts/colors/normals are nulled against
  stale reuse), `renderSubpkgs`' two index vectors are static, and the
  per-textured-bag `RendererCoreTextureBuffers` heap alloc is a stack struct.
  Measured attribution on the same orbit: **pooling alone is worth only ~2%**
  of the partial branch (create cost is the bbox classification math, not the
  allocator); the detail fix does the rest — **after both, every 50-frame
  window locks 50 FPS (20.0 ms), partial branch peaks at 8.5 ms avg / 9.6 ms
  max, EE pre-endFrame <= 10.6 ms, clipper calls max ~93/frame** over 3 full
  orbits; windows matching the original report (~160 calls) now run ~5-6 ms
  with ~60 calls. Verified: SW-renderer screenshots healthy mid-orbit (trees,
  rock, portal, shrine with its usable rim; no pooling artifacts; 50 FPS /
  100% speed / EE ~38%); examples/showcase regenerated and re-verified with
  the final de-instrumented engine. Perf logs archived in
  `%TEMP%\tyra-editor-test\perf-run{1,2,3}*.txt`.
- (81) **Usable-highlight opacity control** — a per-scene `highlightOpacity`
  (0..1, *Preferences > Usable objects* + per-scene override) setting the alpha
  of the **strongest (innermost) shell**; the outer shells keep fading from it
  (×0.55). Replaces the hardcoded `72/100` start alpha in both
  `renderHighlightHull` and `buildHighlightApron` with `HIGHLIGHT_OPACITY *
  128` (128 = PS2 opaque max), so opacity 1 + 1 step = a fully solid outline
  and low values dial the wash down — the natural knob for the overlay mode's
  intensity. Default 0.56 preserves the old 4-step look. Full
  model→JSON→UI→codegen chain (`HIGHLIGHT_OPACITIES` per-scene table +
  `HIGHLIGHT_OPACITY` macro; operator== + save/load + clamps). **Verified in
  PCSX2 (SW)**: overlay showcase at opacity 0.25 renders a visibly fainter
  surface glow than the 0.56 default, 50 FPS unchanged; all five examples
  regenerated + Docker-built.

- (80) **Debug frame profiler (shippable) + experimental highlight overlay** —
  two follow-ups to the highlight perf work (79). **(a) Frame profiler**: the
  ad-hoc COP0/HUD instrumentation used to diagnose the highlight cost is now a
  real debug option. *Project > Preferences > Build* (debug profile) gains
  **Show frame profiler** next to Show FPS / Show memory; the generated game
  draws a per-phase EE-time breakdown (whole FRAME wall-clock / SCENE / HL /
  PART, avg ms over ~1s) in the top-left. `renderScene` brackets its phases
  with EE COP0 `mfc0 $9` reads into file-scope counters (`g_profScene/
  Highlight/Particles`), drawDebugHud averages + prints them; every read is
  behind the `DEBUG_SHOW_PROFILER` constexpr so a release build (or the option
  off) contains none of it. HL is the highlight *overhead* only — the deferred
  bodies are timed into SCENE. New pref `ProjectSettings.showProfiler`
  (operator== + JSON save/load + `{{DEBUG_SHOW_PROFILER}}` codegen). **(b)
  Highlight overlay**: experimental per-scene `highlightOverlay` (Preferences >
  Usable objects + per-scene override, "Draw over object") — the shells drop
  the eye-pushback (`k = 1`), so each grown shell sits at the object's own
  depth and its front faces land just in front of the surface: the glow paints
  ON the object and fades outward into a rim, instead of only a rim behind the
  silhouette. renderScene draws the overlay body in the main pass (not
  deferred) and paints the shells over it; rim mode keeps the deferred-body
  order from (79). New `SceneObjectData`-style per-scene table
  `HIGHLIGHT_OVERLAYS` + `HIGHLIGHT_OVERLAY` macro; full model→JSON→UI→codegen
  chain (project + per-scene override). **Verified in PCSX2 (SW renderer)**:
  showcase with debug profile + both options on — profiler HUD reads
  `FRAME 22.00 SCENE 12.90 HL 1.46 PART 0.99` at 50 FPS (deterministic orbit
  near the save shrine); overlay screenshot shows the shrine washed in a yellow
  surface glow instead of an outline. Profiler-off / overlay-off path (all five
  examples, regenerated) compiles to ELFs in Docker — both constexpr branches
  build on the PS2 toolchain. Documented the profiler + the manual deep-dive
  technique in `docs/profiling.md`. Real-hardware timing unchanged from (79).

- (79) **Usable-highlight perf, round 2: low-detail shell proxy + deferred
  body draw (no repaint)** — the PR #64 rims still dropped the showcase to
  ~25 FPS in PCSX2 near a highlighted object. Measured with COP0 phase
  timers in an owned terrain_game.cpp plus temporary counters inside the
  engine's StaPipCore (deterministic camera orbit around the save shrine,
  250-frame A/B windows, on-screen HUD readout): the highlight pass alone
  was 25 ms of EE time, all of it in the PARTIALLY_IN_FRUSTUM branch of
  `StaPipCore::render` — the shrine is a default-detail-16 box (~9.2k
  verts), a near object always straddles the frustum, and the effect
  submitted that mesh 5 extra times per frame (4 shells + repaint), each
  submit re-running subpackage classification + the EE clipper (~370 clip
  calls/frame vs ~160 baseline). DMA waits, the bbox cache and the z-test
  mode were all measured innocent. Fix, all in the generated runtime
  (templates.cpp): (1) shells now draw a **low-detail proxy** built by
  `buildHighlightProxy` — primitives re-emit through their own builders
  with subdivision forced down (boxes/planes/decals: detail 1 — identical
  silhouette; curved: capped at 12 segments), models concatenate their
  parts into one array (one submit per shell instead of per part); cached
  in ObjectGeometry, cleared by rebuildObjectGeometry, own bbox stamp;
  (2) highlighted-in-reach usables are **deferred out of the main pass**
  (`highlightInReach`, ≤8 per frame, sorted far-to-near) and their body
  draws once AFTER their shells — the body erases the shell wash over its
  own receding faces exactly like the old repaint did, so the second full
  draw of the mesh is gone; nearer bodies still cover farther rims, and
  everything else keeps drawing before any rim. **Verified in PCSX2 (SW
  renderer)**: same instrumented A/B on the rebuilt showcase — highlight
  render cost fell from ~23 ms to ~1.7 ms per frame (scene+highlight
  sums 15.7 vs 14.0 ms; whole frame 56 -> 25 ms on the worst-case orbit
  view), rim/apron/USE visuals unchanged by screenshot comparison against
  the pre-fix build. All four examples regenerated. Real-PS2 A/B still
  pending, same as PR #64. Left open: the partial-frustum path itself
  costs the base scene ~12-14 ms on that view (default primDetail 16
  everywhere is heavy) — separate backlog item.

- (69) **Cutscene Director — a keyframe timeline sequencer (cinematic cutscenes
  on the PS2)** — the editor's first full animation-authoring tool. A
  **Sequence** is a project-wide keyframe timeline that poses scene objects
  **and the game camera** over time; it is authored by scrubbing a playhead and
  snapshotting poses, previewed live in the viewport, compiled to a PS2 runtime
  player, and fired from the flow graph. **Model** (`src/sequence.hpp`, new):
  `Sequence` (name, duration, loop, cameraEnabled) holds `SeqTrack`s (each binds
  one object by name + per-channel flags pos/rot/scale/color/visible + a list of
  `SeqObjectKey` full-pose keyframes) and a camera track (`SeqCameraKey`:
  eye + look-at + FOV). Each key carries an easing (0 linear / 1 smoothstep /
  2 step-hold) for its outgoing segment; the shared `seqEase`/`seqSample`
  helpers are the single source of truth used by both the editor preview and
  the emitted PS2 code. Sequences are project-wide like the grading/ambience
  presets — persisted through `save()`, not part of undo/redo. **UI** (Tools >
  Cutscene Director): sequence list + a timeline with duration/loop/camera
  toggles, Play/Pause/Rewind transport, a playhead scrubber, per-object tracks
  (object combo, channel checkboxes, "Set key from object @ playhead", editable
  key list with easing + Go/Delete) and a camera track ("Set camera key from
  view @ playhead"). **Viewport scrubbing:** `cutscenePosedObjects()` poses a
  copy of the active scene's objects at the playhead with the exact
  `seqSample` interpolation the console runs and hands it to `render()`; a new
  `Viewport::setCameraOverride(eye, target, fov)` flies the preview camera along
  the camera track, and `currentCamera()` reads the orbit camera back out to
  snapshot a camera key. **Codegen:** a new global Script
  `src/scripts/sequences.gen.cpp` (+ `inc/scripts/sequences.gen.hpp`) compiles
  the keyframe tables (object names resolved to (scene, runtime object index)
  here) and, each frame a sequence is active, writes `ctx.objects[i].data.*` +
  `dirty` and — for a camera track — a new `ScriptContext` camera override
  (`cameraOverride`/`cameraEye`/`cameraAt`) that both game loops apply to the
  frame camera right before `beginFrame`. Playback advances by the real frame
  dt (fixed wall-clock speed PAL/NTSC). **Flow graph:** new **Play Sequence**
  (SequenceName param) and **Stop Sequence** nodes (category Scene) compile to
  `sequences::play(index)` / `sequences::stop()`; a `FlowParamKind::SequenceName`
  combo lists the project's sequences, and renames/deletes remap the nodes like
  the grading/ambience presets do. `refreshGenerated` always overwrites the two
  new `.gen` files. Verified: editor builds clean (Layer 0); a save→load
  round-trip harness (linked against the built objects) round-trips a sequence
  with object + camera tracks, mixed easings, loop and camera flags
  byte-for-byte through `operator==`; a scratch fpp project with a `hero` box
  (OnStart→Play Sequence "Intro") and an Intro sequence (hero pos/rot track +
  a 2-key camera track) generated the expected tables
  (`kS0T0K`/`kS0Tracks`/`kS0Cam`, `sequences::play(0)`, the loop's
  `if (scriptCtx.cameraOverride) { cameraPosition = ...; }`); the full Docker
  build compiled + linked all generated sources (`sequences.gen.cpp`,
  `flow_graph.gen.cpp`, `terrain_game.cpp`) into `cutscene.elf` (Layer 3), and
  **PCSX2 booted it** — two screenshots seconds apart show the cutscene camera
  framing the red cube from a keyframed angle and, later, from a different angle
  with the cube risen along +Y (both the camera track and the object track
  animating live, looping, at 50 FPS). The timeline UI compiles clean and
  mirrors the verified Color Grading / Ambience tool windows; a hands-on mouse
  pass over the timeline (drag-scrub feel, per-key editing) still wants a human
  (synthetic clicks don't drive the ImGui menus).

  **Second pass — the director grows into a real cinematics tool** (same PR).
  **(a) Dopesheet UI:** the tracks/keys widget lists became a custom
  ImDrawList dopesheet — one lane per object track plus a camera lane, keys as
  draggable diamonds (click select, drag retime with 10 ms snap, right-click
  easing/delete, double-click a lane to drop a key at that time), a click/drag
  scrubbed adaptive time ruler with a zoom slider, a playhead line with
  grabber, a pinned label column ([+] = snapshot @ playhead, right-click = the
  track-setup popup with the object combo + channel checkboxes) and a
  selected-key inspector below (time/easing plus channel-gated pose fields, or
  the camera-shot editor). Key fills encode the outgoing easing; entity-bound
  shots draw as circles. **(b) Camera entity:** `PrimitiveType::Camera = 14`
  (`+ Add object > Gameplay > Camera`) — a film-camera body marker plus a
  GL_LINES FOV frustum wedge (+Z lens, scaled by tan(fov/2) so the wedge shows
  the true shot; `unitCameraBody`/`unitCameraFrustum` in viewport.cpp), a
  `cameraFov` property (20-110 deg), invisible/non-colliding in the game but a
  full `RuntimeObject`. Camera-track keys are now *shots*: free (stored
  eye/at/fov) or **bound to a Camera entity by name** — bound shots film from
  the entity's CURRENT pose at runtime (`ctx.objects`), so keyframing the
  entity in an object track makes a dolly/crane move; the entity's authored
  pose + FOV are baked at codegen as the fallback for a non-active scene.
  Renaming an object now remaps track/shot references (`objRenameFrom_`), the
  way layer renames do. **(c) Real FOV + shake + skip:** the director applies
  the blended shot FOV to the actual PS2 projection
  (`renderer.core.renderer3D.setFov`, frustum planes recompute) and restores
  the pre-cutscene FOV on end/stop/skip; per-key `shake` interpolates a 3-band
  sine handheld offset (`seqShakeOffset`, mirrored EE-side); a `skippable`
  sequence ends early on START (`pad.getClicked().Start`). The new cleanup
  path also fixes a first-pass bug: `ctx.cameraOverride` was never written
  back to false, so the game camera stayed frozen after a cutscene ended.
  **(d) Widescreen bars + fades:** per-sequence mask styles (Cinema 2.39:1,
  Wide 16:9, Pillarbox, Frame — fractions from `seqBarsFractions`, one source
  for editor preview + codegen) slide in/out over 0.4 s, plus fade-from/to-
  black times; drawn on the PS2 as stretched solid-black sprites (new built-in
  `res/hud/seq-black.png`, 8x8 opaque; the sprite alpha carries the fade) by
  `sequences::renderOverlay()` after the HUD and under the pause menus, and
  previewed 1:1 as ImDrawList rects over the viewport image. New
  `ScriptContext` fields: `barsStyle`/`barsAmount`/`fadeAlpha`. Verified:
  editor builds clean (Layer 0) and opens a scratch project whose viewport
  renders both camera frustum wedges (GUI screenshot); the generated
  `sequences.gen.cpp` inspected (bound shots resolved to (scene,obj) with
  baked fallbacks + entity FOVs 75/35, bars fraction 0.22106, skip/fade
  fields, `renderOverlay`) (Layer 2); the Docker build compiled it all with
  `-Wall` into `cutshow.elf` and **PCSX2 ran the 8 s looping cutscene**:
  screenshot measurement shows the visible image at **2.40:1 inside the 4:3
  frame** (cinema bars exactly at the baked fraction), two distinct
  entity-bound shots (wide 75 deg vs tele 35 deg) with the cube translating
  AND rotating from its track, and center brightness 87.7 (no fade) → 34.7
  (partial) → 1.1 (full black) proving the fade compositor blends (Layer 3).
  Still for a human with a pad: START-skip, shake feel in motion, and the
  dopesheet drag ergonomics.

  **Workflow pass** (user feedback from hands-on use, same PR). **(a) No more
  blind posing:** while playback is paused, SELECTED objects are exempt from
  the preview posing in `cutscenePosedObjects()` — previously a tracked
  object snapped back to its interpolated pose every frame, so dragging the
  gizmo at a new playhead time was blind. Now the gizmo edits what you see;
  bound camera shots read the posed copy, so aiming a selected Camera entity
  updates its shot live. **(b) Auto-key:** a transport checkbox; finishing a
  gizmo drag drops a keyframe at the playhead for every selected object with
  a track in the selected sequence (`cutsceneAutoKey()`, running just before
  the drag's `commitChange()` so the keys share the drag's undo snapshot; the
  snapshot logic moved from a window-local lambda into
  `cutsceneSnapshotObjectKey()`). **(c) Add-track picker:** "+ Add object
  track" no longer silently targets the first object (usually the player) -
  it opens a popup with **Add selected (N)** (one track per selected object,
  already-tracked ones skipped) and the full object list with tracked entries
  disabled; a fresh track immediately gets a starting key at the playhead
  from the object's current pose. **(d) Look-through camera:** a "View:"
  control in the viewport corner (and a "Look through" button in a Camera
  entity's Properties) renders the viewport from any Camera entity - live
  pose + FOV, via the same `setCameraOverride` path - with "Free camera" one
  click away; the Cutscene Director camera preview takes precedence while
  active, renames remap the reference, deleting the entity falls back to the
  orbit camera. Also made key retiming discoverable: a horizontal-resize
  cursor + tooltip on keyframe hover and a slightly larger hit box (the drag
  itself already existed). Verified: full rebuild links clean in a side
  build dir (the user's editor instance held the main exe lock) and the
  post-merge Docker build of `examples/cutscene-demo` compiled the merged
  codegen (HUD texts + video modes from main composited under the cutscene
  bars overlay); the interactive feel of all four changes needs the user's
  hands-on pass.

  **Configurable bars slide** (user request, same PR). The widescreen bars
  used to slide in/out over a hardcoded 0.4 s; now each sequence carries
  `barsSlideIn` / `barsSlideOut` (seconds, default 0.4, authored right next
  to fade in/out and only shown when bars are on). 0 = the bars snap to full
  coverage at the first frame / stay until the last one; larger = a slower
  reveal. `seqBarsAmount()` takes the two times (the single source both the
  editor preview and the emitted PS2 player call), the `Seq` codegen table
  gained the two floats, and the runtime envelope reads them instead of the
  0.4 constant (`kSeqBarsSlide` -> `kSeqBarsSlideDefault`). Projects authored
  before this default the two to 0.4 on load, so they look unchanged.
  Verified: editor rebuild links clean; the regenerated cutscene-demo
  `sequences.gen.cpp` shows the `Seq` row carrying `0.4F, 0.4F` (the
  backward-compat default, since the example predates the fields) and the
  runtime envelope gated on `s.barsSlideIn`/`s.barsSlideOut`; the Docker
  build compiled the widened struct clean. Exact slide timing on-screen
  (vs the already-measured 2.40:1 bar coverage) wants a human eye.

  **Example project:** `examples/cutscene-demo` — a 14 s cutscene ("The
  Reveal") exercising every director feature at once: three Camera entities
  (one of them dollied by an object track), Step-easing hard cuts, shake,
  per-shot FOV (65/90/45/55/60), Cinema bars, fade in/out, skippable, an
  On Start auto-play plus an On Used replay from a usable pedestal, and a
  sparks emitter switched on mid-scene through a visibility track. Verified
  by a Docker build from the checked-in folder (exit 0) and a PCSX2 run:
  screenshots caught the letterboxed dolly shot mid-travel and the low-angle
  finale with the hero ascending, and after the cutscene ended the camera
  handed back to the FPP player with the aftermath intact (hero aloft,
  sparks running) — the release path live. Same versioning shape as
  layer-streaming (bin/res gitignored, regenerated on build).

- (78) **"Open in VS Code" jumps to a file (scripts + custom nodes)** —
  `App::openInVSCode` gained an optional `file` arg: it now runs
  `code "<projectDir>" -g "<file>"`, opening (or reusing) the whole-project
  workspace AND landing on a specific file, so IntelliSense resolves against the
  project. Wired up: the Flow Graph **Custom nodes…** popup drops "Open
  flow-nodes folder" for **Open in VS Code (flow_nodes.hpp)** plus a **Jump to
  node file** submenu (each custom node opens its own `.flownode`, via the
  registry's stored `sourceFile`); the Project **Scripts** list entries became
  clickable — clicking one opens that `src/scripts/*.cpp` in VS Code. On the
  "should we ship a `.flownode` IntelliSense extension" question: no — a
  `call = fn` body already gets full C++ IntelliSense because it lives in a real
  project header (`flow_nodes.hpp`) covered by the generated
  `.vscode/c_cpp_properties.json`; the `.flownode` stays a thin manifest.
  Documented the VS Code options + that rationale in `docs/custom-flow-nodes.md`.
  Verified: editor builds clean; `code "<proj>" -g "<file>"` confirmed to launch
  VS Code with the file; GUI screenshot shows the reworked popup (Open in VS
  Code / Jump to node file) over the example graph.

- (77) **examples/custom-nodes + "propose an example" doc rule** — a focused
  per-feature demo for the custom flow nodes (75)/(76): an FPP scene with three
  crates where **Cross** runs a C++-backed node (`flowNearestVisible` in the
  project-owned `inc/scripts/flow_nodes.hpp`) whose runtime **object output**
  feeds a built-in **Hide Object** (hides crates one at a time), and **Square**
  runs an **inline-snippet** node that spins a crate — covering both flavors and
  the runtime-object-ref feature end to end. Shipped with its own README and
  listed in the top-level README examples. Also added a `tyra-docs` rule:
  proactively propose a small `examples/` demo when a feature is large/
  user-facing enough that someone would want to see it in action. Verified:
  `--build` exit 0 (game compiled to ELF); the generated flow graph shows the
  guarded `ctx.objects[objOut2].visible = false` and `rotation[1] += 45.0F`;
  hit a real footgun first — a comment containing the literal phrase "Generated
  by tyra-editor" in the owned `flow_nodes.hpp`'s first line tripped the
  ownership check and got the file regenerated, fixed by rewording; PCSX2 boots
  the ELF and the F8 snap shows the three crates (red near, green mid, blue far)
  on the terrain. Pad interaction (Cross/Square) not hand-driven; codegen + boot
  are the bar.

- (75) **Custom flow-graph nodes (per-project, file-based)** — a project can now
  define its own Flow Graph **action** nodes without touching the editor's C++.
  Each node is a `<project>/flow-nodes/<name>.flownode` text file: a `key = value`
  header (`title`, `category`, `string = none|text|object`, `num0..3` labels), a
  `---` line, then a raw C++ body emitted verbatim into `flow_graph.gen.cpp` when
  an exec link fires the node, with `{obj}`/`{self}` (object indices),
  `{num0..3}`/`{int0..3}` and `{str}` placeholders substituted at build. New
  `src/flownode.cpp` scans the folder into a global registry
  (`customFlowNodes()` in flowgraph.hpp, `unique_ptr` for stable `FlowNodeType`
  char* addresses); `flowNodeType()` and a new `flowAllNodeTypes()` fold the
  custom nodes into the existing lookup, add-menu and category derivation, so a
  custom node renders and edits exactly like a built-in one (object dropdown /
  text field / drag params, `> do` exec-in). Codegen gets one `flowCustomNode()`
  branch in `actionCode()`. **Load order matters:** `readFlowGraph` drops nodes
  whose type is unknown, so `project::load` registers the folder *before* parsing
  graphs — which is also why custom nodes live in files, not the `.tyra`: copying
  the `.flownode` file (its name is the node identity, `custom:<stem>`) is how you
  move a node to another project; forget the file and its nodes vanish on load.
  Editor UI: **Flow Graph ▸ Custom nodes…** popup (reload folder, scaffold a
  commented `example.flownode`, open the folder). The `.tyra` model/serialization
  is unchanged (nodes already carry an arbitrary `type` string + `str`/`num`).
  Docs: new `docs/custom-flow-nodes.md` (format, placeholders, transfer
  instructions), README + docs index + tyra-editor-dev source map/chain notes.
  Verified end-to-end: editor builds clean; a scratch fpp project with two custom
  nodes (an `object`-kind "Nudge Up" and a `text`-kind "Announce") wired to
  On Start round-trips through `project::load` and generates the expected
  `ctx.objects[0].data.position[1] += 5.0F;` and
  `for (int i = 0; i < 3; ++i) TYRA_LOG("hi there");` — and the whole game
  **compiled to an ELF in Docker** (`=== Build OK ===`), proving the emitted C++
  is valid. GUI screenshot confirms both custom nodes render with their params /
  pins in the Flow Graph and the Custom nodes… control is present.

- (76) **Custom flow nodes, part 2: real C++ bodies + full runtime I/O** —
  extends (71) so a custom node isn't limited to an inline snippet. A
  `.flownode` manifest can now set `call = fn`, binding the node to a function
  the user writes in a new marker-owned `inc/scripts/flow_nodes.hpp`
  (`void fn(ScriptContext&, FlowNodeIO&)`), plus declare `in`/`out` pins (any of
  object/position/bool/text) and `exec_out`. The node is exec-driven: on run the
  codegen builds a `FlowNodeIO` from the resolved inputs, calls the function, and
  **latches** its outputs into per-node class members (`objOut<id>`,
  `boolOut<id>`, `posOut<id>[3]`, `textOut<id>`) that downstream nodes read.
  bool/text/position outputs drop straight into the existing expression planes
  (they're just variable refs); the interesting one is **object output**, which
  is a *runtime* value (e.g. "the object the player is looking at") — so
  `resolveTarget()` was changed from returning a codegen-time `int` to a C++
  int-*expression* string (a literal index for built-in sources, `objOut<id>`
  for a custom node's output), letting a custom node's picked object drive ANY
  consumer including built-in Hide/Move/Show Object. Built-in object actions fed
  such a ref are bounds-guarded (`isRuntimeIdx`) so an invalid pick (-1) is a
  no-op, not an out-of-range access. `exec_out` custom nodes fire their
  downstream inline right after they run (via a new recursive `emitExec` with a
  cycle guard, replacing `linkedActions`), which sequences the data dependency
  (raycast runs → sets output → built-in reads it). `FlowNodeIO` lives in
  `script.hpp`; `flow_graph.gen.cpp` includes `flow_nodes.hpp`. The UI needed no
  changes — the flow-graph editor already renders every pin kind from the
  `FlowNodeType` flags. The `.tyra` model/serialization is still unchanged.
  Scaffolded example is now a working C++-backed "Nearest Object" node
  (`flowExampleNearest`) demonstrating object-out → built-in Hide Object.
  Docs: `docs/custom-flow-nodes.md` rewritten (two flavors, the `FlowNodeIO`
  contract, runtime object refs, transfer now also copies the `flow_nodes.hpp`
  function), README/docs-index/tyra-editor-dev updated. Verified end-to-end: the
  editor builds clean; a scratch fpp project wiring **On Button → Nearest Object
  (custom, object out) → built-in Hide Object** (object input from the custom
  node's runtime output, exec-chained) round-trips through load and generates
  `flowExampleNearest(ctx, io); objOut2 = io.objectOut;` then
  `if ((objOut2) >= 0 && (objOut2) < ctx.objectCount) { ctx.objects[objOut2].visible = false; }`
  — and the whole game **compiled to an ELF in Docker** (`=== Build OK ===`). The
  part-(75) inline-snippet project still generates the same literal-index code
  (no regression from the `resolveTarget` refactor). GUI screenshot confirms the
  custom node renders its object-output and exec-output pins and Hide Object
  reads "Object: from id link". Not hand-tested with a pad in PCSX2 (the graph
  only fires on Cross); the ELF compile + codegen are the verification bar here.

- (70) **Dynamic object spawning: Spawn Object / Despawn Object flow nodes** —
  runtime clones of authored objects, the missing piece for GTA-style traffic
  (spawn the same few templates around the player, despawn what fell behind).
  **Spawn Object** (Object category) clones its target (object link > name >
  self) into a pool slot past the authored objects: the clone starts at the
  linked position (or the template's own), faces the Yaw param, and the
  node's **object output is the CLONE**, not the template - the first
  runtime-valued object reference in the flow graph. Codegen assigns each
  Spawn Object node a global handle (`flowSpawned[k]`, -1 = none, reset with
  the script's scene-generation state); `targetExpr()` walks object-link
  chains like `resolveTarget()` but stops at a Spawn Object source and
  substitutes the handle expression, so **Despawn Object / Set Position /
  Show/Hide / Play Animation / Is Visible / Get Position and the
  Near/Used/AnimFinished triggers all work on clones** (actions wrap in a
  handle-validity guard; Move Object To stays static-only for now, emits a
  comment). **Runtime**: `ScriptContext.spawnObject/despawnObject` function
  pointers (thunk pattern like resolveClip) into
  `TerrainGame::spawnObjectAt/despawnObjectAt`; a 32-slot pool after
  SCENE_OBJECT_COUNT (slots recycle, geometry builds lazily through the
  existing dirty path, `setupAnimObject` runs for skeletal clones, emitter
  clones rebuild the particle pools). Clones keep the template's layer -
  unloading that layer despawns them; `applyLayerResidency` pins the assets
  of live clones and a clone spawned before its model streamed in re-arms
  when the asset lands (`processOneStreamJob`). Save slots never persist
  clones. **Fixed latent OOB bugs this exposed**: `buildParticles`, the
  layer unload/activate/completion loops and the save-state scan indexed
  `SCENE_OBJECTS[i]` with `i` ranging over `runtimeObjects` - fine while the
  two were the same length, out-of-bounds with the pool appended (unload now
  reads the runtime copy `data.layer`, the others clamp to
  SCENE_OBJECT_COUNT); loadScene also clears all active flags before the
  first residency pass so a previous scene's clones cannot pin assets under
  new-scene indices. **Verified**: clean editor build; scratch scene
  (template box BEHIND the spawn, marker in front, EverySeconds(20) ->
  Spawn Object with a Get Position link + Yaw 45, EverySeconds(8) ->
  Despawn Object via the object link, Log nodes on both triggers);
  generated flow_graph.gen.cpp inspected (handle global, generation reset,
  live marker-position read, guarded despawn + handle reset); Docker build
  OK; **PCSX2 (software renderer), event-driven screenshots keyed off the
  game log**: after a SPAWN-TICK the red clone stands at the MARKER's
  position rotated 45 deg (no authored box exists there), after the next
  DESPAWN-TICK it is gone - cycle repeats. Spawning under real gameplay
  (pad) and clones of animated/emitter templates still want a hands-on pass.

- (69) **Layer auto-streaming (zone pivot + radius) + per-layer RAM readout** —
  GTA-style zone streaming without wiring the flow graph. A layer can opt in
  to **Auto stream** (Layers panel): it gets a zone center (world X/Z, with a
  "Center on sel." button) and a radius; the game loads the layer while the
  player is inside the zone and unloads it once they leave radius + a
  hysteresis band (15% + 8 units, so pacing along the border doesn't thrash).
  Data model: `SceneLayer.autoStream/streamX/streamZ/streamRadius` (+
  `operator==` — undo — and JSON keys omitted when off, older files load
  unchanged). Codegen: `SCENE_LAYER_STREAM_XS/ZS/RADII[SCENE_COUNT][
  SCENE_MAX_LAYERS]` tables + accessor macros (radius 0 = script-driven
  layer). Runtime: requests are **edge-triggered** through the same
  `layerRequest` channel the Load/Unload Layer nodes use - a boundary
  crossing queues one request, so scripts can still override a zone until
  the next crossing; initial residency of auto layers comes from the spawn
  point's distance (Player entity, else the type-4 spawn marker), NOT
  `startLoaded` (the checkbox is disabled in the UI for auto layers). Focus
  = `cameraLookAt` (the player in FPP, terrain center in orbit).
  **Per-layer RAM readout**: each layer row shows `N | X.X MB` - the unique
  model/material/texture files its objects reference (materials parsed for
  map_Kd, .obj submeshes for their textures), summed by file size; plus an
  "Always resident (no layer)" line for the unassigned group. An estimate by
  design (PNGs decode/quantize to different sizes, shared assets count in
  every layer using them); cached per layer name, invalidated by
  `commitChange()`. **Verified e2e in PCSX2 (software renderer)**: a scratch
  scene with an auto zone (r=45 at origin, `startLoaded=true` on purpose) and
  the player spawning 100 units away, facing the zone - at boot the zone's
  towers do NOT exist (spawn-distance init beats startLoaded); an
  EverySeconds(6) -> Spawn Player At flow graph then teleports the player
  into the zone and the layer streams in (the zone's towers near the old
  spawn appear in the post-teleport screenshot; the layer also carries a
  .glb spider, exercising the asset-streaming path). Codegen inspected
  (tables + compiled teleport graph); clean editor build; Layers panel
  renders on the scratch project. Walking across a zone border with a pad
  (instead of teleporting) still wants a hands-on pass.

- (71) **Sky dome follows the camera (no more walking out of the sky)** — on a
  large enough map the player could travel past the sky dome, which was baked
  once at world origin with the shared identity `model` matrix and a radius
  capped at 450, so the horizon and zenith stopped surrounding you and the
  scene fell out into the bare clear color. The dome now gets its own
  `skyMat` translation matrix that `renderScene` re-centers on `cameraPosition`
  every frame (X/Y/Z), so the dome always wraps the eye no matter how far the
  map extends — horizontally or up a tall climb. The geometry never rebuilds
  for this (only the matrix moves), the precise frustum/clip flags and
  `fogDisabled` are unchanged, and it costs one matrix set per frame — so no
  measurable perf hit (the concern the request called out). Fix lives in the
  codegen (`src/templates.cpp` `buildSkyDome`/`renderScene`, both camera-mode
  headers get the `skyMat` member); all three example projects (showcase,
  script-demo, layer-streaming) carry the regenerated code. Verified end-to-end:
  editor builds clean, the generated showcase compiles in Docker and boots in
  PCSX2 (software renderer) holding 50 FPS / 100% speed with the sunset dome
  rendering correctly and the horizon still fading into the fog. The
  interactive "walk to the map edge and confirm the sky no longer drops out"
  check still wants a human with a pad on a deliberately huge map.
- (74) **Stop committing generated `docker-compose.yml` (machine-specific path
  leak + merge magnet)** — the compose file is regenerated on every build
  (`refreshGenerated`, always-overwritten list) and bind-mounts the engine
  sources by an **absolute path** computed from the editor exe location
  (`engineSourceDir`), plus a volume-name hash derived from that same path.
  So the checked-in `examples/*/docker-compose.yml` carried whoever-built-it-
  last's local worktree path (script-demo still held a stale
  `terrain-chunking-large-maps-cb55f4` path; showcase flipped to each build's
  worktree) — a constant source of merge conflicts and a leak of local paths.
  Added `docker-compose.yml` to `TPL_GITIGNORE` and to both example
  `.gitignore`s, and `git rm --cached` the two tracked copies. The file still
  generates locally on every build (verified: `--new` scaffolds it and lists
  it in `.gitignore`; `git check-ignore` confirms both examples' copies are
  ignored), so nothing about building changes - it just stops being tracked.
  Verified: editor builds clean; a fresh `--new` project ignores its compose
  while still producing it on disk.

- (73) **Menu Toggle/Choice rows + USE prompt as a HUD element + triggerable
  on-screen texts** — three UI-customization features in one pass. **(a) Menu
  toggles:** two new menu entry actions, **Toggle** (Off/On, labels editable)
  and **Choice** (up to 8 option labels). The state is a save value (param =
  its name) holding the option index — its default is the initial state, it
  persists in save slots, and flow graphs react through the existing pure
  bool chain (*Value At Least* → *On Condition*). Since panels are single
  baked sprites, every option label is baked into a second per-menu strip
  texture (`res/menus/<name>-values.png`, `menubake::bakeValueStripPNG`); the
  game draws the active cell as a MODE_REPEAT sub-rect sprite (the debug-font
  atlas trick) right-aligned on the row. Cross / dpad right cycle forward,
  dpad left backward; the Menus panel edits options inline and the preview
  composites the initial states. **(b) USE prompt:** now a pinned,
  non-deletable entry in Tools > UI Editor (`Project::usePrompt`, a HudImage)
  — position/size editable, sprite replaceable with a custom PNG (baked to
  PS2-valid size/quantization like any HUD image; "Reset to built-in"
  restores the embedded 128x32 sprite). Codegen emits `USE_PROMPT_*`
  constants in `hud_data.gen.hpp`; defaults reproduce the old hardcoded
  placement, so existing projects render identically. **(c) HUD texts:**
  `Project::hudTexts` (UI Editor > Texts) — named multi-line texts with font
  (shared TTF picker, now `App::fontCombo`), size, color, drop shadow, baked
  to centered pow2 sprites (`res/hud/text-<name>.png`) on every build. New
  HUD flow nodes **Show Text** (optional auto-hide after N seconds) and
  **Hide Text** drive them via new `ScriptContext::textRequest/textDuration`
  arrays; `visibleAtStart` texts show from boot. Editor: texts render in the
  viewport overlay (visible-at-start + the selected one), renames follow into
  flow graphs, live baked preview in the panel. **Showcase updated:** the
  GRAPHICS menu's eight "X: On / X: Off" event-entry pairs became four Toggle
  rows bound to `gfx-*` save values (flow graphs rewired to VA→OC→Set), plus
  an `options-hint` text shown 6 s on scene start. Verified: editor builds
  clean; scratch project (`%TEMP%\tyra-editor-test\menutest`) with toggles,
  a choice, texts and Show/Hide Text nodes generates correct
  `menu_data`/`hud_data`/`flow_graph.gen.cpp` and **compiles in Docker (exit
  0)**, showcase regenerates + compiles too; PCSX2 boot screenshot shows the
  title-screen menu rendering "Fog  On" / "Quality  High" from the value
  strip at the right row positions, a visible-at-start two-line shadowed
  text, 50 FPS. Interactive cycling (Cross/dpad) still wants a human pad
  test.

- (69) **StaPip clipping moved from the EE to VU1 (hidden "vu1" mode, M1–M3
  of docs/vu1-clipping-plan.md)** — the engine-fork TODO from
  stapip_clipper.hpp, behind `"clipping": "vu1"` in project.json (no UI
  yet; M4 flips the preference after a real-PS2 pass). A new StaPip `clip`
  VU1 program family (c/d/tc/td) receives raw object-space vertices like
  the cull programs and per triangle: judges the verts against the X/Y
  guard band (clipw) plus the exact near/far planes (constant-z in Tyra's
  clip space, biased into a second clipw judgement), emits fully-inside
  triangles untouched, and Sutherland-Hodgman-clips crossing ones in a
  scratch area above the shrunken double buffer (near plane first so w>0
  for the w-relative side planes), fanning the result and patching the
  prim giftag NLOOP. The EE clipper and as_is programs are bypassed
  entirely in this mode (clip replaces as_is in micro memory - all three
  families don't fit); spot light evaluates on raw verts in c/tc, d/td
  light the original verts and lerp the lit colors, fog recomputes from
  the lerped clip-space w. EE-side: clip packages are capped at
  maxVertCount/5 floored to a multiple of 3 (bounded 7x fan-out; 72/5=14
  split a triangle across packages and ran the VU1 loop off into memory),
  and sub-1/3 packages are classified against the merged bbox of every
  1/3-grid part they overlap (start-bbox-only misclassified visible
  geometry as outside). Pitfalls burned in comments: fcand yields 0/1 not
  a bit pattern; a vertex clipped to exactly |x|=w scales to GS 4096.0 and
  wraps the 12.4 XYZ2 field (side planes cut at 0.9w; scissor equalizes).
  Verified in PCSX2 SW renderer: detail-8 near-plane/guard-band stress
  scene and a full showcase scene (dome, terrain, textured boxes, models)
  are pixel-identical (0 diff) to the EE precise clipper; a textured box
  straddling the camera differs 0.065% (LSB texel shifts on cut edges);
  fast mode differs 31% on the stress scene (proves the scenes exercise
  clipping); animated d/td scene holds 50 FPS. Real-PS2 PERF re-run
  (clipbench) and the SW-vs-hardware ADC check are pending - M4 (making
  "precise" route to VU1 + retiring StaPipClipper/PlanesClipAlgorithm)
  waits for them.
- (70) **Unique sprite/mesh ids — fixes HUD garbling when a menu opens** — a
  bug report showed the debug HUD (FPS/MEM readout) rendering black blocks over
  glyphs, "often" right after opening the pause menu. Root cause was in the
  engine: `Sprite`, `Mesh`, `MeshFrame`, `MeshMaterial` and `MeshMaterialFrame`
  all took their `id` from `rand() % 1000000`, and `srand()` is never called
  (so the sequence is fixed per build). Those ids share **one** lookup namespace
  in `TextureRepository`: a texture is bound to a sprite/material by
  `addLink(id)` and resolved at draw time by the FIRST texture whose link set
  contains that id (`getBySpriteId` / `getByMeshMaterialId`). Two objects handed
  the same id → the sprite draws with the wrong texture (the black blocks).
  Opening a menu allocates a burst of new sprites at once (dim + panel + cursor
  + save sprites), which sharply raised the odds of a collision with the
  always-present debug-HUD glyph — hence "often, when the menu opens". Fix:
  new `renderer/models/unique_id.hpp` `generateUniqueId()` (a monotonic u32
  counter, EE-thread only, skips 0 to stay clear of the texture-buffer
  "unallocated" sentinel) replaces all eight `rand()` id assignments in the
  rendering path. `audio_song.cpp` keeps `rand()` on purpose: its id is a
  separate (audio) namespace and it is assigned on the audio thread, where the
  non-atomic counter would race. Verified: engine recompiled and `libtyra.a`
  relinked in Docker (fpp scratch project, `--build` = Build OK, clean on the
  five touched files); the collision class is removed by construction. A
  hands-on menu-open check on a menu-bearing project (e.g. the showcase) is the
  remaining human confirmation — the failure was probabilistic/per-build, so it
  cannot be forced to reproduce on demand.

- (72) **examples/video-modes - a VIDEO OPTIONS menu test bed + the
  runtime-widescreen freeze fix** - an example project for exercising
  (70)+(71) on a pad. A baked game menu ("VIDEO OPTIONS", title screen at
  boot AND the Start pause menu) lists the three scan modes, 4:3 / 16:9
  and CLOSE; the entries fire flow events consumed by On Menu Event ->
  Set Display Mode (confirm 8 s) / Set Widescreen on the `aspect-ball`
  object. The scene is a calibration set: a white center sphere (the
  aspect judge - must stay round in every mode/aspect on a real TV), four
  colored compass pillars (horizontal FOV), and an overlay hint
  (res/ui/controls.png, rendered from the same CP437 8x8 glyphs as the
  engine's debugfont). Template change: `applyVideoRequests` returns true
  on a scan-mode switch and the loop then CLOSES any open game menu - the
  player judges the new picture unobstructed, and the confirm prompt's X
  can't double as a menu select (the switch frame also skips the Cross
  check, so the selecting press can't insta-confirm). **Bug found by a
  hands-on test of (71): the runtime Set Widescreen froze the picture**
  (EE kept running - pad logs still flowed - but the GS stopped drawing):
  reprogramDisplay() went through the full programDisplay(), whose
  graph_set_mode does a GS reset that wipes the drawing environment only
  the full reinit() path re-creates. Fixed: a widescreen-only change now
  rewrites JUST the DISPLAY window registers. res/.gitignore is customized
  showcase-style (/hud/ and /menus/ ignored, authored res/ui checked in -
  the file is only written at project creation, so it survives builds).
  Verified: Docker build exits 0; PCSX2 boot screenshot shows the
  title-screen menu (panel, cursor, X OK / Triangle BACK hints, dim
  overlay) over the scene at "PAL Interlaced (Field) 512x448" 50 FPS; the
  runtime widescreen path re-verified after the fix with an unattended
  OnStart -> Delay -> Set Widescreen graph (scene keeps rendering,
  geometry goes anamorphic). Menu navigation itself still wants a pad.

- (71) **Runtime display-mode switching (with keep-or-revert prompt) +
  widescreen 16:9** — follow-up to (70). **(a) Runtime switch:**
  `RendererCore::setDisplayOutput(mode, widescreen)` (fork) re-selects the
  scan mode between frames: the VRAM bump allocator resets, frame/z buffers
  and the post-fx scratch buffers rebuild at the new size, every texture is
  evicted (`RendererCoreTexture::evictAll`, lazy re-upload) and the
  projection re-derives - `RendererCoreGS` grew `reinit()` /
  `reprogramDisplay()` (mode setup split into `programDisplay()`).
  **(b) Flow nodes:** **Set Display Mode** (mode combo + "Confirm s") and
  **Set Widescreen** (Scene category) - new `ScriptContext` fields applied
  in both game loops right before `beginFrame` (the safe point). With
  Confirm > 0 the generated game arms a **keep-or-revert countdown**: it
  switches, draws "KEEP VIDEO MODE? X = YES / BACK IN n" centered on
  screen, and reverts to the previous mode automatically when the timer
  (real g_frameDt seconds) expires without an X press - the PC-settings
  safety net, since a mode the TV can't show is a black screen. The prompt
  draws via a new shared `drawHudText` (refactored out of drawDebugHud);
  the embedded 8x8 glyph strip grew A-Z + "?=-:" (42 glyphs, 512x16, two
  rows) and `debugfont.png` is now written to every project on refresh
  (release builds need it for the prompt), with the atlas string kept in
  sync between `debugFontPng()` and the game template. **(c) Widescreen:**
  `ProjectSettings::widescreen` (Preferences > Build checkbox, serialized,
  default false) -> `EngineOptions::widescreen`. The projection aspect now
  derives from the physical shape of each mode's display window
  (`RendererSettings::updateGeometry`): SDTV modes keep their signal and
  let the TV stretch (anamorphic), 1080i widens its GS window from 3x to
  4x MAGH (1792/1920 VCK). Verified in PCSX2 (SW renderer, scratch orbit
  project with OnStart -> Delay 4s -> Set Display Mode(480p, confirm 5s)):
  boot in 480i -> switch at ~4s with the prompt rendering the new glyphs
  over a healthy scene (textures re-uploaded after the VRAM rebuild) ->
  countdown ticks -> prompt clears on timeout with the scene still healthy
  after the second rebuild (the revert is the only inputless path that
  clears the prompt; the final-mode status-bar text could not be read -
  the PCSX2 window sat under the taskbar in captures). Widescreen build
  verified by geometry: the same scene renders visibly narrower
  (anamorphic squeeze) with widescreen on. Codegen inspected for both
  nodes. Hands-on still wanted: the X-confirm path (needs a pad; synthetic
  input is off-limits on this machine) and real-hardware output.

- (70) **Alternative display modes: progressive 480p and 1080i** — new
  Preferences > Build > **Display mode** combo (`ProjectSettings::displayMode`:
  "interlaced" default / "progressive" / "1080i", serialized with a
  backward-compatible default) baked into the generated `main.cpp` as
  `EngineOptions::displayMode`. Engine side (`DisplayMode` enum,
  `RendererSettings::setDisplayMode`, `RendererCoreGS`): **Progressive480p**
  renders 448x448 and scans it out non-interlaced at MAGH 3x — a 1344x448
  window centered in the 1440-VCK 480p raster, exactly 4:3, slightly *less*
  VRAM than the stock 512x448. **HiDef1080i** renders 448x540 (sharper
  vertically than 480i) shown as a 1344x1080 pillarboxed ~4:3 window in the
  16:9 raster; frame+z buffers grow to 2.9 MB of VRAM, so ~1.1 MB is left
  for textures (the UI warns). Both DTV modes bypass `graph_set_screen` —
  it always programs the mode's full VCK width into DW, and **no 64-aligned
  framebuffer width divides the 1440/1920-VCK rasters**, so the GS would
  scan garbage past the buffer's right edge; `setDtvDisplay()` programs
  DISPLAY1/2 directly (gsKit-style window math, centered). The projection
  aspect deliberately stays at the constructor's 512/448 in every mode so
  world proportions match across modes. Flicker filter stays on the stock
  interlaced path only; DTV modes present via both DISPFB circuits at y=0
  (`presentFrameBuffer`). `getRefreshRate()` returns 60 for the DTV modes
  regardless of region, so wall-clock speed normalization keeps working.
  **Pitfall burned into the code comments:** the gsKit/OPL "interlaced FRAME
  mode, MagV--" recipe for 1080i **hard-crashes PCSX2 v2.3.205** (process
  dies ~4 s in, no crash dialog, SW and HW renderers alike); 1080i in FIELD
  mode with MAGV=2x is visually equivalent (each field steps through every
  buffer line - a stable line-doubled 540p picture) and PCSX2 is fine with
  it. Verified in PCSX2 (software renderer, scratch orbit project, one boot
  per mode, status-bar + screenshot each): interlaced baseline "PAL
  Interlaced (Field) 512x448" at 50 FPS unchanged; "SDTV 480p Progressive
  448x448" at 60 FPS, clean right edge (the graph_set_screen overrun would
  show there); "HDTV 1080i 448x540" at 60 FPS, pillarboxed and full-height.
  Codegen + .tyra round-trip checked headlessly; samples/script-demo
  regenerated. Real-hardware check (component cables) still wants a human -
  PCSX2 does not emulate the analog signal path.

- (69) **Pause now freezes particles and skeletal animation** — a pausing menu
  (a menu with the pause flag, or the save menu) already stopped player
  movement, scripts, object physics and the use target, but two effect systems
  kept advancing behind the menu: particle emitters and skeletal-animation
  playback, both driven off `g_frameDt` in `loop()` with no `menuActive` gate.
  Added a `g_gameplayPaused` flag set at the top of `loop()` (both the orbit
  and FPP game-cpp variants). `updateParticles()` early-returns while paused so
  every billboard bag hangs on its last-built frame — the scene render still
  draws it, so particles freeze in place instead of vanishing. Its sibling guard
  from the Set Particles switch (`!g_particlesOn`) stays intact.
  `updateAndRenderAnimObjects()` zeroes the playback step while paused, freezing
  the pose while still skinning and rendering it. Overlay menus (pause flag off)
  keep gameplay running as before. Verified: editor builds clean; codegen for
  fresh FPP and orbit projects emits all changes; the `script-demo` example
  (particles + animated models) compiles on the PS2 toolchain in Docker and
  links (Build OK, exit 0). The visual freeze-on-pause still wants a hands-on
  pad test in PCSX2 (open the pause menu, confirm rain/smoke and animations
  stop).
- (69) **On-demand save (no autosave) + menu-bar icon toolbar** — the editor
  used to autosave the whole `.tyra` on *every* edit (`commitChange()` called
  `saveAll()`), plus a second autosave whenever ImGui settled a layout/docking
  change, plus a forced save on exit. That's gone. `commitChange()` now only
  pushes an undo snapshot and, when the snapshot actually differed
  (`History::push()` returns a bool now), flips a `dirty_` flag; the project is
  written only when the user asks (File > Save, Ctrl+S, or the new toolbar
  button). `dirty_` drives a `*` in the window title and an amber tint on the
  Save icon. Losing unsaved work is guarded: Exit / Open Project / New Project
  route through `requestExit/requestOpenProject/requestNewProject`, which pop an
  "Unsaved Changes" modal (Save / Don't Save / Cancel) when dirty; the main
  loop intercepts the window-close (`while(true)` + `glfwWindowShouldClose`
  check) so the X button is guarded too. Terrain heightmaps used to be written
  to disk on every sculpt stroke and on undo/redo; they're now kept in memory
  (they already ride along in the undo snapshot) and persisted only in
  `saveProject()` alongside the `.tyra`, so a discard truly discards terrain
  edits. Freshly opened/created projects reset `dirty_` after
  `attachProject()`'s asset rescan (rescan-found assets are rediscovered on
  every open, so they don't count as unsaved). The new **toolbar** sits inline
  in the main menu bar after Tools (`drawToolbar()`): a floppy **Save** (amber
  when dirty), a **Build** (no run) hammer, then two tight run/stop pairs
  separated by a wider gap — **[green Play = Build && Run in PCSX2, ▾, red Stop
  PCSX2]** and **[blue Play = Build && Run on PS2, ▾, red Stop PS2]**. Each Play
  has a Visual-Studio-style caret **dropdown** (`##..._more` → `BeginPopup`
  anchored under the caret) offering *Run (no build)* and *Build (no run)*. Each
  Stop cancels a running build when one is in progress, otherwise closes the
  emulator (`Runner::stopEmulator()`) or stops the game on the console
  (`Runner::stopPs2()`); the PS2 pair dims until a ps2link IP is set. Run
  shortcuts (switched to `IsKeyChordPressed` so the modifier state matches
  exactly): **F5** build && run in PCSX2, **Ctrl+F5** run in PCSX2 without
  building; **F6** build && run on PS2, **Ctrl+F6** run on PS2 without building;
  **Ctrl+Shift+B** build only. The no-build shortcuts also show in the caret
  dropdowns and the top-level Build menu. Spacing is
  set explicitly per button (not the default ImGui item spacing) so pairs read
  as groups at any UI scale. Icons are vector-drawn on
  the menu-bar draw list — the editor loads no icon font — so they stay crisp
  at any UI scale. Editor-only change: no `.tyra` format, codegen or PS2
  runtime impact. Verified end-to-end by driving the running editor (synthetic
  mouse/keyboard) on an FPP scratch project and screenshotting each step:
  paste-an-object → title gains `*`, Save icon turns amber, **but `grep` of the
  on-disk `.tyra` shows the edit is NOT there** (no autosave); click the Save
  icon → `.tyra` now contains it and `*` clears; Alt+F4 with unsaved edits →
  the "Unsaved Changes" modal appears and the window stays open; "Don't Save" →
  editor exits and the discarded edit is absent from the `.tyra`; "Cancel" →
  editor stays open. Selecting an object (no edit) does not set dirty. Clean
  editor build both before and after the button-label auto-size fix.
- (69) **Usable-highlight rims moved off the EE (matrix shells + apron ring)** —
  the in-game usable-object highlight could tank the frame rate: every frame,
  for every nearby usable object, `renderHighlightHull` grew `steps × n` hull
  vertices on the EE (9 muls **plus a bilinear `terrainHeightAt` per vertex**
  for the ground clamp), wrote `2 × steps × n × 16 B` of vertex/color arrays,
  and its per-frame `bboxVersion` bump forced a package-bbox recompute over
  all of them — with the default 4 steps a few-thousand-vert usable model
  near the player cost milliseconds of EE time, and the frame is EE-bound
  (see the clipbench measurements in the backlog), so it fell straight to
  the next vsync divisor.
  The rewrite exploits that both per-vertex ops are uniform point scales
  (grow about the object center, depth pushback about the eye): they compose
  into a **single scale+translation model matrix**, so each shell now
  re-submits the object's **own vertex arrays** with a per-shell `hullMat` +
  per-shell single color — StaPip applies the matrix on VU1 (and in frustum
  classify + the EE clipper via the composed MVP), the EE never touches a
  vertex, and the shell's package bboxes ride the part's own `bboxVersion`
  (their own cache slot — single color changes the package size — recomputed
  only when the part rebuilds, never per frame). One game-level hull bag
  replaces the per-object bag + `steps × n` vertex/color copies (a 6k-vert
  usable model used to hold ~750 KB of hull arrays on a 32 MB console).
  The one thing that genuinely needed per-vertex terrain sampling — the
  ground-clamp that turned the bottom rim into a glow apron hugging the
  terrain — is replaced by `buildHighlightApron`: a small terrain-following
  annulus around the base (one band per shell, same growth radii and alpha
  series, 24 segments ≈ 576 verts at 4 steps), world-space and
  camera-independent, built once and cached until `rebuildObjectGeometry`
  invalidates it (move/resize/scene switch). The repaint pass that erases
  shell wash off receding side faces is unchanged. Verified: editor builds
  clean; scratch FPP project (usable box + usable sphere + plain box near
  spawn, highlight on) compiles in Docker and runs in PCSX2 — SW-renderer
  screenshot shows the fading rim around the usable sphere only, clean
  interior, the ground apron around the base, 50 VPS / 100% speed;
  samples/script-demo regenerated + recompiled. Real-PS2 A/B timing of the
  before/after EE cost still needs a hardware session (the COP0 PERF recipe
  from the clipbench notes in the backlog applies as-is).

- (68) **Ambience Editor + Properties docked right + sky dome preview** — three
  related changes. **(a) Docking default:** the first-run DockBuilder layout now
  puts **Properties in a docked column on the right** (Project left, Viewport
  center, Output/Debug bottom) instead of under Project on the left; the
  pre-Properties migration path docks it on the right too. Existing projects
  keep their saved layout. **(b) Sky gradient preview:** the editor viewport
  sky was a flat screen-space quad that ignored the camera. It is now a
  world-space **hemisphere dome** centered on the camera (mirrors the PS2
  `buildSkyDome`): horizon→zenith colors interpolate by elevation over the 90°
  up-arc, so the full 180° horizon-through-zenith sweep reads as one coherent
  dome that tracks camera pitch (clear color still fills below the horizon).
  The generated game dome was already elevation-linear, so its formula is
  unchanged except for a new **Zenith size** control (`ProjectSettings::
  zenithSize` / `AmbiencePreset::zenithSize`, 0.05..0.95, default 0.5 = linear):
  both the preview dome and the generated dome remap the elevation fraction by
  `pow(t, (1-size)/size)`, so a larger value spreads the zenith color down
  toward the horizon (bigger zenith cap) and a smaller one keeps it near the
  top. Codegen bakes it as the precomputed exponent `SKY_ZENITH_EXPS` per
  scene. **(c) Ambience Editor** (Tools > Ambience
  Editor): a new project-wide collection of **AmbiencePreset** bundles (sky
  gradient + baked lighting + distance fog), one markable default — mirrors the
  Color Grading preset system end-to-end. The sky/lighting/fog controls moved
  **out of the crowded Project Preferences** (global + per-scene override
  categories) into this editor; per-scene Preferences now picks a preset
  (empty = default), and the new **Set Ambience** flow node (Scene category)
  repaints the sky from a named preset at runtime (lighting/fog are baked per
  scene at build, so only the sky changes live — like Set Sky Color).
  `project::resolvedSettings` overlays the resolved preset's sky/lighting/fog on
  top of the scene, so all downstream codegen/viewport keeps reading the same
  `ProjectSettings` fields unchanged. New projects seed a "Default" preset;
  loading a pre-Ambience project migrates its global + per-scene sky/lighting/fog
  into presets (a Default plus one per overriding scene) so the look is
  preserved. Verified: editor builds clean; a scratch project round-trips the
  presets + per-scene `ambiencePreset` through save/load; a scene pointed at a
  dark "night" preset bakes the expected `SKY_RS`/`SCENE_AMBIENTS` (5.1F / 0.2F)
  into `scene_data.hpp`; a `Set Ambience` node compiles to
  `ctx.skyColor = Tyra::Color(5.1F, 5.1F, 20.4F)`; GUI screenshots confirm
  Properties on the right, the sky dome gradient, and the Ambience Editor window
  (preset list + sky/light/fog controls). Not yet booted in PCSX2 — the runtime
  Set Ambience sky repaint still wants a hands-on in-game check.

- (67) **HUD images baked to a PS2-valid texture at build (size + bit depth)**
  — the engine hard-asserts textures must be 8/16/32/64/128/256/512 in each
  dimension (`texture.cpp`), and it only fires when the game boots, so a HUD
  PNG imported at, say, 100x40 crashed at runtime with no editor-side warning.
  Now HUD images referenced by the project are resized into `.res-baked/hud`
  (the mirror the game actually ships) to a valid power-of-two, so a mis-sized
  import just works. Per-image controls in Tools > UI Editor, mirroring the
  per-asset material texture quality: Width / Height combos (Auto = nearest
  valid size, or a chosen one) and a Colors combo `Project default / Full
  color / 256 / 16`. The Colors default *follows* Preferences > Textures (so
  the HUD quantizes along with the rest of the project to save VRAM), and any
  element can be overridden - e.g. a crosshair or logo kept full color while
  the rest is 4-bit, or vice versa. Live readout of the source size (orange
  warning when it is not a PS2 size) and the resulting baked size + effective
  depth ("(from project)" when inherited). The on-screen sprite size is
  unchanged — the sprite is stretched, so resizing the texture never changes
  how the HUD looks, only its stored resolution/quality. Model:
  `HudImage::texW` / `texH` (0 = auto) + `texQuant` ("" = follow the project
  `ProjectSettings::textureQuant`, "none" = full-color override, "8bit" /
  "4bit"), serialized in the `hud` array, defaulted for older projects.
  `pngquant` gained `resizeRGBA` (bilinear), `writePngRGBA` (full color via
  stb_image_write), and a buffer-based `quantizeRGBA` (the old file-path
  `quantize` now loads then calls it); `texbake` resolves each HUD entry's
  depth (falling back to the project default) then resizes+quantizes the PNG
  (built-in HUD assets like use.png/loading.png/save-*.png are not project
  entries, so they copy verbatim). Verified: clean editor build; a scratch
  project (project default 4-bit) with a 100x40 PNG (auto size, follow
  project) and a 130x50 PNG (auto, explicit 4-bit) — pre-fix both would
  assert. After `--build`, `.res-baked` shows: badhud 128x32 4-bit and
  crosshair 32x32 4-bit (both inheriting the project default), ammo2 128x64
  4-bit (explicit), and vignette 64x64 full RGBA (a "none" override proving an
  element can keep more color than the project). PCSX2 boot (`-elf`) reached
  "is executing" with no assertion; an F8 snap shows the images rendering
  (gradient resized, boxes palettized). UI checked by scripted clicks: the
  bake section renders, and the mis-sized image shows "Source 100x40 is not a
  PS2 size" + "Baked: 128x32".

- (66) **UI Editor tool: HUD + full-screen effects as a reorderable screen
  stack, bloom and grain as independent layers** — HUD image configuration
  moved out of the Project panel and the bloom/grain sliders out of Project >
  Preferences into a new Tools > UI Editor window. It shows the whole 2D
  composition as a "screen stack" (top entry draws last): every HUD image plus
  two effect layers — `[ Bloom + color grading ]` and `[ Film grain ]` —
  drag-to-reorder. The point: each effect can sit *under* the HUD (so it does
  not smear the crosshair/text) or *over* it, independently. Canonical split:
  bloom under the HUD, film grain at the very top as a filmic overlay over the
  whole screen. Model: `Project::hudBloomLayer` + `hudGrainLayer` (grading
  rides with bloom). `-1` = the pass applies at end of frame over everything
  incl. menus (the old behavior and the load default); `k` = applies before
  HUD sprite `k`, sprites above draw crisp. Serialized as `"hudBloomLayer"` /
  `"hudGrainLayer"` next to `"hud"`; the pre-split `"hudPostFxLayer"` key
  migrates to both; HUD deletion re-indexes both. Engine (vendor/tyra):
  `RendererCorePostFx::apply(int passes)` gained a `Pass` bitmask
  (`PassBloom | PassGrading | PassGrain`) so a subset composites per call;
  `RendererCore::applyPostFx(passes)` applies only the not-yet-applied passes,
  runs the PATH1 drain barrier once (before the first pass that actually
  draws), and `endFrame()` composites whatever is left. Codegen:
  `hud_data.gen.hpp` gains `HUD_BLOOM_LAYER` / `HUD_GRAIN_LAYER`; both game
  templates run the HUD loop applying `PassBloom|PassGrading` at the bloom slot
  and `PassGrain` at the grain slot. Scene Preferences per-scene bloom/grain
  strength overrides are untouched (the layer positions are project-wide, like
  the HUD). The viewport HUD overlay also shows while the UI Editor is open,
  with the selected image outlined. Verified: clean editor build; scratch
  project round-trip (`hudBloomLayer`/`hudGrainLayer` save/load + clamp +
  legacy migration); codegen inspected (`HUD_BLOOM_LAYER = 1`,
  `HUD_GRAIN_LAYER = -1`); GUI exercised by scripted clicks (window opens,
  stack lists the images + both effect rows, selecting each shows its slider,
  dragging saves the layer); full Docker build OK (engine + game relinked);
  PCSX2 boot (`-elf`, emulog "is executing") with bloom 0.9 + grain 0.4.
  Quantified the split from F8 snaps (PIL mean |dx| grain proxy): with grain
  under the HUD the ammo-box UI element scored 0.97 (clean) vs 2.85 on the
  sky; moving grain to the top layer took the ammo box to 3.06 — grain now
  overlays the UI to the same level as the scene — while bloom stayed under
  the HUD (crosshair/ammo render sharp, not glowing). No mid-frame barrier
  hang across either config.

- (65) **Copy/paste flow-graph nodes with Ctrl+C/V** — before this, Ctrl+C/V in
  the Flow Graph window always hit the global handler and copied the *scene
  objects* selected in the viewport, so there was no way to duplicate graph
  nodes. Copy/paste is now focus-aware: `drawFlowGraphWindow()` sets a
  per-frame `flowGraphFocused_` flag (via `IsWindowFocused(ChildWindows)`, the
  same test the node-delete path uses) and, while that window has focus and no
  node param is being typed into, handles Ctrl+C/V on the graph itself. The
  global shortcut handler (which runs later the same frame, after the window is
  drawn) now stands down whenever `flowGraphFocused_` is set, so object
  copy/paste is unaffected everywhere else. Copy grabs the imnodes-selected
  nodes into a `FlowGraph flowClipboard_` plus only the links whose *both*
  endpoints are in the copied set (dangling links dropped). Paste re-ids every
  node from the target graph's `nextId` (so it works into the same graph or a
  different object's graph without collisions), remaps the copied links to the
  new ids, offsets positions by (+20,+20) so the copy sits beside the source,
  resets `flowPositionsApplied_` to push the new positions to imnodes, and goes
  through `commitChange()` for one undo step. Pure editor state — no `.tyra`
  format, codegen or PS2 runtime change. Verified: clean editor build
  (`build.ps1`, link OK). The keyboard-focus routing itself (copy nodes vs
  objects depending on which window is focused) is reasoned from the call order
  — the flow-graph window is drawn before the global shortcut block within the
  same frame, so the flag is fresh — and still wants a hands-on pass, since
  synthetic keyboard-into-GLFW input against the node editor is not reliably
  automatable in this environment (noted on entries 63/64).

- (63) **Flashlight moved from a project preference to a Player property** — the
  camera flashlight used to be a scene-visual category on `ProjectSettings`
  (project default + per-scene override). It is now a property of the Player
  object (`SceneObject::flashlight*`): color/range/cone half-angle plus an
  **Enabled** master switch and an optional **toggle button**. Enabled is
  runtime-controllable — the new **Set Flashlight** flow node (Player category)
  writes `ScriptContext::flashlight` (0/1), which the game folds into a
  `g_flashEnabled` global; loadScene seeds it per scene from the player's
  Enabled flag. The optional toggle button (a pad button on the player) flips a
  separate `g_flashOn` state via the generated `flashlightTogglePressed()`
  helper (a per-scene switch over each player's button); the beam shows only
  while `g_flashEnabled && g_flashOn`, so the toggle **respects Enabled**. The
  per-scene `FLASHLIGHT_*` codegen tables now read the scene's first Player
  object instead of resolved settings; no player = no flashlight. Editor: the
  Preferences and Scene-override flashlight sections are gone, replaced by a
  Flashlight block in the Player object properties (Enabled + color/reach/angle
  + toggle-button combo). Also removed **all "Silent Hill" mentions** from the
  codebase (comments in `project.hpp`, `app.cpp`, `templates.cpp`,
  `vendor/tyra` renderer_core, and PROGRESS entries 47-49) per request.
  Verified: editor builds clean; a scratch fpp project with the flashlight
  enabled + toggle=Circle + an On Start → Set Flashlight graph round-trips
  through save/load, generates the expected `scene_data.hpp` tables, toggle
  helper (`case 0: return engine->pad.getClicked().Circle;`) and flow code
  (`ctx.flashlight = 1;`), compiles under the PS2DEV toolchain (Docker build
  exit 0), and boots in PCSX2 (SW renderer) showing the lit flashlight cone on
  the terrain. Interactive toggle-button press still wants a hands-on pad test.
- (64) **Multi-object selection, group transform, copy & multi-edit** — the
  editor now works on a *set* of objects, not one at a time. A new
  `std::vector<int> selection_` carries the full selection while the old
  `selectedObject_` stays as its *primary* (anchor) member, always
  `selection_.back()` — so the ~30 existing single-select reads (orbit pivot,
  flow-graph "from selected", script-attach target...) keep working unchanged.
  All the scattered `selectedObject_ =` writes went through four helpers
  (`selectOnly`/`toggleSelect`/`clearSelection`/`pruneSelection`) that keep the
  invariant; `pruneSelection` drops stale indices after undo/scene changes.
  **Selecting**: Ctrl/Shift+click (viewport or Project list) toggles an object
  in/out; plain click replaces. Left-drag draws a rubber-band marquee and
  selects every object whose screen-space bounds overlap it (8 unit-box corners
  projected with the same view/proj math as the sculpt-brush overlay, rotated
  Rz·Ry·Rx to match `modelMatrix`); Ctrl+drag extends. To free left-drag for the
  marquee the Default nav scheme now orbits on right-drag only (it already did).
  All selected objects get an amber outline in the viewport, the primary
  brighter (`Viewport::render` now takes the selection vector + primary).
  **Transforming**: with >1 selected the gizmo manipulates a proxy at the
  group centroid, captures ImGuizmo's world-space `deltaMatrix`, and applies
  `delta · model` to every object (decompose back to TRS) — translate /
  rotate-about-pivot / scale-about-pivot for the whole group, one undo step per
  drag. The single-object path (world/camera-relative + snapping) is untouched.
  **Copy/paste/delete** all operate on the set: the clipboard is a
  `vector<SceneObject>`; paste offsets the group by a shared `(+1,0,+1)` so it
  keeps its shape and selects the new copies; delete erases high-index-first.
  **Multi-edit Properties panel** (`drawMultiProperties`): shows only fields
  common to every selected object (intersection of the same isShape/isSolid/
  isEmpty/emitter/light predicates the single view uses). Transforms apply
  *relatively* — a drag nudges the whole group by the same delta, seeded from
  the anchor, so the arrangement is kept. Every other shared field (color,
  physics, usable, collision, draw distance, type/detail, save state, emitter &
  point-light params) is set-all with a "mixed" dash via
  `ImGuiItemFlags_MixedValue` while the values differ; small
  `multiCheck`/`multiDragF`/`multiCombo` lambdas (pointer-to-member) keep it
  terse. A header tallies the selection ("3 Box"); inherently per-object fields
  (name, model/material/sound, scripts) are noted as single-object-only.
  Selection is pure editor state — no `.tyra` format change (only the primary
  persists, as before), no codegen, no PS2 runtime touched.
  Verified: clean editor build; launched on a 3-box scratch scene (distinct
  positions/colors/detail, one with physics). The full pipeline was confirmed
  by screenshot — all three boxes highlighted in the Project list + "3 objects
  selected" hint, and the multi-edit panel rendering "3 objects selected /
  3 Box", relative transforms seeded from the anchor, and the mixed-value dash
  correctly on Physics (one object differs) vs a normal check on Collision (all
  same). The interactive *gestures* themselves — box-drag feel and the group
  gizmo drag — still want a hands-on human pass: synthetic mouse input would not
  register in the GLFW window in this environment (mouse_event and SendInput
  both failed to reach it despite correct focus), so those were verified by
  code review + compile only, while the selection→highlight→panel pipeline they
  feed is confirmed working.

- (66) **Terrain picks a material, not a raw texture — tiling comes from the
  material too** — the terrain used to take a loose PNG
  (`ProjectSettings::terrainTexture` + a `terrainTexScale` slider, project-wide
  + per-scene override); it now takes a Wavefront **material** (`.mtl`) like
  every solid object, so the terrain carries its color, texture *and* tiling
  from one asset. The model field is `terrainMaterial` (the whole
  `terrainTexScale` field is gone); the override flag
  `SceneOverrides::terrainTex` became `terrainMat` (the loader still reads the
  old `"terrainTex"` key so existing per-scene overrides survive). The first
  material's **Kd** tints the terrain, its **map_Kd** (when present) textures
  it, and the map's **`-s <u> <v>`** option (standard Wavefront texture-scale,
  a UV multiplier) drives tiling as *repeats per world unit, per axis* —
  previously discarded by the parser, now read by `objparser` into
  `MtlMaterial::scale[2]`. A material with no texture yields a flat Kd-colored
  surface; no material at all keeps the two-green checker. Old projects lose
  their raw terrain texture and its tiling (a PNG can't become a material) per
  an explicit product decision. A shared resolver
  `project::resolveTerrainMaterial()` returns a `TerrainMaterial{present,
  texture, kd, tile}` so codegen, the editor viewport and the ISO planner
  agree. Codegen (`texture_data.gen.hpp`) emits `TERRAIN_HAS_MATERIALS[]`,
  `TERRAIN_TINTS[][3]`, and `TERRAIN_TILE_US[]`/`TERRAIN_TILE_VS[]` (replacing
  `TERRAIN_TEX_SCALES[]`) next to `TERRAIN_TEXTURES[]`; the terrain runtime
  folds the tint into the per-cell base color (textured → Kd·128 modulation,
  flat → Kd·255) and its UVs become `worldPos·tile`, and the viewport mirrors
  both formulas. The material is compiled away — only its texture reaches the
  disc — so the ISO planner groups that texture, not the `.mtl`. Editor: the
  Preferences and Scene-override "Terrain texture" pickers (and the tile slider)
  are replaced by a "Terrain material" combo listing the project's `.mtl`
  assets; the **Material Editor gained a "Tile repeat" field** that reads/writes
  the `map_Kd -s` option (uniform in the UI, per-axis preserved for hand-edited
  files); deleting a material asset now also clears any terrain that referenced
  it. Verified: editor builds clean; a scratch fpp project set to a textured
  material (`Kd 0.6 0.4 0.2` + `map_Kd`) generates `TERRAIN_TEXTURES={0}`,
  `TERRAIN_HAS_MATERIALS={true}`, `TERRAIN_TINTS={{0.6,0.4,0.2}}`; with no `-s`
  the tiles are `{1.0}`, and with `map_Kd -s 0.25 0.5 1` they become
  `TERRAIN_TILE_US={0.25}` / `TERRAIN_TILE_VS={0.5}` (per-axis parse); a
  color-only material generates `TERRAIN_TEXTURES={-1}` with the same tint —
  all compile under the PS2DEV toolchain (Docker build exit 0). The editor
  viewport and PCSX2 (SW renderer, 50 FPS) both render the terrain as the
  texture tinted warm brown and tiled per `-s`, confirming the twin
  editor/game formulas match. (Material Editor slider round-trip verified by
  code + the hand-authored `-s` parse; the in-GUI drag still wants a human
  pass — synthetic mouse input doesn't reach the GLFW window here.)

- (65) **Scene objects list groups by layer, with drag-and-drop assignment** —
  the "Scene objects" section in the Project panel used to be one flat list of
  every object. When the active scene has layers it now renders a tree: one
  collapsible node per layer (header shows the layer name, its object count and
  a `[hidden]` tag / dimmed text when the layer's editor eye is off), then an
  **Unassigned** group for objects with no layer (or a stale name left by a
  deleted layer). Each layer node expands/collapses to show only its objects.
  Scenes without layers keep the old flat list unchanged. **Assigning is now
  drag-and-drop**: drag any object row (or, if it is part of the current
  selection, the whole selection — the drag tooltip reads "Move N objects")
  onto a layer header to set `SceneObject::layer`, or onto Unassigned to clear
  it; the drop is deferred until after the child so the render loop stays
  stable, and applies as a single `commitChange()` undo step. The existing
  Properties `Layer` combo still works. Object rows keep their multi-select
  clicks (Ctrl/Shift) and hidden-layer dimming. Only `drawSceneSection()` in
  `app.cpp` changed — the data model, serialization and codegen are untouched
  (layers already carried `SceneObject::layer`). Verified: editor builds clean;
  launched on `examples/layer-streaming` and confirmed via screenshot that the
  two building layers render as collapsible nodes with their walls nested
  underneath and the drag hint showing. The drag *gesture* itself wants a
  hands-on human pass — synthetic mouse input does not reach the GLFW window in
  this environment (see entry 64) — but the tree/grouping/drop-target wiring is
  standard ImGui and confirmed compiling + rendering.

- (63) **Delete assets from the editor (with confirmation)** — until now the
  only way to remove a model, material, texture or HUD image was to delete the
  file by hand in Explorer, and the Music/Sounds `x` deleted the file instantly
  with no prompt. Added a single confirmation modal, `drawDeleteAssetModal()`
  (mirrors `drawDeleteSceneModal`), staged via `requestAssetDelete(kind, relPath,
  label, hudIndex)` into `assetDeletePending_`. New `x` buttons on every model
  (.obj/.glb) and material (.mtl) row in Project > Assets; the Music/Sounds `x`
  and the HUD **Delete HUD image** button now route through the same modal
  instead of deleting on the spot. The dialog spells out what still references
  the asset (`countAssetUsers` counts scene objects across all scenes + audio
  flow-graph nodes) and warns that the file removal cannot be undone. On confirm
  `performAssetDelete` deletes the res/ file and clears the dangling references:
  materials reset the referencing objects' `materialPath` (empty = plain color /
  the model's own .mtl); sounds clear `SoundEmitter.soundPath` on top of the
  Play-Sound flow nodes `removeAudioTrack` already handled; models are left
  pointing at the now-missing file (shown as "missing", same as a hand delete)
  and the caches (`modelInfoCache_`/`glbInfoCache_`) + `textureQuality` override
  are dropped; HUD deletes the file only when no other HUD entry still uses that
  path. Audio/model/material deletes go through `commitChange()` (HUD stays on
  `saveAll`, matching the section's existing behavior). Verified: clean editor
  build; launched on a scratch project holding a model, material, music and sfx
  plus scene objects referencing them (a model object + a sound emitter) — the
  new `x` buttons render on the cube.obj / walls.mtl / song.wav rows
  (PrintWindow capture) and triggering a delete opens the confirmation modal
  (ImGui modal dim-backdrop over the panel). The interactive click-through of
  the modal's Delete/Cancel buttons could not be automated here: synthetic mouse
  input does not register against the GLFW/OpenGL window in this environment
  (keyboard does), so the final click + on-disk removal still wants a hands-on
  human confirmation.

- (63) **Animated models (.glb) render their material colors in-game (was
  gray)** — an animated model authored with per-material Base Colors (e.g.
  `spider2.glb`: red body, black legs, gray tee) showed those colors in the
  editor viewport but rendered a flat gray on the PS2. **Root cause**: the
  animated pass is the *only* editor-generated geometry that draws through the
  StaPip **dynamic-lighting** path (`bag->lighting` set); every other path
  pre-bakes lighting into per-vertex colors and draws unlit (`lighting =
  nullptr`). The lit StaPip VU1 programs (`stapip_cull_d_vu1` / `_td` and the
  `as_is` twins) compute the output color *purely* from the directional lights
  and normals via `CalculateTyraDirectionalLights` — they never read the
  vertex/material RGBA — so every animated mesh came out in the plain scene
  light color, i.e. gray. The per-part `mat->ambient` the setup code set (and
  the object-color multiply on it) was silently discarded by the shader.
  **Fix** (codegen only, no engine/VU1 change): fold each part's material
  albedo (glTF `baseColorFactor`, already carried in the `.tskl`) into that
  part's own light + ambient colors, since `outputColor = Σ lightColor·(L·N) +
  ambient` means scaling the colors by albedo M yields `M · sceneLighting`
  exactly. Each `AnimPart` now owns a `PipelineDirLightsBag` + `litColors[4]`
  built in `setupAnimObject` (scene diffuse/ambient × baseColor; directions
  stay shared). This matches the viewport, which tints the baked mesh by
  `shadeOf(n) · baseColor`. Dropped the old object-color multiply so the
  in-game look equals the editor's (the viewport does not tint animated models
  by object color; a non-white default color would otherwise recolor every
  imported .glb). Touches the `AnimPart` struct in both game-header templates
  (orbit + fpp) and the shared `setupAnimObject` body in templates.cpp.
  **Editor UI**: the animated-model Properties block gained a **Materials**
  summary (color swatch + name, "(textured)" tag) from a new `GlbInfo::
  materials` list, so the model's materials are visible where you set the clip.
  Verified: clean editor build; Docker build of a spider2 scene compiles the
  regenerated `terrain_game.cpp` (litColors fold present at lines 903-918) and
  links; `spider2.tskl` carries the three material names + exact colors
  `(1,0,0)`/`(0,0,0)`/`(0.8,0.8,0.8)`; the ELF boots and executes in PCSX2 with
  no assert (emulog). **PCSX2 F8 screenshot confirms the spider renders with a
  red body, black legs and a gray tee — the same colors the editor shows, no
  longer flat gray.** (Capture note: several parallel editor sessions shared
  the single PCSX2 install — each one's `--run` `taskkill`s all PCSX2 and
  steals focus — and this box has no real GPU, so GDI/PrintWindow grabs were
  garbage; only native F8-via-PostMessage to the spidertest window, read from
  snaps/, worked. See the pcsx2-test-environment memory.)

- (63) **Terrain chunking + camera-ring streaming (large maps)** — the terrain
  mesh is no longer one monolithic StaPip bag. The generated game cuts the
  heightmap grid into 16x16-cell chunks (`TERRAIN_CHUNK_CELLS`), each with its
  own `StaPipBag` pointing into a pool slot's vectors; the engine's whole-bag
  bbox frustum check (stapip_core.cpp early-out) then rejects off-screen chunks
  EE-side before any packaging/clipping work — terrain behind the camera no
  longer costs EE time (the 98k-vert clipbench scene spent ~9 ms/frame there).
  On top of that, a new **Preferences > Terrain > View distance**
  (`terrainViewDistance`, 0 = off/whole map) keeps only the chunks within that
  range of the view focus resident: `updateTerrainChunks()` evicts tiles that
  leave the focus rect (one tile of hysteresis) and builds missing ones
  nearest-first at 2 chunks/frame — same trickle pattern as the layer
  streaming; `loadScene` drains the start ring synchronously behind the
  loading screen. Mesh RAM becomes constant in map size (ring rect x ~49 KB
  per untextured chunk). Gameplay is unaffected by unbuilt chunks — physics
  samples the `TERRAIN_HEIGHTS` table, not the mesh — so pop-in is purely
  visual (pair view distance with fog). Reused slots bump `bboxVersion`
  (same bag pointer, new content — otherwise the bbox cacher culls with stale
  boxes, the exact trap entry 61 documented). **Detail cap raised 128 -> 512**
  (slider + load clamp; heights file cap 1025 already allowed it) and the
  Preferences panel now shows a resident-mesh estimate with an orange warning
  above ~8 MB, since 512x512 cells fully resident would be ~75 MB — far past
  the PS2's 32 MB. New-scene terrain size cap aligned to 4096 (the New Project
  dialog already allowed it). **Editor viewport chunked the same way** (64-cell
  chunks): sculpting now rebuilds only the chunks under the brush
  (`Viewport::updateTerrainRegion`, +2-cell shading margin) instead of the
  whole map every stroke frame, and above 128x128 total cells the per-cell
  grid lines drop to chunk borders only (a full grid on 512x512 is solid
  noise and tens of MB of line vertices). Terrain stays outside the layer
  system by design — chunk residency is camera-driven, layers are
  script-driven. **Verified**: clean editor build; headless `--new` codegen
  grep (chunk functions present, no `generateTerrainGrid` outside the V1
  legacy templates); two Docker builds + PCSX2 boots on the **software
  renderer**: 96x96 FPP default (view distance 0 — whole map resident,
  continuous checker, no chunk seams, 50 FPS) and 512x512 at detail 256 with
  view distance 60 + fog 20-58 (ring builds around spawn, fog hides the ring
  edge, 50 FPS, EE% same as the small map); editor GUI opened on the 512 map
  (chunk-border grid renders, fog preview correct). samples/script-demo
  regenerated + rebuilt OK. Walking across chunk borders (pad) and sculpt
  brush feel on a 512 map still want a hands-on human pass.
  **Real-PS2 follow-up (same day, bigdemo stress scene: 2048x2048 at detail
  512, view distance 70 + fog 25-66, 1100 draw-distance objects, 30 skeletal
  spiders with anim/mesh LOD, COP0 PERF lines over ps2link):** steady state
  held a locked 50 FPS while walking (frame_avg ~19.99 ms), but every
  chunk-border crossing hitched one frame to a quantized ~340 ms (17
  vsyncs). Root cause was NOT the chunk mesh work itself: `pointLightAt`
  scanned the whole SCENE_OBJECTS table per vertex looking for point
  lights, and at 1131 objects that streams the ~250 KB table through the
  EE's 16 KB dcache 1024 times per chunk (~170 ms/chunk x 2-chunk budget).
  The old monolithic build paid the same cost once, behind the loading
  screen - chunk streaming moved it into gameplay and exposed it. Fix in
  the game template: point lights are collected once per scene load into a
  small list (`collectScenePointLights`, called at the top of `loadScene`);
  `pointLightAt` iterates that list, so scenes without point lights bake
  chunks in microseconds. PS2-toolchain compile verified in the bigdemo
  container; the console re-test of the fixed build is in the user's hands
  (pad session was live while this landed).

- (62) **Decal object type (transparent textured quad)** — a new
  `PrimitiveType::Decal = 13` for signs/posters/text on walls. A flat unit quad
  in the XY plane facing +Z, textured through the object's assigned material
  (`map_Kd`) with transparency. **Key finding: the PS2 pipeline already supports
  texture alpha** — the PNG loader keeps RGBA / palette+`tRNS` alpha, the static
  pipeline runs an alpha test (`NOTEQUAL` ref 0, drops fully-transparent texels)
  with blending on by default, and `pngquant` preserves `tRNS` through 8/4-bit
  quantization. So the decal needed no new engine rendering path, only: geometry
  (`addDecal`, XY quad nudged +Z by 0.02 so it sits just in front of the surface
  instead of z-fighting), exclusion from player collision (type 13 skipped, like
  the other markers), and editor support. **Editor viewport preview**: the
  fragment shader gained a `uAlpha` cutout/blend path (samples the texture alpha,
  discards near-zero, blends the rest) so decals look the same in the editor as
  in-game; other primitives stay opaque. **UI**: Insert > Object > Decal;
  `addDecal()` defaults to white tint (shows the texture untinted), collision
  none, eye-height; Properties shows the material picker + transform + color but
  no physics/usable/collision (it is a pure visual overlay). Not added to the
  Box/Sphere/... "Type" combo (distinct type; its enum value is non-contiguous).
  **Verified end-to-end**: clean editor build; a scratch project with a decal
  using a hand-made 64x64 transparent PNG (auto-quantized to 4-bit palette +
  `tRNS`, confirmed palette index 0 = alpha 0) built through Docker + PS2 `make`
  (`=== Build OK ===`) and **booted in PCSX2** — the decal renders as a clean
  red disc with a yellow bar and the transparent PNG background is fully cut out
  (the terrain checkerboard shows through the quad's corners, no square outline,
  no z-fighting). scene_data emits `type 13`, material index, collision none;
  load/save round-trips. Interactive placement feel (nudging a decal flush onto
  an arbitrary wall) still wants a hands-on human pass.

- (61) **Plane primitive + gray default for simple objects** — added a `Plane`
  shape (`PrimitiveType::Plane = 12`, kept non-contiguous but stable): a flat
  unit square in the XZ plane, rendered **double-sided** (top +Y and bottom -Y
  quads) so it's visible from both faces. Full chain: enum + `operator==` is
  unaffected (no new field), `primitiveTypeName`/`primitiveTypeFromName`
  (`"plane"`), `unitPlane()` + `plane_` mesh in the viewport (build/destroy/
  `meshFor`), `addPlane()` + dispatch `case 12` in the game codegen
  (`templates.cpp`), Insert > Shapes > Plane and `typeLabel` in the UI. The
  Properties "Type" combo can't cast index→enum anymore (Plane=12 isn't
  adjacent to Box..Cone=0..3), so it now maps through an explicit `kShapeTypes`
  list; `isShape` includes Plane so it gets rotation/scale/color/material +
  box collision like the other primitives. **Gray default**: changed the
  `SceneObject::color` struct default from the terracotta `{0.8,0.35,0.25}` to
  neutral `{0.6,0.6,0.6}`. Safe because every specialized type (spawn, player,
  emitter, sound, light, empty, save point) overrides its color explicitly and
  `color` is always emitted in the `.tyra` save, so only fresh Box/Sphere/
  Cylinder/Cone/Plane/Model objects pick up the new gray; existing projects
  load their stored color unchanged. **Verified**: clean editor build; a
  scratch project with a hand-injected `"type":"plane"` object round-tripped
  through load/save intact and emitted `{12, ..., {0.6F,0.6F,0.6F}, ...}` in
  `inc/scene_data.hpp`; the full Docker + PS2 `make` e2e build succeeded
  (`=== Build OK ===`), so the generated `addPlane` compiles on the PS2
  toolchain. Editor viewport render path reuses the proven `pushQuadShaded`
  quad code; an on-screen viewport pixel check of the plane still wants a
  human/interactive pass (the GDI grab only caught the 4K window's top-left).

- (50) **Viewport camera recenter buttons + toolbar cleanup** — two new
  camera actions plus a reorganization of the viewport overlay controls.
  **Center view** calls the new `Viewport::resetView()`, which restores the
  orbit pivot to the world origin, the default yaw/pitch, and a distance
  framing the whole terrain (the exact framing `setTerrain` picks on first
  load, so a fresh scene and a reset look identical). **Center selection**
  snaps the pivot onto the selected object via the existing `setTarget()` and
  updates `navFocusedIndex_` so the "orbit around selection" preference stays
  consistent; `BeginDisabled` when nothing is selected. Neither touches the
  project model, so no `commitChange()`/serialization.
  **Overlay layout** (per user request): the top-left row now holds only the
  tools (Move/Rotate/Scale + Sculpt, shortcuts 1-4); the gizmo axis space
  (World/Camera) moved to the bottom-right corner and the two camera-recenter
  buttons to the bottom-left (both bottom rows anchored to `imgPos + avail`,
  right edge measured from `CalcTextSize`+FramePadding); the render-mode switch
  (Solid/Wireframe/Wire+Solid) and the PAL/NTSC TV-safe frames moved out of the
  overlay into the menu-bar **View** menu (render mode disabled without a
  project since it persists via `saveAll`). Verified: clean editor build; the
  reorganized toolbar and View-menu items confirmed in the GUI by the user.

- (49) **Fog emitter upgraded to a swirling roll** — instead of a new
  object type, the existing particle emitter's fog kind (2) grew the two
  things the rolling fog needed: per-puff **rotation in the camera plane**
  (angle = golden-angle phase per particle + slow spin, direction
  alternating per puff - billboards get a full 3D right vector so the
  rotation shows) and a **density knob** (the existing Opacity field now
  drives fog alpha, 0..1 -> peak 0..60; the old hardcoded look ~= 0.3).
  Editor Properties shows Opacity for the fog kind with a recipe hint (big
  spawn area + Follow player + soft-alpha texture via material + color
  matched to the distance fog). Game sim and viewport preview twin changed
  in lockstep (both marked keep-in-sync). Verified e2e in PCSX2 software
  renderer: rotated fog quads visibly swirling over the dark scene, fog +
  flashlight still correct underneath, 52 FPS. Known gap: an untextured
  fog puff shows its quad edges - a generated default soft-alpha puff
  texture (stb_image_write is already in the editor) is the natural
  follow-up.

- (48) **Camera flashlight (dynamic spot light)** â€” dynamic spot light
  computed per vertex on VU1 in the StaPip color programs (cull C/TC), the
  paths every editor-generated game renders through. No N.L term (the color
  paths carry no normals - the same flat cone + distance falloff trick early
  hardware-lit games used): `I = clamp((tÂ˛-cosÂ˛Î¸Â·distÂ˛)Â·invSoft) Â· clamp(1-distÂ˛/rangeÂ˛)`
  where t is the distance along the beam - no sqrt and no extra DIV on VU1
  (the cone test compares squares). The world light is transformed per mesh
  into object space on the EE via a new affine inverse in
  `stapip_qbuffer_renderer.cpp` (range rescaled by the mesh scale, correct
  for uniform scales); the three spot quads ride the dir-lights VU1
  addresses, which are free in the color programs. EE-clipped triangles get
  the same formula baked into local color copies before clip interpolation
  (`StaPipClipper` + `addSpotToColor` - keep in sync with
  `CalculateTyraSpotLight` in tyra_macros.i), so screen-edge geometry shows
  no seams. Engine API: `RendererCore::setSpotLight/disableSpotLight`;
  generated games re-aim it at the camera every frame before beginFrame.
  Editor: flashlight is a scene-visual category (enabled/color/range/cone
  half-angle, project + per-scene override), viewport previews the exact
  formula from the editor camera. Verified e2e in PCSX2 software renderer
  (dark scene + fog + flashlight): visible beam brightening the terrain
  strip ahead of the camera and box faces inside the cone, 50 FPS steady.
  Known limits: DynPip (animated models) does not receive the flashlight
  yet; single-color bags only get it on the cull path (all editor bags are
  multicolor); non-uniform mesh scale distorts the cone slightly.

- (47) **GS hardware distance fog (atmospheric fade-out)** â€” per-vertex fog
  coefficient computed on VU1, blended by the GS toward `FOGCOL` for free at
  the pixel level. Engine (`vendor/tyra`): all 8 StaPip + 4 DynPip VU1
  programs now send packed **XYZF2** instead of XYZ2 (new
  `CalculateTyraFog`/`PerformTyraFogClipCheck`/`StoreTyraFog` macros in
  `tyra_macros.i`; F = clamp(w*scale+offset, 0..255) from the clip-space W,
  ftoi4 lands F<<4 exactly in XYZF2's bit 4-11 field). The cull path masks
  the upstream 0x7FFF ADC word down to bit 15 before OR-ing fog bits in
  (XYZ2 ignored the low bits, XYZF2 reads F from them); the as_is C/D
  programs now load the full vertex quad (W carries view distance through
  the EE clipper - its `operator/=` divides xyz only). Z scale went from
  `0xFFFFFF/32` to `0xFFFFFF/2` because XYZF2 reads Z from bits 4-27 -
  numerically identical depth values as before (utility lines and mcpip
  stay consistent without changes), z-buffer stays PSMZ32. Fog params ride
  in the previously-padded words of the per-mesh VU1 options quad (zero
  extra DMA); `RendererCore::setFog/disableFog` + FOGCOL register write;
  per-bag `fogDisabled` opt-out used by the sky dome (it sits past fog end
  and would go solid gray). Fixed in passing: dynpip_d stored vertex 2/3
  ADC at vertex 1's offset (upstream copy-paste bug). Editor: fog is a new
  scene-visual category (project + per-scene override), serialized in
  `.tyra`, codegen emits `FOG_*` per-scene arrays and the game applies
  setFog at init/scene switch; the viewport previews the same coefficient
  in GLSL (sky excluded, same as the game). Verified e2e: fogtest scene in
  PCSX2 **software renderer** - near boxes saturated, far boxes/terrain
  fade to fog gray, sky stays blue, 50 FPS steady, both cull and as_is
  paths exercised (precise clipping); pixel-sampled the gradient. DynPip
  fog compiles but needs an animated-model scene for a visual pass.
- (45) **Material Editor (Tools > Material Editor) with a live preview** â€”
- (49) **Auto-generated mesh LOD for animated models (LOD tier 2)** — new
  Preferences > Rendering > `Mesh LOD distance` (0 = off): the build bakes
  decimated variants of every .tskl part (~50% and ~25% of the welded
  vertex count) and instances render them beyond the distance / twice the
  distance. **Baker** (glbparser): quadric-error HALF-edge collapse - a
  vertex snaps onto a neighbor, so normals/uvs/skin bindings are never
  blended and skinning stays valid by construction; vertices weld by their
  full attribute tuple (uv/normal seams can't be crossed), seam twins and
  open borders are locked, collapses run in sorted-cost rounds. Spider
  test model: legs 1050 -> 456 -> 162 verts. **Format**: .tskl v2 (per
  part: lodCount + arrays); the loader accepts v1, part merging merges LOD
  chains with base-array fallback. **Engine**: SkelInstance holds repacked
  bind data + skin buffers per (part, LOD); ensurePose(lod) re-skins on a
  pose change OR a tier switch (other tiers' buffers hold older skins);
  lodArrays()/lodCount()/currentLod() expose the renderable arrays.
  **Codegen**: tier by camera distance; pose-sharing groups key on the
  tier too. LODs are baked only when the preference is on (they cost RAM
  and file size; spider2.tskl 97 -> 117 KB). Verified: PCSX2 boot shows
  full-detail near spiders and visibly simplified far ones at 50 FPS
  (quality note: tune the distance so reduced meshes are small on
  screen); real-PS2 PERF on the 15-desynced-spider stress scene (12 in
  view, camera 4 units from the nearest): anim+submit 22.9 ms (hard
  25 FPS) -> 17.6 (mesh LOD 8u) -> 14.2 (+anim LOD 8u) -> 11.1 ms (both
  at 5u; the frame then hovers at the vsync boundary, mixed 20/40 ms).
  The stress scene stays extreme by design - real scenes tune distances.

- (48) **Animation LOD (LOD tier 1)** — new Preferences > Rendering >
  `Animation LOD distance` (0 = off): animated-model instances farther than
  this refresh their pose/skinning every 2nd frame, and every 4th beyond
  twice the distance, staggered by object index so refreshes spread across
  frames. Playback time is unaffected (the pose catches up on the next
  refresh) and an instance that just (re)entered the view always skins
  immediately - a held pose can be arbitrarily stale after off-screen time.
  The animated pass now draws in-view instances **nearest first** (two-pass:
  collect + sort by camera distance): the pose-sharing mesh owner becomes
  the group's closest on-screen member (LOD rate follows the closest copy)
  and the GS gets front-to-back z-rejection for free. Verified on the real
  PS2 (15 desynced spiders - unique speeds, so no pose sharing - fixed at
  the heaviest angle, 12 in view): pose+skin 10.36 ms -> 6.51 ms (-37%,
  most instances in the half-rate band), submit 12.5 -> 11.0 ms; the scene
  is still submit-bound at this density (mesh LOD is the next tier).
  LOD off = numbers identical to the previous build.

- (47) **Per-object draw distance (LOD tier 0)** — new `Draw distance`
  property on solid objects (Properties panel, 0 = unlimited): farther than
  this from the camera the object is skipped at draw time in both the
  static and the animated pass (`beyondDrawDistance()` in the generated
  game); collision, sounds and scripts still run, and animated instances
  keep advancing playback time so `animFinished` stays honest. Serialized
  as `"drawDistance"` in the object JSON (omitted at the default 0 - old
  projects load unchanged); baked as a new `SceneObjectData.drawDistance`
  column. The editor viewport intentionally ignores it (perf setting, not a
  look). Also fixed a stale Properties label: animated-model collision box
  is the all-clips AABB since (45), not "frame-0". Verified: editor + Docker
  game build clean; PCSX2 boot of a 15-spider scene with `drawDistance = 8`
  on every second spider shows exactly the near ones (screenshot), 50 FPS.

- (45) **Material Editor (Tools > Material Editor) with a live preview** —
  create/edit the `.mtl` materials the pipeline already consumes, without
  leaving the editor. **No new data model**: materials stay plain Wavefront
  files under `res/` (the same files `SceneObject::materialPath` references
  and the PS2 `LeanObjLoader` parses), edited in place; the editor reads and
  writes the pipeline subset (`newmtl`/`Kd`/`map_Kd`) and preserves unknown
  lines of hand-imported files verbatim. **Brightness**: a 0-2 slider
  multiplied into the written `Kd` (so both the game and the viewport pick it
  up with zero runtime changes) plus a `# tyra-brightness` comment hint so
  the color x brightness split round-trips exactly; parsers all ignore
  comments. Kd is capped at 1.99 (PS2 texture modulation tops out at
  255/128 = ~2x) and the generated `pushVert` now clamps the final vertex
  color at 255 so a bright material cannot wrap the GS color on untextured
  primitives. **Window**: file list + "New material..." (also reachable from
  Project > Assets and an "Edit..." button next to every Material combo in
  Properties), per-file entry management (add/remove/reorder - first entry =
  what primitives/emitters use), name/color/brightness/texture editing that
  saves to disk on every committed change and invalidates the caches
  (`Viewport::invalidateAssets` + modelInfoCache), so the scene viewport
  updates live. Texture combo lists PNGs next to the `.mtl` (map_Kd resolves
  relative to it - same rule as the game), offers Import PNG, and warns on
  non-power-of-two sizes. **Preview** (`Viewport::renderMaterialPreview`):
  own FBO + gradient backdrop + checker floor + a turntable
  box/sphere/cylinder/cone drawn with the shared scene shader and unit
  meshes, camera orbiting (not the mesh spinning - the directional shade is
  baked into the vertex colors). Verified: clean build; a scratch project
  with a hand-written `rusty.mtl` (`# tyra-brightness 1.5`,
  `Kd 1.35 0.9 0.525`, 64x64 checker map_Kd) opened in the GUI - the window
  screenshot shows color decomposed back to (230,153,89) + brightness 1.50,
  the "64x64" size line, the lit textured preview sphere, and the crate in
  the scene viewport textured by the same material; a second shot shows the
  turntable advanced and a "Saved res/materials/rusty.mtl" status from a
  live edit, with the on-disk file byte-identical on reload. Full Docker
  build of the scratch project: `=== Build OK ===`, generated
  `terrain_game.cpp` carries the `c255` clamp, `scene_data.hpp` the
  MATERIAL_PATHS entry, and `bin/materials/rusty.mtl` ships. In-game visuals
  of a >1-brightness material on a real console still merit a human eyeball.
- (46) **Frame drops no longer halve gameplay speed + "Disable VSync
  (experimental)" preference** â€” with double-buffered vsync a PAL game is
  quantized to 50/25/16.7 FPS (a 20.1 ms frame waits for the next vblank),
  and the generated games set `g_frameDt`/`g_frameScale` once at init
  assuming full rate - so dropping to 25 FPS also meant *half-speed*
  gameplay. Now `updateFrameClock()` (generated prolog, called first thing
  in both loop() variants) measures the real frame time (EE COP0 Count,
  wrap-safe) and refreshes dt/scale every frame: within Â±10% of the nominal
  vsync step it snaps to exactly nominal (full-rate behavior stays
  bit-identical), longer/shorter frames pass through clamped to [1/4, 4x]
  nominal (a scene load is a hitch, not a gameplay leap). Known gap,
  documented: frame-counter timers (`everyFrames` - flow-graph Every N
  Seconds, sound-emitter intervals, HUD holds) still count rendered frames
  and stretch at 25 FPS. Second half: **Project > Preferences > Build >
  "Disable VSync (experimental)"** (`ProjectSettings::disableVsync`, saved
  to .tyra, baked as `FRAME_LIMIT` into terrain_config.hpp; init calls
  `setFrameLimit(false)`) - skips the vsync wait before the flip, making
  the frame rate continuous instead of snapping 50 -> 25, at the cost of
  tearing. Verified on the real PS2 (15-spider scene, PERF telemetry,
  full sweeps): vsync ON - 50 FPS up to 11 spiders in view, 25 FPS zone at
  14-15 with `dtus` flipping 19999 -> 40000 exactly when frames double
  (compensation active); vsync OFF - endFrame wait collapses ~12 ms ->
  0.53 ms and frame times go continuous: 7.1 ms empty view (~140 FPS),
  15.8 ms at 11 spiders (~63), 23.2 ms at 15 (~43 FPS instead of a hard
  25). A by-eye TV check of tearing severity stays with a human.

- (45) **Animated models: 50 FPS on real hardware (was 25 at most camera
  angles)** â€” the anim-test-many scene (7 skeletal 1092-vert spiders) halved
  to exactly 25 FPS whenever several were on screen, PCSX2 never showed it.
  Diagnosed with a throwaway instrumented build (COP0 frame-phase timers +
  auto-spinning camera, PERF lines over ps2link): the frame was **EE-bound**
  â€” every instance paid ~0.9 ms pose+skin plus ~1 ms DynPip submit every
  frame, visible or not (~9.6 ms with zero spiders on screen), and with 7 in
  view the busy time alone crossed the 20 ms PAL budget. (An earlier
  guard-band-raster theory did not survive measurement: exact EE clipping
  changed nothing at the bad angles - the "skip one spider" experiment had
  merely removed ~1.5 ms of EE work.) Fix, all four parts measured on the
  console: **(a)** skinned meshes render through **StaPip** instead of
  DynPip - one vertex upload (DynPip sends from/to pairs and lerps them for
  nothing on single-frame skeletal meshes), no VU1 program swap mid-frame,
  EE clipping, per-package culling; **(b)** whole-instance frustum culling
  with the .tskl AABB (now baked as a sampled union over every clip - the
  runtime pads it 10%/axis; also fattens the collision box of clips that
  reach out) - culled instances only advance playback time
  (`SkelInstance::advance()/ensurePose()` split, engine); **(c)** instances
  striking the identical pose (same clip advanced in lockstep - autoplayed
  packs/props) share one skinned mesh via `SkelInstance::poseEquals`;
  **(d)** TsklLoader merges parts with equal texture+color (fewer bags =
  fewer object-data DMAs/packager runs), and the StaPip bbox cacher
  recomputes version-bumped entries **in place** (per-frame `bboxVersion`
  bumps used to pile up 250-frame-retention cache entries). Verified on the
  real PS2 (360Â° sweeps, PERF telemetry): 50 FPS at every angle (worst
  angle was 39.6 ms/frame, now 19.99 ms with ~7 ms vsync idle); offscreen
  anim cost 9.6 ms -> ~6 us; 7 synced spiders skin once (anim 6.0 ms ->
  0.86 ms); a mixed-clip variant (5 synced + 1 stand + 1 slow) shows exactly
  3x the single-skin cost - groups split correctly - still 50 FPS. PCSX2 SW
  renderer: spiders at the old worst angle render identically to the DynPip
  path (lighting formula and light-data layout are shared between the
  pipelines). Docs updated (animated-models.md).

- (44) **PS2 deploy: Stop / second launch hung the console (thread-priority
  regression)** â€” first network deploy worked, but Stop on PS2 and every
  redeploy left the console frozen on the old game. Root cause: entry (40)'s
  audio work demoted the game's main thread to priority 0x10 so the audio
  threads (0x5/0x6) could preempt it â€” but ps2link's EE command thread (the
  one that services `ps2client reset`/`execee` while a game runs, verified
  in ps2link's `ee/cmdHandler.c`) runs at priority 20, and the EE scheduler
  is strictly priority-based. Since the engine's GS/vsync waits are
  busy-spins that never block, a priority-16 game starved ps2link forever
  and the reset UDP command was received (IOP side) but never processed
  (EE side). Fix (`vendor/tyra/engine/src/engine.cpp`): demote to 0x40
  instead â€” identical to ps2link's own user-program priority, so audio
  (0x5/0x6) and ps2link (20) both preempt the render loop; PCSX2/ISO boots
  keep the same audio-over-game ordering as before. Verified: engine +
  game rebuild clean (libtyra re-archived, ps2net relinked); the actual
  stop â†’ redeploy cycle on the real console needs a hands-on test. Note:
  a console already stuck with the old ELF must be power-cycled back into
  PS2LINK once â€” the fix rides in the game binary.

- (43) **Viewport wire boxes rendered as garbage lines** â€” the selection
  outline and the "Highlight usable objects" boxes drew as random
  criss-crossing lines instead of cubes: `unitWireCube()` (viewport.cpp)
  emitted 6 floats per vertex (pos+color) while `uploadMesh()` reads the
  interleaved buffer as the 8-float pos(3)+color(3)+uv(2) layout, so the
  GPU sampled positions at shifted offsets (and vertexCount came out 18
  instead of 24). Present since the wire cube was written; every other
  mesh builder already pads uv. Fix: emit uv 0,0 per vertex. Verified:
  clean build, then a scratch project (usable box/cylinder/cone with
  rotations + a selected sphere, highlightUsable and selectedObject
  pre-set in the .tyra so no synthetic input needed) opened in the GUI -
  the GDI screenshot shows proper yellow highlight cubes hugging each
  usable object (rotation/scale respected) and the orange selection box
  around the sphere, instead of the previous line spray.
- (35) **Unity-style object scripts + the Empty object type** â€” scripts are
  now attachable components instead of always-running globals. **Model**:
  `SceneObject::scripts` (list of class names; part of `operator==`, undo,
  copy/paste and the `.tyra` round-trip - `"scripts": [...]` emitted only
  when non-empty, old projects load with none) and `PrimitiveType::Empty
  = 11`, a pure transform: sphere marker in the editor (`viewport.cpp`
  meshFor), no geometry/collision/USE in the game (type-11 guards in the
  `rebuildObjectGeometry` switch, `collidePlayer`, `updateUseTarget`),
  editable rotation/scale/color (color doubles as a free script parameter)
  and save-state. **Script API** (`script.hpp`, marker-owned): class
  `ObjectScript` with `self`/`selfIndex` and `onStart`/`onUpdate`/`onUsed`
  virtuals + `TYRA_OBJECT_SCRIPT(Name);` which registers a factory under
  the stringized class name; the old global `Script`/`TYRA_SCRIPT` pair
  stays (flow graphs and existing user scripts untouched). **Codegen**: new
  always-regenerated `src/scripts/object_scripts.gen.cpp` - a
  (scene, object, class-name) attachment table straight from the editor
  plus an `ObjectScriptDriver` global script that rebuilds instances on
  every `sceneGeneration` bump (one `new` per attachment of the active
  scene, `onStart` once, delete on scene switch), refreshes `self` and
  forwards `onUpdate`/`onUsed` - so user-owned `terrain_game.cpp` files
  keep working with zero changes, and per-frame cost is one virtual call
  per attached instance (nothing attached = an empty loop; factory lookup
  is scene-load only). Unregistered attachments log
  "Object script not registered" and are skipped. **UI**: Properties >
  Scripts on every object type (attach combo fed by scanning
  `src/scripts/*.cpp` for `TYRA_OBJECT_SCRIPT(...)` with an mtime cache,
  red "not found" for stale names, per-entry remove, "New script..." which
  writes the stub - now an `ObjectScript` - and auto-attaches it to the
  selected object); `+ Add object > Empty`. Verified e2e (Docker + PCSX2):
  scratch orbit project with `Spinner` attached to two boxes and `SkyTint`
  + a bogus `MissingScript` on an Empty - sky renders in the Empty's color
  (onStart + self on a marker object), per-instance marker files written
  from `onUpdate` read `rotY=270.0` and `rotY=315.0` after 150 frames (90
  deg/s from 0 and 45 deg starts - independent per-instance state,
  wall-clock dt), no sphere rendered in-game at the Empty's position, the
  missing script only logs (seen in the editor Debug panel via bin/log.txt)
  and the game runs on; save/load round-trip checked with a host harness
  linking project.cpp.obj (`ROUNDTRIP OK`); editor GUI screenshot shows the
  Empty properties + Scripts list with the not-found marker. `onUsed` (USE
  on a usable object with an attached script) compiles and mirrors the flow
  graph's On Used dispatch but still needs a hands-on pad test.
  samples/script-demo regenerated with the new templates. README updated;
  user guide added (docs/object-scripts.md: lifecycle, self/ScriptContext
  reference, Empty objects, performance, troubleshooting).
- (34) **Skeletal animation skinning moved from the EE FPU to VU0 (macro
- (42) **Main thread demoted below the audio threads (music dragged at low
  FPS)** â€” music played cleanly at 50 FPS but audibly slowed when a heavy
  scene dropped the game to 25: the EE is one core, the engine's GS waits
  are busy-spins that never yield, and depending on the boot path the main
  thread arrives at priority 0 - so a compute-bound frame starved the audio
  thread (0x5) and the song streamer (0x6), which could never preempt it.
  Engine::initAll now drops the main thread to 0x10; the audio threads cost
  microseconds per wake, so the game loses nothing. Verified in PCSX2 (50
  FPS, steady music, overlay fine); the 25-FPS-scene behavior needs the
  real console. Also: MEM reads used/total, the music PS2-build row fits
  the mono checkbox again (a wide combo pushed it out of the panel).

- (41) **Song streamer thread + EE-load overlay** â€” the last structural fix
  for network music: the fillbuf callback fires when the audsrv ring is
  nearly EMPTY, so any file read on the audio thread races a ring holding
  under ~1/9th s of audio - one slow network round trip at that moment is a
  guaranteed audible underrun (the remaining "skipping slow-mo" after the
  RPC fix). AudioSong now runs a dedicated streamer thread (priority 0x6,
  16 KB stack) that keeps a 96 KB single-producer/single-consumer ring
  topped up with 16 KB freads in the background; work() only memcpys from
  memory and never blocks on IO. Wrap-safe u32 produced/consumed counters
  (EE is single-core, aligned word writes are atomic); load/rewind/unload
  reposition the FILE through a generation handshake (the streamer owns all
  file access while active, so an in-flight fread of stale data is simply
  discarded); transient starvation feeds a short chunk and retries without
  waiting for a fillbuf signal that would never come; songFinished now
  requires true end-of-data. Verified in PCSX2: OnStart tone plays with a
  flat WASAPI peak across the loop point. **EE-load overlay**: the debug
  FPS line gains "EE nn" - T3 ticks (576 kHz) between loop() entry and
  drawDebugHud as a percentage of the frame budget, i.e. the game's
  main-thread work before it starts waiting on the GS; works on real
  hardware (unlike PCSX2's emulator-side EE% readout).

- (40) **Music + sound emitters = FPS drop: audsrv RPC contention fix** â€”
  reproduced in PCSX2 (finally an EE-side bug, not the network): a scene
  with one sound emitter ran 50 FPS silent but 42 FPS with music playing,
  while a scene with music and NO emitters held 50. Mechanism:
  updateSoundEmitters issued a synchronous audsrv RPC per emitter per frame
  (setVolumeAndPan, even when nothing changed), and every audsrv call
  shares one client-side completion semaphore with the music thread's
  wait_audio/play_audio transactions - the main thread queued behind the
  stream several times a second. The generated game now caches the last
  volume/pan per channel (quantized 5/10 steps so a moving player does not
  defeat the cache) and only issues the RPC on a real change; the
  scene-switch mute keeps the cache in sync. Verified in PCSX2: the same
  emitter+music scene is back to 50 FPS. Also confirmed the debug FPS/MEM
  overlay has no emulator gate - it renders on real hardware too (the "not
  on the console" report was a stale pre-debug-profile deploy).

- (39) **Per-track music build conversion + Stop on PS2** â€” the experiment
  window for network music: every Music-panel track gets "PS2 build"
  controls (rate: keep/48000/32000/22050/11025 + mono; only rates audsrv has
  an upsampler for - a first cut offered 16000, which audsrv does NOT
  support). Key insight from audsrv sources: 48000 Hz 16-bit is SPU2-native
  and its "upsampler" is a plain channel demux with zero arithmetic - after
  TCP_NODELAY the bottleneck is the 36 MHz IOP (which also runs the ps2link
  network stack and answers the main thread's per-frame pad/audsrv RPCs -
  the observed "FPS drops when music starts"), so upsampling ON THE PC to
  48 kHz and streaming more bytes is the win, not less. The res/audio source
  stays untouched; after every build the Runner re-converts the bin/audio
  copy the game actually streams (wavconvert grew a mono downmix - channel
  average before resampling), so PCSX2, PS2 deploys and exported ISOs all
  hear the downgraded version and each step halves the streamed byte rate.
  Stored as a path-keyed map in the .tyra (absent = ship as-is). Verified:
  a 22050 stereo tone with rate=11025+mono builds a bin copy exactly 1/4
  the size (mono, 11025 Hz per the WAV header) with the source untouched,
  and PCSX2 plays it with a steady WASAPI peak - the low-byte-rate
  small-chunk path works with the read-ahead stage. **Stop on PS2** (Build
  menu): kills the ps2client file server and resets ps2link, so the console
  reboots back into its listening state instead of hanging on dead file
  handles when you just want the game gone.

- (38) **Song read-ahead stage (network music without hiccups)** â€” feeding
  audsrv straight from per-chunk freads gives every read a hard deadline of
  one chunk of audio (46 ms at 22 kHz stereo); over ps2link any latency
  outlier is an audible hiccup ("almost right, still snags" on the real
  console after the 22 kHz conversion). AudioSong now pulls the file through
  a 24 KB read-ahead stage - one large sequential fread every ~6 chunks,
  amortized against the audsrv ring (~1/9th s) - so only a sustained
  throughput drop below the byte rate can starve playback, not a single slow
  round trip. Loop/rewind resets the stage; the legacy headerless path keeps
  its read-until-EOF semantics (short fread ends the stream). Verified in
  PCSX2 with a generated 440 Hz 22 kHz stereo tone wired OnStart -> PlayMusic:
  WASAPI peak meter shows a rock-steady level for the whole window (a
  starving stream shows dropouts); real-console listen pending. Also
  upstreamed the ps2client TCP_NODELAY fix as ps2dev/ps2client#25.

- (37) **Skeletal animation skinning moved from the EE FPU to VU0 (macro
  mode)** â€” the backlog's era-correct split (animation on VU0, 3D on VU1,
  game code on EE), engine-fork only, zero authoring/codegen changes. The
  `SkelInstance::skinParts` vertex loop is now COP2 inline asm (the
  `M4x4::cross` lqc2/vmadda pattern, no microprogram): matrix-palette blend,
  position+normal transform, vrsqrt normal normalization and the skinned
  AABB (vmini/vmax, kept resident in $vf20/$vf21 across the loop; the
  min.w slot carries the epsilon that replaces the old `len > 1e-6` guard).
  The constructor repacks bind data into 16-byte-aligned qwords (positions
  w=1, normals w=0 so the translation column drops out), prenormalizes the
  u8 weights to floats summing 1, and sorts each vertex's joints by
  descending weight so the loop dispatches on influence count: 1 bone = 4
  matrix loads and no blend, 2 bones = 2-matrix blend (the hot case: 95% of
  the test character), 3-4 = full blend, 0 = zero matrix (vertex collapses
  to the origin exactly like the EE loop). A first, dispatch-less version
  that always blended 4 matrices measured *slower* than the EE loop in
  PCSX2 (median EE 38.7% vs ~37%) - PCSX2's EE JIT prices a COP2 op like an
  FPU op, so op count is everything there; the dispatch version restored
  parity. Pose evaluation (mulM4/fromTrs/slerp) deliberately stays plain C
  for bit-parity with the editor's preview math. Verified (Docker + PCSX2
  SW renderer): skelpar frozen-pose scene (skinned char x2 poses + rigid
  flag, tint) pixel-compared EE vs VU0 - **548 900 game-area pixels, 0
  differ** (both the naive and the dispatch version); skelperf (3 animated
  1440-vert characters, 1368 verts with 2 influences / 72 with 1) holds 50
  FPS with EE median 37.0% (6 samples) vs EE-loop baseline 37.2% (6
  samples, range 34-41%) - emulator parity, the real win is expected on
  hardware where one COP2 FMAC does 4 lanes (real-HW run still pending the
  ps2link memory-card install). The 0- and >=3-influence paths compile but
  no test asset exercises them (the >=3 block is the pixel-verified naive
  blend with sorted slots). Docs updated (animated-models.md).
- (36) **Color grading** â€” DaVinci-style per-project looks, applied as a GS
  post pass (zero EE cost, like bloom/grain). **Engine**:
  `RendererCorePostFx::setGrading/clearGrading` draws flat full-screen
  sprites between bloom and grain â€” per-channel gain via FBMSK-masked
  `(Cd-0)*FIX` sprites (equal gains collapse to one), per-channel lift as
  additive/subtractive flat colors, and one alpha-blend sprite mixing toward
  a constant color (tint + the saturation approximation; two lerps toward
  constants fold into one). Worst case 6 extra sprites, alpha byte always
  FBMSK-protected, z writes stay masked; postfx packet grew 160 -> 224 qw.
  **Model**: `ColorGradingPreset` (brightness/contrast/saturation/
  temperature/tint/lift/gain) + `compileGrading()` in the new grading.hpp â€”
  the ONE place that folds the friendly controls into quantized GS numbers
  (contrast pivot into gain+lift, saturation as a mix toward mid-gray â€” the
  GS has no per-pixel luma, documented as an approximation);
  `Project::gradings` + `defaultGrading`, saved/loaded with defaults for old
  projects (not part of undo snapshots, same as menus). **UI**: Tools >
  Color Grading window (preset list, quick looks, sliders, live "GS pass:"
  readout of the compiled numbers). **Viewport twin**: a fullscreen
  post pass (GRADE_FS) replicates the integer GS math incl. the 0..255
  clamp per blend step; previews the edited preset while the window is
  open, else the project default (gl_loader gained Uniform1f). **Codegen**:
  GRADING_* tables + templated `applySceneGrading()` in scene_data.hpp
  (header stays engine-include-free), `applySceneGrading(engine,
  GRADING_DEFAULT)` in both game templates' init (grading is global â€”
  scene switches keep the active preset), new **Set Color Grading** flow
  node (Scene category, preset picker, "<none>" = clearGrading). Verified:
  editor builds clean; scratch fpp project with two presets round-trips the
  JSON; generated tables hand-checked against compileGrading math; Docker
  build OK (engine + empty- and 2-preset tables compile on the PS2
  toolchain); PCSX2 e2e on the software renderer â€” timed screenshots show
  boot applying the sepia default (sky measured (157,156,148) vs computed
  (151,148,145)) and an Every-6s -> Set Color Grading("night") graph
  switching the frame to the night look at runtime; viewport A/B
  screenshots confirm the editor preview matches. **Color wheels**: Lift and
  Gain are edited through Resolve-style trackballs (custom ImDrawList
  widget: hue disc, draggable puck, double-click reset) - the puck carries
  the zero-mean between-channel tint (exactly the wheel's 2 DOF, so
  puck <-> rgb roundtrips), a master slider under each wheel carries the
  common level, and a compact drag row shows the numbers; commits once per
  gesture (live mutation while dragging, commitChange on release). Window
  rendering verified via a temporary auto-open build + GDI screenshot
  (presets, quick looks, sliders, both wheels and the GS readout all
  render); the drag FEEL still needs a quick human pass (no safe way to
  drive ImGui with synthetic input while the user works the machine).
- (35) **UI (DPI) scaling** â€” the editor was near-unreadable on high-DPI small
  screens (a 4K laptop). On startup it now auto-matches the monitor's content
  scale via `ImGui_ImplGlfw_GetContentScaleForWindow` (a 4K laptop reports e.g.
  2.0-2.5x), so it "just works" with zero config. A new always-available
  **View** menu (plus `Ctrl+=` / `Ctrl+-` zoom and `Ctrl+0` for auto) lets the
  user override: presets 100-300% or auto. `applyUiScale()` resets the style to
  a `baseStyle_` captured once at init, then `ScaleAllSizes(scale)` +
  `FontScaleMain = scale` - resetting from the base each time means repeated
  changes never compound. Fonts stay crisp because this ImGui (1.92) rasterizes
  dynamically and the GL3 backend advertises `RendererHasTextures` (rebuilds the
  atlas on scale change). The override is machine-level, not project data, so it
  lives in a tiny global config `%LOCALAPPDATA%\tyra-editor\editor.ini`
  (`uiScale=<float>`, 0 == auto) - the same editor-owned dir already used for the
  PS2SDK IntelliSense cache; the per-project `.tyra` format is untouched.
  Verified: editor builds clean; launched and screenshotted at forced 100% and
  200% (UI visibly doubles, text stays crisp) and with no config (auto detected
  this machine's ~2.5x DPI and scaled accordingly) - all without crashing.
  Gizmo/ImNodes use screen/clip-space sizing so they were already DPI-neutral.

- (34) **Custom particle kind: full physics knobs (jets, leaks, steam)** â€”
  sixth emitter kind "Custom" exposes the simulation instead of a preset:
  **Speed** (u/s, +-20% jitter) along the emitter's **+Y axis rotated by the
  object rotation** (emitters gained the Rotation field for this - tilt 90
  deg = a horizontal pipe leak; the viewport cone marker rotates with it),
  **Spread** (cone half-angle built from a per-emitter orthonormal tangent
  basis), **Gravity** (u/s^2, negative = buoyant steam), **Weight** (air
  drag ~ 1/weight applied after the pull, so a natural terminal velocity
  emerges - heavy water keeps falling, light steam brakes to a drift),
  **Lifetime** (+-25% jitter), **Grow** (size multiplier reached at death),
  **Opacity** (base alpha, fades in the last quarter of life) and **Die on
  terrain** (per-frame terrainHeightAt check; the particle vanishes and
  respawns - water soaks into the ground instead of clipping through).
  Serialized inside the emitter JSON block only for kind "custom" (8 new
  SceneObjectData columns emitted for every object; defaults keep old
  projects identical); the editor preview runs the same math (shared
  rotateEuler matching the game's rotated()). Add-menu preset: a small
  water-like jet. Verified: editor builds clean; fixture got a "pipe-leak"
  (rot 90 deg X, speed 7, weight 3, die-on-terrain) and a "steam-1"
  (gravity -1.5, weight 0.25, grow 3, opacity 0.35) - generated
  scene_data.hpp rows checked column-by-column, full Docker build to ELF
  OK; editor screenshots show the water arc bending down onto the terrain
  and the growing translucent steam plume, plus the new Properties knobs
  (Speed/Spread/Gravity/Weight/Lifetime/Grow/Opacity + Rotation); PCSX2 SW
  renderer at 50 FPS renders both (240 live particles with the rain from
  (32)); a zoomed pixel check confirmed the steam stays neutral gray (a
  suspected color bug was a contrast illusion against the sky). Hands-on
  pass left for a human: knob feel while watching the live preview.

- (33) **Particle emitters v2: live viewport preview, rain, textures,
  enabled + follow-player** â€” emitters no longer render as scaled cones in
  the editor: the viewport now runs the same per-kind particle simulation as
  the generated game (spawn/velocity/size/alpha formulas copied from
  `updateParticles()` â€” the twin-formula comment marks both sides) on
  per-emitter CPU pools, drawn as alpha-blended camera-facing quads through a
  new small shader (pos + RGBA + UV dynamic buffer, depth-test on / z-write
  off, drawn last like the game). A fixed-size cone marker remains for
  selection/gizmo (dimmed when the emitter is disabled); picking still uses
  the scale box = spawn area. New **Rain** kind (4): thin world-up streaks
  (size = streak length) falling 14-20 u/s from the emitter height and dying
  exactly on the terrain (`terrainHeightAt` per drop), preset under the
  Effects add-menu (area 20x20 at y=12, count 96). New per-emitter options:
  **texture** via the shared Material combo (first material's map_Kd on
  fixed per-quad UVs through a StaPipTextureBag; color tints the texture,
  same in the viewport), **Density (count)** cap raised 128 -> 256,
  **Enabled** (off = starts invisible; the existing Show/Hide/Toggle Object
  flow nodes enable/disable emitters at runtime â€” that mapping is the
  documented on/off switch), and **Follow player** (the emitter position
  becomes an offset from the camera â€” rain that tracks the player instead of
  covering the map; editor previews it in place). Data model: emitterEnabled
  / emitterFollowPlayer (+ operator==), `"enabled"`/`"followPlayer"` in the
  emitter JSON block, `emitEnabled`/`emitFollow` columns in
  SceneObjectData. gl_loader grew glDepthMask. Verified: editor builds
  clean; scratch fpp project with a follow-player rain emitter + a disabled
  fire emitter carrying a material â€” generated scene_data.hpp row checked
  (kind 4, count 96, enabled/follow flags, material index resolved), full
  Docker build to ELF OK; GUI screenshots show animated rain streaks in the
  viewport, the dimmed disabled-fire marker and the new Properties fields;
  `.tyra` round-trips the new keys on reopen+resave; PCSX2 (SW renderer,
  50 FPS) shows rain falling around the FPP camera and the disabled emitter
  correctly absent. Hands-on pass left for a human: checkbox/combo feel and
  a textured emitter in-game (codegen + texture-bag path compiled and
  mirrors the terrain texturing, but no PNG-textured emitter was booted).

- (32) **Flow graph node overhaul** â€” one batch of user-requested block fixes.
  **Trimmed pins**: On Start / On Button / Every N Seconds lose their object
  and bool outputs, Near Object / On Used / On Animation Finished lose the
  bool output (the logic-gate plane keeps its pure sources: Value At Least,
  Get Bool, Int At Least, the new Is Visible); stale links whose pins no
  longer exist are pruned when a graph is opened - imnodes must never see a
  link to an unsubmitted pin. **On Button** now offers all 16 pad buttons
  (D-pad, L1-L3/R1-R3, Start, Select - codegen uses the PadButtons field name
  as-is). **Delay** (new "Time" category): exec-through action, fires its
  "after >" output N seconds later (fractions fine, everyFrames-based
  countdown member per node; re-trigger restarts). **Move Object To**: glides
  the target toward X/Y/Z or a live position link at Speed units/s
  (g_frameDt-based per-frame step; FlowNode::num grew to 4 floats for the
  Speed slot). **Play Animation clip picker**: the node resolves its target
  like codegen (object link chain > name > self) and offers the .glb's clip
  list from glbInfo instead of free text. **Object getters**: new pure
  Is Visible bool source; Self exposes a position output. **Save texts**:
  Project::saveTexts (name + default, "Save data" panel), stored in fixed
  32-byte slots in SaveGameData (SAVE_VERSION 2 - old card saves are
  rejected), zeroed/defaulted at boot, Set/Get Save Text flow nodes,
  ScriptContext::saveTexts. **Text link plane** (cyan pins, pin ids widened
  to 16 slots per node - never persisted, so safe): Log Message takes any
  number of text inputs (appended space-separated after its static text);
  pure text sources Get Save Value, Get Save Text, Get Int As Text, and a
  "Convert" category with Position To Text / Bool To Text. **Fixes**: Set
  Save Value's combo and drag were both labeled "Value" in one ID scope
  (ImGui conflict error) - numeric params now live in their own PushID;
  Value At Least / Int At Least explain in-node that they are per-frame bool
  sources for On Condition / gates; flow node str/str2 are JSON-escaped on
  save (quotes in Log messages used to corrupt the .tyra). Verified: editor
  builds clean; a scratch project exercising every new node round-trips
  through save/load (host harness incl. escaped quotes), generates the
  expected flow_graph.gen.cpp (inspected: delay countdown, mover blocks,
  TYRA_LOG concatenation, snprintf into saveTexts) and compiles+links on the
  PS2 toolchain in Docker; Flow Graph panel screenshot shows the new
  nodes/pins; samples/script-demo regenerated. In-game behavior (delay
  timing, mover feel, pad buttons) still needs a human PCSX2/pad pass.

- (31) **Per-stick deadzones + Project panel cleanup** â€” the Input deadzone
  splits into "Left stick deadzone" (movement) and "Right stick deadzone"
  (camera): worn pads rarely drift equally, and one shared value forced the
  healthy stick to pay for the drifting one. ANALOG_DEADZONE ->
  ANALOG_DEADZONE_L/_R in terrain_config.hpp, axis helpers take the deadzone
  per call site; a legacy "stickDeadzone" key in old .tyra files seeds both.
  The Project panel's Build button row is gone (superseded by the top-level
  Build menu) - the panel keeps only a build-progress/failure line. Verified:
  codegen emits both constants, game compiles on the PS2 toolchain, editor
  builds clean.

- (30) **ps2client TCP_NODELAY (~100x host: throughput), Build menu + Clean,
  orphaned-adpcm sweep** â€” three fixes from the second real-hardware session.
  **TCP_NODELAY**: file serving to the console ran at ~4 KB/s (424 KB texture
  = 106 s, boot = 10 minutes, streamed music sounded like a crashed GameBoy -
  the audsrv ring starved at 44.1 kHz stereo needing 176 KB/s). Root cause:
  ps2client sets no socket options and ps2link fileio is synchronous small
  request/response exchanges - Nagle on the PC side colliding with the PS2's
  delayed ACKs stalls every exchange ~200 ms. `tools/ps2client` now ships a
  patched binary (TCP_NODELAY on the request socket; nodelay.patch + README
  vendored, stock binary kept for comparison, .gitignore whitelists it).
  Measured on the console: the same texture in ~1 s, the same obj 59 s -> 1 s,
  full boot ~30 s. Music needs an ear re-check but now has ~10x bandwidth
  headroom. **Build menu**: VS-style top-level Build menu in the menu bar
  (Build Ctrl+Shift+B / F5 / F6 variants moved out of the Project dropdown;
  Project keeps Preferences + ISO/disc entries; the Project-panel buttons
  stay) plus **Clean** - wipes host bin\ and the container game volume's
  obj+bin, next build starts from scratch. **Orphan sweep**: .adpcm whose
  source WAV vanished from res/sfx (sound removed in the editor) lived
  forever in the container volume and the copy-back rsync resurrected them
  on the host every build - the sfx step now deletes them in-container.
  Verified: menu screenshot (entries, shortcuts, disabled-without-IP PS2
  items), deploy to the real console with the patched ps2client (timestamped
  logs above), editor + Docker game build clean after all changes.

- (29) **Real-pad feedback round: stick deadzone, HUD/particle race fix, PS2
  buttons in the Project panel** â€” three findings from playing on the real
  console. **Deadzone**: the hardcoded 0.20 stick deadzone becomes
  Preferences > Input > "Stick deadzone" (0-0.9, ANALOG_DEADZONE in
  terrain_config.hpp); motion above the threshold now rescales from zero
  instead of stepping at the edge, so raising it for a drifting pad only
  costs stick range. Applied in both player paths (walk/noclip); verified:
  codegen emits the constant and the game compiles on the PS2 toolchain.
  **HUD/particle race** (engine): on real hardware particles flickered out
  in a rectangle around the HUD crosshair every few frames. Sprites go out
  over PATH3 with only a GIF-channel wait, racing the tail of the async
  PATH1/VU1 scene; when the sprite won, it stamped z=max across its whole
  rect (transparent margins included) and the late particle quads z-failed
  inside it. Renderer2D now drains PATH1 once per frame before the first
  sprite (new RendererCore::drained3DFor2D, reset in beginFrame), gated on
  Path1::isVU1Configured like the post fx barrier so pure-2D frames (loading
  screen) cannot spin on a FINISH that VU1 never delivers. Verified in PCSX2
  SW renderer: fire emitter + centered HUD sprite, steady 50 FPS, no
  dropouts; the actual race needs a re-check on the console. **UI**: Build &
  Run on PS2 / Run on PS2 buttons in the Project panel's Build row (disabled
  with a tooltip until the ps2link IP is set), matching the Project-menu F6
  entries.

- (28) **Run on PS2 â€” real-hardware fixes (game verified running on a PS2)** â€”
  the first live deploys surfaced four independent landmines, each verified on
  the actual console:
  (a) **execee arguments never arrive** â€” ps2link passes args in a
  non-standard way that needs a newer newlib crt0 than the `h4570/tyra`
  toolchain image ships, so `-ps2link` silently never reached main() and the
  game reset the IOP, killing ps2link (symptom: rom0:ROMVER opened, every
  host: open dead). Detection now uses a `bin/ps2link.run` marker: written by
  the PS2 deploy, deleted by PCSX2 launches, probed by the game over host:
  BEFORE the engine boots (argv stays as a fallback). Excluded from ISOs.
  (b) **sbv_patch_fileio corrupts ps2link's fileio** â€” it pokes jumps at
  fixed offsets valid for Sony's ROM FILEIO; ps2link's environment runs
  PS2SDK's FILEIO_service reporting the same 1.1 version (the patch's only
  guard) with a different code layout. Skipped under keepIopResident, as are
  iomanX/fileXio loads (ps2link provides both; a second iomanX re-hooks ioman
  onto an empty device table).
  (c) **libpng's chunked freads over the network** made a 400 KB texture take
  minutes (every small read is an EE->IOP->TCP round trip): png_loader now
  reads the whole file with large sequential freads (no fseek - unreliable on
  host fs) and decodes from memory; verified in PCSX2 after the change.
  (d) **the shared engine volume races parallel checkouts** â€” every git
  worktree (parallel Claude sessions) rsyncs its own vendor/tyra over
  `tyra-engine-shared`, endlessly rolling back other checkouts' engine fixes
  (one game even compiled against headers another session had reverted
  seconds earlier). The volume name now carries an FNV-1a hash of the engine
  source path: same checkout shares, parallel checkouts isolate.
  QoL from the same session: Output-panel lines are timestamped (profiling
  slow console boots), TYRA_LOG streams live into the Output panel under
  ps2link (writeLogsToFile off - ps2link forwards EE printf over UDP), the
  post-build step drops stale bin/sfx WAV copies host-side (they shipped in
  ISOs) and warns when an sfx .adpcm exceeds 1 MB (SPU RAM is 2 MB; a 15 MB
  song imported as a sound effect was most of a 10-minute network boot).

- (27) **Run on PS2 â€” network deploy to a real console (ps2link)** â€” the game
  boots on a physical PS2 over ethernet straight from the editor: no ISO, no
  SMB/OPL. The console runs ps2link (one-time memory-card install; package +
  IPCONFIG.DAT prepared under F:\PS2\ps2link-mc for this machine) and the
  editor drives it with ps2client (`tools/ps2client`, fetched by setup.ps1
  alongside a ps2link release). Runner gains `buildAndRunPs2`: the usual
  Docker build, then `ps2client reset` + 3s + `execee host:<name>.elf
  -ps2link` with **cwd = bin/**, so the game's `host:` cwd maps to bin/
  exactly like a PCSX2 run and every asset is served from this PC over the
  network (the PS2 connects back to TCP 18193; logs arrive over UDP 18194 â€”
  both firewall rules created, Private profile, program-scoped). The execee
  process IS the file server, so it stays alive across the session (killed on
  the next deploy/exit; its output streams into the Output panel as "[ps2]"
  lines, giving live console logs in the editor). Engine:
  `IrxLoader::keepIopResident` skips the boot-time `SifIopReset` that would
  unload ps2link mid-boot; generated main.cpp sets it only when launched with
  `-ps2link`, so PCSX2 boots keep the stock reset path (zero regression
  surface). UI: Project > Build && Run on PS2 (F6) / Run on PS2 (no build),
  gated on a new editor-side pref "PS2 (ps2link) IP" (Preferences > Real PS2;
  persisted in the .tyra editor block); CLI: `--build <dir> --run-ps2 [ip]`
  (stays alive relaying console logs while the file server runs). Liveness
  gotcha: ps2link commands are UDP fire-and-forget â€” reset/execee "succeed"
  against a dead IP â€” so success is declared only when the console's first
  log line arrives (15s window), verified both ways: offline IP now fails
  with a helpful message (was a false "Game running"); PCSX2 path re-verified
  end-to-end after the engine change (SW renderer, 50 FPS). Real-hardware
  run pending the one-time ps2link memory-card install.

- (26) **Animated models stage 2: true skeletal runtime (.tskl) + crossfade
  blending** â€” the baked-morph-frame backend from (25) is replaced by a real
  skeletal one; the entire authoring surface (.glb import, clip names,
  Properties fields, flow nodes, script API, RuntimeObject anim fields and
  animFinished semantics) is unchanged. The editor now serializes what
  `glbparser` always parsed instead of sampling it: the .glb parse was
  split into a shared `parseGlb` (used by both `bake()` - still the
  viewport-preview/import-validation path - and the new `parseSkel`), and
  `writeTskl` emits a `.tskl` binary: node hierarchy with bind-pose TRS,
  matrix palette (skin joints with IBMs; rigidly-animated mesh nodes become
  weight-255 identity-IBM slots, so rigid + skinned share ONE runtime path
  and .tanm writing is gone), keyframe tracks (times f32, rotations
  16-bit-quantized quats, constant tracks collapsed to 1 key, CUBICSPLINE
  degraded to linear like stage 1, times rebased per clip) and the expanded
  bind-pose triangle lists with u8 joints/weights (sum-normalized to 255,
  max 256 palette slots). Engine fork additions: `TsklLoader` (whole-file
  read, bounds-checked, validates hierarchy/palette/joints once at load) and
  `SkelInstance` â€” per-object playback (current + fading layer, per-channel
  O(1) key cursors), EE pose evaluation (slerp/step sampling identical to
  the editor's math, parents-first hierarchy walk, palette = global x IBM),
  crossfade as a local-TRS blend (lerp T/S, sign-fixed nlerp R) before one
  hierarchy walk, and EE skinning of positions + normals into an owned
  single-frame DynamicMesh that DynPip renders as verticesFrom==verticesTo
  (interpolation 0) â€” zero new VU1 code, stage-1 lighting/tint/texture
  plumbing reused, frame bboxes refreshed from the skinned AABB every skin.
  Codegen: `GameAnimModel` holds the shared SkelModel + per-part textures,
  `setupAnimObject` builds a SkelInstance per object (fresh material ids
  re-linked, ambient = part color x object color), the render pass applies
  animRestart via `play(clip, loop, fade)` and advances by `g_frameDt *
  animSpeed` (wall-clock, PAL/NTSC-safe); collision still uses the clip-0
  frame-0 AABB (now stored in the .tskl). New surface (M2): optional
  **Fade** param on Play Animation (seconds, 0 = instant), `playAnimation(
  ..., fade)` default arg + `RuntimeObject::animFade` â€” both backward
  compatible; docs/animated-models.md rewritten for the skeletal pipeline.
  Import status now reports the skeletal RAM estimate. Verified: host
  harness + python cross-checker re-evaluates the .tskl pose/skinning math
  at every stage-1 baked frame time and compares vertices (generated
  skinned+rigid 2-clip .glb: 180 comparisons, and a 1440-vert 4-bone 2-clip
  character: 89 280 comparisons; worst pos err 9.5e-4 = quat quantization);
  e2e in Docker+PCSX2 SW renderer: frozen-pose scene rendered by stage-1
  (.tanm, editor built from origin/main) vs stage-2 (.tskl) differs in 17
  of 580 800 pixels (0.003%, sub-pixel silhouette shifts); 3 animated
  1440-vert characters hold 50 FPS / 100% speed (3 samples, EE 34-37% vs
  stage-1's 40%); debug free-RAM overlay: 27.7 MB free vs 23.8 MB stage-1
  (+3.9 MB from one character model; .tanm 2.15 MB -> .tskl 60 KB on disk);
  crossfade: flow graph EverySeconds -> Play Animation(fade 2s) captured in
  0.4s-burst screenshots shows a smooth twist->wave blend with consecutive-
  frame pixel deltas in the same band as normal playback (no pop; fade
  codegen inspected in flow_graph.gen.cpp). Sample regenerated + rebuilt in
  Docker. VU1 skinning (M3) stays in the backlog â€” EE headroom says it is
  not yet the bottleneck.

- (25) **Animated models: .glb import baked to PS2 morph frames (stage 1)** â€”
  the engine's dormant dynamic pipeline (DynPip: MD2-style two-frame VU1
  interpolation) is now wired end to end. OBJ has no animation, so animated
  models come in as **.glb** (Blender: glTF Binary export): the new
  `src/glbparser.cpp` (hand-rolled on top of json.cpp, no new deps) parses
  the GLB container, samples every named clip at 12 fps, CPU-skins the
  vertices (4-joint matrix palette, rigid node animation too, LINEAR/STEP +
  CUBICSPLINE fallback) and, at every build, `refreshGenerated` writes a
  compact `.tanm` binary + extracted PNG textures (JPEG transcoded) next to
  the source. Engine grew `TanmLoader` (fork addition; whole-file read, no
  fseek) building a `DynamicMesh`, and a fix for upstream's out-of-bounds
  `DynamicMeshAnimation::restart()` (double-indexed the sequence - crashed
  for any clip not starting at frame 0). The generated game keeps StaPip and
  DynPip initialized side by side and swaps VU1 programs per frame
  (`reinitVU1Programs`; `usePipeline` would reallocate everything), renders
  animated objects with per-instance DynamicMesh copies (shared frames, own
  clip state, object-color tint via the copy's material ambient) and a
  directional light matching the baked static look (point lights stay
  baked-only). Per-object data: start clip / autoplay / loop / speed
  (serialized under `"anim"`); collision uses the baked frame-0 AABB (box
  only). Scripts get `playAnimation/stopAnimation/animationFinished` +
  `ctx.resolveClip`; the flow graph gets **Play Animation**, **Stop
  Animation** and the **On Animation Finished** trigger (End + every loop
  wrap, via the engine callback). The editor previews clips in the viewport
  (CPU lerp into dynamic VBOs, same math as VU1) and shows clips/warnings in
  Assets + Properties (memory estimate warns above ~8 MB baked).
  Verified: glbparser unit-tested against a generated skinned+rigid 2-clip
  .glb (frame counts, AABB, skinning spot-checks); full e2e in Docker+PCSX2
  (SW renderer, 50 FPS steady): two textured animated objects play different
  clips ("bend" visibly bends between screenshots), flow-graph
  Play/Stop/OnFinished compile into flow_graph.gen.cpp and run without
  crashing, editor viewport plays the same clips with textures. Hands-on
  pass still worth doing: Properties clip combo + gizmo feel on animated
  objects (screenshots could not click the UI). Stage 2 (true skeletal
  runtime) is specced in the Backlog.

- (24) **Texture quantization: project-wide palette textures + per-asset
  quality override** â€” the PS2-native "texture compression". The GS has no
  DXT-style format; era games shipped palettized PSMT8/PSMT4 textures, and
  the engine's PNG loader already eats indexed PNGs directly - so the editor
  now produces them. **Preferences > Rendering > Textures** picks the
  project-wide quality (full 32-bit / 256 colors / 16 colors - new projects
  default to 4-bit, existing ones load as "none" so their output never
  changes silently), and every model/material row in **Assets** gets a
  quality combo override; when several assets share a texture the HIGHEST
  requested quality wins - "everything 4-bit, but the hero stays full color"
  works per design. Non-destructive: sources in res/ are never touched; a
  build-time bake (runner, before the docker sync) mirrors res/ into
  `.res-baked/` quantizing PNGs per policy (hud/fonts exempt - UI
  legibility), and the generated Makefile's RESDIR now points at the mirror.
  New `src/pngquant.cpp`: median-cut over RGBA (pixel-weighted, alpha
  counted double), Floyd-Steinberg dithering when lossy, lossless
  pass-through for images already within the palette budget, and a
  hand-rolled indexed PNG writer (PLTE + tRNS, deflate via stb's zlib)
  matching what the engine's 4/8bpp paths expect (even width required for
  4bpp). `src/texbake.cpp` resolves the per-PNG policy by parsing every
  .obj/.mtl asset (objparser), mirrors/cleans the bake dir and reports
  counts to the build log. Verified: pngquant host tests (16 asserts: IHDR
  depth/type, palette bounds, alpha hole survives, opaque stays opaque,
  2-color image lossless); e2e in PCSX2 (SW renderer, 50 FPS): global 4-bit
  + walls.mtl pinned to full - .res-baked shows models/bricks.png as
  depth-4 type-3 (623 -> 136 B) while the pinned material stays 32-bit, HUD
  untouched, and two boxes side by side (4-bit palette vs full color)
  render identically from the GS's PSMT4 and 32-bit paths. Editor viewport
  still previews the full sources (quantized preview = follow-up; the
  Preferences/Assets combos need a hands-on GUI pass). Sample regenerated
  (Makefile RESDIR + .gitignore .res-baked).

- (23) **Materials replace per-object textures** â€” the loose "slap a PNG on
  an object" texture is gone; .mtl material libraries are the one texturing
  mechanism. Every solid object gets a **Material combo** in Properties
  listing the project's .mtl assets (a new `res/materials` folder for
  universal libraries + the models' own mtls under `res/models`):
  primitives take the file's FIRST material (Kd tint + map_Kd on their UVs,
  still modulated by the object color), models use the assigned .mtl as an
  **override** that replaces their own libraries (usemtl names resolve
  against it) - one "walls" library can repaint/retexture many objects.
  Data model: `SceneObject.texturePath` -> `materialPath` (old "texture"
  keys are dropped on load). Codegen: models are keyed by the (obj, mtl)
  PAIR (`MODEL_PATHS` + `MODEL_MTLS`), primitives get `MATERIAL_PATHS` +
  `SceneObjectData.material`, and the game grew `loadMaterials()` (first
  material of each library: Kd + probed map_Kd via the texture repository).
  Engine: `LeanObjLoader::load` takes an optional override .mtl (replaces
  mtllib/sibling; textures then resolve relative to the override) and a new
  `LeanObjLoader::loadMtl` parses standalone libraries - both mirrored in
  the editor's objparser. Editor: viewport draws primitive materials and
  model overrides identically to the game; the Assets section swaps its
  Textures list for **Materials** (res/materials, per-file material summary
  + missing-texture flags, `Import .mtl...` copies the library with its
  textures, references rewritten); `res/textures` survives only for the
  tiled terrain texture (its Pick... popup gained the Import PNG... item).
  Verified in PCSX2 (SW renderer, 50 FPS): a box assigned `walls.mtl`
  renders brick-textured, and a model assigned `repaint.mtl` switches from
  brick walls + dark roof to green walls + yellow roof (usemtl-name match);
  the editor viewport shows the identical result, the Material combo and
  the red MISSING flags in Properties. fpp + empty presets rebuilt clean in
  Docker; sample regenerated.

- (22) **Missing textures fail soft + are visible in the editor** â€” a model
  whose .mtl referenced a texture that never made it into the project used to
  kill the game at boot ("Failed to load ... png_loader.cpp:39" assert - the
  texture repository trusts its callers). The generated game now probes every
  texture file first (models AND the scene TEXTURE_PATHS): a missing one logs
  a TYRA_WARN and the affected part draws in its Kd/object color. The editor
  surfaces the problem instead of hiding it: the Properties material summary
  paints missing textures red ("walls (textures/t.png) - MISSING" + a hint
  that paths resolve relative to the .obj), and the Assets section flags such
  models with a "missing textures!" marker (tooltip lists the paths).
  Texture rows in Assets also got a hover thumbnail (PNG preview + size,
  reusing the HUD texture cache). Verified: reproduced the crash scenario
  (res/models/tower/tower.obj with map_Kd textures/t_C_3.png, no such file) -
  the game now boots at 50 FPS with "Model texture missing:
  models/tower/textures/t_C_3.png" in the game log; editor builds clean.

- (21) **Sibling-.mtl matching, asset subfolders, Add-menu restructure** â€”
  three usability follow-ups. **Implicit MTL**: a `.mtl` named like the `.obj`
  next to it is picked up even without a `mtllib` line (the common exporter
  convention); explicit mtllib files still parse afterwards and win on name
  clashes. Implemented in BOTH parsers (editor `objparser` + engine
  `LeanObjLoader` - they must stay in sync) and the import copies the implicit
  library like an explicit one (sanitized stems keep matching). **Subfolders**:
  asset listing/pickers (`listAssetFiles`) and the audio rescan are recursive,
  so `res/models/props/tree.obj` or `res/sfx/steps/wood.wav` just work; the
  Runner's adpenc loop covers two levels of sfx subfolders (glob fan-out - the
  quoting-hostile docker/cmd pipeline rules out find) and the bin/sfx WAV
  cleanup follows. Codegen/ISO paths already carried full relative paths.
  **Menus**: the add palette starts with `Object -> Simple / Model` (instead
  of a top-level Simple and a separate Model menu), and the top-bar Scene
  menu nests everything under `Scene > Add`. Verified: parser host test (obj
  without mtllib gets both materials from the sibling); PCSX2 run at 50 FPS
  with mtllib stripped from the test house (bricks still textured = engine
  sibling matching) plus a model under `res/models/props/` with its own
  mtl+texture (loads clean - no LeanObjLoader warnings in the game log);
  editor builds clean.

- (20) **Pick-from-project asset flow + Assets section + MTL visibility** â€”
  the object/terrain pickers no longer open file dialogs: textures pick from
  `res/textures` (object texture, Project Preferences and Scene Preferences
  terrain texture - all through one `pickProjectTexture()` popup), model
  objects get a **Model combo** over `res/models`, sounds already picked from
  the project list. Importing moved to one place: a new **Assets** section in
  the Project panel lists `res/models` (with tri/material counts) and
  `res/textures` straight from disk, with `Import .obj...` / `Import PNG...`
  buttons (model import copies the .mtl + textures as before but no longer
  auto-creates an object - add it from the Add menu's Model submenu, which now
  only lists project models). The Music/Sounds/HUD import buttons are renamed
  `Import...` so it is obvious they copy into the project. Since materials are
  a property of the .obj/.mtl file (not of the object), the Properties panel
  now shows a read-only summary for models: triangle count + each MTL material
  with its map_Kd texture or "(color)". Verified: editor builds clean; GUI
  screenshot shows the Assets section, the Model combo, the materials summary
  ("walls (bricks.png), roof (color)" for the test house) and the Pick...
  texture flow on the mtltest project.

- (19) **Asset import rework: drop-into-res + rescan, in-editor WAV converter** â€”
  assets no longer have to go through the import dialogs. WAVs dropped by hand
  into `res/audio` / `res/sfx` are picked up by a rescan (runs on project open
  + Rescan buttons in the Music/Sounds sections); entries whose file vanished
  are removed like a manual delete (flow-node references cleared). The Add
  palette's Custom menu lists `.obj` files already in `res/models` ("From
  res/models", no copy), and the object Texture row gets a "Project..." picker
  over `res/textures`. **WAV converter** (`src/wavconvert.cpp`): rewrites any
  readable WAV (integer PCM 8/16/24/32-bit + 32-bit float, mono/stereo, box
  low-pass on downsample) as 16-bit PCM **in place** - the project keeps one
  copy of each asset instead of source+converted pairs. Sfx imports convert to
  22050 Hz automatically; music converts only unplayable formats (float/24-bit
  / out-of-range rates); hand-dropped files get a warning marker + Convert
  button (format checks cached per file, not per frame). Also stopped shipping
  dead weight: the Runner deletes `bin/sfx/*.wav` after adpenc (the Makefile's
  `cp -r res/*` used to leave source WAVs next to the .adpcm, tripling each
  sfx and landing on the ISO), and the ISO exporter skips `bin/log.txt`.
  Verified: converter round-trips checked by a host harness (44.1k float
  stereo, 8-bit 11k mono upsample, 24-bit 48k downsample - format + RMS of a
  sine preserved; garbage rejected, original untouched); editor builds clean;
  Rescan buttons + pickers visible in the GUI. Hands-on drop-a-file-and-rescan
  pass left for a human.

- (18) **MTL materials, runtime model loading and mesh collision** â€” models
  went from "baked gray blob" to the full 2002 experience. **Engine** (new,
  marked "Added by tyra-editor"): `LeanObjLoader` - a lightweight OBJ+MTL
  loader (per-material split, Kd + map_Kd, flat per-face normals and V-flip
  matching the editor parser 1:1, sequential reads - no fseek, all paths
  through `FileUtils::fromCwd` so host: and cdrom0: both work);
  `CollisionMesh` - triangle-soup collider with an XZ uniform grid
  (raycast + resolveSphere with a walkable-slope filter); `Ray::intersectTriangle`
  (Moller-Trumbore). Plus a real bug fix: `TyraDebug::writeInLogFile` opened
  `cdrom0:LOG.TXT;1` for WRITE on every TYRA_LOG when booted from a disc image,
  wedging the CDVD driver before the first frame - guarded to skip read-only
  media (this had silently broken every ISO boot of a logging game).
  **Editor**: `objparser` now reads mtllib/usemtl/Kd/map_Kd into per-material
  submeshes + model AABB; the viewport renders one part per material (map_Kd
  textures, Kd baked into vertex colors); model import copies the .mtl and its
  textures next to the .obj, rewriting references to the sanitized names.
  **Codegen**: `model_data.gen.hpp` no longer bakes vertices into the ELF
  (3000-tri cap gone) - it emits `MODEL_PATHS` and the game loads models once
  at startup via LeanObjLoader, one StaPip bag per material part (object
  texture still overrides all parts), per-material textures de-duplicated
  through the TextureRepository. **Collision modes** per object ("collision"
  in the .tyra, combo/checkbox in properties): Box (default; models now use
  their real mesh AABB instead of the unit scale box), Mesh (models: the
  player walks the triangles - ground by a local-space downward raycast,
  steep faces push a chest-height sphere out; honors full rotation + scale),
  None (decoration). Both walkers (FPP template + Player entity) share one
  `collidePlayer()`; emitters/markers no longer block the FPP player.
  Verified: CollisionMesh host tests (12 asserts: ramp heights, wall push,
  slope threshold, 2048-tri grid); editor + PCSX2 SW renderer at 50 FPS - two
  houses with brick map_Kd walls + dark red Kd roof, one rotated 30 deg,
  identical in the viewport; mesh collision proven numerically via TYRA_LOG
  (teleport above the rotated house -> rests at exactly y=2 = roof; spawn
  0.05 into a wall -> pushed out to the analytically predicted XZ to 5
  decimals); ISO export boots from cdrom0: and renders (models + MTL + PNG
  loaded from the disc). Interactive walk-into-walls pad feel left for a
  human.

- (17) **Two project presets + per-scene override of scene-visual settings** â€”
  tidy-up of project creation and preferences. **New project presets** cut from
  three (orbit / fpp / showcase) to two: `empty` (orbit camera, no objects) and
  `fpp` (FPP game template + a single Player entity, nothing else). `create()`'s
  `gameTemplate` arg became `preset`; the showcase content (house/pillar/HUD/two
  flow graphs), the FPP spawn+box+ball seed and the CLI `[orbit|fpp|showcase]`
  are gone (`--new ... [empty|fpp]`). **Per-scene overrides**: the scene-visual
  half of `ProjectSettings` (lighting, sky, clipping, terrain texture, post-FX,
  usable-highlight) can now be overridden per scene. `SceneData` dropped its
  loose lighting/terrainTexture fields for a full `ProjectSettings settings` +
  `SceneOverrides overrides` (one bool per category, all off by default);
  `project::resolvedSettings(p, scene)` returns the project defaults with each
  active category swapped for the scene's values. Everything downstream reads
  through it: the viewport (`applyProjectToViewport`), ISO export, and codegen.
  A new **Scene > Scene Preferences** dialog mirrors Project Preferences with an
  "Override project settings" checkbox per category â€” off = the widgets are
  grayed and preview the inherited project value, on = editable from that value.
  Project Preferences now edits only the project-wide defaults (it no longer
  writes lighting into the active scene, which also fixed a latent bug where the
  loader unconditionally overwrote every scene's lighting from the project
  settings, so per-scene lighting never survived a reload).

  Codegen: sky/clipping/post-FX/highlight moved from scalar `constexpr` in
  `terrain_config.hpp` to `SCENE_COUNT` arrays in `scene_data.hpp` (like the
  existing per-scene lighting), reached through `SKY_R = SKY_RS[g_activeScene]`
  style accessor macros. Those macros (and the whole SCENE_*/TERRAIN_* set)
  moved out of the game-cpp prolog into `scene_data.hpp`, and `g_activeScene`
  became a real extern global (defined once in the game cpp) â€” so user scripts,
  which include `scene_data.hpp` via `script.hpp`, keep seeing `SKY_R` etc.
  (the first Docker build caught this: the example script failed to compile
  until the macros were visible to its TU). `loadScene` now re-applies the
  scene's clip mode, sky horizon/clear color and bloom/grain on every switch,
  not just at boot.

  Verified: editor builds clean; headless `--new` gives empty (0 objects) and
  fpp (1 player) as expected; the `.tyra` round-trips the new `settings` +
  `overrides` blocks (load exercised by `--build`). Docker-built both variants
  to an ELF. A hand-authored 2-scene project (scene 1 overriding sky black +
  highlight on, scene 0 inheriting) generated the right split arrays
  (`SKY_RS = {63.75, 5.1}`, `HIGHLIGHT_USABLES = {false, true}`, while
  un-overridden `CLIP_PRECISES`/`SCENE_BRIGHTNESSES` stay equal) and still
  compiled + linked. Not yet booted in PCSX2: the runtime *visual* effect of a
  per-scene switch (loadScene re-application) and the Scene Preferences dialog's
  graying are the standard hands-on human checks.

- (16) **Single `.tyra` project file + per-project window layout** â€” normalized
  the on-disk project. Previously a project was `project.json` (game data,
  tracked) plus a gitignored `<name>.tyra` solution (editor state + undo) plus
  binary `terrain-*.heights`, and the ImGui window layout lived in a global
  `imgui.ini` in the cwd (shared across all projects). Now the whole project is
  one `<name>.tyra` file: game data + editor-side state (selection, gizmo, view
  mode) + the ImGui docking layout, so the window arrangement is restored per
  project. `project.json` is gone (no backward compat); `load()` finds the
  single `*.tyra` in the dir. The undo history moved to a sidecar
  `<name>.history` (JSON, gitignored â€” churny, rewritten on every edit); heights
  stay binary sidecars as before. `io.IniFilename` is nulled so ImGui never
  writes `imgui.ini`; the layout is captured via `SaveIniSettingsToMemory` into
  the `.tyra` whenever ImGui settles a layout change (and on graceful exit), and
  applied via `LoadIniSettingsFromMemory` in `attachProject`. Generated
  `.gitignore` drops `*.tyra` (now the tracked source) and adds `*.history`.
  `saveSolution/loadSolution` â†’ `saveHistory/loadHistory`; a `jsonEscape` helper
  handles the layout's newlines/brackets. Sample migrated (`project.json` â†’
  `script-demo.tyra`, pure rename). Verified headless (`--new showcase`: exactly
  one `.tyra` + one `.history`, no `project.json`, `.gitignore` correct) and in
  the GUI: opened the project (loads fine), the live layout autosaved into the
  `.tyra` (`"layout"` grew 14â†’1036 chars, no `imgui.ini` written anywhere), then
  hand-widened the stored Project panel (383â†’700), reopened, and the wider panel
  was honored â€” proving the loadâ†’applyâ†’saveâ†’reload round-trip. Migrated sample
  also opens correctly (title, objects, viewport). Interactive drag-to-rearrange
  feel is the standard human check; the persistence mechanism is verified.

- (15) **Film grain dropout root-caused: alpha test vs stale RGBAQ** â€” the
  real mechanism behind "grain vanishes for a few frames when looking at the
  fog": post fx blits send only UV+XYZ, so their vertex alpha is whatever
  RGBAQ the scene left in the GS - and the drawing environment's alpha test
  rejects alpha==0 fragments. Particles render last and fade to exactly
  alpha 0, so on frames where a fully-faded fog vertex was the last thing
  drawn, BOTH grain blits were discarded whole. Diagnosed by measurement:
  ~6 fps native-F8 screenshot bursts (300 shots via PostMessage, no window
  focus needed) scored by mean horizontal gradient in the sky region - 10/175
  frames at near-zero grain pre-fix; an untextured same-packet marker sprite
  survived those frames, proving the packet ran and pinpointing sampling/test
  state. Fix in renderer_core_postfx: pin RGBAQ (0x80) and disable the alpha
  test for the pass, restore via draw_enable_tests() after. Post-fix burst:
  316/316 frames with grain (min 3.50 vs median 3.69). Along the way two more
  fixes in vendor/tyra: (a) 2D sprites and clearScreen end their PATH3 stream
  with a data-less EOP giftag instead of draw_finish() - keeps the un-consumed
  FINISH writes (which could release align3D's barrier early) out of the GS
  while preserving the EOP bit, which is load-bearing: dropping the whole tag
  deadlocked the GIF (PATH3 never terminated, PATH1/XGKICK starved, first 3D
  frame froze at the TYRA banner; found via host-fs marker files); (b)
  endFrame arms the post fx barrier only once a pipeline configured VU1
  (Path1::isVU1Configured), so pure-2D loading frames never handshake a VU1
  that can't answer. All verified in PCSX2 SW renderer at steady 50 FPS.

- (14) **Outline close-up fixes** â€” two artifacts visible when standing next to a
  usable object: (a) no bottom rim - grounded objects' shells dip below the terrain
  and the ground in front z-rejects them from a low camera; shell vertices are now
  lifted just above the terrain surface, turning the bottom rim into a glow apron
  hugging the ground around the base. (b) shells washed out receding side faces -
  the camera pushback clears the front face but not glancing ones; the object is
  now repainted right after its shells (wins the GEQUAL depth test) which erases
  the wash without touching the rim outside the silhouette. Verified in PCSX2 up
  close (USE prompt range): clean side faces, ground glow at the base, 50 FPS.

- (13) **Configurable outline blur** â€” Preferences > Usable objects gains "Blur
  width (units)" (total rim size, 0.05-2.0) and "Blur steps" (1-8 shells; 1 = sharp
  solid edge). Baked into terrain_config.hpp as HIGHLIGHT_WIDTH / HIGHLIGHT_STEPS;
  shells are spaced evenly up to the width with alpha halving outward. Verified in
  PCSX2: width 0.7 / steps 6 produces a visibly wider, smoother glow than the
  0.35 / 4 default.

- (12) **Soft usable-object outline + draw-order fix** â€” the single hull rim could
  be punched through by objects drawn later in the loop (rim pixels carried the
  background's z, so e.g. a house behind the highlighted box overdrew the rim).
  Rims now render after the whole scene: four concentric shells with fading alpha
  (blur), each pushed away from the camera by a uniform scale around the eye point â€”
  screen silhouette unchanged, but the object's own z-buffer rejects the interior
  and the rim is depth-tested like normal geometry (one shared pushback for all
  shells; per-shell depths made the terrain cut each shell on a different line).
  Verified in PCSX2 (software renderer, 50 FPS): house directly behind the usable
  box â€” rim glows in front of the house with a smooth falloff.

- (11) **Usable-object highlight** â€” Preferences > Usable objects: "Highlight usable
  objects" + proximity (units) + color. In-game, objects marked Usable get a colored
  outline while the player is within proximity: a flat-color copy of the object grown
  ~0.12 units around its center is drawn just before it with z-test but no z-write,
  so the object overdraws the interior and only a rim survives. The no-z-write mode
  is a new engine enum (`PipelineZTest_TestOnly`, stapip + dynpip) implemented purely
  in the GS TEST register (alpha-test all-fail + AFAIL keep-zbuffer) â€” the VU1
  options layout is untouched. Editor viewport marks usable objects with a wire box
  in the highlight color when the pref is on (proximity is runtime-only). Verified
  in PCSX2 (software renderer, 50 FPS): near usable box shows a yellow rim, far
  usable pillar and non-usable objects stay clean; prefs UI + JSON round-trip +
  viewport preview screenshotted. Dead end for the record: a classic inverted-hull
  outline doesn't work here â€” the Tyra pipeline has no backface culling, so a scaled
  hull drawn normally occludes the object; the no-z-write underdraw sidesteps that.

- (10) **FPP showcase template** â€” third choice in New Project: seeds a fresh project
  with all features (built-in house.obj + crosshair.png embedded in the editor,
  physics ball, pillar, HUD, starter flow graph). Fresh copy every time, so the
  shared sample no longer gets wrecked by experiments. Verified in PCSX2.

- (65) **"Showcase" sample project** -- a second, much larger checked-in sample
  under `examples/showcase` that exercises most of the editor's feature set in
  one game, added on request ("a bigger project for the examples"). Two scenes
  reachable through a usable portal pair: `vale` (a 192x192 heightmapped valley,
  golden-hour dusk) and `cavern` (a dark, blue-fogged interior with a Nightfall
  grading override). It demonstrates: streaming layers loaded *dynamically*
  (`village`/`ruins` start unloaded; two `Near Object` gates `Load Layer` the
  district you approach and `Unload Layer` the other), a purpose-built skeletal
  model (`res/models/wobbler.glb` -- a cylinder skinned to a 5-joint chain with
  `Wiggle`/`Twist` clips, baked to `.tskl`), object draw-distance + animation
  LOD + baked mesh LOD, a baked directional sun plus point lights (campfire,
  lanterns, ruins, crystals), particle emitters (fire/smoke/rain/sparks/fog/
  fireflies), GS distance fog, bloom + film grain, two colour-grading presets,
  a title + pause menu, a HUD crosshair, a first-person player with a
  toggleable flashlight, a save point + a save value collected from a usable
  relic, usable-object highlighting, a gradient sky dome, and ambient music +
  a spatial campfire sound. The project (`showcase.tyra` + authored `res/`
  assets + `terrain-*.heights`) was authored programmatically (Node generators
  that emit the loader-format JSON, procedural heightmaps/ground texture, the
  WAVs, and a hand-built glTF); the generated game tree is committed like
  `script-demo` (the `Makefile` is create-only, so a sample cannot be
  source-only), with `res/.gitignore` keeping the authored assets while
  ignoring the built-in `hud/`/`menus/` and the baked `.tskl`. Verified: the
  `.glb` parses through the real `glbparser` (`bake` + `parseSkel`, 1248 verts,
  5 joints, 2 mesh LODs); `--build examples/showcase` refreshes codegen, bakes
  the `.tskl`, quantizes the ground texture and converts the sfx, compiles
  under the PS2DEV toolchain and links (exit 0); PCSX2 (software renderer)
  boots both scenes at 50 FPS / 100% speed -- the title menu, the vale (wobblers
  visibly bent mid-wiggle, trees, fog, bloom, grain, highlighted portal) and
  the cavern (flashlight cone, glowing crystals, blue fog, Nightfall look) all
  render. Interactive paths (walking the streaming boundary to see a district
  page in/out, USE on the portal/relic, pad flashlight toggle, hearing the
  audio) still want a hands-on pad test.

- (66) **Consolidated `samples/` + `examples/` into one `examples/` dir** -- the
  repo had grown two parallel homes for checked-in projects (`samples/` for the
  playground + showcase, `examples/` for per-feature demos). Merged them: moved
  `script-demo` and `showcase` under `examples/` (git rename, history preserved),
  deleted `samples/`. Updated all references -- `README.md` (the example-projects
  section + the Structure list), the three skills that pointed at
  `samples/script-demo/` (`tyra-editor-dev`, `tyra-pr`, `tyra-testing`), and
  `examples/showcase/README.md`'s build command. Also added a **`tyra-docs`
  skill** stating the standing rule: every change updates the docs in the same
  commit (README, PROGRESS, the relevant skills, example READMEs, and regenerate
  any affected example project). No code or generated files changed; the moved
  projects still build unchanged (paths in `.tyra`/compose are relative).

- (67) **showcase: terrain chunking + tighter LOD ring (hardware perf)** -- the
  showcase ran slowly on real PS2 hardware. After merging main's terrain
  chunking + camera-ring streaming (PR #52), turned it on for the big vale:
  `terrainViewDistance = 88`, so only the terrain chunks near the camera stay
  resident instead of the whole 192x192 mesh. The small cavern (64) fits inside
  the ring and stays whole. Verified LOD was already enabled (animLodDistance
  35, meshLodDistance 45, per-object drawDistance) -- and tightened it: props
  now cull with the ring (trees 84, rocks 82, down from 135/120, which exceeded
  the map extent and never culled). Pulled the fog in (start 20, end 82, from
  32/168) so the streaming edge sits in full fog and the cutoff is invisible;
  the trade-off is a foggier, more compact vista. Regenerated the project
  against the chunking codegen. Verified: `--build examples/showcase` exit 0;
  PCSX2 boots the vale at 50 FPS with the terrain fading cleanly into fog (no
  visible chunk edge). The real win is on hardware (PCSX2 was already at 50) --
  chunking bounds the resident/clipped terrain instead of it growing with the
  whole map; `terrainViewDistance` / `fogEnd` can be lowered further together if
  more headroom is needed.

- (68) **Runtime graphics-toggle flow nodes (Set Fog / Bloom / Grain / Particles)**
  -- four new Scene-category flow nodes that change graphics settings at
  runtime, mirroring how Set Flashlight / Set Grading already work. Set Fog
  (On) re-applies the active scene's own fog or disables it; Set Bloom / Set
  Grain take a 0..1 amount (compiled to the engine's 0..128 fixed point); Set
  Particles is a global switch that makes `updateParticles` skip all emitter
  simulation + draw (a new `g_particlesOn`). Each writes a `ScriptContext`
  field (`fog`/`bloom`/`grain`/`particles`, -1 = leave) that both game loops
  (orbit + fpp) apply and reset next to the flashlight block; the engine
  already exposed `setFog`/`disableFog`/`postFx.setBloom`/`setGrain`. UI is the
  generic flow-node renderer (On = checkbox like Set Flashlight, Amount =
  DragFloat). Motivating use: wire them to On Menu Event entries so a game can
  offer a graphics-options menu / let players trade effects for frame rate on
  real hardware. Verified: editor builds clean; the showcase's options menu
  generates the expected `ctx.fog/bloom/grain/particles` calls (`0.35 -> 45`,
  `0.14 -> 18`), compiles under PS2DEV, and an `On Start -> Set Fog(0) + Set
  Particles(0)` test in PCSX2 visibly removed the fog (terrain extends to the
  horizon) and every particle. Interactive menu navigation still wants a pad test.
- (69) **showcase: perf pass + graphics options menu** -- the showcase ran at
  ~8 FPS on real PS2 hardware (50 in PCSX2, whose software renderer hides GS
  fill-rate + EE geometry cost). Lightened the scene: the animated wobbler
  model dropped from 1248 to 123 base verts (skeletal skinning is EE-bound),
  terrainDetail 64 -> 48, particle pools + the big `fog`-emitter quad sizes cut
  hard (rain 120 -> 34, cave/ruins fog counts + sizes down), post-FX bloom +
  grain off by default, tree count 14 -> 10, animLod/meshLod onset pulled in
  (24 / 30). Added a floating **GRAPHICS options menu** (open with Select) wired
  to the new Set Fog/Bloom/Grain/Particles nodes so the effects can be toggled
  live while the on-screen **FPS + free-RAM overlay** (now enabled -- buildProfile
  debug) is watched, to pinpoint the hardware cost. Verified: `--build` exit 0;
  PCSX2 boots both scenes at 50 FPS with the overlay, and the toggle nodes work
  end-to-end. The overlay/debug profile is diagnostic -- flip to release +
  showFps/showMemory off (a `gen_project` comment notes this) once perf is
  dialed in on hardware.

- (70) **showcase: the title screen was the real perf killer; removed it, restored
  effects** -- turning the graphics toggles on hardware showed the ~8 FPS was the
  boot **title-screen menu**, not the scene: a paused menu still renders the whole
  scene behind it every frame plus its panel + full-screen dim overlay, which
  dropped even PCSX2 to ~17 FPS (50 in gameplay). Dropped the title screen so the
  game boots straight into the vale (the pause menu on Start and the floating
  options menu on Select stay). With that gone the scene holds ~50 FPS, so the
  earlier defensive cuts were reverted: **bloom + film grain back on**, particle
  pools back to full (rain 120, campfire/ruins/cavern emitters). Kept the cheap,
  near-invisible geometry wins (terrain chunking, lean skeletal model + LOD, draw
  distances). The FPS/RAM overlay + options menu stay on so the fuller effect set
  can still be confirmed on hardware (flip buildProfile to release when happy).
  Verified: `--build` exit 0; PCSX2 boots directly into gameplay with fog, bloom,
  grain and the full particle set at ~50 FPS.

## Done

- Core editor: project creation (orbit/FPP templates), solution files + undo history,
  scene primitives + spawn points, gizmos, wireframe view modes, preferences,
  C++ scripts with VS Code IntelliSense, engine clipping fixes, sample project.
- (1) **Runtime scene v2** â€” objects are mutable at runtime (`RuntimeObject` in
  ScriptContext: position/rotation/scale/color/visible + `dirty` flag), each object
  renders from its own bag rebuilt on change. Terrain keeps precise clipping;
  objects skip culling (engine bbox cache is keyed by pointer and goes stale for
  moving objects). Verified in PCSX2: falling sphere rests on the ground, 50 FPS.
- (2) **Player physics (FPP)** â€” gravity + jump on X (JUMP_SPEED pref), XZ collision
  with scene objects (AABB + player radius), walking on top of boxes (step 0.5).
  Compiles & boots; interactive feel needs a pad test.
- (3) **Object physics** â€” `Physics` checkbox per object: falls with GRAVITY pref,
  rests on the terrain. New FPP projects seed a falling ball as demo. Verified.
- (4) **Sky gradient dome** â€” vertex-colored dome (horizon/zenith preference colors),
  same gradient in the editor viewport. Scripts changing ctx.skyColor retint the
  dome at runtime. Verified in PCSX2.
- (5) **Flow graph** â€” CryEngine-like visual logic (imnodes window, tab next to
  Viewport): triggers (On Start, On Button, Near Object, Every N Seconds) wired to
  actions (Set Sky Color, Show/Hide/Toggle Object, Move Object By, Set Object Color,
  Log). Graph lives in project.json, compiles to src/scripts/flow_graph.gen.cpp on
  every build. Runtime verified in PCSX2 (OnStart->SetSky retints the dome).
  Editor node UI compiled but needs a hands-on pass.
- (6) **Directional lighting** â€” light direction + ambient/diffuse in preferences,
  baked into vertex colors at build; terrain shaded by its up normal; viewport uses
  the same formula. Gotcha: PS2SDK math3d.h #defines LIGHT_AMBIENT - constants use
  SCENE_ prefix. Verified in PCSX2 (side light: directional shading on sphere/box).
- (7) **Custom .obj models** â€” "+ Model" imports a .obj into res/models/, shown in
  the viewport (shared parser, per-face normals) and compiled into the game as
  vertex data (capped 3000 tris/model). Full citizen: gizmos, physics, scripts,
  lighting. Verified in editor + PCSX2 (hand-written house model).
- (8) **Gizmo snapping** â€” hold Ctrl while dragging: 0.5 units / 15 deg / 0.25 scale.
- (9) **HUD from images** â€” "+ Image (PNG)" imports into res/hud/; position
  (normalized, center anchor) + pixel size editable; live preview overlaid on the
  viewport (stb_image); in-game rendering via Tyra Renderer2D sprites. Verified in
  PCSX2 (crosshair over the 3D scene).

- (11) **Engine optimization: fast EE clipper (patch v2)** â€” three engine files
  patched via the Runner (marker `/tyra/.tyra-editor-patch-3`, originals restorable
  with `git checkout` inside `/tyra`):
  - `planes_clip_algorithm.cpp`: Cohen-Sutherland outcodes - fully-visible
    triangles skip the 6-plane Sutherland-Hodgman entirely, fully-outside ones are
    rejected instantly; `clipAgainstPlane` no longer copies two 64-byte structs
    by value per edge per plane.
  - `stapip_clipper.cpp`: static vertex pool instead of a heap-allocated
    std::vector per clip call (per subpackage, per frame).
  - `stapip_qbuffer.cpp`: persistent per-buffer arrays instead of up to four
    new[]/delete[] pairs per fill call.
  Benchmark (128x128 terrain, detail 128 = 98k verts, precise clipping, FPP at
  ground level, PCSX2, 3 samples each): **12/12/12 -> 15/15/15 FPS (+25%)**,
  pixel-identical output. Real scenes with a higher clipping share should gain
  more (the pathological benchmark is partly VU1/DMA-bound).

- (12) **Engine optimization v3: clipping leaves the EE (the author's TODO)** â€”
  resolves the engine author's own comment in stapip_clipper.hpp ("clipping
  algorithm should be moved to VU1... too much time"). How: the classic PS2
  guard-band trick. The shared `PerformClipCheck` VU1 macro now tests XY against
  a 3x wider window (one extra `muli.xy` on a vertex copy; Z/W test untouched,
  coordinates stay inside the GS 4096 raster window so nothing wraps) and the
  GS scissor trims the pixels in hardware. `RenderBBox::clipFrustumCheck`
  reclassifies packages crossing only the side planes as cullable; the EE
  clipper survives solely for the near-plane band (`+1.5` unit margin), where
  perspective division would explode - the one case a scissor cannot fix.
  Applied by the Runner (marker `.tyra-editor-patch-4`, awk script swaps the
  VCL macro, VU1 microprograms force-rebuilt).
  Benchmark (same 98k-vert scene, 3 samples): **12 -> 50 FPS (4.2x, full PAL
  frame rate)**, frame pixel-clean. VU1 usage 1% -> 6%: the work moved to the
  chip that was built for it.

- (13) **Terraforming** â€” sculpt the terrain with a brush: *Sculpt (T)* mode in the
  viewport, LMB raises / Shift+LMB lowers (cosine falloff, radius + strength
  sliders, RMB orbits), brush ring projected onto the relief. Heightmap lives in
  `terrain.heights` (vertex grid = terrain detail; resampled when the grid config
  changes), compiled into the game as `terrain_heights.gen.hpp` with a bilinear
  sampler. The FPP player walks the relief, physics objects rest on it, terrain
  shading follows the height gradient in both the viewport and the game.
  Verified in editor + PCSX2 (generated hill + valley; the physics ball landed on
  the hilltop). Sculpting itself needs a hands-on mouse test. Not in undo history
  (saved on stroke end).

- (14) **Textures (PNG)** â€” the PS2-native format Tyra loads (32/24bpp + fast
  palletized 8/4bpp; power-of-two sizes recommended). Per-object texture
  (Set.../Clear in object properties; object color modulates the texture, white =
  plain) and a tiled terrain texture (preference + world-units-per-tile scale).
  UVs generated for all primitives, `vt` parsed from .obj models, terrain tiles in
  world space. PS2 side: StaPipTextureBag per bag, textures loaded once via the
  TextureRepository, modulation-correct colors (128 = 1.0). Editor viewport renders
  the same textures via stb_image + a sampler in the shader (wire passes stay
  untextured). Verified in editor + PCSX2 (bricks on sculpted terrain and a box).

- (15) **Scene light management** â€” light color (tints the diffuse term) and a
  global brightness multiplier (0..2), next to the existing direction/ambient/
  diffuse in Preferences > Lighting (same dialog as the sky). Shading is now
  per-channel RGB in the whole pipeline (game codegen + viewport). Verified in
  editor + PCSX2 (warm sunset light over the textured terrain).

- (16) **Player entity** â€” "+ Player" inserts a playable player into any scene, no
  FPP template required (works in orbit projects too; the first Player wins over
  the template camera). Per-object parameters in the properties panel: movement
  mode (**Walk FPP** â€” terrain relief + AABB collision + gravity/jump on X, or
  **Noclip** â€” free flight toward the look direction, Cross up / Square down),
  walk speed, look speed, eye height, jump speed. Left stick moves, right stick
  looks. Shown as a gold humanoid marker in the viewport (nose = facing),
  invisible in the game. Stored as `"player": {...}` in project.json, compiled
  into `scene_data.hpp` as PLAYER_* constants. Verified in editor + PCSX2
  (FPP project: camera starts at the entity on the sculpted terrain; orbit
  project: noclip camera at the entity, scene objects framed as expected).
  Interactive pad feel needs a hands-on test.

- (17) **Music playback** â€” background music controlled from the Flow Graph.
  New Music section in the Project panel imports WAV tracks into `res/audio/`
  (16-bit 22050 Hz stereo - the format Tyra's song player streams; the importer
  reads the WAV header and warns about anything else). Three new action nodes:
  **Play Music** (track combo, volume slider 0-100, loop checkbox), **Stop
  Music**, **Set Music Volume** - wired to `engine->audio.song` (audsrv) in the
  generated flow_graph.gen.cpp. "Play from scene start" = the existing On Start
  trigger -> Play Music. Verified in PCSX2: game boots at full frame rate and
  the emulator's WASAPI session peak meter pulses with the test melody
  (generated 22kHz arpeggio); Triangle -> Stop Music compiled in. Speaker check
  by ear is left for a human.

- (18) **Sound effects (ADPCM)** â€” one-shot samples from the Flow Graph. Sounds
  section in the Project panel imports WAV (16-bit 22050 Hz, mono or stereo)
  into `res/sfx/`; the Runner converts them with the PS2SDK `adpenc` tool at
  build (`bin/sfx/*.adpcm`, skipped when already up to date). New **Play Sound**
  action node: sound combo, volume slider, SPU channel slider (0-23, or "auto" =
  round-robin over all 24 - ADPCM voices cannot be stopped, so rotation avoids
  drop-outs when shots overlap). Samples load once in the generated script's
  init(), playback via `engine->audio.adpcm.tryPlay`. Verified in PCSX2: with an
  Every-2-Seconds -> Play Sound graph the emulator's audio session peak meter
  shows silence with a burst exactly every 2 s (test chirp). Speaker check by
  ear is left for a human.

- (19) **Per-object flow graphs + categorized menus** â€” the single global flow
  graph is gone: every scene object can carry its own graph (stored inside the
  object in project.json; legacy project-level graphs migrate to the first
  object on load). The Flow Graph tab gets a "Graph of" combo (objects with a
  graph are starred) and a "Selected object" jump button. Object-referencing
  nodes resolve their target as: incoming **object-id data link** (new square
  pins + amber links, id output on triggers and object actions) > explicit
  name > **self** (the graph's owner - the new "(self)" combo default), plus a
  "From selected" button that grabs the object selected in the editor.
  Resolution happens at codegen (one script class per object graph); copying
  an object copies its graph, and self-references follow the copy - graphs now
  work as reusable components. Insert menus modernized into category trees:
  scene objects (Simple / Gameplay / Custom - single "+ Add object" button
  instead of the overflowing button row) and flow nodes (Triggers / Object /
  Scene / Audio / Debug). Graph edits now land in undo history (graphs are
  part of scene snapshots). Verified: codegen resolves a data-link chain and
  self-references correctly (phys-demo), game boots at 50 FPS; node-editor
  interactions (pins, combos) need a hands-on mouse pass.

- (20) **Position pins + player spawning** â€” second data type in the flow
  graph: XYZ positions travel over green triangle pins (object ids stay on
  amber squares; pure data nodes have no exec pins). New nodes: **Get
  Position** (pure - reads the target object's position live at the consumer),
  **Set Object Position** (X/Y/Z params, overridden by an incoming position
  link; passes both the object and the position through) and **Spawn Player
  At** (Player category - teleports the Player entity, or the FPP template
  player, to the target object's position; e.g. On Button -> Spawn Player At
  a spawn point = respawn). ScriptContext gained a teleport request the game
  loops apply per template. Position links resolve at codegen into direct
  `objects[i].position` reads, so chains stay zero-cost. Also: flow zoom now
  scales the ImGui spacing vars, so node layouts shrink uniformly instead of
  drifting at low zoom. Verified via generated code + PCSX2 boot (box adopts
  the house position through a Get Position -> Set Object Position link);
  Square-button respawn needs a pad test.

- (21) **In-tree Tyra engine + WAV-aware music player** â€” the engine-patch
  machinery (embedded sources, awk macro swap, `/tyra/.tyra-editor-patch-N`
  markers) is gone: `vendor/tyra/engine` is now a versioned fork (Apache 2.0,
  upstream `9273416`) with all editor fixes applied directly. The Runner
  bind-mounts it read-only at `/engine-src`, checksum-rsyncs into the shared
  volume and rebuilds libtyra + relinks games only when something actually
  changed - editing the engine is now a regular workflow. New engine fix in
  the fork: `audio_song.cpp` parses the WAV header (RIFF chunk walk, done
  in-memory - fseek/ftell are unreliable over the PS2 host fs) instead of
  assuming 16-bit/22050/stereo at offset 0x30, configures audsrv from the
  file and streams exactly the data chunk; small formats get a smaller chunk
  size + fill threshold (mono 22 kHz starved audsrv's ring buffer before).
  Verified in PCSX2 with a tone/silence pattern: 44.1 kHz stereo with LIST
  metadata, mono 44.1 kHz and mono 22 kHz all play clean at correct speed
  (the old player fed metadata bytes to the speakers and halved the tempo).
  Music importer keeps files untouched and reports the format (PCM 16-bit,
  mono/stereo, 11-48 kHz; float/24-bit flagged as unplayable). Also: Spawn
  Player At now applies the target's Y rotation to the player's yaw.

- (22) **8-bit WAV crackle fix + player jump toggle** - the user's "correct in
  foobar, crackles on PS2" track turned out to be unsigned 8-bit PCM (48 kHz
  stereo): WAV stores 8-bit samples unsigned (0x80 = silence) while audsrv
  mixes them as signed, so the waveform wrapped at every zero crossing. The
  in-tree song player now converts the chunk in place (XOR 0x80) for 8-bit
  files. Verified with a quiet-tone pattern: peak 0.24 (full-scale wrap
  garbage) before, 0.02 (clean sine at the expected amplitude) after. Also:
  Player entity gets a "Can jump (X)" checkbox (PLAYER_CAN_JUMP gates the walk
  jump; jump speed hidden when off).

- (23) **"Use" interaction + global control mapping** â€” objects get a "Usable"
  checkbox: when the player camera is close (USE_DISTANCE) and looking at the
  object (USE_LOOK_DOT), a built-in "USE" sprite shows bottom-center
  (res/hud/use.png, 128x32 - PS2 textures need power-of-two sizes; shipped
  into every project when missing, replace to customize). Pressing the use
  button then sets ctx.usedObject for one frame, which fires the new flow
  graph **On Used** trigger (object param, self default - drop the graph on
  the usable object itself). Buttons are no longer hardcoded: the generated
  `inc/controls.hpp` is the single mapping place (BTN_USE = Square,
  BTN_JUMP = Cross, BTN_FLY_UP/DOWN for noclip + use-interaction tuning
  constants); marker-owned, so deleting the first line makes it a per-project
  settings file. Verified in PCSX2: USE prompt renders while facing a usable
  box up close; On Used -> Log compiled (`ctx.usedObject == idx`); the actual
  Square press needs a pad test.

- (24) **Viewport camera panning** â€” the editor camera orbits a movable target
  now: middle-mouse drag pans in the view plane, WASD flies over the terrain
  along the camera heading (both scale with zoom, so screen-space speed feels
  constant). Picking, sculpt raycast and the gizmo all follow the moved
  camera. Tool shortcuts moved off the letters to make room: Move/Rotate/
  Scale/Sculpt are 1/2/3/4 (button labels updated). New-terrain projects
  re-center the target. Interactions need a hands-on mouse/keyboard pass.

- (25) **Rendering corruption root-caused and fixed for real** - the
  recurring "objects render twice / giant smeared polygons" was never the
  engine patches (bisected all of them - even full upstream reproduced it):
  per-object bags used frustumCulling **None**, so objects behind or far
  off-screen were submitted raw and their coordinates wrapped the GS 4096px
  raster window. PCSX2's HW renderer often masks the wrap (hence "czasem
  dziaĹ‚a"); the SW renderer - and real hardware - show it faithfully. Fix:
  every bag (terrain, objects, sky dome) now goes through per-package frustum
  classification; the engine's bbox cache got a `bboxVersion` field on
  StaPipBag (mixed into the cache key) which the game bumps on every geometry
  rebuild, killing the stale-bbox problem that originally motivated None.
  Fast clipping mode = Precise classification + per-triangle cull (cheap, no
  wraps). The upstream "crappy guard band" bbox margins are zeroed (exact
  classification), and the VU1 guard-band experiments are fully retired
  (three variants all corrupted ADC bits; documented in vcl_sml.i). The EE
  clipper keeps the outcode + pool optimizations. Verified on the SW renderer
  (the honest one): known-bad camera positions render clean and stable,
  gameplay views correct, near-plane clipping right, 50 FPS.

- (26) **4:3 frame, HUD preview toggle, HUD flow nodes** â€” the viewport gets a
  "4:3" toolbar toggle that dims everything outside a centered 4:3 frame (a
  rough preview of the console picture on a TV); the HUD editor overlay maps
  into that frame when active. The HUD preview itself is now hidden by
  default ("Show in viewport" checkbox in the HUD section). New flow graph
  category **HUD**: Show HUD / Hide HUD / Toggle HUD flip all HUD images at
  runtime via ctx.hudVisible (scripts can use it too; the USE prompt is
  independent). Verified in PCSX2: Every-2-Seconds -> Toggle HUD blinks the
  crosshair while USE stays; editor overlays verified by screenshot.

- (27) **Multiple scenes** â€” every scene owns its objects (with their flow
  graphs); terrain/heightmap, settings, HUD and audio assets stay shared. The
  Scenes section creates (+ Scene modal), switches (click) and deletes (x)
  scenes; the first scene is the start scene. New **Switch Scene** flow node
  (Scene category) requests a change applied between frames. Memory design:
  all textures/models load once at startup for every scene, so a switch only
  rebuilds the runtime objects (vectors and per-object bags are reused/freed
  - no leaks, no VRAM churn, takes a frame). Generated code holds one object
  table per scene (SCENE_OBJECT_TABLES + per-scene PLAYER_* arrays indexed by
  currentScene); scene scripts are guarded by the active scene index and
  reset their state via a scene-generation counter on every (re)entry - a
  reused scene starts fresh. Legacy single-scene projects and old solution
  files migrate automatically. Verified in PCSX2: a two-scene ping-pong
  (Every 4 s -> Switch Scene both ways) alternates correctly and the state
  after re-entry is identical to the first visit; 50 FPS throughout.
  Trade-off noted: assets of ALL scenes stay resident (fine for editor-scale
  projects; per-scene asset streaming = future work).

- (28) **Per-scene terrain and lighting** â€” each scene now owns its terrain
  (size, sculpted heightmap in terrain-<scene>.heights, tiled texture) and
  its lighting (direction/ambient/diffuse/color/brightness); sky and physics
  prefs stay project-global. Preferences edit the ACTIVE scene. Generated
  code: per-scene arrays (TERRAIN_WIDTHS, HM_*_HEIGHTS tables, SCENE_LIGHT_*)
  behind accessor macros bound to a file-scope g_activeScene; loadScene()
  rebuilds the terrain mesh + sky dome on switch (vectors reused, bboxVersion
  bumped - still leak-free). Legacy project-level terrain/light migrate into
  every scene on load. Verified: legacy project renders identically at
  50 FPS; scene switching rebuilds terrain per scene.

- (29) **Particle emitters (fire/smoke/fog/sparks)** â€” new Emitter object
  (Effects submenu presets; cone marker in the viewport; properties: effect
  kind, pool size 1-128, particle size; color tints, scale X/Z = spawn area).
  2002-style runtime: fixed pools allocated once per scene load, one LCG,
  no trig or allocations in the per-frame path; particles are camera-facing
  color quads (no textures - alpha does the softness) with per-kind ramps
  (fire cools orange->red and shrinks, smoke grows, fog fades in/out on big
  lazy puffs, sparks burst and fall). One bag per emitter, Precise-culled,
  bboxVersion bumped per frame; rendered last (alpha over the scene).
  Show/Hide Object nodes switch emitters on/off. Verified in PCSX2: fog
  animates frame to frame at a steady 50 FPS.

- (30) **Post effects: bloom + film grain** - "shaders" the PS2 way, no pixel
  shaders involved. New engine module RendererCorePostFx (GS framebuffer
  blits at end of frame): bloom = frame downsampled to 1/8 res (bilinear),
  softened with 4 offset taps, re-added over the frame additively (the
  bilinear filter is the blur); film grain = 64x64 noise texture drawn
  subtractive+additive with independent random offsets every frame
  (zero-mean grain from unsigned GS math). ~8 textured sprites per frame,
  z writes masked, all touched GS registers restored (ALPHA/TEX1/CLAMP/
  FRAME/ZBUF/XYOFFSET - a leftover additive ALPHA made VU1 re-blend the
  whole scene and compound-brighten it). Editor: Preferences > Post effects
  sliders (0-1), baked as POSTFX_BLOOM/POSTFX_GRAIN (0-128). Verified in
  PCSX2 SW renderer at steady 50 FPS.

- (31) **PAL + NTSC viewport frames** - the single 4:3 frame became two
  labeled toggles (PAL white, NTSC yellow). PAL fills 4:3 exactly; NTSC has
  fewer active lines so the same 512x448 buffer reads slightly wider
  (~10:7). Dim outside the union; HUD preview maps into the PAL frame.
- (32) **Loading screens between scenes** - scene switches show
  res/hud/loading.png centered on black for ~0.7s (a generated
  "LOADING..." placeholder is written when missing; replace the file to
  customize; Preferences > Scenes toggle). The load itself stays
  synchronous - the hold is presentation. Verified in PCSX2 by
  ping-ponging two scenes: steady 50 FPS, no leaks.
- (33) **Sound emitter entity** - violet sphere marker; picks one of the
  imported sounds, autoplay + range + interval in the properties. In the
  game: volume falls off linearly with the distance to the player
  (channels 16-23, one per emitter). Interval 0 loops the sample
  seamlessly (tryPlay retriggers as soon as the channel frees); > 0
  retriggers every N seconds; Hide Object mutes. Verified with a WASAPI
  peak meter: near emitter 0.29, same emitter at 15/20 range units 0.03.
- (34) **Sculpt flatten mode** - a Flatten checkbox + Level height in the
  sculpt toolbar; the brush lerps terrain toward the target height with
  the same cosine falloff (strength = lerp rate). Undoable like sculpting.
- (35) **PS2 disc export + PCSX2 HostFs guard** - Project > Export PS2 ISO
  builds <project>/<name>.iso from bin/ with an in-tree ISO9660 writer
  (iso9660.cpp; no mkisofs), so file data LBAs follow load order:
  SYSTEM.CNF + ELF, optional iso-layout.txt pins, startup assets (HUD,
  sfx), per-scene textures/models, music, rest. Layout is printed to the
  build log. Engine side: FileUtils::fromCwd converts cdrom0: paths to
  ISO9660 form ('\', upper-case, ";1"), and extension/filename helpers
  strip the ";1" version (the selector trapped on "png;1" - caught by
  booting the ISO in PCSX2). Runner also flips HostFs=true in PCSX2.ini
  before launching (missing HostFs = the "Failed to load ...png" assert
  on first fopen). Verified: fixture ISO mounts on Windows byte-identical
  with the expected LBA order; my-game.iso boots in PCSX2 from cdrom0:
  and renders (screenshot check). Caveat for real DVD-Rs: keep asset
  names <=30 chars, A-Z 0-9 . _ - (exporter warns); animated obj/md2
  sequences don't get the ";1" suffix appended yet.
- (36) **Disc Layout window** (Project > Disc Layout...) - the ISO plan as
  a drag-to-reorder table (order persists to iso-layout.txt; pinned rows
  marked *, boot files locked first; Reset returns to the automatic group
  order) next to a physically-honest disc view: the spiral track drawn as
  constant-pitch rings filled inside-out, LBA->radius via the area formula
  (outer turns hold more data, like a real CLV disc), colored by load
  group with hover tooltips, click-to-select sync with the table and a
  capacity switch (fit/CD-R/DVD-5) with an over-capacity warning.
  isoexport gained plan()/saveManualOrder() (shared ordering with build),
  iso9660 gained plan() (layout without writing). Replans automatically
  after builds. Verified by driving the editor UI: reorder wrote
  iso-layout.txt, DVD-5 view shows the 2.3 MB image as a hairline at the
  hub, fit-to-data shows the ELF band + asset arcs on the outer track.
- (37) **Flow graph logic gates** - a "Logic" node category (AND, NAND, OR,
  NOT, XOR, XNOR) plus "On Condition". New boolean data plane (violet round
  pins, FlowLinkBool): every trigger now exposes a bool output = "does this
  condition hold this frame?" (Near Object -> isNear, On Button -> held,
  Every N Seconds -> the pulse, etc.). Gates fold their bool inputs (the
  bool-in pin accepts several links: AND/OR/XOR fold, NAND/NOT/XNOR negate);
  On Condition bridges back to exec and fires its "then" on the rising edge
  of the folded bool. Codegen inlines each bool as a self-contained C++
  expression (diamonds OK, cycles guard to false). Verified with a codegen
  harness: "Near AND Button -> Toggle" and "Near XOR Button -> Show" emit
  edge-gated blocks with the expected && / parity expressions.
- (38) **Point light entity** - new "Lighting > Point light" object
  (PrimitiveType 9): color (shared Color field), brightness and radius in
  the properties. Editor preview: an unshaded bulb glowing in the light
  color, a wireframe reach sphere scaled to the radius, and a **live light
  preview** - the viewport fragment shader adds up to 8 point lights on
  top of the baked directional shade (same (1-d/r)^2 * N.L formula as the
  game; flat normals from screen-space derivatives, so the shared unit
  meshes need no extra vertex data and the pool of light follows gizmo
  drags in real time). In the game the light is baked into nearby terrain
  and object vertex colors at build (additive diffuse, clamped to 1.0 like
  the directional term) - static, so zero runtime cost; no geometry and
  non-colliding. Verified: generated scene_data + terrain_game.cpp emit
  the fields and the pointLightAt bake (codegen harness); editor preview
  verified with a GDI screenshot (orange pool on the terrain, lit box
  faces, no bleed onto faces pointing away); PCSX2 boot pending.

- (39) **Post fx grain/bloom flicker fix** - film grain dropped out for a few
  frames every so often ("turns off for a fraction of a second"). Cause: the
  dynamic pipeline kicks the 3D scene on PATH1/VU1 asynchronously (double
  buffered - `sendPacket()` returns while the VIF1 DMA is still draining and VU1
  is still rasterizing), but `RendererCore::endFrame()` ran `postFx.apply()`
  with no barrier. PostFx composites over the framebuffer via PATH3 and masks z
  writes, so on frames where VU1 lagged, late scene triangles reached the GS
  after the grain sprites and drew back over them (passing the GEQUAL z-test),
  erasing the grain across all scene surfaces - bursty, exactly matching "every
  few frames". Fix: drain PATH1 before compositing with the engine's existing
  `sync.align3D()` handshake (a VU1 draw-finish tag + GS FINISH spin-wait -
  previously defined but never called in this fork), guarded by a new
  `postFx.isEnabled()` so games without post fx pay nothing. Verified in PCSX2
  SW renderer (showcase, grain=max): boots without hanging on the new spin-wait,
  steady 50 FPS, grain now fully and uniformly present over sky, terrain and
  every object across sampled moving frames.

- (40) **Memory card saves** - a save system spanning the whole chain. New
  "Save point" entity (PrimitiveType 10, Gameplay menu): a solid box that is
  implicitly usable; pressing USE on it in the game opens a 3-slot save/load
  menu. Per-object "Save state" checkbox (position/color/visibility persisted);
  project-level "Save data" values (name + fresh-game default, Project panel)
  editable and persisted per slot; new Flow Graph "Save" nodes: Set Save Value,
  Add To Save Value, Value At Least (pure bool source for logic gates) and
  Open Save Menu. Slots store scene index, player feet position + yaw
  (restored via the existing teleport request, so it survives scene switches
  and covers both player kinds), flagged objects and all save values in one
  fixed-size `SaveGameData` block sized at codegen (`SAVE_OBJECT_MAX`).
  Runtime (`save_system.gen.cpp`, always regenerated) uses **libmc** RPC with
  rom0:XMCMAN/XMCSERV (MCMAN/MCSERV fallback, nothing embedded - `-lmc` added
  to Makefile.base); the menu is pure sprites (`res/hud/save-*.png`, baked
  text rendered by a PIL script, embedded in `src/save_assets.cpp`, written
  when missing) because the engine has no font. Menu pauses gameplay (player,
  scripts, use target, object physics), with a 15-frame input grace after
  opening (the pad reports garbage transitions at boot - without it a
  spurious Cross click saved to slot 1 instantly). Hard-won mc lore: ps2sdk
  newlib `#error`s on direct fio use and does NOT route `mc0:` paths (errno
  EMLINK) - libmc is the only sane path; loading a rom0 module twice hangs
  the IOP; `mcGetInfo` out-params are junk across module variants (reported
  type=1/PS1 for an unformatted PS2 card), so card health is judged by a real
  mkdir+open probe and `sceMcResNoFormat` (-2) alone triggers a format
  (virgin PCSX2 card images are unformatted; formatting one destroys
  nothing). Host fallback (save<n>.sav next to the ELF) when no card answers.
  Verified e2e in PCSX2: probe script wrote+read a slot on the actual card
  image (values and object state round-tripped, no host .sav files, card file
  mtime moved), the OnStart-opened menu screenshot shows the panel with USED
  on the probe's slot, saving via pad (keyboard bindings + PostMessage) marked
  slot 1 USED, and loading the probe slot teleported the player, restored the
  ball position and turned the sky orange - proving Value At Least (loaded
  coins=42.5 >= 8) -> On Condition -> Set Sky fired from loaded data. Editor
  UI (Save data section, save-point in list + viewport) screenshot-verified.

- (41) **Menu generator** - author in-game menus in the editor, no font
  needed on the PS2: the editor rasterizes a Windows TTF (stb_truetype,
  Consolas Bold with Arial fallback, src/menubake.cpp) into one panel PNG
  per menu (title, entry labels, button hints, accent border) on every
  build (res/menus/*.png, always rebaked - derived data), and the game
  runtime only draws the panel + a cursor. New "Menus" section in the
  Project panel: entries with actions (Close, Switch Scene, Open Save
  Menu, Open Menu = submenus with a Triangle back-stack, Set/Add Save
  Value, Flow Event), per-menu accent color, "Title screen" flag (opens at
  boot, one per project, Back cannot dismiss it) and a **live WYSIWYG
  preview** of the exact panel pixels (in-memory bake -> GL texture,
  rebaked on change). Flow graph gained a "Menus" category: Open Menu
  (action) and On Menu Event (trigger + bool source) - menu entries with
  the "Flow event" action fire named events that graphs react to; event
  names resolve to indices at codegen (collectMenuEvents is the shared
  contract between menu_data.gen.hpp and flow_graph.gen.cpp). Menus pause
  gameplay like the save menu (player, use target, physics, scripts -
  except the frame an event fires, so On Menu Event triggers can run);
  the save menu draws on top when both are open. Layout constants
  (256-wide panel, rows at 44+i*24, pow2 canvas with transparent slack)
  live in menubake.hpp as the baker/preview/runtime contract. Verified
  e2e in PCSX2: title screen "SAVE-E2E QUEST" opens at boot (screenshot),
  pad-driven navigation (keyboard bindings + PostMessage) entered the
  orange-accent OPTIONS submenu, its "Sunset sky" Flow-Event entry turned
  the sky sunset via On Menu Event -> Set Sky while the menu stayed open
  (screenshot), Triangle popped back to the title and "Start game" closed
  it - gameplay resumed with the sunset sky kept (screenshot). Baked
  title.png inspected 1:1. Editor Menus section renders (built + GUI run);
  the ImGui preview widget shares the verified baker path.

- (42) **Pause management** - menus grew two flags. "Pauses the game"
  (default on): a pausing menu freezes gameplay under a new fullscreen dim
  overlay (res/hud/menu-dim.png, an 8x8 translucent black stretched to the
  screen, written when missing like the other built-ins; the save menu dims
  too); switched off, the menu floats over the RUNNING game - scripts,
  physics and the player keep going and pad presses reach both (by design,
  noted in the tooltip). "Open on Start button" designates the classic pause
  menu (one per project, like the title screen): Start opens it in-game and
  Start closes it again while its root shows (submenus first pop with
  Triangle). Runtime: updateGameMenu() now returns "is a PAUSING menu open"
  (MENUS[].pause from menu_data.gen.hpp; PAUSE_MENU = the Start-button
  target), so the loop gating needed no structural change. Also fixed a
  boot-window bug this exposed: the title screen's input grace was 15 frames,
  but the pad reports garbage clicks while it reconfigures for ~3.5s after
  boot (emulog "Pad: DS2 Config Finished" up to 3.4s) - one such click
  pressed "Start game" and unpaused the title, which is how the sunset-sky
  scripts betrayed it. Title grace is now 200 frames. Verified e2e in PCSX2:
  idle 13s boot keeps the title open over an unchanged (and now dimmed) blue
  scene; Start opens PAUSED (dimmed, frozen); entering the pause-off OPTIONS
  submenu un-dims and un-freezes - with the menu still open the ball fell
  from the sky and landed and EverySeconds pushed coins past 8 turning the
  sky sunset (one screenshot shows all three); Triangle back to PAUSED
  re-dims; Start resumes gameplay at full brightness (screenshot pair).

- (43) **Menu Editor window** - menu editing moved out of the cramped
  Project-panel section into a dedicated dockable "Menu Editor" window
  (Project > Menu Editor..., or click a menu in the panel's slim list, which
  now just lists menus with [title]/[start] tags). Left: menu list with
  "+ New menu"; right: properties, a Duplicate button (copies everything but
  the unique title-screen/Start-button slots), entries laid out one per row
  with reorder arrows (rows = dpad order in the game) and inline
  target/amount widgets, and the live baked preview under its own separator.
  This also fixes the reported ImGui "2 visible items with conflicting ID"
  error when deleting a Flow event entry: the old section used integer
  PushID offsets (2000/3000 ranges) and shared "##param"/"x" ids inside the
  busy Project window; the rework gives every entry a clean per-index PushID
  scope in a fresh window ID stack and every widget an explicit unique ##id
  ("x##delete", "##event", "##scene"...) - collision-proof by construction.
  Flow-graph invocation was already in place (Open Menu node, "Menus"
  category) and is now advertised in the window's empty-state hints.
  Verified: editor builds; the user drove the new window live (accent edits
  from it landed in project.json through the commit path) - the delete-entry
  repro needs their confirmation with the ImGui debug check active.

- (44) **Menu editor v2** - three usability upgrades. (a) New top-level
  "Tools" menu in the menu bar holds "Menu Editor..." (the Project-menu item
  and the Project-panel Menus section are gone - the window is the single
  home). (b) The preview gained display modes: "Panel (1:1)" plus "TV PAL" /
  "TV NTSC" - the panel composited onto a mock TV screen (the 512x448
  buffer stretched to 4:3 / ~10:7, same aspect approximations as the
  viewport TV frames), over the project's sky gradient + terrain green and
  under the pause dim when the menu pauses - so pixel-aspect distortion and
  on-screen size are visible before any boot. (c) Menus can carry a custom
  PNG ("Image: Set..." in the editor, copied into res/hud/), composited into
  the baked panel as either a logo block above the title (the panel grows,
  entry rows shift - menubake::panelLayout() is now the single geometry
  source consumed by the baker, the preview AND menu_data.gen.hpp codegen,
  so the game cursor lands on the shifted rows automatically) or a
  background stretched under everything with a dark wash for text contrast.
  Image scaling is bilinear, capped 224x160, canvas cap raised to 512 (pow2).
  Verified: title menu with a 192x72 logo baked correctly (panel content
  138->218, row0Y 44->124 in menu_data), PCSX2 boot shows the logo title
  screen with the cursor aligned on the shifted rows (screenshot). TV
  preview modes and the background image mode follow the same verified bake
  path but their look was checked in code only - a human glance in the
  editor window is welcome.

- (45) **Menu layout system** - panels stopped being a fixed centered
  256-wide stack. Per menu: texture width (128/256/512 - pow2, PS2 cap),
  normalized screen position of the panel center (like HUD images; the TV
  preview and the generated game share the same math), a "Show title" toggle
  (logo-only menus) and **layout presets** (Centered dialog / Title at the
  bottom / Corner card / Wide banner) that set width+position in one click.
  The single menu image grew into an **image list**: each entry has a slot -
  three flow slots (Above title / Above entries / Below entries) that stack
  in list order and push the text down, Background (stretched + dark wash)
  and Overlay (drawn over the text at a freeform offset) - plus a scale
  multiplier and a px offset (nudge for flow images, absolute position for
  overlays). menubake::panelLayout() stays the single geometry source
  (baker + preview + menu_data codegen), now returning per-menu panelW and a
  `clipped` flag surfaced as a red warning in the editor when content would
  blow past the 512px texture cap. Legacy "image"/"imageMode" project.json
  fields load into the list. Editor: new Layout + Images sections with
  reorder arrows per image. Verified e2e: title menu converted to the new
  format (logo above-title + a gold gem overlay at offset 206,8 scale 0.8,
  screenPos 0.5/0.7) baked correctly (panel PNG inspected), menu_data
  carries the position (0.5F, 0.7F), and the PCSX2 boot screenshot shows
  the panel sitting low on screen with both images and the cursor tracking
  the moved rows. 128/512 widths and the presets follow the same layout
  path but had no dedicated boot test.

- (46) **File-dialog freeze fix + drag&drop import** - on this machine the
  shell-based IFileOpenDialog wedges (select file, click Open -> the button
  grays and the dialog never returns), and giving it an owner HWND
  (glfwGetWin32Window) did NOT cure it - kept anyway as correctness. Two-part
  fix: (1) all FILE pickers now use the classic comdlg32 GetOpenFileNameW
  (folder picking keeps IFileOpenDialog/FOS_PICKFOLDERS - no legacy
  equivalent); (2) a dialog-free path: glfwSetDropCallback accepts PNGs
  dragged from Explorer - they copy into res/hud and, with the Menu Editor
  open, attach straight to the selected menu's image list (status bar
  reports what happened). Root cause of the shell wedge unconfirmed
  (OneDrive/shell-extension suspicion); if the legacy dialog ever wedges
  too, drag&drop is the escape hatch. Needs the user's interactive
  confirmation - the freeze never reproduced under automation.

- (47) **Per-menu fonts + text sizes** - menus pick their typeface and text
  scale. GameMenu::fontPath: "" = the default chain (Consolas Bold ->
  Arial Bold -> Arial), "res/fonts/x.ttf" = a font imported into the project
  (travels with it - reproducible builds), bare "impact.ttf" = a stock
  Windows font resolved via \Windows\Fonts. The Font combo in the Menu
  Editor lists project fonts, an existence-checked curated set of stock
  Windows faces (Arial/Comic Sans/Courier/Georgia/Impact/Segoe/Times/
  Trebuchet/Verdana bolds) and an "Import TTF..." action; dropping a
  .ttf/.otf on the editor window imports it and assigns it to the selected
  menu (same flow as PNG drops). Title and entry pixel sizes are editable
  (10-48 / 8-32); the entry size drives the row pitch (rowH = entrySize+9)
  and the title size the title block, all through menubake::panelLayout -
  MenuData.rowH was already per-menu data, so the game cursor follows
  automatically. menubake now caches fonts per path (map) instead of one
  static; a missing/unreadable font falls back to the default chain rather
  than failing the bake. Verified: bake + codegen check with impact.ttf and
  enlarged sizes on the e2e project (panel PNG + menu_data row geometry),
  PCSX2 boot screenshot.

- (48) **Output panel autoscroll fix** - the Output window now reliably sticks
  to the bottom as new build/launch lines arrive, and lets go the moment the
  user scrolls up (to read or select). The old implementation reconstructed
  InputTextMultiline's internal child-window name and drove it via
  FindWindowByName/SetScrollY - fragile against ImGui internals and it silently
  did nothing. Replaced with the canonical pattern: our own scrolling BeginChild
  owns the scrollbars, the read-only InputTextMultiline is sized to its content
  (still mouse-selectable) inside it, and SetScrollHereY(1.0f) fires only while
  GetScrollY >= GetScrollMaxY (one-frame lag keeps us pinned across appends).
  Note: this shows only the editor's build/launch pipeline; in-game TYRA_LOG
  (Flow Graph "Debug > Log Message") is printf on the EE and lands in PCSX2's
  console / emulog.txt, not here (PCSX2 is launched without an inherited pipe).
  Verified: clean build; behavior matches ImGui's official log-autoscroll
  example (live confirmation needs an interactive editor run).

- (49) **Sound emitters silent for filenames with spaces** - an autoplay sound
  emitter with a sound whose file had a space in the name (e.g. "Norwegian
  Horror Saga.wav") produced no audio in-game. Root cause was the wav->adpcm
  conversion loop in runner.cpp: `$f`/`$o` were unquoted, so a spaced filename
  word-split into multiple arguments - `basename` errored ("extra operand"),
  `[ ! $o -nt $f ]` errored ("too many arguments") which the `if` read as false,
  and adpenc was silently skipped. No `.adpcm` was produced, so at runtime
  `audio.adpcm.load` failed and the emitter played nothing. Inner double-quotes
  around the variables would be the obvious fix but cannot survive the
  cmd.exe /S + docker.exe argv unquoting layers (see Runner::exec), and single
  quotes would block expansion - so the loop now sets an empty `IFS`, which
  disables word splitting on the unquoted expansions while leaving the `*.wav`
  glob (pathname expansion, IFS-independent) intact. Verified: reproduced the
  broken loop locally to confirm the skip; editor rebuilt; `--build` on
  F:\Tyra-Projects\new-new-york now runs adpenc on the spaced file and produces
  bin/sfx/"Norwegian Horror Saga.adpcm" (16 MB) next to the ELF where before
  only the raw .wav was present.

- (50) **Sanitize asset filenames on import** - belt-and-suspenders for (49):
  every asset copied into res/ now runs its filename through
  `sanitizeAssetName` (app.cpp), which folds anything outside [A-Za-z0-9._-] to
  '_'. Applied at all import sites - models, object/terrain textures, HUD
  images, music, sound effects, menu fonts/images, and the Explorer drag-drop
  handler - so both the copied file and the relative path stored in the model
  use the same pipeline-safe name. This keeps spaces (and shell/ISO-special
  chars) out of the adpenc loop, Makefiles and the ISO9660 writer, which stores
  identifiers verbatim and so relies on clean input (sanitizing inside the ISO
  writer instead would desync the on-disc names from the paths baked into the
  game). Note: only affects newly imported assets - a project that already
  references a spaced file keeps that name until re-imported (runtime still
  works via the (49) hostfs fix). Verified: editor rebuilds clean; each import
  site audited to use the sanitized name for both the copy destination and the
  stored path.

- (51) **Debug window: game + emulator logs, configurable emulator path** -
  added a dockable "Debug" window (tabbed next to Output) that tails a log file
  from disk so game output is visible without leaving the editor. Two selectable
  sources: (a) **Game log** - the game's own `TYRA_LOG`/`TYRA_WARN`/`TYRA_ERROR`
  output and assertion dumps, and (b) **Emulator log** - PCSX2's `emulog.txt`
  (boot progress, BIOS/ELF-load errors). The window reads the last 1 MB on a
  Reload button or, while "Auto" is on, at most twice a second (per-frame reads
  would be wasteful on a large log), and follows the tail unless the user
  scrolls up (Output-window pattern). "Clear log" best-effort truncates the file.
  The game log is the key part: `TYRA_LOG` was previously a dead channel because
  it `printf`s to the EE console, which does not reach emulog (see tyra-testing),
  and the runner builds with plain `make` (no `NDEBUG`, so the macros are *not*
  stripped). Tyra already knows how to append logs to a host-side `log.txt`
  (`TyraDebug::writeInLogFile` via `FileUtils::fromCwd`) when the
  `Tyra::Info::writeLogsToFile` static is set - generated games just never
  flipped it. So the generated `src/main.cpp` bootstrap (templates.cpp) now sets
  it before constructing the Engine, and `main.cpp` was moved into
  `refreshGenerated`'s always-regenerated set (it is a 6-line editor-owned entry
  point; game logic lives in the ownable `terrain_game.cpp`/scripts) so existing
  projects pick it up on the next build. `bin/log.txt` is deleted before each
  launch (runner) so the window shows only the current run; no engine fork edit
  was needed. Because all 54 engine `TYRA_LOG` sites are init-time or edge-case
  warnings (none per-frame), routing them to a file is safe. Also added
  `Project > Preferences > Emulator`: a PCSX2 executable path (Browse.../Clear).
  The path is editor-side state (stored in the `.tyra` `editor` object next to
  selection/gizmo/layout, escaped like `layout` - NOT in ProjectSettings, so it
  is never baked into codegen, never per-scene, not part of undo); both
  Build && Run and the emulator-log lookup prefer it over the Program Files
  auto-detect (`resolveEmulator`), and a configured-but-missing path reports
  itself specifically instead of silently falling back. Verified: clean rebuild
  (all TUs); `--new` project serializes `"emulatorPath": ""` and stays valid
  JSON; a backslash Windows path written into the `.tyra` round-trips (jsonEscape
  `\`->`\\`, parser `\\`->`\`) and `--build` loads it and reaches the build stage
  with no parse error; fresh `--new` `main.cpp` carries the
  `Tyra::Info::writeLogsToFile = true` line, and a deliberately overwritten
  `main.cpp` is rewritten back by `refreshGenerated` on the next `--build`. NOT
  verified here (needs a PCSX2 + BIOS run, which launches an emulator window on
  the active machine): the actual host-fs write of `log.txt` from the running
  ELF and the live tail rendering. This reuses upstream Tyra's own logging path,
  so the host-fs write is expected to work where asset *reads* already do
  (HostFs is read-write), but it wants a hands-on run to confirm end to end.

- (52) **Sound emitters: oversized samples crash/silent - safe load + SPU2
  budget guard** - after (49)/(50) fixed the spaced-filename conversion, a
  reimported sound still played nothing. Root cause was the sample itself: a
  ~5 min stereo 44.1 kHz WAV (56 MB) becomes ~16 MB ADPCM, but sound emitters
  are audsrv one-shots loaded whole into SPU2's ~2 MB sample RAM, so it can
  never fit. Worse, the engine's `AudioAdpcm::load` read the file into a
  variable-length array on the EE stack (`u8 data[size]`) sized via fseek/ftell
  (unreliable over host fs) - a guaranteed stack overflow / bogus size for
  anything large, and it ignored the audsrv load result. Three changes:
  (a) engine `audio_adpcm.cpp` - read incrementally into a heap buffer (no
  fseek/ftell, no stack VLA), check the audsrv result, and return nullptr on
  any failure; `tryPlay` treats null as a benign no-op. (b) codegen - the
  emitter update loop skips null samples. (c) editor - import estimates the
  ADPCM footprint (~2/7 of the WAV) and warns when a single sound exceeds the
  ~2 MB SPU2 budget, and the Sounds panel shows each sound's estimated size and
  a running "SPU2 sample RAM: ~X / 2.0 MB" total that turns red over budget.
  Verified end-to-end: built a scratch fpp project with a short mono 22050 Hz
  tone on an autoplay emitter; libtyra rebuilt with the engine change (clean
  compile + link in Docker), beep.wav -> 5 KB adpcm, game boots at 50 FPS, and
  the WASAPI render-endpoint peak meter read a sustained 0.44 while running and
  0.00 after quit - i.e. the emitter is audible. The oversized new-new-york
  sample now degrades to silence without crashing (game boots fine). Editor UI
  warning path is compile-verified (native file dialog can't be driven headless).

- (53) **Positional stereo for sound emitters (audsrv upgrade + panning)** -
  emitters played dead-center regardless of position: the image's PS2SDK ships
  an old audsrv whose only ADPCM volume call sets the SPU2 voice's L and R
  levels equally, and audsrv has no pan RPC at all. Upstream ps2sdk added
  per-channel L/R in `66ae317d` ("Implement positional audio in adpcm",
  2023-01: IOP `audsrv_adpcm_set_volume(ch, voll, volr)` + EE
  `audsrv_adpcm_set_volume_and_pan(ch, vol, pan)` with pan -100..100), but the
  audsrv build system was later rewritten to need srxfixup + a newer toolchain
  (`00f199ae`, 2025-01) that the image cannot run. Solution: built audsrv from
  the last pre-srxfixup commit (`e78a9cb2`, pinned; builds with the image's
  `mipsel-ps2-irx-` toolchain and has the pan feature) and vendored the three
  artifacts in `vendor/tyra/audsrv-pan/` (audsrv.irx + libaudsrv.a + audsrv.h,
  README has the rebuild recipe; marked binary in .gitattributes so the
  vendor/tyra LF rule can't corrupt them). The runner overlays them over
  `$PS2SDK` at the start of every build. Engine: `AudioAdpcm::setVolumeAndPan`
  added; old `setVolume` still compiles via the header's 2-arg back-compat
  macro (centered). Codegen: `updateSoundEmitters` computes pan by projecting
  the horizontal emitter direction onto the camera's right axis (same right
  vector as the particle billboards) and calls setVolumeAndPan.
  Two pitfalls burned into the runner logic: (a) the `.irx-em` make rule
  depends only on the `.irx-em` text file, NOT the IRX binary it embeds - so
  swapping the SDK's audsrv.irx did nothing until the stamp check also deletes
  `obj/irx/audsrv.o` (bin2s re-runs, libtyra re-embeds). Diagnosed via nm: the
  embedded `audsrv_irx` symbol was still the old module's size. (b) with the
  old IRX + new EE lib, the 3-word volume RPC is read as the old 2-word one -
  data[1] (the L level) becomes the mono volume, so pan=100 gave total silence
  and pan=70 gave quiet-equal-both - measurements that first looked like a
  PCSX2 downmix. (c) the first shipped pan was MIRRORED (user report: sound on
  the left heard on the right): the right vector was borrowed from the particle
  billboards ((fwd.z, -fwd.x)), whose sign was never validated because
  billboard quads are symmetric. In this right-handed Y-up world screen-right
  is fwd x up = (-fwd.z, fwd.x). The original meter test only proved
  pan-sign <-> speaker-side consistency, not formula <-> screen-side - the
  emitter was off-screen and invisible. Re-verified with the loop actually
  closed: a visible red box + emitter at the same spot, screenshot shows the
  box on the LEFT half of the screen and the WASAPI per-channel meter reads
  L=0.298/R=0.086 (and the pre-fix runs measured the full matrix:
  centered -> equal 0.39/0.39, pan +-70 -> ~4-9x asymmetry; instrumented
  TYRA_LOG confirmed dist/vol/pan). Diagnostic logging removed after
  verification; editor + game build clean.

- (54) **Properties window + per-type property cleanup + collapsible Project
  sections** - object properties moved out of the Project panel into a new
  "Properties" dock window (default layout: below Project in the left column;
  projects saved before this change get it docked there automatically - a
  pending-dock pass splits the Project node when the stored layout ini has no
  `[Window][Properties]` section). The properties themselves are now gated by
  what the game actually reads per type (matrix derived from the generated
  runtime: geometry build, collision, use-target and physics loops all skip
  marker types): sound emitters/point lights/spawn points/player no longer
  offer texture, rotation, scale, physics or "usable" (e.g. a texture set on a
  sound emitter was a dead setting - only geometry types 0-3/5/10 ever bind
  textures); color hidden for pure markers (sound/spawn/player) since they
  draw in fixed editor colors; "save state" limited to solids + emitters +
  sound emitters (lights are baked at build time); the Type combo now only
  converts between the four primitive shapes instead of also offering
  spawn-point/model/player conversions that silently produced misconfigured
  objects (it also indexed past its 7-name array for types 7-10). Project
  panel sections (Scenes, Scene objects, HUD, Music, Sounds, Save data,
  Scripts) are CollapsingHeaders now - Scenes + Scene objects default open,
  the rest collapsed. Verified: editor built clean; scratch project with a
  player + injected sound emitter + box (`.tyra` edited directly, selection
  preset) screenshotted per case - sound emitter shows only
  name/type/position/save-state/sound params, box shows the full set, and a
  layout with the Properties section stripped re-docks it under Project on
  load. Sections collapse/expand state confirmed in the same screenshots.

- (55) **Flow graph: Self node + typed variables (Set/Get Int, Bool,
  Position)** - "Self" is a pure data node exposing the graph's owner as an
  object output; object params already defaulted to self when empty, this
  makes the reference explicit and wireable (it needed zero codegen changes -
  resolveTarget's chain walk ends on a node with no object input and no
  explicit name, which is exactly self). New "Variables" node category:
  named game-global values in three separate namespaces (int / bool /
  position), zeroed at boot, kept across scene switches, NOT saved to the
  memory card (Save values remain the persistent store). Setters (Set Int,
  Set Bool, Set Position) run on exec; Set Position accepts a position link
  that overrides its X/Y/Z params (so Get Position on an object -> Set
  Position stores a live object position). Readers are pure data sources in
  the house style: Get Bool -> bool output for logic gates / On Condition,
  Get Position -> position output for Set Object Position / Spawn Player At,
  Int At Least -> bool (mirrors the save Value At Least). A variable exists
  by being named on any node - codegen collects names across every scene's
  graphs into static flowInt/flowBool/flowPos arrays at the top of
  flow_graph.gen.cpp (statics, not ScriptContext - script.hpp is
  user-ownable, so adding context fields would break owned copies). Editor:
  VarName params are free text + a "Pick..." popup of same-type names used
  anywhere in the project (typo guard); Set Bool renders a checkbox; the
  node registry drives pins/serialization so project.cpp needed no changes.
  Verified: editor builds clean; a graph exercising every new node (Self ->
  Get Position -> Set Position "home" on On Start, Set Bool/Set Int, Get
  Bool + Int At Least OR-folded into On Condition -> Hide Object, Every N
  Seconds -> Set Object Position fed by Get Position "home") was injected
  into a scratch project's .tyra; the generated flow_graph.gen.cpp is
  exactly right (statics with name comments, self resolves to the owner
  index, rising-edge OnCondition folds both variable reads) and the full
  Docker PS2 build compiled it clean (bin/propwin.elf linked). GUI
  screenshot confirms the nodes render with pins/params (Flow Graph tab
  forced via the layout ini's Selected TabId - ImHashStr("#TAB", seed =
  ImHashStr(name)), CRC32c in this imgui). Not boot-tested in PCSX2; the
  generated code paths are the same ctx.objects/flag mechanics as existing
  nodes.

- (56) **Sound emitters: "Play on player" (2D stereo) property** - for
  dialogs/narration: a sound that always plays "on the player" at full
  volume, centered, regardless of the emitter's position. New
  `SceneObject::soundOnPlayer` (saved as `"onPlayer"` in the sound block,
  defaults false on old projects), `SceneObjectData::sndOnPlayer` in the
  always-regenerated scene_data.hpp, and a branch in the terrain_game
  template's updateSoundEmitters that skips the whole distance/range/pan
  computation (vol=100, pan=0) when set - visibility mute and the
  interval/loop retrigger logic still apply. Properties UI: checkbox under
  Autoplay; Range is hidden while it's on (no falloff to range) and the help
  text switches to the dialog wording. Verified: editor builds clean;
  `onPlayer: true` set on a scratch project's emitter produces a `1` in the
  right SceneObjectData slot (neighbors 0), the generated
  updateSoundEmitters carries the sndOnPlayer branch, and the full Docker
  PS2 build compiled + linked. GUI screenshot shows the ticked checkbox,
  hidden Range and swapped help text (load path of the flag proven by the
  ticked box). Not ear-tested; the playback path below the vol/pan values
  is unchanged from (53).
- (57) **Window docking lost when opening a project mid-session** - user
  report: docking didn't seem to persist and panels scattered after loading a
  project. Root cause: `attachProject()` called
  `ImGui::LoadIniSettingsFromMemory` mid-frame (File > Open / Ctrl+O fire from
  inside drawUI), which imgui explicitly does not support between
  NewFrame/EndFrame (commented-out assert in imgui.cpp) - the dock settings
  handler clears and rebuilds all dock nodes while the current frame's windows
  still reference the old ones, so the layout applied half-broken. ~5 s later
  the `io.WantSaveIniSettings` autosave then wrote that mangled layout back
  into the freshly opened project's .tyra, destroying its good saved docking
  (hence "doesn't save"). Second overwrite path: a WantSave pending from
  before the open captured the *previous* project's on-screen layout into the
  new project's file in the same frame. Fix: `attachProject()` only sets
  `layoutLoadPending_`; the run() loop applies the layout at the frame
  boundary (before NewFrame), where imgui's ApplyAll handlers re-dock existing
  windows properly; `saveProject()` keeps the stored layout string while a
  load is pending instead of capturing the stale screen. The startup path
  (project dir on the command line) goes through the same deferred path.
  Verified: clean build; scratch project run 1 saves a layout with
  `[Docking][Data]` into the .tyra on exit, run 2 restores it through the
  deferred path (screenshot: Project left, Viewport/Flow Graph center,
  Output/Debug bottom) and the layout string round-trips byte-identical. The
  mid-session File > Open path needs a hands-on mouse test (no synthetic
  input from automation), but it now runs the exact same deferred code path
  as the verified startup load.
- (58) **Gizmo axis spaces: absolute (world) vs camera-relative** - two
  transform modes for move/rotate/scale, toggled by World/Camera buttons in
  the viewport toolbar (next to Move/Rotate/Scale) or key 5, persisted in the
  .tyra editor block as `gizmoSpace` (like `gizmo`/`viewMode`). Absolute mode
  passes ImGuizmo::WORLD for all ops: move and rotate follow the world X/Y/Z
  (rotate previously used LOCAL - object axes; world axes is what "absolute"
  means and the camera mode covers view-based rotation). Scale stays on the
  object's own axes in both modes - ImGuizmo forces SCALE to LOCAL because a
  world/camera-axis scale on a rotated object cannot be stored in a TRS
  (shear). Camera mode manipulates a unit-scale proxy matrix whose local
  frame is the camera rotation (transpose of the view 3x3; ImGuizmo/GL share
  column-major layout) with ImGuizmo::LOCAL: translate lands in the proxy
  position (per-frame incremental in ImGuizmo, so rebuilding the proxy from
  the object each frame is correct); rotate extracts the world-space delta
  W = proxyOut * view (proxyIn^-1 = the view rotation), applies model' =
  W * model about the object position and takes only the euler rotation from
  the decompose (W is orthogonal - position/scale unchanged by construction,
  discarding them avoids float drift); scale reads ImGuizmo's deltaMatrix -
  which is cumulative over the whole drag, hence a drag-start scale snapshot
  captured while !IsUsing() - and applies the dominant diagonal factor
  uniformly. Ctrl-snap works in all cases (ImGuizmo snaps translation in the
  gizmo's local frame, rotation by angle, scale by factor). Verified: clean
  build; `--new` writes `"gizmoSpace": 0` in the editor block; hand-editing
  it to 1 and opening the project shows the Camera button active in a GUI
  screenshot (load path proven), World/Camera sit in the toolbar between the
  tool and shading groups. Drag feel (camera-axis move/rotate on a rotated
  object, uniform camera scale) needs a hands-on mouse test - no synthetic
  input from automation.

- (58) **Target system (PAL/NTSC) + debug/release build profiles** - Project >
  Preferences grew a "Build" section: **Target system** (Auto / NTSC 60 Hz /
  PAL 50 Hz) and **Profile** (Release / Debug) with "Show FPS" and "Show memory
  usage" checkboxes that are grayed out unless the profile is Debug. All four
  live in `ProjectSettings` (saved in the `.tyra` `settings` block, defaults
  `auto`/`release`/off - old projects load unchanged). Engine: new
  `Tyra::VideoMode` enum threaded `EngineOptions.videoMode` -> `Renderer::init`
  -> `RendererSettings`; `RendererCoreGS::allocateBuffers` keeps
  `graph_initialize` (region auto-detect) for Auto and otherwise runs the same
  ps2sdk call sequence with a forced `GRAPH_MODE_PAL/NTSC` (verified identical
  by disassembling libgraph's `graph_initialize`; the 512x448 framebuffer is
  kept for both signals). Generated `main.cpp` passes the mode via
  `EngineOptions` - and must also set `options.writeLogsToFile = true`, because
  the options ctor re-applies that flag and silently reset the static set the
  line before (log.txt vanished until this was spotted). Debug HUD: constexpr
  `DEBUG_SHOW_FPS/MEM` in `terrain_config.hpp` (forced false in release, so the
  overlay folds away), a `drawDebugHud()` helper in the game-cpp prolog drawing
  `FPS n` / `MEM n.n MB` (from `Info::getFps()` / `getAvailableRAM()`, the
  latter sampled every ~2s - it malloc-probes the heap) with an 8x8 CP437-style
  glyph strip the editor bakes into `res/hud/debugfont.png` (glyphs on a 16px
  stride so bilinear sampling can't bleed neighbors; written by
  `refreshGenerated` only when a debug overlay is on). Verified e2e in PCSX2
  (software renderer, Europe/PAL BIOS): forced PAL -> status bar PAL, 50 VPS,
  overlay reads "FPS 50 / MEM 27.9 MB"; forced NTSC on the same PAL BIOS ->
  NTSC, 60 VPS, "FPS 59" (proves the force, not region detect); release
  profile -> overlay gone, still forced NTSC. **Testing pitfall that cost an
  hour**: the first e2e ran the project from the session scratchpad whose path
  is ~185 chars - PS2 `loadelf` truncates the `host:` path (emulog showed a
  mangled `secname`) and the ELF jumped to null before the banner; looked
  exactly like an engine-ABI bug. Scratch projects for PCSX2 must live in a
  short path (e.g. `%TEMP%\tyra-editor-test\`). The Preferences graying uses
  the standard staged-copy + `BeginDisabled` pattern; the modal itself wasn't
  exercised by hand (no input injection) - one human click-through pending.

- (59) **Wall-clock normalization: same game speed on PAL and NTSC** - all
  generated game logic was per-frame with 50 FPS baked in (`GRAVITY/(50*50)`,
  `JUMP_SPEED/50`, `EverySeconds * 50`, particle `dt = 1/50`...), so an NTSC
  build ran 20% fast. Engine: GS init now resolves `VideoMode::Auto` to the
  console's real region (`graph_get_region`) and writes it back into
  `RendererSettings`, which gained `getRefreshRate()` (50/60); the Auto and
  forced paths collapsed into one explicit `graph_set_mode` sequence. Codegen:
  `scene_data.hpp` declares `g_frameRate` / `g_frameDt` (1/rate) /
  `g_frameScale` (50/rate - the "tuned at 50 Hz" conversion factor) plus an
  inline `everyFrames(seconds)` helper, all defined in the game cpp and set in
  `TerrainGame::init()`; every per-frame site now multiplies through them:
  FPP + Player-entity look/walk/fly/gravity/jump, orbit step, object physics,
  particles, sound-emitter retrigger, flow-graph `EverySeconds` (emitted as
  `frame % everyFrames(N)` instead of a compile-time constant), loading-screen
  hold, save feedback and the debug-HUD MEM refresh. User scripts see the
  globals through `scene_data.hpp` via `script.hpp`. Verified with a wall-clock
  measurement, not by eye: an `EverySeconds(1s) -> Log("TICK")` graph, counting
  TICK lines in the host-side log.txt against real time - forced NTSC 60 Hz:
  0.998 ticks/s over 60 s; forced PAL 50 Hz: 0.998 ticks/s over 45 s (the old
  codegen would read 1.2/s on NTSC). Both modes verified booting at their
  vsync rate (status bar 50/60 VPS). Legacy V1 templates untouched (their
  byte-identical match must keep working).
- (60) **Configurable viewport navigation + orbit around selection** - the
  camera controls were hard-coded (LMB/RMB orbit, MMB pan, WASD fly) and the
  orbit pivot was a free point, never the selection. Added a global `NavConfig`
  (app.hpp): mouse **scheme** (Tyra/Blender/Maya/Unity - each maps which
  drag+modifier orbits vs pans; Blender/Unity free the LMB for selection, Maya
  adds an Alt+RMB dolly), **fly keys** (WASD or arrow keys), orbit/pan/zoom
  **sensitivity** multipliers, invert X/Y, and an **orbit-around-selection**
  toggle. These are machine/muscle-memory prefs, not project data, so they
  extend the existing global `editor.ini` in %LOCALAPPDATA% (the old single-key
  uiScale reader/writer became a full `EditorConfig` load/save; whole file
  rewritten on any change). Input in `drawViewportWindow` now switches on the
  scheme and scales/inverts pixel deltas; `Viewport::zoom` became continuous
  (`distance *= 0.9^wheel`) so sensitivity and drag-dolly move proportionally
  while one wheel notch keeps the old 0.9x feel; new `Viewport::setTarget`
  snaps the orbit pivot to the selected object's position whenever the
  selection index changes (independent of the transform gizmo mode) - pan/fly
  afterward still move the pivot freely. New "View > Navigation controls..."
  modal (clone of the Preferences modal pattern) edits it all live and persists
  on every change, with a Restore-defaults button. Verified: clean build;
  editor launched on a scratch fpp project and ran the new per-frame nav code
  (scheme gating + focus snapping execute every frame) without crashing;
  `editor.ini` round-trip confirmed by triggering a save (Ctrl+= -> setUiScale)
  and reading back all nine nav keys at their defaults, then restoring auto DPI.
  The interactive mouse feel per scheme (Blender/Maya/Unity muscle memory,
  invert/sensitivity tuning) still wants a hands-on human pass - the GDI
  screenshot tool can't capture ImGui's multi-viewport menu popups.
  Follow-up: with arrow-key movement, ImGui's keyboard nav (NavEnableKeyboard,
  on globally) was cycling focus through the viewport overlay tool buttons
  (Move/Rotate/Scale, World/Camera, view mode) as the arrows were pressed. Fixed
  by giving the Viewport window `ImGuiWindowFlags_NoNav` - those buttons keep
  their 1/2/3/5 hotkeys, so nothing is lost. Verified: forced arrow mode in
  editor.ini, focused the viewport, sent 6x Down + 6x Right - the tool stayed on
  Move (no focus migration), screenshot-confirmed.
- (61) **Per-object triangle detail for curved primitives** - Sphere, Cylinder
  and Cone were tessellated at fixed hard-coded segment counts (sphere 10x14,
  cyl/cone 16 in the game; 12x18 / 24 in the viewport), so the poly budget of a
  primitive was not authorable. Added a single `SceneObject::primDetail` field
  (radial segment count, default `kDefaultPrimDetail = 16`, clamped 3..64) that
  drives the tessellation. The mapping lives once in `project.hpp`
  (`clampPrimDetail`, `primSphereStacks` = ~5:7 of the segment count,
  `primTriangleCount` for the UI readout) and is mirrored in the two generators
  that can't share it across the editor/game boundary: `viewport.cpp`
  `unitSphere/unitCylinder/unitCone` now take a detail arg, and the generated PS2
  runtime `addSphere/addCylinder/addCone` (templates.cpp `TPL_GAME_CPP_PROLOG`)
  read `o.primDetail` with an inline clamp. Box keeps its fixed 12 tris (detail
  hidden for it). **Data model**: field + `operator==` (undo), JSON save
  (`"detail": N`, emitted only for curved primitives at a non-default value -
  same implicit-default style as `collision`) and load (clamped) in project.cpp,
  and a new trailing `primDetail` column in the generated `SceneObjectData`
  table (struct decl + empty-scene initializer + per-object emission). **UI**: a
  "Detail" DragInt (3..64 segments) with a live `(N tris)` readout in the
  Properties panel, shown only for Sphere/Cylinder/Cone, committing one undo
  step per edit. **Viewport**: curved primitives no longer share one static
  mesh - a per-detail `std::map<int,Mesh>` cache (one per shape) builds meshes
  lazily and shares them across objects at the same detail; the fixed
  `sphere_`/`cylinder_`/`cone_` stay at the default for markers and the Material
  Editor preview. Verified: clean editor build; a scratch project with spheres
  at detail 5/6/40/48, a default (16) cone and a detail-5 cylinder round-tripped
  through the .tyra loader into `inc/scene_data.hpp` with the exact trailing
  counts (`..., 1.0F, 6}`, `40`, `16`, `5`); a full Docker PS2 build of that
  project returned `=== Build OK ===`, so the generated per-object tessellation
  compiles and links on the mips64r5900 toolchain. In-editor visual diff of a
  chunky vs smooth sphere and an in-PCSX2 side-by-side still want a human
  eyeball - the GDI window capture only grabbed a fixed strip of the 4K editor
  window (Project panel) and the top sky band of the PCSX2 output, so neither
  framed the objects.
  Follow-up: extended the control to **Box** so every geometry primitive is
  authorable (requested; box subdivision also gives baked point-light gradients
  something to shade). `primDetail` is now type-aware: for a box it counts
  subdivisions per edge (1 = the plain 12-tri box, capped at 16 -> 3072 tris
  since box triangles grow quadratically), for curved shapes it stays radial
  segments (3..64). Added `kDefaultBoxDetail`/`primDetailMin`/`primDetailMax`/
  `defaultPrimDetail` and made `clampPrimDetail` take the type (project.hpp);
  `unitBox`/`addBox` now tessellate each face into an n x n grid (UVs span 0..1
  per face, so textures don't tile with detail; at detail 1 the output is
  byte-identical to the old box, and winding was checked face-by-face). The
  Properties slider shows for all four shapes with per-type range and a
  "segments"/"subdivisions" label, and re-fits the value when the Type combo
  switches shape; `addObject` seeds the type's default. Load defaults a box with
  no `"detail"` key to 1, so old projects keep their plain boxes. It is freely
  editable any time (no write-once lock - the slider reads the live value every
  frame and commits one undo step per edit). Verified: clean editor build;
  editor launched on a 2-box scene (plain + detail-6) rendered without crashing
  (exercises the box mesh cache / `uploadMesh(unitBox(6))` on the draw path);
  Docker PS2 build `=== Build OK ===`; `scene_data.hpp` emits `plainBox` -> `1`
  (no key, backward-compatible) and `subBox` -> `6` (432 tris). Same 4K/PCSX2
  capture limitation still blocks an automated visual poly-count diff.


- (60) **Streaming layers: per-scene object groups the game loads/evicts at
  runtime (GTA3-style interior streaming)** - `SceneData::layers` (name +
  `startLoaded` + editor-only `editorVisible`), `SceneObject::layer` (by
  name, "" = always resident), serialized in the `.tyra` scene block and the
  history file. Editor: a "Layers" section in the Project panel (add /
  rename-in-place / delete, eye toggle, "start" checkbox, per-layer object
  count; rename remaps object + flow-node references, delete unassigns), a
  Layer combo in Properties, and hidden layers vanish from the viewport
  (render, click picking, gizmo, light preview, emitter previews) via a
  hidden-index mask passed to the viewport; the object list dims them with
  a [hidden] tag. Flow graph: Load Layer / Unload Layer actions and a pure
  Is Layer Loaded bool (new `FlowParamKind::LayerName` combo), compiled to
  writes into new `ScriptContext` fields (`layerRequest`/`layerState`,
  mirroring the requestScene pattern). Runtime (generated game, both orbit
  and FPP): asset residency is now demand-driven - `buildScene()` no longer
  loads every scene's models/materials/anim models/terrain textures at
  boot; `loadScene()` computes what the scene's start-resident layers need,
  loads it synchronously behind the existing loading screen and frees what
  nothing needs any more (scene switches now also evict the previous
  scene's assets instead of keeping everything forever). During gameplay
  `updateLayerStreaming()` applies script requests: unload drops the
  layer's objects the same frame (new `RuntimeObject::active` gates render,
  collision, USE, sounds, physics, anim pass and particle pools) and frees
  assets no resident layer uses; load streams missing assets from a queue
  at ONE asset per frame, then trickle-activates the layer's objects 4 per
  frame - the whole cost hides in a corridor walk, no frame stall. Shared
  textures are reference-counted by path in a texture cache
  (`TextureRepository::free` releases the GS buffer + destructs); models
  free their geometry, collider and texture refs. Layer state resets to the
  authored defaults on scene (re)load, like the rest of the runtime state;
  point lights stay baked (an unloaded layer's light keeps shining exactly
  as a hidden light does today). Legacy V1 templates untouched. Verified:
  clean editor build; scratch FPP project (short path) with an "exterior"
  start layer (spheres, one with a stone .mtl texture) + non-start
  "interior" (boxes, a brick-textured box, house.obj with mesh collision)
  and an OnStart -> Delay 4s -> Load Layer interior / Delay 8s -> Unload
  Layer exterior graph, built in Docker and run in PCSX2 at 50 FPS: timed
  screenshots show exterior-only -> both (the house model + brick texture
  visibly stream in mid-game) -> interior-only; `IsLayerLoaded ->
  OnCondition -> Log` printed INTERIOR-NOW-LOADED to host log.txt; the
  debug MEM overlay dropped 4.8 -> 4.4 MB after the unload in the
  primitive-only variant (freed vertex buffers + texture). Editor side:
  layers round-trip the .tyra through a GUI reopen (screenshot shows both
  rows incl. a persisted eye-off state); sample script-demo regenerated and
  builds (zero-layer degenerate case). Hands-on pending (no synthetic
  input from automation): clicking the eye/rename/delete controls and
  walking a real corridor with a pad. Testing fix that cost three runs:
  the bundled screenshot-window.ps1 captured a scaled-up crop of the
  window's top-left corner on a display scaled above 100% - the script now
  calls SetProcessDPIAware() first. Follow-up in the same PR: user docs
  (docs/streaming-layers.md, linked from docs/README.md and the root
  README) and a new `examples/` folder ("one runnable project per
  feature") whose first entry, examples/layer-streaming, is the canonical
  two-buildings-and-a-corridor scene: four Near Object trigger markers in
  the corridor swap the buildings' layers bidirectionally (each direction
  passes a harmless no-op unload first, then the load for the building
  ahead, then - close to the far door - the unload for the one behind, so
  walking back and forth needs no extra logic), debug MEM/FPS overlay on.
  Verified: Docker build exit 0 from the repo checkout; the compiled
  trigger graphs inspected in flow_graph.gen.cpp (four correct
  layerRequest writes). A PCSX2 boot check collided with a parallel
  session's emulator run (the runner taskkills the shared PCSX2 on every
  launch), so the pad walkthrough stays a hands-on check - the streaming
  runtime itself was e2e-verified above through the same codepaths.

- (61) **Layer streaming stability: stale bbox cache, leaked GS VRAM,
  mid-frame texture uploads** - user repro on the layer-streaming example:
  streamed-in objects sometimes rendered wrong/misplaced, and longer play
  hit the `index < partsCount` assert in stapip_bag_packages_bbox.cpp:76.
  Three root causes, all firsts exposed by streaming (nothing ever freed
  buffers or textures mid-game before):
  1. *Bbox-cache aliasing.* The engine's frustum-bbox cache is keyed by
     (vertex pointer, bboxVersion). Streaming frees vertex buffers and the
     next layer's vectors can land on the recycled heap address; with
     per-bag counters both sides often sit at version 1, so the new buffer
     inherited the dead buffer's cached boxes - packages misclassify
     (smeared/vanishing geometry) or the package index runs past the cached
     part count (the assert). Fix: generated games stamp every rebuilt bag
     from one monotonic `g_bboxStamp` (all sites: object parts, terrain,
     particles, sky dome, hulls, anim parts - pose-sharing followers still
     copy the owner's stamp), plus a defensive count check in the engine
     cacher (`stapip_bag_bboxes_cacher.cpp`) for non-regenerated games.
  2. *TextureRepository::free leaked the GS side.* `removeBufferId()` only
     tombstones the allocation entry (id = -1): the VRAM pages and both
     texbuffer_t structs leaked on every free. Engine fix: the repository
     now calls a new `RendererCoreTexture::freeTextureBuffers()`
     (sender.deallocate + unregisterAllocation) from free()/removeById();
     the old tombstone path remains only for a repository initialized
     without its core.
  3. *Mid-frame PATH3 uploads.* Pipelines upload a texture to GS VRAM on
     first use - in the middle of a rendered frame, racing the in-flight
     VU1/GIF work. Boot-time loads got away with it on the first
     near-empty frames; textures streamed in by Load Layer hit it
     repeatedly mid-gameplay and eventually hung the frame (the stress
     repro froze after 3 load/unload cycles with no assert logged).
     Fix: the generated game's acquireTexture() calls
     `renderer.core.texture.useTexture()` right after the repository add -
     updateLayerStreaming/loadScene run outside beginFrame/endFrame, so
     the upload happens with the GIF quiet.
  Verified: stress scene (EverySeconds 6 -> load interior/unload exterior,
  Delay 3 -> swap back; models + two textures churned every 3 s) in PCSX2:
  before the fixes it froze after 3 cycles; after, 63 cycles over ~10
  minutes at the exact 6 s cadence, log clean (0 asserts, 0 warns), process
  alive until killed. Editor clean build; example + sample regenerate and
  build (engine relinked). Visual spot-check of this run was blocked by
  window occlusion (the desktop was in use; PrintWindow cannot see the GS
  surface, only CopyFromScreen can) - the earlier phased screenshots plus
  the assert-free 10-minute run carry the verification; a pad walk on the
  example remains the standing hands-on check.

- (62) **Known regression on main: the debug FPS/MEM overlay freezes the
  first frame** - found while re-verifying layer streaming after merging
  the fog/flashlight/LOD batch (#31/#34/#35...). NOT caused by the layers
  branch: a pure origin/main editor build with a fresh no-layers FPP
  scratch project (fog off) freezes the same way the moment
  `showFps`/`showMemory` are on - an EverySeconds(2)->Log ticker printed
  0 lines in 40 s with the overlay, 17 without. TYRA_LOG probes place the
  hang after renderScene() completes on frame 0, in the 2D block - most
  likely drawDebugHud()'s first renderer2D.render() (lazy PATH3 upload of
  debugfont.png and/or the 3D->2D drain) against the merged VU1/qbuffer
  changes; renderer/2d and path3 sources are untouched by those PRs.
  Workaround in this branch: examples/layer-streaming ships with the
  overlay off (its README says how to re-enable); a separate task tracks
  the real fix. Layer streaming re-verified post-merge with the overlay
  off: 24 stress cycles over ~140 s at the exact 6 s cadence, 0 asserts.
- (63) **Wall see-through fix (near clip vs collision clearance)** - pressing
  the camera against scene geometry let you look inside/through it: the
  generated games' near clip plane sat 0.5 units in front of the camera
  (clipMargin = -(near+0.5)) while collision keeps the eye only 0.35
  (playerRadius) from a wall face, so any face closer than 0.5 was clipped
  away - thin walls vanished entirely, boxes showed their inside. The clip
  distance is now 0.15: past the real near plane (0.1), and safely under the
  worst-case in-frustum depth of a face at the 0.35 collision distance
  (~0.25 at the default 60-deg FOV). The clearance now holds vertically too:
  collidePlayer gained a `ceiling` out-param (lowest box underside overhead;
  in mesh mode an upward ray, so door lintels/floors count) and both walkers
  clamp jumps so the eye stays EYE_CLEARANCE (0.2, > clip 0.15) below
  overhead geometry; boxes with less than that eye room refuse walking
  under (with an escape hatch when the player is already beneath them).
  Side effect: jumping head-first through mesh floors from below no longer
  works (it used to land you on top). Verified A/B in PCSX2 (SW renderer,
  50 FPS both ways): FPP scratch scene, 0.1-thick wall 0.39 units from the
  spawned eye - the old 0.5 build renders sky/terrain straight through the
  wall, the 0.15 build a solid wall. Ceiling clamp compiles (PS2 toolchain)
  and the codegen was inspected in both walkers; jump feel needs a pad test.

- (64) **Open menu blocks gameplay input** - pressing X while a menu was open
  also reached the player (the character jumped, use targets fired) because
  the loop gated player movement, the use target and the flashlight toggle on
  `menuActive` = "a *pausing* menu is open". Overlay (non-pausing) menus, and
  the single frame X closes a pausing menu, left `menuActive` false, so the
  same click that drove the menu also drove gameplay. Both walkers (orbit/
  terrain loop and the FPP loop) now compute a separate `menuOwnsPad` -
  `saveMenuActive || gameMenuWasOpen || gameMenuIndex >= 0` (a game menu open
  at any point this frame, plus the save menu which already owns its pad
  through its close frame) - and gate `updatePlayerEntity`/`updatePlayer`/
  `updateCameraOrbit`, `updateUseTarget` and `flashlightTogglePressed` on it,
  also clearing `usedObject`/`useTargetIndex` while a menu owns the pad so a
  stale USE prompt/trigger can't leak through an overlay menu. `menuActive`
  still gates world-sim pausing (physics, particles, animation, scripts) so
  overlay menus keep the world running as before. Codegen inspected in both
  loop templates; needs a PCSX2 pad test (open an overlay menu, press X - the
  entry selects and the player no longer jumps). All five example projects
  (custom-nodes, layer-streaming, script-demo, showcase, video-modes) were
  regenerated in the same commit and rebuilt clean in Docker ("Build OK"), so
  the generated game (this fix included) compiles on the PS2 toolchain; the
  regen also pulled in accumulated codegen drift these samples had missed
  (VU1-clipping toggle, data-driven USE prompt, menu value strips).

- (65) **Keyboard & mouse controls (USB)** - generated games are playable
  with a USB keyboard/mouse: WASD walks, the mouse looks, E uses, Space
  jumps, Esc pauses, arrows + Enter drive menus. Engine fork grew a
  `KbdMouse` device (`pad/kbd_mouse.*`: libkbd raw-mode 256-bit key bitmap +
  libmouse DIFF-mode deltas/buttons, polled in `realLoop`), `ps2kbd`/
  `ps2mouse` IRX embeds, `IrxLoader` split (usbd / mass storage / HID) and
  `Pad::injectVirtual` - a virtual-pad overlay that ORs held buttons into
  the freshly polled pad, derives click edges, and offsets the sticks. The
  generated `controls.hpp` (user-ownable) holds the whole mapping - HID key
  codes -> pad buttons, WASD -> full left-stick deflection (deadzone/curve
  apply as usual), mouse buttons -> pad buttons, `MOUSE_SENSITIVITY` - plus
  `applyKeyboardMouseInput`, called first thing in both loop() templates, so
  menus / save menu / flow *On Button* / scripts react with zero knowledge
  of the keyboard. Mouse look bypasses the sticks: both walkers add the
  per-frame deltas to yaw/pitch directly (no g_frameScale - deltas are
  already per-frame; no deadzone eating slow swipes). Guarded by
  `TYRAX_KBD_MOUSE` so an older user-owned controls.hpp keeps compiling.
  New `ProjectSettings::keyboardMouse` (default on, in `==`, saved/loaded,
  Preferences > Build checkbox) -> `options.loadUsbKbdMouse` in main.cpp;
  under ps2link the engine skips the drivers (a second usbd on an IOP that
  may already run one - ps2link booted from a USB stick - wedges the USB
  stack, and PS2MouseInit would spin forever without its IRX). The Runner
  now also configures PCSX2's emulated USB ports before launch
  (`pcsx2::ensureUsbKbdMouse`, same force-policy as HostFs: USB1=hidkbd
  bound to the host keyboard, USB2=hidmouse bound to Pointer-0). Verified
  e2e in PCSX2 on an FPP scratch project: both drivers enumerate
  (`KbdMouse: ... ready` in bin/log.txt), synthetic-focused W-hold walked
  the player z 0->31 with the virtual stick visible in a debug log
  (`ljv=0`), Space fired the example script's Cross interaction twice
  (click edges work), and mouse capture behaves like a real FPS (PCSX2
  hides + recenters the cursor; signs verified: cursor above center =
  look up, right = turn right; controlled wiggle gave symmetric yaw with
  no drift). Docker build clean incl. the engine rebuild (-lkbd -lmouse).
  Caveat: mouse BUTTONS never registered under synthetic input
  (SendInput/PostMessage with verified focus; motion worked throughout) -
  they may require real hardware events in PCSX2, so LMB/RMB/MMB need a
  hands-on click test; keyboard covers every mapped action meanwhile.
  Editor GUI checkbox is compile-verified only (stock ImGui pattern).

## Backlog (rough order)

- **Session internet exposure** — today sessions are LAN (or any mesh VPN:
  Tailscale/ZeroTier make remote peers look local, zero code). The researched
  built-in options, in preference order: a Cloudflare quick tunnel
  (`cloudflared`, free, no account, random URL per session = invite link;
  needs a WebSocket `wire::Transport` impl - client side via native WinHTTP,
  no OpenSSL), playit.gg (free TCP tunnels, account required), UPnP
  (miniupnpc, best-effort, dies on CGNAT). The `wire::Transport` interface is
  the only integration point - protocol/session code never sees sockets.

- Hands-on pass over the Flow Graph editor UX (needs a human with a mouse)
- Object physics vs objects (stacking), player physics polish (pad feel)
- Model picking uses the unit-box approximation (big models pick imprecisely -
  the parser now exposes the real AABB, the viewport pick could use it)
- HUD images draggable directly in the viewport
- Positional audio (volume falloff by distance to an object)
- Compressed music streaming (SPU2-native ADPCM/VAG, ~3.5:1 vs 16-bit PCM) -
  needs a custom double-buffered SPU RAM streamer in the engine; audsrv only
  streams PCM and plays ADPCM one-shots
- Flow graph: more nodes (timers with reset, variables)
- Animations: measure VU0 skinning (entry 34) on real hardware once ps2link
  is installed - PCSX2 prices COP2 ops like FPU ops so it can only show
  parity; the SIMD win (one FMAC = 4 lanes) is a hardware-only effect.
- Animations stage 3 (optional) - VU1 skinning microprogram: matrix palette
  in VU memory, weights in the vertex stream, new VCL program (respect the
  vcl_sml.i history first). Measure EE headroom before starting - skinning
  now runs on VU0 in macro mode (entry 34) at ~37% EE load for 3 characters
  in PCSX2, so the EE is not the bottleneck yet. Palette per batch limited
  by VU memory (~24-32 bones).
- Engine perf, next target: the packager's per-frame package arrays are
  pooled now (entry 79 - measured worth only ~2%) and the per-package bbox
  classification is now the cheap object-space AABB test (entry 100 - 47->73
  FPS precise / 120 FPS vu1-clipping on the 98k PCSX2 benchmark, so the
  companion work below is DONE in PCSX2 terms); the real endgame is the
  engine author's own TODO in
  stapip_clipper.hpp - move clipping to VU1 entirely ("too much time").
  **Measured on real PS2 (2026-07-11**; clipbench: 128x128 terrain at detail
  128 = ~98k verts, spinning FPP camera, COP0 timers around `clipper.clip` +
  loop/pre-endFrame markers, 4 runs x ~2400 frames): the EE clipper costs
  **8.6-9.8 ms per frame (max 11.6 ms)** in "precise" mode - ~28% of the
  frame - at ~1.3 us per crossing-package vertex; ~52% of surviving packages
  crossed the frustum. The frame is **100% EE-bound**: with vsync off, EE
  work is ~33 ms and endFrame is 0.5 ms (GS/VU1 idle). But the same scene in
  "fast" mode still misses 50 FPS (EE ~26.5 ms, and it submits 841 vs 356
  packages because only the precise path classifies per package), so moving
  clipping to VU1 *alone* gets this scene to ~24.4 ms EE - still 25 FPS
  vsync-locked (~41 FPS with vsync off, up from ~30). Real payoffs: ~9-11 ms
  of EE headroom, scenes hovering just over the 20 ms budget flip 25->50,
  the sky dome / animated objects / particles (hardcoded fullClipChecks=true)
  come along for free, and "fast" mode's vanishing-triangle artifacts stop
  being the price of speed. Worth doing **together with packager pooling**
  to actually reach <20 ms on heavy scenes. Full design + milestones:
  `docs/vu1-clipping-plan.md`; PCSX2 undercounts the clip cost ~15-20%, so
  measure on hardware. Reusable instrumented scene:
  %TEMP%\tyra-editor-test\clipbench (terrain_game.cpp owns a perfTick() +
  auto-spin patch, codegen marker removed).

- (65) **AI: flow-graph generation, agent CLI, and per-project AI support** -
  three pieces. (a) *Generate with AI* in the Flow Graph window (src/aigen.cpp
  + App::drawAiGenerateModal): the system prompt is built per request from the
  live flowNodeTypes() registry (custom .flownode nodes included) plus the
  project's referencable names, so it never drifts from the code; the backend
  (claude CLI / copilot CLI / OpenAI-via-curl, picked with model + Thinking in
  Edit > Preferences > AI assistant, persisted in editor.ini) runs on a worker
  thread with the prompt passed via temp file + stdin (never the command line
  - newlines/32k limit), stderr split to a file so it can't corrupt the reply,
  and the child tree in a kill-on-close Job Object so Cancel actually stops a
  token-burning node process; the reply parser tolerates fences/prose, rejects
  unknown node types, drops pin-rule-violating links (same switch the editor
  prunes with) and auto-lays-out unpositioned nodes; the graph lands as one
  commitChange (undo-able), with an append mode that id/position-shifts.
  (b) Agent CLI in main.cpp: --dump / --list-nodes (= the system prompt) /
  --dump-graph / --apply-graph / --refresh-gen / --ai-graph /
  --add-ai-support, so an assistant inside a generated project can inspect,
  edit and regenerate without the GUI (docs/ai-tools.md). (c) "Add AI
  support" (New Project checkbox + Project > Preferences + CLI): installs
  Claude Code skills (tyra-project/-flowgraph/-scripting/-building) +
  CLAUDE.md and/or .github/copilot-instructions.md into the project; content
  lives in ai-support/ (markdown, single source of truth), embedded into the
  exe by cmake/embed_ai_support.cmake, {TYRAX_EXE} replaced with the real exe
  path at install, refresh gated by the delete-the-marker-to-own rule (the
  marker sits below SKILL.md frontmatter, so the check scans the head, not
  line 1). Caught during verification: the first system-prompt draft claimed
  actions chain exec->exec - false, ordinary actions have no exec output
  (only triggers + execThrough Delay/Raycast), which the link validator
  correctly enforced against the prompt's own advice; prompt + skills fixed.
  Verified: mock-reply --apply-graph e2e (fence stripping, unknown-type
  rejection, invalid-link drop + auto-layout, save, codegen shows the nodes
  in flow_graph.gen.cpp via --refresh-gen); full --ai-graph pipeline against
  a stub claude.cmd on PATH (stdin prompt -> reply -> parse -> append-merge
  -> save); real claude CLI reached the API (model-404 and usage-limit
  errors surfaced verbatim in CLI and modal - the account's limit blocked a
  successful real run today, plumbing itself proven); GUI pass via the
  screenshot harness (Flow Graph shows the button + applied graph, modal
  renders, spinner animates, Cancel present, error shown in red);
  --add-ai-support installs 6 files, second run after deleting a marker
  keeps the user-owned file.

- (66) **AI graph generation is edit-aware (no mode switch)** - "Generate
  with AI" (and --ai-graph) now sends the object's CURRENT graph along in
  the prompt whenever it has one, serialized in the same schema the model
  must reply in, with instructions to judge from the request whether to
  change, extend or rebuild - and to always answer with the COMPLETE
  resulting graph (unchanged nodes keep ids/positions/params; omissions
  delete). So "change the timer to 5 seconds" edits in place and "also do X
  on Circle" extends, with no Edit/Add/Replace UI - an earlier draft had a
  3-way radio, dropped per feedback for the model deciding itself. The
  reply always just replaces the stored graph; appendGraph() remains only
  for --apply-graph --append. Verified with the stub-claude harness (see
  65/ai-backend-testing): a demo project whose graph had a 3s timer +
  Triangle-hide branch, request "change the timer from 3 to 5 seconds and
  remove the Triangle hide logic" - the dumped prompt contains the CURRENT
  GRAPH section with both, and the stub's edited reply (5s, no Triangle
  nodes) landed as the saved graph with untouched ids preserved. Editor
  builds clean; modal shows a hint that the AI sees the current graph.

- (67) **Get Position gained exec pins (sample-and-latch)** - user-found gap:
  Get Position was a pure node, so there was no way to trigger a read - you
  could not capture "where was the object when X happened" and keep it after
  the target moved on (a pos link always read live at the consumer's exec).
  It is now execThrough (like Raycast) while REMAINING a live source when
  its exec pins are unwired, so every existing graph compiles identically:
  codegen keys off "has an incoming exec link" (getPosLatched in
  templates.cpp) - unwired nodes never run and posExpr resolves them live
  as before; wired ones get a posOut<id>[3] member (reset on scene reload),
  an action branch latching the target's position at exec time, a posExpr
  branch handing consumers the latched member, and emitExec chains their
  "after" exec inline (the registry's execThrough sites all extended, per
  the tyra-editor-dev note). Object output stays compile-time - only the
  position latches. Verified via --apply-graph + --refresh-gen on a graph
  with both forms: OnButton -> GetPosition -> (exec+pos) -> SetVarPos emits
  the latch then flowPos[0][i] = posOut2[i], while an unwired GetPosition
  feeding SetPosition still emits the live ctx.objects[i].data.position
  read; full Docker PS2 build of the project compiles clean (Build OK).

- (68) **Node descriptions live on the node (registry .desc + tooltips)** -
  node docs used to exist only as a side table inside aigen.cpp, invisible
  in the editor and easy to forget for new nodes. FlowNodeType gained a
  `desc` field and the whole flowNodeTypes() registry was rewritten with
  C++20 designated initializers (defaults on every field, entries state only
  what a node HAS - kills the positional-bool footgun) carrying the
  descriptions verbatim; aigen's nodeDoc() table is deleted and the AI
  catalog reads t.desc. The same text now shows in the editor: hovering an
  entry in the right-click add-menu, and resting the mouse ~0.6 s on a node
  in the canvas (delayed + suppressed while any button is down, so wiring
  never flickers). Custom .flownode nodes get a `desc =` header key
  (flownode.cpp parse + starter template, VS Code extension SPEC + grammar
  updated per the sync rule) - their descs flow into tooltips AND the AI
  catalog, so a project's own nodes document themselves for the assistant
  too. Verified: --list-nodes catalog before vs after the registry rewrite
  is byte-IDENTICAL (the transfer introduced no pin/param/text drift); a
  scratch shake.flownode with desc shows the text in its catalog line; GUI
  screenshot shows the hover tooltip on a node (On Button + its desc).
- (69) **Preference descriptions moved to hover tooltips** - the Project and
  Scene Preferences dialogs had grown to several screens tall because nearly
  every control carried its multi-paragraph explanation inline as a
  `TextDisabled` block under it. Added a tiny `prefHelp(tip)` helper (SameLine
  + a dimmed `(?)` + `SetTooltip` - the exact idiom the Layers list and node
  tooltips already use) and folded every one of those long descriptions into a
  `(?)` marker sitting on the same line as its control. Section notes with no
  control of their own (Ambience, Loading screens, AI support) attach the `(?)`
  to their button instead; the dynamic "Resident terrain mesh" readout and the
  short one-line footer stay inline. The vestigial "Post effects" section (just
  a "bloom/grain moved to the UI Editor" redirect, no control) was dropped
  entirely. Gotcha caught in review: the old
  `TextDisabled` blocks are printf format strings (literal `%` written `%%`),
  but `prefHelp` passes the text through `SetTooltip("%s", tip)`, so the two
  affected strings (display-mode "14%", mesh-LOD "~50%/~25%") had their `%%`
  collapsed to `%` or they would have shown a stray percent. Net effect: the
  Project Preferences modal now fits without scrolling and the same wording is
  one hover away. Verified: `build.ps1` links clean; the tooltip idiom is
  byte-identical to the existing working markers (fontCombo, layers, scenes).
- (70) **matbake: UV-space raytraced map baker (Material Editor core)** - the
  foundation of the Material Editor expansion: a new host-only module
  (src/matbake.cpp/.hpp, the decalproj pattern - no GL) that rasterizes a
  mesh's paintable triangles in UV space (conservative: corner-grazed texels
  get a nearest-interior-point sample, so island borders never gap),
  interpolates 3D position/normal per texel through the barycentrics, and
  fires cosine-weighted hemisphere rays through a flat binned-SAH BVH. One
  pass produces the whole map set: AO (linear distance falloff, epsilon
  origin offset - no acne), bent normals, thickness (same spiral mirrored
  below the surface), curvature (discrete mean curvature from edge normal
  deltas, p90-normalized - no rays), position and object-space normal maps.
  High-poly support: with a second mesh the texel points are cage-projected
  along the smoothed low-poly normals onto the dense mesh first, and rays
  occlude against it. Deviations from the backlog, on purpose: golden-angle
  spiral + per-texel seeded hash rotation instead of Hammersley (the proven
  aobake recipe; any prefix is well distributed, which makes progressive
  rounds honest), and all bonus maps ride the same rays instead of separate
  bakes. Progressive matbake::Baker (worker thread, growing rounds
  8/16/32..., full snapshot after each; gbuffer+BVH cached across start()
  calls keyed by mesh signature + raster params, so sampling-only slider
  drags restart nearly free). Deterministic by construction: fixed spiral,
  seeded hash, threads own fixed texel ranges - same inputs = bit-identical
  maps at any core count. All maps flood-dilated N texels (ring averages
  filled neighbors, UV-wrapping) against bilinear/mip seam bleed. Docs:
  docs/material-baking.md. Verified with a scratch harness linking the
  build .obj files (memory recipe): sphere-over-plane contact shadow
  gradient (center 113 / penumbra 223 / open 255, no acne - open-plane
  min=max=255), two fresh bakes bit-identical, high-poly projection changes
  the normal map, 256^2 x 64 rays against a 100,352-tri occluder in 773 ms
  including BVH build (~11M rays/s).
- (71) **Material Editor: Bake maps UI (progressive preview + auto layer)** -
  the matbake front end. The property column gained a "Bake maps" block:
  Preview combo ("AO on material" multiplies the baked occlusion over the
  textured preview mesh; "Map view" swaps the material for the raw
  AO/curvature/thickness/bent/OS-normal/position map via a "@matbake-view"
  pseudo-texture), a High-poly slot (cage projection, auto/manual Cage
  offset), and the parameters (Resolution 64-512, Rays, Max distance = THE
  artistic knob, Anti-alias supersampling, Backface hits, Padding, Seed).
  Parameter changes restart the worker-thread bake immediately - the Baker's
  gbuffer+BVH cache makes sampling-only drags feel instant, and the first
  progressive round lands on the mesh in milliseconds. "Bake & add AO layer"
  = the magic auto-hookup: full-quality bake drops onto the entry's texture
  as a "Baked AO" MULTIPLY layer (re-bakes overwrite it in place instead of
  stacking; layer-undo covers it); "Save all maps" writes the six-map set as
  PNGs next to the .mtl for smart-mask material work. Key safety decision:
  the AO-on-material preview multiplies into the GL upload ONLY
  (matEdUploadComposite) - matEdPaintPixels_/the PNG on disk never contain
  the preview, so saving a paint stroke mid-preview ships clean. Bake
  params persist per .mtl as a "# tyra-bake" hint line (written only when
  non-default; %20-escaped high-poly path), so with the fixed seed a
  re-open reproduces the bake bit for bit. Mesh inputs are cached keyed by
  path+mtime (external re-exports re-bake automatically); closing the
  window or switching files cancels the worker. Docs:
  docs/material-baking.md (Using it), docs/material-painting.md pointer.
  Verified: build.ps1 links clean; the matbake core underneath has the
  harness coverage of (70). GUI visual verification is BLOCKED by the known
  machine state (PROGRESS 2026-07-21 note: editor GL window presents
  white/black on this AMD driver; reproduced with the pre-change baseline
  binary at D:\tyra-editor\build - not a regression of this change). A
  human pass over the new panel is pending: scratch project recipe in
  the entry-(70) harness notes (steps.obj demo model generator in the
  session scratchpad).
- (72) **Material Editor: UV layout panel + display modes + hover sync** -
  the M0 "see your UVs" block. The preview toolbar gained a display-mode
  combo (Solid / Wireframe overlay / UV checker) and a "UV" toggle. The
  wireframe overlay is a second glPolygonMode(GL_LINE) pass with the fill
  pushed back by glPolygonOffset(1,1) (no z-stitching); the UV checker is a
  generated 256^2 8-cell texture (two grays, texel grid, red-toward-u /
  green-toward-v hue wash) that replaces every texture on the preview mesh
  - painting pauses in checker/map-view modes since strokes would be
  invisible. The UV toggle splits the preview 58/42: 3D on top, a 2D UV
  layout panel below - the entry's paintable triangles drawn with ImDrawList
  over the LIVE texture (viewport_.sharedTexture = the same GL cache the
  painter uploads into, so paint strokes appear in the panel in real time),
  wheel-zoom around the cursor, drag pan, 0..1 border. Hover sync both
  ways: 3D hover -> materialPreviewPick UV -> every triangle whose UV
  region contains it fills amber in the panel (UV overlaps thereby expose
  themselves) + a dot marks the exact texel; panel hover -> the triangle is
  outlined blue on the mesh via the new Viewport::materialPreviewProject
  (exact inverse of the pick raycast: model-space point -> preview image
  coords through the same stored camera basis). The panel reuses the bake's
  cached MeshInput (matBakeMeshLow_), so UV data costs nothing extra; a
  matEdUvIssueTris_ highlight list is already wired into both views for the
  upcoming UV validator (red outlines). Docs: material-painting.md.
  Verified: build.ps1 links clean; visual pass pending the same known
  white-window machine state as (71).
- (73) **Material Editor: UV validator** - matbake::validateUv (host-only,
  harness-testable) inspects the preview mesh's paintable UVs: overlapping
  islands via a texel-center ownership raster (wrapping modulo 1 like the
  GS samples, >= 2 shared texels to ignore exactly-on-edge centers - shared
  island edges never false-positive because a texel center lies strictly
  inside one triangle), UVs outside 0-1 (eps 1e-3), flipped triangles
  (minority UV winding - the majority orientation is the mesh's convention,
  so a fully mirrored map doesn't drown the list), degenerate UV area over
  real surface, and texel-density outliers (>4x / <0.25x of the
  area-weighted mesh average). Findings cap at 400. UI: "UV check" section
  under Bake maps - a Validate button, a per-kind summary line and a
  clickable list; selecting a finding highlights the triangle(s) red in the
  UV panel AND on the 3D mesh (the matEdUvIssueTris_ hook from (72)),
  auto-opening the UV view. Results pin to the mesh key they ran against
  and clear when the shape/model/entry changes. Verified in the headless
  harness: a crafted 5-triangle mesh yields exactly the expected
  overlap/out-of-range/flipped/low-density findings (tri indices checked),
  a clean two-triangle quad reports zero; the whole matbake suite still
  passes (contact shadow, determinism, high-poly, 100k-tri perf).
- (74) **Material Editor: PS2 CLUT preview + memory budget** - the "how will
  it actually look on the console" mode today's editor lacked. New
  pngquant::quantizePreviewRGBA: an in-memory twin of the shipped
  quantizeRGBA path (same weighted median cut, same nearest-with-2x-alpha
  metric) that returns the palettized image expanded back to RGBA plus the
  palette, with three dither flavors - Floyd-Steinberg (identical loop to
  the shipped bake), 4x4 ordered Bayer (amplitude scaled to palette
  coarseness: 40 at 16 colors, 18 at 256) and none. The Material Editor
  display combo gained "PS2 CLUT": the composite is quantized at GL-upload
  time in matEdUploadComposite (stacked AFTER the AO-on-material multiply,
  so the preview quantizes what would really ship; disk PNG untouched,
  painting keeps working and strokes appear pre-quantized), palette size
  follows the resolved policy (per-asset textureQuality override of the
  .mtl, else ProjectSettings::textureQuant) or an explicit 16/256/full
  override, a swatch strip shows the surviving palette, and a live budget
  line prices the texture ("128x128 4-bit = 8.0 KB + 64 B palette") - the
  same line now also replaces the bare dims readout under Texture, with the
  GS +8 KB allocation-overhead caveat in its tooltip. Known approximation:
  texbake lets the highest quality claimed by ANY asset sharing a PNG win;
  the editor resolves only the open .mtl's claim (noted in the code).
  Verified headlessly (scratch harness linking pngquant.cpp + stb impls):
  a 256-wide RGB gradient quantized to 16 colors holds the budget in all
  three dither modes (16/16/16 unique colors, 16-entry palette), FS and
  ordered outputs differ from undithered, and a 4-color image passes
  through bit-identical with its 4-entry palette (lossless path).
- (75) **Material Editor: smart masks + material presets (M4)** - procedural
  wear/dirt driven by the baked map set. matbake::generateMask (host-only,
  harness-tested): sources Edge wear / Cavity grime (curvature), Occlusion
  dirt (1-AO), Thin rims (thickness), Height Y / Facing up
  (position/normals), Perlin 3D / Worley 3D - both sample noise AT THE BAKED
  SURFACE POSITION (AABB-normalized), so patterns flow across UV island
  seams instead of restarting at them (the triplanar effect of M4.3 for
  free) - and UV-space running-bond Bricks with a mortar width. Signal ->
  smoothstep Range window -> optional Invert -> optional Breakup (multiply
  by world-space Perlin). All hashed-corner noise, no tables, fully
  deterministic. UI: "+ Mask" adds a generated layer (its pixels = fill
  color through the mask alpha; marked "*" in the list), generator controls
  appear under the layer list for the active mask layer, masks REGENERATE
  LIVE as the progressive bake refines (matBakeTick hook) and after a paint
  target loads; a matBakeRunOnce_ flag lets masks request maps without
  turning the bake preview on. Params persist per layer in the layers.json
  sidecar ("gen" object). Presets (M4.4): "Presets" popup saves the
  gen-layers' PARAMETERS as material-presets/<name>.matpreset in the
  project root (outside res/, the flow-nodes/ dir pattern - never ships);
  applying regenerates the same wear recipe from the target material's own
  bake. Hand-painting on a mask layer is overwritten by regeneration
  (tooltip warns; paint on a normal layer above instead - a deliberate
  simplification over per-stroke mask compositing). Verified in the
  headless harness: occlusion-dirt mask strong under the sphere / zero in
  the open (250 vs 0), flat plane grows no edge wear (max 0), Perlin
  deterministic + seed-sensitive + well spread (0..255, mean 122), bricks
  mortar fraction sane (0.18). Docs: material-baking.md "Smart masks".
- (76) **Material Editor: preview-mesh stats line (M0.1)** - under the
  shape/display row: "<N> tris (<M> on this entry) - <V> verts", amber with
  a "no UVs (paint/bake need them)" or "no faces use this entry (check
  usemtl names)" warning when applicable. Computed from the cached bake
  MeshInput, recomputed only when the mesh key changes. Verified:
  build.ps1 links clean (visual pass rides the same pending human check).
- (77) **Texture hot reload over Live Link (M5.3)** - the biggest-ROI item of
  the pipeline backlog: repaint a texture in the Material Editor and the
  RUNNING game re-uploads it within a fraction of a second - no rebuild, no
  reboot. Editor side (App::liveTexNotify, hooked into every
  matEdSavePaintTarget): re-bakes the composite into bin/<path> in exactly
  the format the build shipped (palette layout read from the existing PNG's
  IHDR - color type 3 + bit depth -> 16/256 colors through the same pngquant
  the bake uses), written tmp+rename, then bumps bin/livetex.bin ("TXLT" v1:
  seq + cumulative path->generation records + footer echo, the livelink.bin
  idiom; capped 64 paths, cleared by the Runner at build start alongside
  livelink.bin). Game side: a generated sibling poller
  (src/scripts/live_tex.gen.cpp, same debug+liveLink gate, registered like
  LiveLink, whitelisted in refreshGenerated) re-reads the file every 6/25
  frames, matches repository textures by the fork's NEW Texture::sourcePath
  (set in TextureRepository::add - `name` keeps only the basename, ambiguous
  across dirs), re-decodes through PngLoader (CLUT rotation matches the
  original load), memcpy's the pixel + CLUT data into the existing
  TextureData buffers and re-sends them to the SAME GS VRAM address via
  RendererCoreTexture::updateTextureInfo - the bump allocator is never
  touched. Dimension/format drift is rejected with a TYRA_SOFT_ERROR
  ("rebuild to apply"); torn files fail the size/footer check or come back
  as the 8x8 placeholder and fail the dimension check. Engine mods (marked
  Modified by TyraX): Texture::sourcePath + its assignment in
  TextureRepository::add - two lines, everything else rides existing fork
  API. Gotcha found live: the engine ALWAYS constructs the clut TextureData
  (null data for 32-bit textures), so clut presence must be tested via
  t->clut->data, not the object pointer - the first e2e attempt tripped the
  guard on a 32-bit texture and proved the soft-error path for real.
  Verified e2e in PCSX2 (scratch project, steps.obj model): full-color
  32bpp swap tan -> red/white checker ON SCREEN in the running game
  (before/after F8 screenshots), then the palettized path - project
  rebuilt at 4bit, shipped PNG colorType 3/depth 4, replacement quantized
  through the editor's own pngquant - swapped to a blue/yellow checker
  with correct CLUT colors; bin/log.txt clean in both runs. The editor-side
  liveTexNotify path is code-identical to the harness scripts used in the
  e2e (same format detection, same file writes) but was not driven through
  the GUI (the known white-window machine state); a human paint-stroke
  pass remains. Docs: docs/live-link.md "Texture hot reload", README.
- (78) **Texture atlasing with GS page control (M5.4)** - the last big item
  of the pipeline backlog. Preferences > Build > "Texture atlasing" (default
  off, ProjectSettings::textureAtlas): small clamp-safe map_Kd textures pack
  into shared 256x256 pages at build - one GS VRAM allocation (+~8 KB
  overhead) per page instead of per texture, fewer texture switches. New
  host module src/texatlas.cpp computes the DETERMINISTIC plan (the aobake
  single-source pattern): eligibility scan (models' textured submesh UVs
  checked against the real mesh via objparser, <=128 baked size via the
  texbake dim rule, same-directory map_Kd tokens only - pages group by the
  .mtl's directory so the rewritten reference never needs ".." over PS2
  host fs; terrain/emitters/decals/mirrors/portals/refl-maps/
  textureQuality-pinned assets excluded with reasons), then dir-grouped
  shelf packing with 2-texel gutters. texbake consumes the plan: composites
  members into .res-baked/<dir>/tyra-atlas-N.png (edge-dilated gutters,
  page quantized AS ONE IMAGE - shared 256-color CLUT when the project is
  palettized, the era trade), skips the members' individual bakes, rewrites
  baked .mtls (map_Kd -> page + "# tyra-uvrect u0 v0 du dv" hint;
  stale-rewrite purge when the plan stops covering a file; sweep exemption
  for the sourceless pages). Engine (Modified by TyraX): LeanObjLoader
  parses the hint - model vertex UVs multiply through the rect at load,
  LeanMtlMaterial::uvRect exposes it. Codegen: GameMaterial::uvRect (both
  template copies!) <- loadMaterialAsset, staged as g_primUvRect and
  multiplied in pushVert's staged-material path only (model parts pass
  kdArg and are already remapped - no double-apply); TEXTURE_ATLAS_INFO
  constant in model_data.gen.hpp logged at scene boot; live_tex hot reload
  naturally no-ops for atlased members (missing individual bin PNG).
  Verified: headless harness (temp project fixture): membership exactly as
  designed (2 primitive textures + 1 model texture in, oversized/emitter/
  out-of-bounds-UV textures out), same-dir grouping, non-overlapping
  gutter-respecting placements, bit-deterministic plan, off => empty. E2E
  in PCSX2: scene with a patterned model + striped box + ringed sphere
  built with atlas OFF then ON - screenshots visually identical (all three
  patterns correct, no seam bleed), bake log + game boot log both report
  "Texture atlas: 3 textures in 2 page(s)", .res-baked member PNGs gone,
  baked .mtl carries the page + rect. 4bit project policy => shared
  256-color pages exercised. Docs: docs/texture-atlasing.md, README,
  both skills.
- (79) **examples/material-lab: the material pipeline showcase** - a small
  diorama exercising the whole epic in one project: a generated stone altar
  .obj (three stacked boxes, 6x3 UV atlas) whose committed texture is a
  REAL layer stack - base stone mottle, "Baked AO" multiply layer (matbake,
  96 rays/texel, params persisted as "# tyra-bake" in the .mtl), "Cavity
  grime" (Occlusion source, multiply) and "Edge wear" (Edges source,
  noise-broken) smart-mask layers with full generator params in the
  .png.layers sidecar, so opening Tools > Material Editor lands on a live,
  regenerable stack; brick pillars + tiled orbs whose 64^2 textures join
  the texture atlas with the altar's 128^2 ("Texture atlas: 3 textures in
  2 page(s)" at boot, 4bit project = shared per-page CLUT); and a
  material-presets/worn-stone.matpreset applying the wear recipe to any
  other material. Assets are generated deterministically by a scratchpad
  tool linking the editor's own matbake/pngquant objects - the committed
  composite is exactly what the editor's own compositing math produces.
  res/.gitignore replaced with the showcase-style one (the --new scaffold
  trap from the memory notes - verified with git ls-files). Verified:
  Docker build exit 0 with the atlas line in the bake log, PCSX2 boot
  ("is executing", clean bin/log.txt with the atlas boot line) and an F8
  screenshot showing the altar's baked contact darkening/grime/wear, the
  brick pillars and orbs - all sampling shared atlas pages. Editor-side
  panel walkthrough is described in the example README (the visual GUI
  pass rides the same pending human check as the rest of the epic).
- (80) **Material Editor: draggable panel splitter + preview-rotation UX**
  (user request) - the property/preview split was a fixed 48%/260px-floor
  formula and the preview often came out cramped; it is now a real
  splitter: an InvisibleButton strip between the columns with a drawn
  separator line (hover/active tinted, ResizeEW cursor), dragging trades
  property width for preview width within 25..75% (both sides keep a
  scaled floor), and the ratio persists per machine as editor.ini
  matEdSplit through the standard EditorConfig chain (field + load/save +
  saveGlobalConfig aggregate + startup seeding - the skill recipe), saved
  on drag release. Rotation with Paint off: the reported "can't rotate the
  model" was Spin fighting the hand - the turntable kept adding yaw DURING
  a drag, so drags never stuck. The turntable now yields: any orbit drag
  records matEdLastOrbitT_ and the auto-spin pauses while dragging and for
  1.5 s after. Also: RMB-drag now orbits ALWAYS (previously only while
  painting - one muscle memory for both modes), and the pitch floor
  loosened from -5 to -30 degrees (low-angle shots; clamp changed in BOTH
  twins - the app input clamp and renderMaterialPreview's). Verified:
  build.ps1 clean; the splitter/orbit math is input-driven UI logic riding
  the same pending human visual pass as the rest of the epic (known
  white-window machine state).
- (81) **examples/material-lab: live-loop out of the box** (user request) -
  the showcase now demonstrates the whole epic without any setup: build
  profile switched to DEBUG (Live Link + texture hot reload compiled in),
  every window layout requests the Material Editor ("open": ["material"]
  in the manifest), and a new 6x3.2 "paint-canvas" wall stands behind the
  altar with a 256x256 plaster+target texture - 256 is deliberately over
  the atlas's 128 eligibility cap, so the canvas stays an individual
  hot-reloadable file while the altar/pillars/orbs keep demonstrating the
  atlas ("Texture atlas: 3 textures in 2 page(s)" unchanged). The asset
  generator gained the canvas (deterministic - regen left every existing
  asset byte-identical, only canvas.* appeared). README rewritten around
  the F5-paint-watch loop and the intentional atlased-vs-hot-reloadable
  split. Verified e2e in PCSX2: booted the rebuilt example, F8 before
  shot, then simulated a paint save exactly the way liveTexNotify writes
  it (16-color quantized PNG matching the shipped IHDR + livetex.bin
  bump) - the wall repainted to rainbow stripes IN THE RUNNING GAME
  within the poll interval; clean log with the atlas boot line intact.
- (82) **Material Editor: layers always visible, Paint only arms the brush**
  (user request) - the whole layer stack UI (list, blend/opacity/visibility,
  "+ Mask" smart masks, Presets, generator controls) previously lived
  inside the Paint gate, so inspecting or tuning a stack forced paint mode
  on. Split: the paint target now loads whenever the selected entry has a
  texture (same matEdPaintTexRel_ guard the bake/CLUT previews already
  used), the layers section renders whenever a target is loaded, and Paint
  gates only the brush controls + stroke/ghost input. Side benefit:
  opening a material with smart-mask layers refreshes the masks (and
  requests bake maps) immediately, without touching Paint - material-lab's
  altar stack shows up the moment the file opens. Docs: material-painting
  "Layers" section + the example README. Verified: build.ps1 clean; brace
  restructure only - stroke/ghost gating unchanged (canPaint semantics
  preserved), rides the same pending human visual pass.
- (83) **Material Editor: an orbit drag unchecks Spin** (user request,
  refining (80)) - the 1.5 s turntable pause turned out to be the wrong
  model: the user wants the hand to WIN permanently. Any orbit drag now
  sets matEdSpin_ = false (the checkbox visibly unchecks - the state is
  discoverable, not a hidden timer), framing stays put, and re-ticking
  Spin resumes the turntable. The matEdLastOrbitT_ pause timer from (80)
  is removed; the checkbox gained a tooltip stating the behavior. Docs
  updated. Verified: build.ps1 clean (one-line interaction change).
- (85) **UV unwrap for animated models (user request)** - .glb/.fbx sources
  can't be rewritten (FBX has no writer at all), so the unwrap rides a
  SIDECAR instead: uvunwrap refactored into a shared smart-project core
  with two fronts (unwrapObjFile as before + unwrapTriangles for a flat
  position-welded triangle soup), and the editor writes "<model>.uvs"
  ("TXUV" v1: per part material name[32] + corner count + u,v floats; each
  part unwraps into its OWN 0..1 square since parts carry their own
  textures). The sidecar is folded in at the animimport chokepoints -
  bake() for every editor preview/matbake consumer AND parseSkel() for the
  .tskl writer, whose LODs are generated afterwards and inherit the
  mapping (generateSkelLods rides UVs along the collapse). Parts match by
  material name + vertex count, so a re-exported model with changed
  geometry ignores the stale entry instead of corrupting; deleting the
  sidecar restores the original mapping. texbake treats .uvs as
  editor-only (the shipped .tskl already carries the applied UVs). The
  "Unwrap UVs..." modal now enables for animated preview models with
  sidecar-specific wording; the editor deletes any existing sidecar before
  baking the unwrap source so re-unwraps run on the ORIGINAL geometry.
  Verified: the obj harness still passes post-refactor (identical
  assertions), and a new animated harness on a real .glb (wobbler, 540
  verts -> 7 charts): Baked path carries the replacement, Skel path (the
  shipped-.tskl source) carries it too, every part validator-clean, and
  sidecar deletion restores the original UVs bit-for-bit. The tskl
  writer/PS2 loader consume SkelPart::uvs verbatim (verified against the
  code in the design pass), so the harness's Skel-path check covers what
  ships; a visual PCSX2 pass on a textured animated model stays on the
  human-check list with the rest of the GUI passes.
- (86) **Unwrap: chart fold-over fix + multi-part UV visibility** (user
  report: spider2.glb "unwrap only covered the abdomen"). Two findings.
  REAL BUG: a planar chart spanning too much curvature can FOLD over
  itself - two faces of spider.tee landed on the same 816 texels (the
  validator harness caught it once pointed at the real model). Fix in
  unwrapCore: chart growing is now a reusable subset pass, every grown
  chart runs a 64x64 ownership-raster fold check in its own projection,
  and folded charts RE-GROW at half the angle threshold (recursively; at
  <=6 degrees coincident/duplicated geometry isolates into per-face charts,
  which cannot overlap - guaranteed termination). spider2: 91 -> 92
  charts, validator-clean on every part; the cube harness unchanged.
  UX CONFUSION (the actual "tylko dupe objal"): the model has THREE parts
  (spider 4 tris / spider.legs 350 / spider.tee 10) and the UV panel only
  drew the SELECTED ENTRY's islands - the unwrap covered everything, but
  with entry "spider" selected the panel showed 4 triangles. The panel now
  draws the OTHER entries' islands dimmed gray for context (whole-model
  layout visible, selected entry highlighted; switch entries in the combo
  to edit each part). Also: the animated bake-mesh cache key now includes
  the .uvs sidecar mtime, so external sidecar changes (delete/re-unwrap
  outside the modal) refresh without a restart. Verified: both unwrap
  harnesses green including the user's actual spider2.glb (Baked + Skel
  paths carry the fix, per-part validator-clean, delete-restores).
- (87) **Multi-entry workflow: pick-to-select + one-click textures** (user
  request: "mud only on the clothes, quickly") - the per-entry model was
  all there but navigating it was blind. Three additions: (1)
  materialPreviewPick gained an outMaterial param (the sweep knows the hit
  part) and clicking a part in the 3D preview JUMPS TO ITS ENTRY - hover
  names the part ("spider.legs - click to edit this entry"; parts without
  a matching entry say so), a clean click is distinguished from an orbit
  drag by MouseDragMaxDistanceSqr, and painting keeps LMB for the brush;
  (2) when the selected entry has no texture the Layers box shows a
  "Create texture for this entry" button (matEdEnsurePaintTexture: a
  256^2 white "<entry>-tex.png" next to the .mtl, unique-named, Props
  undo, assigned + saved + loaded as the paint target) - masks, presets
  and painting bootstrap in one click; (3) the Entry combo marks
  untextured entries with "(no texture)". Combined with (86)'s dimmed
  whole-model UV panel, the clothes-mud flow is: click the shirt in the
  preview -> Create texture -> Presets -> worn-stone; click the pants ->
  repeat. Verified: build.ps1 clean; input-logic + file-creation paths
  ride the standing human GUI pass (known white-window machine state).
- (88) **Fix: stale paint target leaked bake results across entries** (user
  report: "baked AO on the jaw, switched to the legs entry, baked again -
  the AO showed up on the jaw"). Root cause: the paint target
  (matEdPaintTexRel_ + pixels + layers) only ever switched when the NEW
  entry had a texture; selecting an untextured entry (fresh multi-part
  models after material extraction) left the PREVIOUS entry's texture
  loaded, and both the "AO on material" preview multiply and
  matBakeApplyLayer blindly used the loaded target - the new entry's AO
  (rasterized on ITS UV islands) landed on the old entry's texture,
  visually smearing the previous part. Three locks, defense in depth:
  (1) matBakeTick unloads the paint target whenever the selected entry has
  no texture (new matEdUnloadPaintTarget - pixels, layers, stroke/ghost
  state); (2) matBakeApplyLayer verifies the loaded target actually
  belongs to the selected entry before writing anything ("apply skipped -
  the loaded texture belongs to another entry"); (3) a pending "Bake & add
  AO layer" is armed for the entry it was clicked on
  (matBakeApplyEntry_) and switching entries cancels it with a status
  message instead of cross-applying whenever the bake finishes. Verified:
  build.ps1 clean; the failure needed the GUI to reproduce (entry combo +
  bake button sequencing), so the fix rides the standing human pass - the
  three locks are each independently sufficient for the reported path.
- (89) **Fix: clicking an animated model's material in the asset list
  previewed on the sphere** (user report) - openMaterialEditor's no-hint
  auto-pick only ever tried a same-stem sibling .obj under res/models, so
  a material extracted from an animated model (which lives at
  res/materials/<model>.mtl per the "+ New material from this model" flow)
  fell through to the default sphere, while an .obj's own library matched.
  The auto-pick is now a heuristic chain: (1) a scene object of type Model
  assigned this material as its override - the ground truth, catches any
  naming; (2) a same-stem sibling model in the .mtl's OWN directory, all
  three extensions (.obj/.glb/.fbx - a model's own library, now covering
  animated siblings too); (3) the extraction naming convention
  res/materials/<stem>.mtl -> res/models/<stem>.{obj,glb,fbx}. Hand-named
  universal materials with no consumer still land on the sphere, as
  before. Verified: build.ps1 clean; the pick chain is pure path logic
  riding the standing human GUI pass.
- (90) **UI nit: the lone "+ Add" button now says "+ Add entry"** (user
  report) - with a single-entry .mtl the Entry combo hides and the add
  button stood alone with no context. Label only; the tooltip already
  explained the semantics.

- (91) **Keyboard/mouse on real hardware: ps2link debug override + USB
  settle delay** (user report: kbd/mouse works in PCSX2 but does nothing on
  a physical PS2 over F6 "Run on PS2"). Diagnosis first: not a bug - the
  engine computes `withKbdMouse = loadUsbKbdMouse && !keepIopResident`, and a
  ps2link deploy sets `keepIopResident`, so the drivers never load (pad still
  does, hence "pad works, kbd/mouse dead"). The guard exists because a second
  `usbd` on ps2link's IOP wedges the resident one. Two things added: (a) an
  experimental `ProjectSettings::keyboardMousePs2Link` preference (*Build >
  Keyboard & mouse > Force under ps2link*) → `EngineOptions::
  loadUsbKbdMouseUnderPs2Link` → the engine keeps the drivers under ps2link
  but the IrxLoader **reuses ps2link's resident usbd instead of loading its
  own** (only loads `ps2kbd`/`ps2mouse` on top), so the driver-load logs reach
  the EE console live via ps2client - a real debug loop on hardware; (b) a
  fixed `delay(5)` settle after loading `ps2kbd`/`ps2mouse` in
  `loadKbdMouseModules`, because USB HID enumeration is async on real hardware
  (instant in PCSX2) and `PS2KbdInit`/`PS2MouseInit` were running before any
  device attached. `KbdMouse::init` now also logs the failure cases (driver
  NOT ready) so the console shows *why*. Full chain wired (project.hpp +
  `operator==`, save/load, Preferences UI, `{{KBD_MOUSE_PS2LINK}}` codegen).
  Verified: editor builds clean; scratch project round-trips the flag through
  `--resave` and emits `options.loadUsbKbdMouseUnderPs2Link = true/false` in
  the generated `main.cpp`; all 17 examples regenerated. Engine change
  compiles only in Docker and the actual hardware behavior (does the override
  bind, does the delay fix enumeration) is a **pending hands-on test on the
  user's PS2** - the code is the debugging instrument, the console logs are
  the readout. See docs/keyboard-mouse.md ("Debugging on real hardware").

- (92) **UI nit: two inline help walls moved into (?) tooltips** (user
  report) - the *Keyboard & mouse controls* and *Reflection probe: aim along
  the reflected ray* checkboxes still printed their whole explanation inline
  as `TextDisabled` while every neighbour used the `prefHelp` "(?)" hover.
  Swapped both to `prefHelp`; text unchanged. Compiles clean (app.cpp
  recompiled; the linker only skipped overwriting a running editor exe).

- (93) **ps2link kbd/mouse override: load our OWN usbd (real-hardware fix)**
  (user tested (91) on a physical PS2 over F6). The console logged `Unknown
  device 'usbkbd'` / `open fd = -19` / `KbdMouse: keyboard driver NOT ready`
  then **froze**. Diagnosis from the device list (`tty:(TTY via SMAP UDP)` +
  `dev9x:`, no USB): this ps2link is **network-booted**, so there is **no
  usbd resident** on the IOP. (91)'s "reuse ps2link's resident usbd" therefore
  had nothing to reuse - `ps2kbd`/`ps2mouse` self-unloaded, and the following
  `PS2MouseInit()` span forever binding the now-gone RPC server (the exact
  hang the original guard warned about). Fix: under the override the IrxLoader
  now **loads its own usbd** (reverted the `&& !keepIopResident` gate on
  `loadUsbd` to the original `withUsb || withKbdMouse`), which is safe on a
  network ps2link (nothing to conflict with) and keeps `ps2mouse` resident so
  `PS2MouseInit` binds instead of spinning - removing the freeze as a side
  effect. Caveat now documented everywhere (engine.hpp/.cpp, project.hpp,
  Preferences tooltip, generated main.cpp, docs): on a **USB-booted** ps2link a
  second usbd may wedge the resident one - boot the game from that USB instead.
  Editor recompiles clean; **engine change reaches the game through the Docker
  resync on the next build (no editor relink needed)**; real-hardware retest
  still pending on the user's PS2.

- (94) **ps2link kbd/mouse: keyboard-only (the mouse init hangs the boot)**
  (user retested (93)). Now `open name usbkbd:dev ... open fd = 3` +
  `KbdMouse: keyboard driver ready` - **the keyboard works** (own usbd loaded,
  ps2kbd opened its iomanX device). But the game then froze on the Tyra logo,
  with no mouse log line after "keyboard driver ready": it hung in the very
  next call, `PS2MouseInit()`. The keyboard rides an iomanX device (`usbkbd:`)
  that ps2link's resident IOP serves fine; the mouse rides SIFRPC, and under a
  resident-IOP ps2link the `ps2mouse` RPC server never registers, so
  `PS2MouseInit`'s `while(server==0)` spin never ends (banner.show already drew
  the logo, so it sits frozen on screen). Fix: `KbdMouse::init(bool withMouse)`
  - engine.cpp passes `!keepIopResident`, so under ps2link the mouse is skipped
  (keyboard only) and boot proceeds; off ps2link (PCSX2 / exported ISO) the
  mouse runs unchanged. Logs `mouse skipped (keyboard only under ps2link)`.
  Mouse-look over the ps2link debug path is therefore unavailable by design -
  full keyboard+mouse on hardware is the exported-ISO path. Docs / tooltip /
  project.hpp updated. Engine-side, reaches the game via the Docker resync;
  hardware retest pending.

- (95) **ps2link kbd/mouse: reuse-resident mode (for a custom ps2link)** -
  part A of getting full keyboard+mouse over the network dev loop. The mouse
  can't init when we load ps2mouse post-hoc onto a running ps2link (its RPC
  server never registers). The plan: boot a CUSTOM ps2link with
  usbd+ps2kbd+ps2mouse baked in, so those register at ps2link's own clean
  boot; the game then reuses the resident stack. New nested preference
  `keyboardMousePs2LinkResident` (Build > Keyboard & mouse > Force under
  ps2link > "ps2link already has USB drivers (reuse + mouse)") ->
  `EngineOptions::ps2LinkHasUsbHid`. When set (with the ps2link override, under
  ps2link) the engine loads NONE of its own USB modules (a second usbd would
  wedge the resident one) and enables the mouse - `PS2MouseInit` binds the
  already-registered server instead of spinning. Stock ps2link (flag off) keeps
  the (94) load-our-own keyboard-only path. Full chain wired (project.hpp +
  `operator==`, save/load, nested Preferences checkbox, `{{KBD_MOUSE_PS2LINK_
  RESIDENT}}` codegen, engine.hpp/.cpp). Editor compiles clean. Part B (the
  custom ps2link.elf build recipe) and the hardware test are separate/pending.

- (96) **Custom ps2link with USB HID baked in** (part B - the console side of
  (95)). New `tools/ps2link-usbhid/`: a three-file patch to ps2dev/ps2link
  (`ee/Makefile` embeds usbd+ps2kbd+ps2mouse IRX, `ee/irx_variables.h` externs
  them, `ee/ps2link.c` `loadModules()` execs them after ps2link_irx - **usbd
  first**, since ps2kbd/ps2mouse import its symbols) plus a `build.ps1` that
  clones a pinned ps2link, applies the patch and builds `ps2link.elf` in the
  official `ps2dev/ps2dev` toolchain image, and a README. A research subagent
  (web, cited) mapped ps2link's `loadModules` and corrected the root cause:
  ps2mouse's RPC registration is NOT gated on clean-vs-busy IOP timing (my
  earlier hypothesis) - it hard-depends on **usbd being resident** at load, and
  loading usbd ourselves onto ps2link's running IOP doesn't bring it up
  cleanly; baking it into ps2link's own reset-then-load boot does. Built with
  the current ps2dev toolchain, NOT the older `h4570/tyra` game image (ps2link
  master needs newer ps2sdk headers - `startup.h`, `PS2_DISABLE_AUTOSTART_
  PTHREAD`; ps2link is standalone so the toolchain need not match the game's).
  Verified: `build.ps1 -Clean` reproduces end-to-end (patch applies clean, elf
  = 283188 bytes, `strings` confirms "PS2 USB keyboard driver" / ps2mouse
  embedded). The elf and its `build/` tree are gitignored (binary sent to the
  user directly). Real-hardware test - flash it, tick both ps2link checkboxes,
  F6, expect `keyboard driver ready` AND `mouse driver ready` - pending.

- (97) **Mouse read: zero-init the buffer + force DIFF mode (real-hardware
  fix)**. With the custom ps2link (96) both drivers finally came up on hardware
  (`keyboard driver ready` AND `mouse driver ready`, game booted) - but the
  camera orbited the avatar on its own, mouse unusable. Cause: `KbdMouse::update`
  read into an **uninitialised** `PS2MouseData data;`. A real USB mouse only
  sends packets on activity, so on a still frame `PS2MouseRead` returns success
  without writing the struct - we then fed stack garbage (a near-constant value)
  into `mouse.dx`, and the walkers add dx straight to yaw => perpetual spin.
  PCSX2 never showed it: its emulated mouse delivers a packet every frame, so
  the struct was always freshly written. Fix: `PS2MouseData data = {};` (a
  no-data frame now yields 0 deltas). Also set `PS2MouseSetReadMode(PS2MOUSE_
  READMODE_DIFF)` explicitly at init instead of trusting the driver default -
  ABS mode would return absolute position read as a huge constant delta (same
  spin). Pure engine change (kbd_mouse.cpp), reaches the game via the Docker
  resync - no editor rebuild. Fixes mouse-look on ALL real-hardware paths (ISO
  too), not just ps2link. Hardware retest pending.

- (98) **Keyboard/mouse: one ps2link option + TyraX-branded ps2link; hardware
  verification parked**. The hardware hunt ended inconclusively: with the custom
  ps2link both drivers report ready and the game boots, but no input arrives -
  and the user's keyboard/mouse turn out not to be recognised by the console at
  ALL (uLaunchELF doesn't see them either), i.e. they don't speak the USB HID
  **boot protocol** `ps2kbd`/`ps2mouse` require. Nothing left to fix on our
  side without hardware that works, so the path is documented as **verified in
  PCSX2, unconfirmed on hardware** (docs + tools README carry a Status note;
  the uLaunchELF cross-check is written down as the way to tell a device
  problem from a TyraX problem). Cleanups the user asked for:
  (a) **two nested checkboxes collapsed into one** - *Also over ps2link - needs
  the TyraX ps2link* (`keyboardMousePs2Link`). It now always means the custom
  ps2link, so `keyboardMousePs2LinkResident` / `EngineOptions::ps2LinkHasUsbHid`
  and the whole "stock ps2link, load our own, keyboard-only" branch are gone;
  the engine under ps2link simply reuses the resident stack. Load tolerates the
  retired key (verified by round-tripping a .tyra that still has it).
  (b) A **safety guard replaces the removed branch**: `KbdMouse::init` only runs
  `PS2MouseInit` if the keyboard device opened (proof the stack is resident), so
  ticking the box on a stock ps2link logs "mouse skipped" instead of freezing
  the boot on the Tyra logo.
  (c) **ps2link boot screen branded** "Welcome to TyraX ps2link (USB keyboard +
  mouse)" so the custom build is identifiable on the console; patch regenerated
  and the elf rebuilt (`strings` confirms branding + HID drivers).
  Also fixed a silent `build.ps1 -Clean` failure (read-only git objects made the
  removal fail, then it built a stale tree while printing "reusing existing
  clone"). Editor builds and links clean; all 17 examples regenerated.

- (99) **Fix: empty scenes didn't compile (placeholder row lost `physSleep`)** -
  found by the pre-push Docker build required before touching a PR. A project
  whose scene has NO objects emits a hardcoded one-row placeholder in
  `scene_data.hpp` (C++ forbids a zero-sized array), and that row had drifted
  one field behind `SceneObjectData`: `physSleep` (added with the per-object
  sleep delay) was never inserted, so every later column shifted one field left
  and the PS2 build died far from the cause with `narrowing conversion of
  '0.0f' from 'float' to 'int'` (emitSize landing in `int emitCount`).
  **Pre-existing on origin/main**, not from this branch - `git show
  origin/main:src/templates.cpp` carries the identical broken row; it arrived
  with the physics-sleep commit and nothing built an empty scene since.
  Inserted the missing `3.0F` and extended the row's comment to name the exact
  failure mode, since the mismatch is silent by construction. Verified: struct
  fields vs row values counted programmatically (51 vs 51) and a full Docker
  game build of an empty-scene project returns exit 0 (`Build OK`,
  `bin/kmtest.elf` linked) - which also compiled every engine change on this
  branch (kbd_mouse/engine/irx_loader) for the first time, since vendor/tyra
  only compiles inside the container. Examples unaffected (none has an empty
  scene). Note: the editor exe in `build/` was locked by a running editor, so
  this was built and verified from a throwaway `build-verify/` tree.
- (100) **Tree Generator** (*Tools > Tree Generator*, user request: "generate
  trees into the assets and place them from the editor - but not 100k-vertex
  monsters") - procedural low-poly trees, EZ-Tree-inspired (MIT),
  reimplemented host-side as `src/treegen.cpp` (the stochtile/matbake/
  decalproj pattern: no GL, no Project dependency, pure functions over a
  `Params` struct). Recursive branch skeleton -> tapered tubes with
  parallel-transported frames, leaves as camera-agnostic quads on the outer
  levels, plus two procedurally baked 128² textures (tileable bark: rough
  ridges / birch lenticels / cracked plates; leaf card: broadleaf cluster /
  needle sprig / single blade). `writeAssets()` emits `.obj` + `.mtl` + the
  two PNGs into `res/models/trees/` and the tree enters the scene through
  the EXISTING `addModelObject()` - so the whole model -> serialization ->
  codegen -> runtime chain is untouched: no new object type, no new
  manifest field, no codegen, no engine change. A generated tree is
  indistinguishable from an imported .obj. Leaf transparency needed nothing
  new either - the static pipeline already alpha-tests material textures -
  but the leaf PNG bakes **hard 0/255 alpha** on purpose (the tRNS->CLUT
  path loses a soft gradient) with opaque colors dilated into the
  transparent margin so bilinear sampling never rings a dark fringe.
  Determinism was a design constraint, not a nicety: each branch derives
  its RNG stream from (parent seed, child index) via a splitmix mixer, so
  dragging one slider ADJUSTS the tree instead of reshuffling it - without
  that, every tweak of "Trunk sides" regenerates a different tree and the
  tool feels random rather than dialable. Presets keep the current seed (a
  preset is a shape, not a dice roll); Roll re-seeds. Tessellation is
  explicit - radial sides and length rings interpolate from the trunk
  values down to the `*Min` values on the outermost level, so detail lands
  where it reads - and the window shows a live triangle count (green under
  1800, amber past it, red past 3000, advisory only). The tool is a
  parameter panel + a live turntable preview rendered from the IN-MEMORY
  mesh/textures (nothing touches disk or the shared asset caches until you
  add, so slider drags stay instant) in its **own framebuffer**, not the
  Material Editor's - sharing `prevFbo_` was the first cut and is wrong:
  both tools can be open at once and size their previews independently, so
  one target would thrash its size and each window would show the other's
  image. Doc: docs/tree-generator.md.
  Verified: build.ps1 clean; a headless harness (the
  headless-model-harness recipe - link `treegen.cpp.obj` +
  `objparser.cpp.obj` against a tiny main) checked all six presets for
  non-degenerate geometry and budget (**Oak 596, Birch 437, Spruce 943,
  Poplar 646, Dead tree 549, Bush 620 triangles** - the "no kobyły"
  requirement, comfortably met), determinism (same params -> identical
  sizes/bounds; different seed -> different geometry), the leaf texture's
  hard cutout (some fully transparent AND some fully opaque texels), and a
  full **round-trip through objparser**: the written .obj re-loads with two
  submeshes named bark/leaf, both textured, and a triangle count matching
  the generator exactly (596 == 596), with the vertex dedup confirmed (739
  positions vs 1788 raw corners). A second harness ran the non-GL half of
  "Add to scene" (add a Model object -> ensureObjectIds -> save ->
  refreshGenerated) against a scratch project: all clean. In the GUI the
  window renders correctly at uiScale 1.5 (screenshot: full slider panel, a
  real tree with trunk/branches/green leaves in the preview, live triangle
  readout) and Add to scene writes the assets and adds the object.
  **Found while verifying - a GL DRIVER crash, not this feature's code, but
  reachable through it.** With the **Material Editor open**, adding a
  generated tree model to the scene kills the editor (~50-100% of the time)
  the moment the preview shows that model for the first time. Diagnosed
  under gdb on a RelWithDebInfo build, so this is exact and not a guess:
  the faulting call is **`glTexImage2D` at viewport.cpp:1810** inside
  `Viewport::glTexture("res/models/trees/tree-12-bark.png")`, called from
  **`Viewport::renderMaterialPreview`** (NOT the scene viewport), three
  frames deep inside `atio6axx.dll`. Every argument is valid: 128x128,
  comp=4, non-null pixels, power-of-two, RGBA8. A valid RGBA8 upload
  segfaulting inside the driver is a driver fault; this machine has
  documented AMD GL quirks (the white-window note in the screenshot
  harness). Control: with the Material Editor **closed** the same add is
  stable 4/4.
  Two hypotheses were tested and **killed**, recorded so nobody re-runs
  them: (1) "creating textures inside a render pass" - a `warmAssets()`
  pre-pass that acquired every asset *before* any framebuffer was bound
  still crashed, in the pre-pass itself; position within the frame is
  irrelevant, and that change was reverted rather than shipped with a
  wrong explanation attached. (2) "any textured .obj does it" - false: a
  hand-written 12-triangle cube never reproduced it (0/10), not even
  carrying the tree's own PNGs, not even with two materials and two
  textures; a fresh project holding only `res/models/trees/` crashes 2/4.
  So the trigger needs the generated tree model itself (596 tris, 2 parts)
  plus the Material Editor. An earlier stash A/B "proving this predates the
  feature" was flawed - it varied the binary while holding the poisoned
  assets constant; what it does still show is that the faulting code is not
  treegen's (the base binary, `treegen` not even compiled, crashed 4/4 on a
  project full of generated trees). Filed as its own task and deliberately
  NOT papered over with a "close the window on add" workaround. **Fixed in
  (101)** - the entry below has the answer.
  One real fix did come out of the hunt: the AO occluder pass called the
  full `modelDraw()` (uploading meshes AND textures to GL) purely to read a
  model's AABB - it now uses a new GL-free `Viewport::modelBounds()`
  (objparser + its own cache), so reading bounds mid-frame never triggers a
  GL upload. That is a straightforward win regardless of the crash.
- (101) **Fix: the AMD GL driver crash on texture upload** (user report: "I
  open the tree editor and it crashes instantly; a reboot didn't help" - and
  it had worked during my own testing, which made it look like a regression
  from the main merge; it wasn't). Windows' own Application Error log settled
  it in one query: `Faulting module atio6axx.dll 31.0.21921.11005`,
  `0xc0000005`, **fault offset 0x2152beb - byte-identical across every crash**,
  the user's and mine, over several different builds. So: one driver bug, not
  a regression and not several bugs; the user simply hit it earlier, because
  merely opening the Tree Generator uploads its two preview textures, while
  my repro needed a model added with a preview window open.
  The fix is the *form* of the upload, not its arguments (which gdb showed
  were always valid - 128², RGBA8, power-of-two, non-null pixels): a single
  `glTexImage2D` carrying the pixel pointer faults, so every RGBA upload now
  goes through **`glUploadTexRgba()`** in `gl_loader.h/.cpp` - allocate the
  level empty, then fill it with `glTexSubImage2D` (`TexSubImage2D` added to
  the loader's X-macro list). Applied at **all ten** upload sites, not just
  the tree ones (viewport: disk textures, live paint, animated-model embedded
  textures, the UV checker, the tree preview; app: HUD image cache, the
  built-in USE sprite, HUD text, the text preview, the menu preview), plus the
  R32F heightmap upload for the same reason - a driver bug does not care which
  feature triggers it, and leaving nine sites armed would just relocate the
  crash. Framebuffer attachments already allocate empty, so they were fine.
  Verified on the three paths that used to fail: opening the Tree Generator
  **0/4** (was crashing on every attempt for the user), Material Editor + add
  a tree model **0/4** (was 4/4 on a clean base), Tree Generator "Add to
  scene" **0/4** (was 3/4) - 12 clean runs where the previous binary managed
  at most one. build.ps1 clean.
  Also corrected in this pass: the `modelBounds()` comment still justified
  itself with the earlier in-a-render-pass theory, which the gdb evidence had
  already killed (the crash happened in a pre-pass too). The change stands on
  its own merit - reading an AABB should not upload a model as a side effect -
  and now says so instead.

- (102) **Build: one dependency list, so a missing vendor/ clone can never
  reach cmake again** (user report: a fresh worktree died with `Cannot find
  source file: vendor/ufbx/ufbx.c` + `No SOURCES given to target`). The
  dependency set lived in TWO places: `setup.ps1` cloned seven directories,
  while `build.ps1` guarded only the original four (imgui/glfw/imguizmo/
  imnodes) - so stb, ufbx and tyra were never checked. Every dependency added
  after that guard was written (ufbx came with the FBX importer, #119) was
  invisible to it, and any worktree created before the addition walked
  straight into a cmake error that reads like a corrupt checkout rather than
  "run setup". PROGRESS (1545) records the same trap firing once already.
  Now `deps.ps1` holds the single list (`$VendorDeps` + `$StbHeaders` +
  `$Ps2Tools`), dot-sourced by both scripts: setup fetches from it, build
  probes every entry marked `Build` and runs setup itself when one is
  missing, then re-probes and fails loudly with the offending path if the
  fetch didn't help. Probes are **files the build actually compiles**
  (`vendor/ufbx/ufbx.c`, `vendor/imgui/imgui.cpp`, ...), not directories, so
  an interrupted clone counts as missing instead of passing the guard.
  Two smaller fixes rode along: `vendor/tyra` is in-tree (its engine sources
  are versioned here), so cloning into it always failed with a `fatal:
  destination path already exists` that looked like a real error - it now
  reports as present; and native tools run through `Invoke-Native`, because
  git and tar write progress to stderr and Windows PowerShell turns that into
  a terminating error under `$ErrorActionPreference='Stop'` **only when the
  caller captured the stream** (build.ps1 piped into a log, CI) - the exit
  code is what actually decides.
  Verified both directions: renaming `vendor/ufbx/ufbx.c` away makes build.ps1
  stop before cmake with the explicit path, and deleting the whole directory
  makes it re-clone and build clean through to `tyrax-editor.exe`.

- (103) **Fix: alpha-cutout foliage - black cards in the editor, z-stamping
  transparent texels on the PS2** (user report on the Tree Generator: "in the
  editor the leaves have a black background instead of alpha, and in the game
  they have alpha but you can't see other leaves through them"). Two
  independent bugs that happened to land on the same asset.
  *Editor:* `Viewport::modelDraw` recorded a part's texture but nothing about
  its transparency, and the scene pass drew every model part with
  `alpha = false` - the shader's cutout discard was reserved for decals. A
  leaf card is 13 395 of 16 384 texels at alpha 0, 12 302 of them pure black
  RGB, so ignoring alpha renders exactly the black rectangle the user saw.
  `glTexture()` now records whether an image carries any non-opaque texel
  (only when the FILE has an alpha channel - an opaque RGBA PNG keeps the
  cheap path), `ModelPart::alpha` reads it, and the scene, the mirror
  reflections and the Material Editor preview all draw cutout parts with the
  discard on. Opaque parts draw first, cutout after, the order the tree
  preview already used.
  *Engine:* the static pipeline's standard alpha test was
  `GS_SET_TEST(..., ATEST_METHOD_NOTEQUAL, 0x00, ATEST_KEEP_FRAMEBUFFER, ...)`
  - and that ps2sdk constant reads backwards: `ATEST_KEEP_FRAMEBUFFER` is
  **2 = ZB_ONLY**, "keep the framebuffer, update z" (`ATEST_KEEP_ZBUFFER` is
  1 = FB_ONLY - the pair names what is PRESERVED, not what is written; the
  header is `ps2sdk/ee/include/draw_tests.h`). So every fully transparent
  texel drew no colour and still stamped the z buffer, and the invisible part
  of a cutout card occluded whatever was drawn behind it later - leaves cut
  along the straight edges of the card in front of them, holes of sky inside
  the canopy. `ATEST_KEEP_ALL` (0) writes neither buffer, which is what a
  cutout means; opaque geometry carries alpha 0x80 and never fails the test,
  so nothing else moves. Fixed in both twins (stapip + dynpip).
  Verified: editor screenshot of the trees project shows terrain through the
  foliage with no black cards; on the PS2 side, built and booted the same
  project in PCSX2 (software renderer, 50 FPS) with the engine line reverted
  and restored - the upstream build shows leaves amputated along invisible
  card boundaries, the fixed one a properly layered canopy. A pixel A/B was
  not possible: the FPP camera lands in a different pose each boot (the known
  per-run camera problem in tyra-testing), so the comparison is on the leaf
  artefact, not on identical frames.

- (104) **Tree Generator: height scales the tree, and conifers grow by their
  own rule** (user, on the first real use of the generator: "when I raise the
  height the tree gets thinner, and there is no way to make a Christmas tree").
  Two separate shortcomings, both about the parameter MODEL rather than the
  mesh code.
  *Proportions:* `height` was a world length while `trunkRadius` and
  `leafSize` were world lengths too, so the Height slider stretched the trunk
  and left the girth behind - a taller tree became a pole, a shorter one a
  stump. Height is now the tree's SIZE: `thickness` and `leafSize` are
  fractions of it (the sliders read `% of h`, tooltips show the resulting
  units), so dragging Height is a uniform scale. Measured with a host harness:
  the Oak's width/height is 0.5969 at heights 5, 10 and 20 - bit-identical
  proportions, which is exactly the property that was missing.
  *Conifers:* the recursion only knew one habit - children spiral up every
  parent and the crown emerges from ratios. A spruce is not that shape with
  different numbers: its trunk keeps an unbroken leader and carries WHORLS
  whose length follows a profile ALONG THE TRUNK (longest low, vanishing at the
  apex - that profile is the cone) with the tilt sweeping from drooping at the
  bottom to raised at the top. `lengthTaper` is a per-generation ratio and
  cannot express either, which is why the old Spruce preset was a bare pole
  with tufts on stalks. Added `Params::crown` (0 spread / 1 conical) +
  `whorls`; conical mode runs `conicalWhorls()` off the trunk, reads
  `children[0]` as the count per whorl, and offsets each whorl by the golden
  angle so boughs never stack into columns.
  Foliage needed two fixes to match: anchors now carry the **branch length they
  own** (`Anchor::span`) and needle cards spread over it instead of over the
  card size - a low-poly bough has two or three rings, so without this its
  needles clumped at those points with bare tube between them - and the
  conifer's leader is sampled at its own fixed rate rather than at the trunk's
  rings, because foliage is shared out per anchor and the apex's two rings lost
  every time against the ~200 anchors down in the whorls (measured: 13 leaf
  triangles above y=8.25 before, 33 after, on a 10-unit tree). Needle cards
  also lie along the twig and spin around it now instead of facing a random
  direction. Spruce preset rebuilt around all of it: 10 whorls of 5, needles
  down the whole bough, **1440 triangles** (was 943 for a shape nobody wanted).
  Verified with a scratch harness (`treegen` has no GL/Project dependency, so
  it links into a 40-line host program): triangle counts and bounding boxes for
  every preset, the proportionality table above, a foliage-per-height-band
  histogram to find the starved apex, and orthographic silhouettes of the
  result from three angles - the shape is a continuous cone from base to spire,
  and the five other presets are unchanged. That harness loop found three
  problems the GUI would have made me squint at; it belongs in the scratchpad,
  not the repo.
  *Follow-up, same session:* the user dragged the finished Height slider and
  found it still gave "two shapes it jumps hard between". Scaling the world
  DIMENSIONS was not enough - the "too small to bother" cutoffs that drop a
  child branch (`clen < 0.02`, `crad < 0.004`) were still absolute, so below
  about height 2 whole whorls fell through them and a 0.5-unit spruce came out
  a pole with a skirt (315 bark triangles against 840 at height 20 - the
  triangle counters in the two screenshots were the tell). They are fractions
  of height now, as is the degenerate-radius guard in the bark `vStep`. Proven
  by the strong form of the property rather than by eye: generated at heights
  0.5 through 20 every preset holds one triangle count and one width/height
  ratio, and the height-5 mesh multiplied by 4 is **bit-identical** to the
  height-20 mesh, vertex for vertex, for all six (exact because 4 is a power of
  two; a non-power-of-two ratio would differ in the last float bits). Lesson
  worth keeping: a size control must not change what it is sizing, and an
  absolute epsilon inside a parametric generator is a shape parameter in
  disguise.
- (105) **Asset Browser: res/ as a browsable, reference-aware asset library**
  (user: the Project panel's asset list was "a mega crude list", the ask was
  folders, moving, deleting and filtering by type). New window
  (*Tools > Asset Browser*, `src/assetbrowser.cpp` - App:: methods in their own
  TU, the save_assets.cpp precedent): folder tree, thumbnail grid or detail
  list, type chips carrying the count in the current scope, name search, a
  recursive scope toggle, and an inspector with each type's own controls (the
  texture-quality combo, the LOD popup and - after merging main's world-scale
  work - the *Size...* dialog moved out of the old flat list into
  `drawAssetQualityCombo`/`drawAssetLodButton`/`drawAssetSizeButton`). The
  Project panel's Assets section is now a summary plus the import buttons.
  Two things carry the feature, and neither is UI. **The reference census**
  (`rebuildAssetUsage`): one flat pass over the model recording everything that
  *uses* an asset - object model/material/sound, terrain material and painted
  layers, HUD/menu/splash/loading images, fonts, custom LOD tiers, audio flow
  nodes - which is what lets the inspector list *who uses this file* (with a
  Select button that jumps to the object, switching scenes), badge the ones
  nothing references, and warn per file before a delete. It is keyed off
  `modelEditSerial_`, so it costs one pass per edit, not per frame. Per-asset
  **settings** are deliberately NOT uses (texture quality, the recorded
  real-world size, music build options, clip edits, membership of the
  disk-scanned audio lists): they are metadata on the file, and counting them
  would mean no imported asset ever reads as unused - which is the one question
  the census exists to answer. They still travel with the file and are cleaned
  up on delete, which is a different list (`retargetAssetPath`).
  **The sibling invariant** is the part that took the thinking: a Wavefront
  reference (`mtllib`, `map_Kd`, `refl`) is a bare file name resolved next to
  the file that named it, and the PS2 loads from a flat ISO9660/host path with
  no `..` - so "move this texture into res/textures" is not a file operation,
  it is a broken material. The move therefore takes a transitively closed
  dependency group along (`assetWavefrontDeps`), **copies** a dependency that
  files left behind still need (both folders keep resolving; the status line
  says how many), and **refuses with the reason** instead of half-applying a
  move that would still break something. A rename inside one folder has no such
  problem, so there the siblings that name the file are rewritten instead
  (`rewriteWavefrontRef` - last token only, so `-s 2 2` / `-mm 0 0.5` survive),
  and the `.mtl` a model exclusively owns is renamed with it (`tree.obj` +
  `tree.mtl` -> `oak.obj` + `oak.mtl`, how every import writes them); a shared
  library keeps its name and the model gets an explicit `mtllib` line, because
  the implicit `<stem>.mtl` sibling rule would otherwise leave it materialless.
  Everything else follows from those: sidecars (`.uvs`, `<tex>.layers/`) travel
  with their asset, the baked `.tmdl` is deleted for the next build to redo, a
  WAV moved between `res/audio` and `res/sfx` changes role and swaps lists, and
  build-written files (menu panels, text sprites, glyph atlases, `.tmdl`) are
  hidden behind a *Generated* toggle and read-only here.
  Also: drag a model onto the **viewport** and it lands where the cursor points
  (the placement raycast, so it rests on what is under it), and `Viewport::assetThumb`
  renders a 128² preview per asset once into a dedicated framebuffer and copies
  it into its own texture (`glCopyTexImage2D`, a few new thumbnails per frame) -
  a material rides a sphere, an image is its own thumbnail, everything else gets
  a colored plate with its extension.
  **A new field that stores an asset path now has two obligations**:
  `retargetAssetPath` (or move/rename silently breaks it) and
  `rebuildAssetUsage` when it is a real reference rather than a per-asset
  setting (or the asset reads as unused). Written into the tyra-editor-dev skill
  next to the other chain rules - and immediately exercised by merging main's
  world-scale work, whose `modelUnitMeters` map is keyed by asset path: it joined
  the retarget list (a moved model keeps its recorded real-world size) and stayed
  out of the census (a size is a setting, not a use). That merge also moved
  main's *Size...* button into the browser's inspector, since the flat list it
  lived on is gone. `retargetAssetPath` additionally repoints the editor's own
  staged paths - the Material Editor's open `.mtl` and paint target, the pending
  size dialog, the Animation Editor's model - because a save through a stale one
  would recreate the file at its old location.
  *Verified* with a throwaway host harness (the pattern from 104, and the reason
  the logic is host-only): standard headers, then `#define private public`, then
  link the harness against `build/`'s object files minus `main.cpp` - no window,
  no GL context, `ImGui::CreateContext()` alone is enough for the one
  `GetTime()` call. On a copy of `examples/material-lab` seeded with a
  `models/props` subfolder, a stray texture and both WAV roles it showed: the
  scan (30 files, 8 folders, kinds right, `.layers` sidecars not listed as
  assets); the census (`pillar.mtl` project=4 from four objects, `canvas.png`
  wavefront=1 from its .mtl, `ground.png` unreferenced); dependency resolution
  in both directions; and then the operations - the texture move refused with
  *"altar.png is referenced by altar.mtl, which would stop finding it"*, the
  model move landing `.obj` + `.mtl` + `.png` in the new folder with `mtllib
  altar.mtl` / `map_Kd altar.png` still bare and the object's `modelPath`
  retargeted, the rename producing `statue.obj` + `statue.mtl` + a rewritten
  mtllib line, a folder rename retargeting everything inside, the paint-layer
  sidecar riding along through both, the WAV leaving `music` and joining
  `sounds`, and a referenced material's delete clearing the objects' paths with
  no dangling reference left. The ImGui half was checked by running a **Debug
  build (IM_ASSERT active)** with the window open for 15 s - no assertion, so
  the Begin/End, child, table, popup, ID and drag-drop pairs are balanced - and
  the window's presence proven from the layout dump the editor saved
  (`[Window][Asset Browser] Size=2880,1800` = the 960x600 default at this
  machine's 3x DPI scale, so `scaled()` is applied). **The look is unverified**:
  this machine is still in the white-window state from 101/PROGRESS notes (the
  AMD GL present quirk reproduces on baseline builds), so screenshots capture
  nothing - the visual pass needs a human.
