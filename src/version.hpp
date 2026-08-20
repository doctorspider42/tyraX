#pragma once

// Editor and project-format versioning. Two independent numbers:
//
// - The editor version (semver, for humans): every feature bumps MINOR, every
//   fix bumps PATCH, a breaking change bumps MAJOR. Shown in the title bar and
//   written into the .tyra manifest as "editorVersion" - purely informational
//   ("which editor wrote this file"), never used for decisions.
//
// - kFormatVersion (a monotonic int, for machines): the on-disk project format
//   contract. Bump it on EVERY change to what project::save() writes - new
//   fields included - so an older editor can refuse a newer file instead of
//   silently dropping the fields it does not know and destroying them on its
//   next save. When old files additionally need active transformation (a
//   rename, a semantic/unit change, moved data), register a migration step in
//   migrations.cpp for the same bump; purely additive bumps need no step and
//   open silently. See docs/format-versioning.md.

// 1.78.0 (lamps ARE the body - docs/vehicles.md): lamp-material geometry
// splits out of the palette merge into its own parts (lamp-rear/lamp-front,
// fixed order so the recorded indices survive rebakes), baked FULLBRIGHT
// (ke = kd), and the runtime writes those parts' vertex colors PER INSTANCE
// every frame - dark red, lit red, brake flare; warm white headlamps. Mesh
// lamps stick to every shape by construction; the heuristic glow quads
// remain the fallback for models with no lamp materials (regression-booted
// on the CC96). This is the one thing the engine asks of a model: name
// your lamp materials. kFormatVersion 47. MINOR.
//
// 1.77.0 (roads, second pass): the road OBJECT no longer collides (its box
// was an invisible wall), align-terrain is FLAT across the width (the
// flatten brush's cosine crowned it - shoulders keep the falloff),
// per-point LIFT above the terrain (Catmull-Rom along the spline, ramps
// climb smoothly; ROAD_LIFT table + twin arithmetic in buildRoads), every
// road previews as a FILLED strip (selected gets edges/markers), a
// viewport EDIT mode (click ground = append, click point = drag, click
// line = insert; Esc stops; one undo step per operation), and the engine
// sound moved from Driver into the Sounds tab where the rest of the pack
// lives. kFormatVersion 46 (roadHeights, additive). MINOR.
//
// 1.76.0 (lamps off the materials - docs/vehicles.md): "zalatwic swiatla
// materialem, bo kazdy pojazd ma inny ksztalt". The import pools the
// canonical AABBs of lamp-named materials (lamp/light/brake/tail/stop/head,
// vertex-end split when the name does not say which end) into rear and
// front clusters on the definition; the tail-lamp glow draws AT the
// measured spots and the headlight beam starts from the measured front.
// Pure measurement, re-seeded on every re-import; size 0 = the shape-blind
// heuristic stays the fallback (verified on-console: the CC96 has no lamp
// materials and its lamps look exactly as before, on the new road no
// less). kFormatVersion 45, additive. MINOR.
//
// 1.75.0 (roads - docs/roads.md): a Road object is a polyline, a width and
// ONE texture; a Catmull-Rom spline threads the points and the surface is
// tessellated AT BOOT into procChunks (owner -3, ~24 stations each) - the
// proc pipeline's AABB culling and bag economy for free, and a kilometre of
// road costs a few hundred .tyra floats plus one texture in VRAM. The
// tessellator is a twin (src/roadgen.* on the host, its raw-string copy in
// buildRoads - CHANGE ONE CHANGE BOTH); the editor previews the exact strip
// and edits points in the panel; "Align terrain to road" flattens the
// heightfield to the smoothed grade along the line, one undo step. Three
// integration lessons paid for on-console: the object type must be
// authoring-only in rebuildObjectGeometry (it rendered as a white box), the
// setup call must run AFTER the proc build (which clears procChunks - five
// chunks built and wiped ten lines later), and ROAD_TEXTURE_PATHS strip the
// res/ prefix (the game's asset root is bin/ - "Texture missing" named it).
// kFormatVersion 44. MINOR.
//
// 1.74.2 (the lamps light up - docs/backlog.md entry closed): two stacked
// causes, neither of them the suspected blend path. (1) The lights
// bookkeeping sat inside the smoke's SLIP-GATED block, so a car that never
// slipped never initialised its lights - the numeric probe showed the
// drifting rival lit while the parked player stayed dark, which named it.
// Moved to once-per-vehicle-per-frame, before anything mutates inBrake.
// (2) The lamp quads were sized entirely under the rear trim's black band -
// a giant-quad probe proved vertical quads render fine, so they are sized
// past the trim now, with the brake flare growing them again. Verified on
// camera: dim slivers + beam pool with the lights on, unmistakable red
// flares while braking. The glow bag stays on standard z from the bisect -
// TestOnly was exonerated but never needed. PATCH.
//
// 1.74.1 (tail lamps in the dark - docs/backlog.md): tail/brake lamps and
// the DpadUp lights toggle are IN (red quads on the rear face past
// bodyOverhang, flared by inBrake, per-vehicle lightsOn seeded from the
// definition), the d-pad lost its driving fallbacks by the author's call,
// and all light points moved past the bumper overhang (they rendered
// INSIDE the body mesh - z-tested away, "no lights" while the bag
// submitted). What still does not show is the lamps themselves: the glow
// bag provably submits them (telemetry probe) through three blend/winding
// variants - filed OPEN in the backlog with the evidence and the next
// probes. PATCH.
//
// 1.74.0 (the visual pack - docs/vehicles.md, "The visual pack"): skid
// marks (a 96-quad terrain-flat ring under slipping rear wheels, distance
// paced, colors-only fade so bboxVersion bumps only on spawn), backfire
// (an upshift pops an additive quad at the exhaust for 0.09 s - the shift
// sound's visual twin), and headlight beam pools (an additive trapezoid on
// the terrain ahead, gouraud falloff, per-definition toggle, the example's
// CC96 has them on). Skids are one alpha-over submit, backfire+headlights
// share one additive glow submit; all skipped when idle. Both bags ride
// Precise culling with full clip checks - the engine ASSERTS on the None
// combination, which the first boot found the honest way. kFormatVersion
// 43 (headlights bool, additive). Verified on PCSX2: the beam pool visible
// on camera ahead of the nose, launches and drifts with zero asserts.
// MINOR.
//
// 1.73.0 (the sound pack - docs/vehicles.md, "The sound pack"): a vehicle
// definition can now author a HIGH-REV loop (crossfaded with the base one
// on the engine speed - the era's two-sample engine, volumes quantised and
// written on change like the pitch), a TYRE SQUEAL loop (volume rides
// DriveState::slip, the same number the smoke and telemetry read), and a
// GEAR-SHIFT one-shot - all in the Vehicle Editor's new Sounds tab, all
// silent until authored. Vehicle projects reserve four voices per core
// (base+20..23); the emitter bank runs four slots short there. The example
// ships a deterministic set (tools/veh-sound-pack.py). kFormatVersion 42,
// additive. Verified on PCSX2: boots with the pack wired, drives through
// gear changes and a drift with zero asserts - the ear test is the
// author's. MINOR.
//
// 1.72.0 (trading paint - docs/vehicles.md): car vs car stopped being a
// wall ("nieklimatyczne jeb i oba stoja w miejscu") and became momentum.
// Vehicles leave each other's wall gather; a pair pass after the vehicle
// loop tests two-disc capsules and answers in two modes: a closing hit is
// an impulse along the contact normal (authored masses, restitution 0.35 -
// a thump with bounce), a resting contact velocity-matches the pair
// (e = 0), because the bouncy impulse plus full separation acted as glue
// and stalled a pusher nose-to-tail with the gas held. Separation resolves
// 60% per frame, inverse-mass weighted; both matrix paths are notified.
// Verified on PCSX2 against a parked rival: the hit exchanges 8.8 -> 4.1
// u/s and launches it, and holding the gas bulldozes it 28 units to the
// platform with the pair rolling at ~8 u/s - tyre smoke off the shove for
// free, since the slip consumer never knew where the slip came from.
// MINOR.
//
// 1.71.0 (the sprung rig - docs/vehicles.md): the recurring "car breaks
// apart on a bump" had ONE structural cause, not many small ones: the body
// SNAPPED to the contact plane (pos = restY every frame - the four-sample
// mean jumps across a ridge, so the body teleported vertically) while a
// rate-limited pitch/roll hung mid-swing, wheels riding their own samples.
// The body is a damped spring rig now, in both twins: heave at wn 14 (0.9
// critical) with plane-velocity feed-forward (a plain spring rode half a
// unit under every climb), attitude at wn 11 (0.8 - a crest gets the small
// overshoot a snap never had), airborne glide at wn 4, landings keep their
// fall speed for the spring to absorb, and grounded gained slack (the
// binary test dropped steering and grip for a frame on every bump). The
// planar handling - speed, grip, yaw, walls - is untouched, and the
// pre-powertrain regression stays bit-exact (flat ground is the springs'
// equilibrium). New vehicle-check property: full throttle across a
// washboard keeps the frame height step under 0.3 (measured 0.097, the old
// rig teleported ~0.5), attitude sane, pace kept. MINOR.
//
// 1.70.0 (the bumper exists - docs/vehicles.md): the wall test sampled the
// AXLE rectangle, so a car stopped when its axles met the wall and the
// bonnet clipped a bumper's length inside ("dalej sie da wjechac w sciane
// maska"). DriveSpec grew bodyOverhang - the bumpers' reach past the axle
// line, measured off the BAKED body by the import (max extent vs half the
// wheelbase), seeded like the other measured geometry, editable in the
// panel like everything in specFields - and both twins sample the body
// rectangle now. kFormatVersion 41, additive (no migration step: a missing
// key reads as the 0.3 default, which is a typical sedan). Verified on
// PCSX2: nose-first into the north wall stops with the bonnet clear.
// MINOR.
//
// 1.69.0 (the car rides the world - docs/vehicles.md): the dig-in bug and
// the driver's seat rearranged. (1) Wheels (and the chassis) ride OBJECT
// floors: each ground sample is the max of the terrain and any mountable
// object top there (box tops within half a unit of the feet, mesh props'
// walkable faces), gathered in the same one-pass collider sweep the walls
// use. Before, a mesh slope was answered with "wall": the car nosed in, the
// wheel read as buried in the ground, and the head-on refusal braked it
// every frame ("kolo sie wbija w glebe, zostaje i hamuje"). (2) The default
// drive is R2 gas / L2 brake / Cross nitrous / Circle handbrake - and the
// throttle is ANALOG through the DualShock 2 button pressure (inputAnalog,
// new engine Pad::rawButtons()); digital sources read as 1. (3) The first
// REAL migration: v39 -> v40 rewrites bindings still at the old defaults,
// with the editor's backup + prompt machinery exercised end to end.
// Verified on PCSX2: R2 crosses a 0.45-high platform ON its top at full
// speed, Cross drains the tank, the migration rewrote exactly the three
// rows. MINOR.
//
// 1.68.0 (the driver's seat is rebindable - docs/vehicles.md,
// docs/input-bindings.md): three fixes and the backlog's Input Map item.
// (1) The wheel-arch clamp is measured from the TILTED body plane (terrain
// pitch + lean), not the flat pos[1]: on a climb the front arch rides ~0.4
// above the centre, so the window pinned the wheels - the front pair sank
// into the slope, the rear pair floated over the deck, and the whole car
// read as sheared ("co sie odpierdala, jak sie pod gorke jedzie"). This
// subsumes 1.65.3's lean-only term. (2) Engine: Pad::reset() never cleared
// pressed.L3/R3/Start/Select - the very four a previous fix ADDED to the
// pressed set - so the first R3 latched the rear view for the rest of the
// run. (3) Six Input Map roles cover every vehicle button (throttle, brake,
// handbrake, nitrous, camera, rear view), seeds matching the old hardcoded
// pads, per-role constexpr fallback for maps that deleted an action; the
// analog reads stay hardwired (an axis is not an action). Proven by
// rebinding the fixture's throttle to L2 and driving on it. kFormatVersion
// 39: the seeded vehicle actions are new .tyra content an older editor
// would round-trip into role-less custom actions. MINOR.
//
// 1.67.1 (al dente - docs/vehicles.md, "The three cameras"): the glance
// capped at +-60 degrees and R3 held = an instant rear view. The full orbit
// tanked the frame rate exactly broadside - the widest view of the map is
// also the most expensive one - and the only thing it bought over a glance,
// looking straight back, is now a cut that never sweeps through those views
// at all. The rear view takes the BODY yaw, not the lagging boom: mid-slide,
// "what is behind the car" is a question about the car. Verified on PCSX2:
// the glance stops at the three-quarter view, R3 mid-drive shows the grille
// and the road falling away behind. PATCH.
//
// 1.67.0 (the glance - docs/vehicles.md, "The three cameras"): the right
// stick orbits the chase/far camera around the car (X, a full circle in ~2 s)
// and lifts or sinks the boom (Y); both offsets spring back to zero on
// release, so the stick is a glance at a rival or an apex, never a re-aim.
// The car stays the look-at, the bumper cam stays bolted to the body on
// purpose, and the signs follow the steering stick's convention. Verified on
// PCSX2: mid-drive front-quarter view under stick right, sprung back behind
// the tail on release. MINOR.
//
// 1.66.0 (the world got solid - docs/vehicles.md): four driver reports, one
// round. (1) Cars no longer drive INSIDE objects: the wall test grew from
// four corners to eight points (a pillar narrower than the corner spacing
// drove between four), an object floor >0.5 over the car's feet blocks (a
// mesh prop's walkable face was a door into its inside), and the overlapped
// case moves only AWAY from the blocked points' centroid - which also closes
// the backlog's arena-escape bug, reproduced live (x 232, wall at 152) before
// closing. Colliders gather once per vehicle per frame; the runtime is now
// structurally the host twin. (2) Tyre smoke stopped punching holes in the
// car: the billboard submit moved to the frame's translucent tail and never
// writes Z (PipelineZTest_TestOnly). (3) The wheel-arch clamp tightened to
// 65% in compression (wheels rode through the bonnet at full travel).
// (4) The AI rival un-sticks itself (reverse-out + waypoint advance) - the
// walls holding is what parked it against a pillar forever - and far
// vehicles skip their shine pass (35u), wheels and smoke (70u). Verified on
// PCSX2 GS captures + telemetry; --vehicle-check grew pillar/overlapped/
// thin-wall properties. MINOR.
//
// 1.65.3 (the wheels lean WITH the car - docs/vehicles.md): the droop clamp
// was the right cap but the wrong diagnosis - the daylight in the report came
// from the LEAN, not the travel. The weight-transfer squat/roll rotates the
// body while the wheels stayed glued to flat ground, so a corner exit lifted
// an arch ~0.18 units off its own wheel. Each hub now adds the body plane's
// lean offset at its anchor (lz*sin(leanPitch) - lx*sin(leanRoll), signs
// mirroring the render's rotX/rotZ exactly); terrain pitch/roll stay out - the
// wheels answer those with their own ground sampling, which IS the suspension
// look. Verified on GS frame captures mid-donut: wheels tucked at full lean.
// PATCH.
//
// 1.65.2 (the wheel stays owned - docs/vehicles.md): the suspension's visual
// clamp was symmetric, so on a crest a wheel could hang a FULL
// suspensionTravel below the body (x1.5 instance scale = 0.27 units of
// daylight) - "kolo za bardzo potrafi odejsc od karoserii". The clamp is
// asymmetric now: full travel in compression, 45% in droop - real suspension
// droops less than it compresses, a tyre still shows daylight on a crest, the
// wheel just keeps reading as part of the car. One line, verified on a dune
// saddle capture. PATCH.
//
// 1.65.1 (the body finally lifts its nose - docs/vehicles.md): the sim's
// pitch is "positive = front higher" (slope gravity reads sin(pitch) with
// that sign and has decelerated every climb correctly since day one), but a
// positive rotX takes a point at +Z toward -Y - nose DOWN. The unnegated
// write had the BODY pitching into every hill while the wheels rode up it
// ("przod sie nie podnosi... dziwnie to wyglada"), and it survived until the
// map grew dunes because a flat arena never pitches anything. Negated at both
// writers - the runtime's transform write and the editor test drive's - so
// the weight transfer now reads correctly on screen too: squat is nose-up,
// brake dive and the wall-hit dip are nose-down. Found by the user's eye;
// verified by a dune climb capture and by the rollback physics (a car
// released mid-climb rolls back down and the reverse gear engages - the
// slope gravity sign was always right, only the picture lied). PATCH.
//
// 1.65.0 (AI drivers - docs/vehicles.md, "AI drivers"): a second car drives
// itself. The whole feature is proof of one architectural bet placed on day
// one: `DriveInput` is a struct a CALLER fills, never a pad read - so the AI
// is ~25 lines that fill the identical four numbers, and the gearbox, the
// kickdown, the wall grind, the tyre smoke and the weight transfer all come
// along for free, because the AI is just another caller of the same sim.
//
// Authoring is a NAME PREFIX (SceneObject::vehicleRoute): codegen collects
// every object in the scene whose name starts with it, sorted by name, and
// bakes their positions as the instance's waypoint loop - an Area per corner
// is the natural marker (invisible at runtime, no collider), and the baked
// table means no runtime name matching at all. The controller is pure
// pursuit: steer from the heading error, throttle backed off in tight
// corners, advance within a radius. A player can HIJACK a patrolling car -
// the pad branch simply outranks the AI branch while they drive, and getting
// out resumes the patrol where it stood.
//
// The acceptance line is VEHAI telemetry every ~2 s (position, waypoint,
// speed), so `grep VEHAI` proves a patrol advanced its loop with no pad
// attached - the backlog's own "done when" criterion, machine-checked.
//
// kFormatVersion 37 -> 38: "route" inside the object's vehicle block, written
// only when non-empty. Additive, reader defaults, no migration step. MINOR.
//
// 1.64.0 (tyre smoke - docs/vehicles.md): DriveState::slip finally has its
// consumer. Past 0.35 the rear anchors feed a 48-puff ring at a rate
// proportional to the slip - burnouts, handbrake slides and wall grinds all
// smoke, because they all ARE slip, and one number feeding both the smoke and
// the telemetry is what keeps them from disagreeing. Camera-facing billboards
// in ONE submit (the particle system's exact bag shape - VU1 expands centre +
// 2x2 basis weights into a quad), untextured grey with per-puff alpha,
// swirling and swelling as they fade (the fog puff's recipe). A dead puff is a
// degenerate quad and the bag is skipped when the pool is empty, so a clean
// drive pays nothing. Ticks under the same !menuActive gate as the emitters,
// so puffs hang frozen behind the pause menu. Verified mid-handbrake-spin on
// PCSX2: a grey trail behind the sliding car. No format change. MINOR.
//
// 1.63.0 (the wet lacquer - docs/vehicles.md, "A shiny body"): the NFS paint
// pass, and WITHOUT the dedicated VU1 program everyone assumed it needed. A
// fresnel rim (0.3 + 0.7*(1-|N.V|)) rides the env pass's per-vertex RGB and a
// Blinn-Phong (N.H)^8 white specular rides the per-vertex ALPHA, drawn with
// the GS's HIGHLIGHT2 texture function - RGB = Tex*Cv>>7 + Av - so both
// effects share the ONE existing env submit and the additive FIX blend still
// carries the authored Body shine. HIGHLIGHT2 was always in the GS; the
// engine just never selected it. One new engine field
// (StaPipTextureBag::textureFunction, per-bag TFX - safe on a shared texture
// because TEX0 is re-emitted per bag) and a per-frame EE loop over the env
// colours, the wheel-bag precedent, ~1100 vertices of a few flops each.
//
// Scoped to vehicles (vehiclePaintFor), so chrome and mirror balls elsewhere
// keep their exact look. Three rules from the fields underneath: write through
// envColorBag->many (LOD tiers re-aim it), never bump bboxVersion (the env
// bag shares the base pass's frustum cache entry - worth 4-6% of frame rate),
// and alpha >= 1, because the GS alpha test is NOTEQUAL 0 and a zero specular
// would erase the reflection with it. Also --vehicle-check (the sim's
// property tests as a CLI verb, CI-ready) and the suspension the wheels now
// actually DRAW (each hub rides its own wheel's sampled ground within the
// travel). Viewport per-pixel program mirrors the paint terms; the
// PS2-shading GS variant keeps plain reflection, stated in the doc.
//
// No format change. MINOR.
//
// 1.62.0 (the shine you can SEE - docs/vehicles.md, "A shiny body"): the
// user's verdict on 1.60's reflection was "szczerze to nie widze, zeby sie
// cokolwiek odbijalo", and they were right for a structural reason: the
// "@sky" env map is a SMOOTH GRADIENT, and a gradient reflection is nearly
// invisible by construction - there are no features to see move. The era's
// answer was a static high-contrast sphere map (Underground's wet lacquer is
// vertical light streaks in exactly such a texture), so a vehicle's paint
// now mirrors an AUTHORED map: VehicleDef::bodyReflMap, a res/ image, with
// tools/nfs-streak-map.py generating the classic streaks (deterministic, no
// RNG - a re-run is byte-identical). Empty keeps "@sky".
//
// MATTE TYRES, because the user asked whether the engine even allows it: it
// does - tmdl reflection is PER PART - and the bake now uses that. The
// untextured merge splits into "merged" (paint) and "merged-matte" (rubber
// and near-black trim, by name first and luminance under 0.12 second; glass
// forces shiny by name, or a deep-blue window would land under the
// threshold). The reflection pass attaches to the paint alone. One more
// submit, paid only when shine is on, and the Cost tab reports it.
//
// THE WHEELS WERE OFF because the body kept the EXPORTER's origin: the sim
// places wheel anchors at +-wheelBase/2 around the chassis origin, and the
// reference car's pivot sat 0.25 behind the axle midpoint - every wheel rode
// visibly forward of its arch. The bake re-origins the body to the AXLE
// CENTRE at HUB HEIGHT (mean of the detected wheel centres in the canonical
// frame), which also makes rideHeight = wheelRadius put the tyres exactly on
// the ground.
//
// Also: the D-PAD drives (a keyboard emulating a stick - PCSX2 in a VM above
// all - can drop chorded key events, and full-lock-plus-throttle is exactly a
// chord; the d-pad is independent booleans end to end), and the body lean got
// a knob (DriveSpec::leanAmount, a spec field, so it serializes and edits by
// existing) plus a stiffer 35 deg/s follow - 25 read as a boat from the
// driver's seat.
//
// kFormatVersion 36 -> 37: bodyReflMap plus leanAmount (which rides
// specFields, the one list). Additive, readers default, no migration step.
// MINOR.
//
// 1.61.0 (four reports from the driver's seat - docs/vehicles.md): the
// steering was INVERTED, cornering killed the throttle, hills swallowed the
// car, and the wheels rode outside the arches. All four were real.
//
// THE STEERING: in the canonical frame (forward +Z, up +Y, right-handed) the
// body's right is -X - cross(forward, up) - while positive steerAngle turns
// the yaw toward +X, and screen X runs opposite world X besides. So "stick
// left" turned the car screen-right, and the original acceptance test never
// saw it because it only proved yaw MOVED under stick input, not which way
// the car went on screen. DriveInput.steer keeps its "positive = the
// driver's right" meaning and is negated once, inside the sim (both twins),
// so the test drive's A/D and the pad fix together. The doc's telemetry
// samples flip their yaw signs with it.
//
// CORNERING-KILLS-THE-GAS was an input truth, not a physics bug: the stick's
// throttle is its vertical deflection, and a stick at full lock has none
// left - so a stick-only driver lost the gas exactly when steering hard,
// then engine braking ground them to zero. R2 is a second throttle button
// now (the era's racers put the gas on a button for exactly this reason).
//
// HILLS: with gearTorque 1 the top gear pulls 0.43x, which loses to a
// 15-degree dune, and the passive downshift waits for 50% of redline - the
// car wallowed through two gears before any torque came back. KICKDOWN: flat
// out with the engine under 72% of redline drops a gear immediately. The
// landing guard leaves 0.15 of headroom under the up-shift point, not 0.05,
// because the shift CUT itself decays the speed - with the tighter margin
// the box kicked down into its own up-shift for ever and the harness car
// crawled 170 units in 50 seconds ON THE FLAT. Harness: launch to top gear
// on the flat, kick down on a 15-degree ramp, hold >= 5 u/s, climb 314
// units - PASS, with the pre-powertrain regression still 0.000000000.
//
// THE WHEELS: the example's .tyra carried the struct DEFAULTS (track 1.40
// against a 1.414-wide body - wheel centres exactly on the paint, tyres
// fully outside the arches; radius 0.32 against a 0.232 baked wheel - the
// car floated). The editor adopts measured geometry on import but only in
// the GUI tick, and this example was authored headless, so nothing ever
// said so. The build log states the measurement now ("[vehicle] ...
// measured wheelBase 2.066 track 1.248 radius 0.232 ...") and the example
// carries the measured numbers. gearTorque softened 1 -> 0.6 while there,
// so the top gear holds the dunes it drives on.
//
// No format change. MINOR for the kickdown and R2.
//
// 1.60.0 (the drive, perfected - docs/vehicles.md): an adversarial review of
// the whole vehicle branch plus the fixes it demanded, three physics upgrades,
// a reflective paint option and a four-times-bigger playground.
//
// THE REVIEW (an agent told to refute, then everything verified here) found
// ten real defects. The ones worth remembering: the engine note's voice
// base+23 was EMITTER SLOT 7 - all 24 SPU2 voices of a bus were already spoken
// for, so a continuous loop could only get a channel by taking one, and the
// emitter bank is now generated one slot short in a vehicle project
// ({{SND_SLOTS}}); the HUD font was emitted in the WRONG INDEX SPACE (project
// fonts index where FONTS[] is indexed by atlas position - it worked only
// because the example has one font); setupVehicles REUSED array slots across
// scenes without a reset, so a revisited scene's car kept the previous
// scene's gear, nitrous and - because the scene-load mute had zeroed that
// voice - a stale engineCh that suppressed the re-play and left the engine
// permanently silent; the pause menu froze the engine note at its last pitch
// (updateVehicles is gated on !menuActive and was the only volume writer);
// shiftTimer never ticked in reverse, so a car that rolled backwards
// mid-shift kept its throttle cut; and the adpenc cache was mtime-only, so a
// bin/sfx/x-loop.adpcm encoded BEFORE -L existed would never re-encode - the
// staleness test now reads the encoded header's own loop byte back.
//
// PHYSICS: walls SLIDE now - axis-separated, the grind scrubbing speed by
// impact angle, with "a slide is only a slide if that axis carries real
// motion" (the first cut let a head-on grind in place at a phantom 5 u/s -
// the harness caught it); weight transfer (squat/dive/lean, presentation-only
// and deliberately never fed back into the pitch the slope gravity reads);
// and five host/runtime divergences closed - the handbrake now actually
// SLOWS the car on the console, maxSlopeCos stopped being a slider that did
// nothing there, pitch/roll are rate-limited (they feed sin(pitch) gravity,
// so this is longitudinal behaviour, not cosmetics), and airborne attitude
// settles level.
//
// THE PAINT: VehicleDef::bodyShine bakes refl "@sky" into the body's .tmdl
// parts - fields the format already carried. What made it POSSIBLE is an
// engine-side change: reflective parts were banned from the matrix fast path
// because their env normals were baked in world space, frozen at the
// promotion pose. The local bake captures LOCAL normals now and renderEnvPass
// folds the object's rotation into the env camera basis (dot(R n, e) =
// dot(n, R^T e) - a constant per mesh per frame, zero per-vertex work), so a
// shiny car keeps both its two submits and a correct reflection while
// yawing. The viewport preview reads the same tmdl fields, so the editor
// shows the shine the console draws.
//
// kFormatVersion 35 -> 36: "bodyShine" plus writers that no longer DROP
// authored values when their switch is off (unticking the HUD used to reset
// hudSpeedScale on the next load). All additive, readers default, no
// migration step. MINOR.
//
// 1.59.0 (the driver gets instruments - docs/vehicles.md, "The HUD"): speed,
// gear and the nitrous tank on screen while driving. The powertrain already
// supplied every input, so this is the drawing and nothing else.
//
// It is RUNTIME text, so a vehicle with the HUD on joins
// Project::atlasFontIndices() - without that the font ships no glyph atlas and
// the readout draws nothing at all, which reads as a broken feature rather than
// as a missing asset. Horizontal positions carry the widescreen squeeze, the
// same 4:3-over-window-aspect factor the menus use, because anamorphic
// widescreen keeps the framebuffer's shape and lets the TV stretch it.
//
// The trap worth keeping: the first version put the nitrous line at 0.945 of the
// frame height, where a screenshot showed the EMULATOR'S OWN picture cutting it
// in half - on a CRT it would not have been there at all. Layout is title-safe
// now (docs/safe-areas.md) and the bottom row is what to re-check. Verified on
// PCSX2 reading 88 / gear 5 / NOS 3 at top speed under nitrous.
//
// kFormatVersion 34 -> 35: `hud`, `hudFont` and `hudSpeedScale`, written only
// when a definition HAS the HUD on, so a project without it resaves byte for
// byte. No migration step. MINOR.
//
// 1.58.0 (a drive is no longer silent - docs/vehicles.md, "Engine sound"): a
// looping engine note whose SPU2 PITCH follows the engine speed the powertrain
// computes. It closes the oldest entry on the vehicles backlog.
//
// The blocker was never the pitch. SD_VPARAM_PITCH is reachable, libsd is
// already linked into the engine and logVoiceState already READS that very
// register - what was missing was that nothing could LOOP. The loop turns out
// to live in the encoded sample rather than in the play call: `adpenc -L` sets
// the SPU2 block loop flags, so the build now encodes any `res/sfx/*-loop.wav`
// that way and the convention is in the file name because adpenc runs over a
// directory and has no access to the model (the *-lit.png arrangement). The
// engine fork gains exactly one function, AudioAdpcm::setPitch.
//
// Two costs shape the runtime. sceSdSetParam is a BLOCKING SifCallRpc, so the
// register is quantised to 32 steps and written only when it moves - no calls
// at all at a steady cruise. And a looping voice cannot be stopped (audsrv's
// own doc comment), so getting out sets the volume to zero.
//
// Verified on PCSX2 two ways. The telemetry proves the tracking: idle 800 rpm
// -> pitch 1408 (the sample's own 1881 times the authored 0.75), 6585 rpm ->
// 4192, and the register DROPS at every upshift. And PCSX2's own audio output,
// captured and analysed, proves it is audible: the spectral centroid runs
// 194 Hz at idle -> 417 Hz at the first-gear redline -> 243 Hz once it has
// changed up, i.e. the RPM sawtooth, heard.
//
// kFormatVersion 33 -> 34: `engineSound` plus its pitch pair and volume, all
// written only when a definition HAS a sound, so a project without one resaves
// byte for byte - the bump is so an older editor refuses a file carrying them
// rather than dropping them on its next save. No migration step. MINOR.
//
// 1.57.0 (the powertrain - docs/vehicles.md, "The gearbox"): a driven car now
// has a GEARBOX, an engine speed and nitrous, which is what everything an
// arcade racer is made of hangs off - the engine sound's pitch, a tacho, and
// the shift the player hears.
//
// The load-bearing decision is that the gearbox is DERIVED, not simulated. The
// gear and the RPM are computed from the speed the existing longitudinal model
// already produces and feed nothing back, so `accel` means exactly what it
// meant before and every vehicle authored without a gearbox accelerates
// identically with one - checked by a harness that reproduces the
// pre-powertrain arithmetic independently and reads a worst-case difference of
// 0.000000000 over 14 s of full throttle. Two knobs let it bite and BOTH
// default to off: `shiftTime` (a throttle cut between gears) and `gearTorque`
// (the ratio shaping acceleration, geometric and centred on the middle gear so
// it changes a car's character rather than its performance - the geometric mean
// of the multipliers is 1.0000). Nitrous is gated on `nosCapacity`, seconds of
// boost, defaulting to 0: the TANK is the switch, so there is no second flag
// that could disagree with it.
//
// The down-shift threshold is COMPUTED rather than validated (`safeShiftDownFrac`
// / `vehShiftDownFrac`): an author is free to dial shift-up and shift-down into
// a contradiction, and the point is held below where an up-shift lands so the
// box cannot hunt between two gears for ever. Measured with deliberately
// contradictory thresholds: 4 gear changes over 14 s, which is a clean climb.
//
// kFormatVersion 32 -> 33, purely additive: twelve new keys inside a vehicle's
// existing "drive" object. The writer emits every specFields() entry, so a
// project WITH a vehicle gains those keys on its next save - which is the whole
// reason for the bump, so an older editor refuses the file instead of silently
// dropping them. The reader defaults each one to the struct's own value, so an
// older file opens unchanged and needs no migration step. A project with no
// vehicle still resaves byte for byte. MINOR by this file's own rule.
//
// 1.56.0 (cars you can drive - docs/vehicles.md): a Vehicle object type, a
// project-wide VehicleDef the instances name, and an importer that takes one
// authored .glb/.fbx and finds the wheels in it.
//
// The wheels are found by GEOMETRY, not by node name. The reference asset
// (CC96/car1.fbx, CC0) names its nodes Cube and Cylinder.001..003 - Blender
// defaults - so a name-matching importer fails on the first real model. Mesh
// nodes are clustered by shape and clusters of 2/4/6 scored on roundness,
// thinness, height in the model and size; names and materials are a bonus
// only. The vehicle's own frame falls out of the cluster (the axle is the axis
// a wheel is thinnest along; of the rest, the one the centres barely spread
// along is up), so no exporter axis metadata is read anywhere. What the
// importer CANNOT decide is which end is the nose, and it says so rather than
// guessing quietly - there is a flip in the panel.
//
// The reference car is 40 materials and 36 mesh parts, and a .tmdl part is one
// bag at ~1 ms of fixed EE time: 36 submits is nearly two PAL frames for one
// parked car. Because pushVert folds a material's kd into the vertex colours,
// untextured materials merge losslessly - they become one part whose vertices
// point at cells of a generated palette texture. 36 parts -> 2 submits.
//
// kFormatVersion 31 -> 32, purely additive: PrimitiveType::Vehicle (20), the
// per-object "vehicle" block and Section::Vehicles, all of which an existing
// project simply does not carry - a project with no vehicles resaves byte for
// byte. MINOR by this file's own rule. (Authored as 1.55.0; renumbered in the
// merge - main had independently taken 1.55 for the packaging fixes below.)
//
// 1.55.3 (an INSTALLED TyraX could not build a game at all - docs/updates.md):
// both packagers staged vendor/tyra minus "*.o", "*.a" and "*.elf", meaning "a
// dev checkout's build leftovers are not content" - and
// vendor/tyra/audsrv/bin/libaudsrv.a is not a leftover. It is a COMMITTED
// artifact of the in-tree audsrv fork (the per-channel L/R panning sound
// emitters need), which runner.cpp overlays onto the build image's PS2SDK
// together with audsrv.irx and audsrv.h. Those two matched no pattern and
// travelled, so the hole was exactly one file wide and the failure wore
// somebody else's face: the overlay is a `cp a && cp b && cp c`, it died on the
// missing lib BEFORE reaching the header, the game then compiled against the
// image's stock PS2SDK copy, and every build ended
//
//     md5sum: /engine-src/audsrv/bin/libaudsrv.a: No such file or directory
//     inc/audio/audio_adpcm.hpp:108:5: error:
//         'audsrv_adpcm_set_volume_and_pan' was not declared
//
// on EVERY project, for everyone who installed the editor and for nobody who
// built it from a checkout - where the file is present and the same build is
// clean. That asymmetry is why it survived two releases: the only people who
// could reproduce it were the ones who could not debug it.
//
// Both packagers now exclude by DIRECTORY, and the list is exactly what
// .gitignore drops under vendor/tyra (engine/obj, engine/bin, audsrv/.work):
// what git keeps, the package ships. runner.cpp additionally checks the three
// overlay files before it starts and names the missing one, because an editor
// packaged before this fix stays broken until it updates - and the message it
// used to give pointed at the engine's audio code, which was never wrong.
//
// Verified on both halves of the pair. Linux: stage_tree's find expression over
// this tree stages 402 files where the old one staged 401, and the difference
// is libaudsrv.a. Windows: ISCC compiles tyrax.iss with all three
// audsrv/bin files in its "Compressing:" list and zero paths under engine/obj,
// engine/bin or audsrv/.work. PATCH: no capability appears, nothing changes
// shape on disk, a build that could not run starts running.
//
// 1.55.1 (the self-screenshot reaches the console it was built for -
// docs/devkit.md): the feature below shipped WORKING IN THE EMULATOR ONLY, and
// nothing said so. On hardware the picture never came back, deterministically,
// and the cause is one call inside ps2sdk's libdebug: `ps2_screenshot_file()`
// creates its output with `open(name, O_CREAT|O_WRONLY)`, and over ps2link that
// create arrives at the `host:` server as a MKDIR OF THE TARGET NAME. The host
// ends up with a DIRECTORY called frame.tga, the open that follows returns -1,
// and the function reports nothing at all - it has no failure path. Measured on
// a real PS2, twice, byte for byte the same:
//
//     remove file host:frame.tga
//     mkdir name host:frame.tga
//     mkdir wrong mode, using fallback value 493
//     open name host:frame.tga flag 202  ->  open fd = -1
//
// while livedbg.bin, livetime.bin and every other devkit file - all written
// through fopen(name, "wb"), flags 0x602 on the wire - succeeded in the same
// session over the same server. So the runtime keeps the half of libdebug that
// carries the value (ps2_screenshot, the VRAM readback) and writes the file
// itself. PCSX2's own host: server accepts libdebug's spelling, which is
// exactly why this could ship as emulator-only without anybody noticing.
//
// Two hardware-only traps came with owning the write, and both are guarded:
// the readback lands in RAM BEHIND THE EE'S DATA CACHE (each line is flushed
// after its transfer, or the picture repeats rows - invisible in an emulator
// that emulates no cache), and ps2_screenshot REFUSES to run while VIF1's DMA
// channel is busy, saying so only through its return value, so refusals are
// counted and reported rather than written out as picture. A third bug was on
// the EDITOR side and needed no console to be wrong: the panel gave a capture
// six polls (~2.4 s) to finish before calling the file malformed, which PCSX2
// meets between two frames and ps2link cannot - one capture is ~900 KB at a
// network round trip per 1.4 KB, measured at about three seconds. It now waits
// on PROGRESS (the file still growing) and reports only a write that has
// stalled.
//
// Also here, from the same session: every capture is kept as a PNG under the
// project's screenshots/ folder, and *Show file* reveals THAT rather than
// bin/frame.tga - the channel file is overwritten by the next capture and
// deleted by every launch, and explorer answers a path that does not exist by
// opening the user's Documents folder, which reads as a broken button (it was
// reported as one). platform::revealInFileManager now walks up to the nearest
// ancestor that exists, so no caller can reproduce that. The picture is written
// opaque, so nothing downstream has to know that a frame buffer's alpha is a
// working channel rather than coverage.
//
// VERIFIED on the user's PlayStation 2 over ps2link, unattended: the capture
// comes back 512x512, 1048594 bytes = 18 + 512*512*4, exactly the expected
// size, with 0 repeated rows and no VIF1 refusals; it agrees with the same
// scene captured in PCSX2 to 2.0/255 mean absolute difference; a second capture
// after a --pad camera turn shows the turned view, so it is live rather than a
// stale buffer; and the whole user-facing loop (menu > Run on PS2 > Debugger >
// Screen > Capture frame > the picture on screen) was driven with --ui-script
// and asserted with expect, exit 0. The PNG that lands in screenshots/ is
// pixel-identical to the TGA it came from.
//
// PATCH: a fix. The generated devkit runtime changes, so a project must be
// rebuilt to get it; nothing on disk changes shape.

// 1.55.0 (the game photographs itself - docs/devkit.md, "The game's own
// screenshot"): a sixth one-shot on the Live Debugger's command channel (flags
// bit 6, beside the VU1 capture and the RAM measurement). The game reads its
// last finished frame out of GS VRAM through ps2sdk libdebug's VIF1 reverse
// FIFO, writes bin/frame.tga over the same host: channel every other devkit
// file uses, and the Debugger's new Screen tab decodes and shows it.
//
// IT IS THE ONLY CAPTURE PATH THAT DOES NOT NEED A DESKTOP, which is the whole
// argument for it: the emulator's F8 key, a GDI grab and PrintWindow all need
// the window present and unoccluded on an unlocked session, and none of them
// exists on a console at all. This one answers from hardware, from a locked
// machine and from an unattended script.
//
// Four traps, each of which fails silently and three of which were found by
// measuring rather than by reading. `fb->address` is in GS WORDS while the API
// wants BLOCKS, so a missing /64 overflows SBP's 14 bits and reads buffer 0 with
// its pages scrambled. The buffer must be getPreviousRealFrameBuffer(), never
// the current one (half-composed) or getPreviousFrameBuffer() (which can be a
// synthesised extrapolated frame). libdebug opens the file O_CREAT|O_WRONLY with
// NO O_TRUNC, so a shorter capture over a longer one leaves the previous
// picture's tail behind and still decodes - the runtime deletes first. And
// ps2_screenshot_file's RETURN VALUE IS NOT A VERDICT: upstream returns 0 both
// when open() fails and when everything worked, so the first version logged
// "capture failed" over a perfectly good 1 MB picture. The check is the file's
// own size against 18 + w*h*4.
//
// The panel decodes the TGA by hand rather than through stbi_load, and that is
// deliberate: the editor's stb_image is built STBI_ONLY_PNG + STBI_ONLY_JPEG and
// answers "unknown image type" to every TGA (which is how this was caught, on
// screen, in the honest-failure text). Adding TGA would widen what every other
// stbi_load in the editor accepts - the asset importer above all - for one debug
// preview, where the format has exactly one writer whose source is known.
//
// Verified end to end in PCSX2 on an fpp fixture: the self-capture agrees with
// PrintWindow's grab of the emulator's own render area to **0.91/255 mean
// absolute difference** with the horizon at the same fraction (0.531 vs 0.530),
// which is what says the address, the row order and the channel order are all
// right. The Runner's stale-delete was checked by looking for the file after a
// relaunch, and the whole loop - tab, button, command, capture, preview - was
// driven with --ui-script and no human. --audit-release fails on the debug ELF
// naming `frame.tga` among six findings and comes back clean on the release one,
// whose devkit TU is three lines.
//
// MINOR: a capability appears, nothing changes shape on disk. (Authored as
// 1.54.0 and RENUMBERED on the merge, this file's standing arrive-second rule -
// main took 1.54.0 with #245 while this branch was open. Two of the entries
// below are the argument for this one: 1.53.1 was diagnosed with every capture
// taken by the GAME ITSELF, by hand, because the desktop was locked all night;
// and 1.54.1 is a fault PCSX2 structurally cannot show, which is the other half
// of the same problem - when only the console can reproduce something, only the
// console can photograph it.)
//
// 1.54.1 (the flashlight's wall patch stops killing the game on real
// hardware): setupLightPools set the wall slice's shading type thirty lines
// BEFORE it allocated the bag holding it - a store through a null unique_ptr
// at offset +4, which is where StaPipInfoBag::shadingType sits. Every project
// with a torch took it during scene setup, i.e. the instant the loading screen
// ended. It was invisible for a release and a half because PCSX2 has main RAM
// at address 0, so the write landed in low memory and every emulator test
// passed; a console has nothing mapped there and raises a TLB refill on store
// (cause 3, BadAddr 0x00000004). The write now happens after the make_unique,
// which also makes the wall patch Gouraud as the surrounding code always
// intended - the per-vertex reach falloff renderSlice has been feeding it all
// along. Cause 3 is handed back to the kernel by the crash handler on purpose
// (see crash_handler.cpp), so this class of fault produces ps2link's raw
// register dump and no crash.txt - docs/devkit.md says so now.
//
// 1.54.0 (the viewport draws the light beams too - docs/flashlight.md): a
// scene with Point Light > Beam used to look materially different in the
// editor than in PCSX2, because the editor drew neither half of it. It draws
// both now, from the game's own numbers: the additive corona billboard with
// the camera pull (a quarter of the light radius, capped at three quarters of
// the camera distance, size-compensated - without the pull the editor shows
// the very z-fight seam 1.53.1 removed from the console), and the eight-
// segment apex-to-black cone shaft for Beam: corona + shaft. One sprite bake
// serves the beams, the ground pools and the night sky's star dot
// (Viewport::coronaTex, at menubake::kCoronaSpriteSize - the pools were still
// uploading it at the flare size after 1.53.1 moved kind 2 to 128, i.e. a
// quarter of the image). Beams draw in EVERY shading mode, unlike the ground
// pools: a beam is geometry the game submits, not a simulation of how the
// console shades. The runtime LEVEL is deliberately not reproduced - flicker,
// Set Light and a streamed-out light are runtime state, and a glow pulsing
// over a rock-steady pool of light would be a new lie rather than less of one.
// Verified against PCSX2 on examples/night-walk's street lamp by differencing
// beam-on against beam-off in each renderer (which cancels the editor's
// gizmos and every shading difference) at a matched eye/aim/FOV from three
// vantages: the added light lands within 0.17 % of picture width and 0.64 % of
// height, its area agrees to 8 %, and the editor's amplitude tracks the
// sprite's own alpha curve to 3 % at two brightnesses. Behind a wall both add
// exactly zero, so the depth test still hides a glow the way it should. Also
// corrected on the way through: the console capped the pull at HALF the camera
// distance while its own commit message, docs and this file all said three
// quarters - the measurement that picked the value is in 1.53.1's entry, and
// the code kept the value it rejected. MINOR: the viewport gains a capability,
// nothing changes shape on disk.
//
// 1.53.1 (a lamp's glow stops sawing its own pole): reported from
// examples/night-walk with a screenshot - a hard, stair-stepped lit/dark
// boundary running up the street lamp's pole. Diagnosed in PCSX2 by bisection
// at the reporter's own vantage (torch toggled: unchanged; light removed:
// gone; Beam set to 0: gone - so the corona), with every capture taken by the
// GAME ITSELF (ps2sdk's ps2_screenshot_file into host:, VIF1 reverse FIFO),
// because the desktop was locked all night and no host-side capture can see a
// window there. Two causes, two fixes, both in the generated
// updateAndRenderLightBeams/menubake pair:
//
// THE SEAM IS A Z-FIGHT WITH ITS OWN FIXTURE. The corona is a depth-tested
// additive billboard centred exactly on the bulb, so it slices through the
// lamp's own pole and arm, and the GS's fixed-point z cuts the soft sprite on
// a chunky seam that wanders as the camera moves. The sprite is now PULLED
// toward the camera (a quarter of the light radius, capped at three quarters
// of the camera distance - a half-distance cap measurably parked the seam at
// the pole's base when looking steeply up, which is how the cap value was
// chosen) and shrunk by the same fraction, so its apparent size is untouched:
// the glow blooms OVER the thin fixture the way a real lens does, and a wall
// between camera and lamp still occludes it. The cone shaft (Beam: shaft)
// stays at the true position - it is world geometry.
//
// AND THE CORONA WAS 64 TEXELS ACROSS A THIRD OF THE SCREEN. Up close the
// radial gradient's texels are ~4 px, so its rim contours in visible steps
// whatever the z does. Kind 2 - the beam corona, which the star field also
// draws through - bakes at 128 now (menubake::kCoronaSpriteSize; the 2D
// lens-flare sprites stay 64, they draw small). The file is rewritten on
// every refreshGenerated, so existing projects pick it up on their next
// build; the editor viewport's star-dot upload follows the same constant.
//
// What this deliberately does NOT fix, measured so it is not re-chased: the
// few-pixel stepping that remains at the pole's base is the pole model's own
// edge aliasing at native resolution - identical with the corona's z-test
// off, identical at 64 and 128, present with the beam entirely removed once
// the contrast is matched - and would need AA or a higher raster, not a pass
// change. PATCH: no capability appears, a defect goes away; the format is
// untouched.
//
// 1.53.0 (the viewport learns to lie less - docs/ps2-viewport.md): two new
// look simulations beside the PS2 output mode, both machine-global. "PS2
// shading" re-runs the viewport's ONE lighting chunk per triangle corner in a
// geometry stage - the console's per-vertex shading, with TyraShadingFlat
// mirrored per draw, dynamic lights on the VU1 slot formula (radial, no N.L),
// the terrain's dynamic light drawn as the console's ground POOL (same corona
// pixels, same FIX scale) and the flashlight kept per-pixel like its projected
// pool. "GS colour" quantizes the picture to PSMCT16's 5 bits through the
// engine's own DIMX dither matrix, following the project's Colour depth by
// default. Verified A/B against a PCSX2 frame of a lamp + sphere fixture
// (savestate-embedded screenshot; the pool, the lit ball and the banding
// match). MINOR: two capabilities appear, nothing changes shape on disk.
//
// 1.52.1 (the .rpm stops being twice its own size): v1.52.0 shipped a 31 MB
// rpm of a tree that packs into 13, because a spec that says nothing about its
// payload gets the BUILDER's default - and the CI runner's rpmbuild (Ubuntu
// 22.04, rpm 4.17) reaches for gzip where a modern one reaches for zstd. It is
// stated now, as xz: rpm 4.8 (2010) on the installing machine rather than zstd's
// 4.14, and Debian-family rpm links liblzma for certain, which is not something
// to bet a release job on. Verified by packing the same tree both ways and
// reading %{PAYLOADCOMPRESSOR} back off the result - and the shrunken rpm's
// payload was extracted and its editor run (--vu-check) out of it.
//
// Also here: the repository was renamed tyra-editor -> tyraX, so the four
// tracked strings that still named the old one follow (the generated
// THIRD-PARTY-NOTICES, the VS Code extension's README, package.json and its
// packager). GitHub redirects the old URL, so nothing was broken - it was
// merely lying about where this comes from. The committed .vsix still carries
// the old URL in its manifest and is deliberately NOT repackaged for a metadata
// string; the next real extension change picks it up.
//
// 1.52.0 (Linux gets packages of its own, and one of them updates itself):
// docs/updates.md. `installer/build-package.sh` is the POSIX twin of
// build-installer.ps1 - it stages the repo-shaped tree ONCE (bin/, vendor/tyra,
// tools/, the nine VU sources, examples, the licence files) and emits three
// formats from it, so they cannot disagree about their contents:
// `tyrax-<v>-linux-x86_64.tar.gz`, `tyrax_<v>_amd64.deb` and
// `tyrax-<v>-1.x86_64.rpm`. The release workflow gained a build-linux job that
// attaches all three - stamping the released PATCH into this file's workspace
// copy exactly as the Windows job does, or a tarball install would report the
// file's number, disagree with its own release and offer itself an update for
// ever. It runs on ubuntu-22.04 ON PURPOSE, because a binary runs
// on a newer glibc than it was built against and never an older one, so the
// runner image IS the compatibility floor.
//
// THE TARBALL IS THE PRIMARY FORMAT AND THE OTHER TWO ARE A CONVENIENCE LAYER,
// which is a statement about what made the Windows installer good: not that it
// is an installer, but that it installs PER USER, without root - which is the
// only reason an update can install itself with nothing to authenticate
// against. A .deb or .rpm cannot do that, so those are handed to the package
// manager, out loud: `update::installKind` reads a one-word `.tyrax-package`
// marker at the install root (absent = a source checkout) and
// `selfInstallBlocked` turns each answer into either the install button or ONE
// sentence naming what to do instead. `update::parseRelease` now picks its
// asset by `platformAssetSuffix()` rather than by `.exe`, and the Linux half of
// `runInstaller` writes a small detached script that waits for the editor to
// exit, unpacks over the install root and starts it again - the same overlay
// semantics tyrax.iss has always had.
//
// The .deb/.rpm live in /opt/tyrax with a /usr/bin symlink, which works because
// platform::exePath resolves /proc/self/exe through canonical() - so the
// editor's four exe-relative lookups land in the real tree. Verified: all three
// packages built and inspected, a project created by the unpacked tarball's
// binary bind-mounts ITS OWN vendor/tyra, and a full self-update (refuse for
// deb/rpm/checkout/read-only, unpack, relaunch) driven from a harness.
//
// MINOR: a capability appears, nothing changes shape for an existing project.
//
// 1.51.0 (TyraX ships as an installer, and tells you when there is a newer
// one): three pieces that only make sense together - docs/updates.md.
//
// AN INNO SETUP 7 INSTALLER (installer/tyrax.iss + build-installer.ps1). What
// it packages is not just the .exe: the editor resolves the Tyra engine, the
// PS2 tools, the VS Code extension and the VU framework sources RELATIVE TO
// ITS OWN BINARY, one directory up, so the installed layout reproduces the
// shape a development checkout has (bin/tyrax-editor.exe beside vendor/, tools/
// and src/) - a bare .exe would install an editor that cannot compile a game.
// Per-user by default (%LOCALAPPDATA%\Programs\TyraX), which is what lets an
// update install itself without a UAC prompt.
//
// EVERY PUSH TO main IS A RELEASE (.github/workflows/release.yml). The version
// is authored HERE, and the TAGS record which patches are spent: CI reads these
// three macros, releases them as they stand if v<that> is untagged, and
// otherwise goes one PATCH past the highest v<MAJOR>.<MINOR>.* tag - stamping
// that number into a workspace copy of this file before it compiles, so the
// binary, the installer and the tag cannot disagree. It never writes to main
// (the branch ruleset forbids it; tags are exempt), which means that between
// releases the PATCH below is a FLOOR rather than a fact. A human bumping MINOR
// for a feature (with the paragraph above it, as here) is what SHOULD happen
// and resets that sequence; the automatic patch is only the floor that stops
// main from sitting unreleased.
//
// AND THE EDITOR CHECKS FOR ITSELF (update.cpp / update_ui.cpp, Help > Check
// for updates). One HTTPS request to the repository's releases at startup, on a
// worker thread, through curl the way aigen.cpp already reaches the OpenAI API;
// a modal only appears when there IS something newer, "Download and install"
// runs the new installer silently and comes back, and the whole thing is one
// checkbox away from off in Edit > Preferences. The failure of a startup check
// is deliberately silent - an editor that opens a dialog because the machine is
// offline is an editor people turn the check off in.
//
// MINOR by this file's own rule: three capabilities appear, nothing changes
// shape for an existing project (both new settings are editor.ini, not the
// .tyra - kFormatVersion is untouched).
//
// 1.50.0 (the ground bake stops shadowing itself, and three switches start
// doing what they say): a round of reports off the 1.49.0 build.
//
// THE GROUND WAS SHADOWING ITSELF, in a lattice of dark blotches nobody could
// place. Two causes, both the same mistake - a gather ray fired from a point
// that is not on the surface the rays are traced against.
//   1. The terrain map's SUB-SAMPLES inherited the texel centre's height while
//      moving up to half a texel horizontally. On any slope that puts the
//      sample under the ground; the whole hemisphere hits terrain and the texel
//      bakes black. Re-sampled now (aobake::terrainAOMap).
//   2. The traced ground is a DECIMATED mesh (gibake caps it at 96x96 cells)
//      while the bake hands out points on the fine bilinear heightfield the
//      game walks on. Wherever the decimation cuts a bump, the point sits under
//      the triangles - and the residue showed up along the coarse cells'
//      DIAGONALS, which is what named the cause. Scene::coarseH keeps those
//      corner heights and gibake::groundSurfaceY reads the traced height back,
//      so the ground's light function snaps its origin onto the surface the
//      BVH actually has.
// Measured on the reporter's showcase: texels under 8/255 went 0.1% -> 0.5% ->
// 0.0% across the two fixes (the middle number is fix 1 alone uncovering the
// second cause), map alpha mean 95.5 -> 86.7, and the map now reads as relief
// shading plus real tree and village shadows with no lattice in it.
//
// A CHECKBOX NEVER REPORTS IsItemDeactivatedAfterEdit, and three of them were
// asking. A checkbox activates on mouse-down and both edits and deactivates on
// mouse-up, so "was edited while active in a previous frame" can never be true.
// Fog enabled, Gradient sky dome and the VU stage's Enabled were all relying on
// it. The Ambience window's section-JSON comparison happened to catch the first
// two, so nothing was lost - but that is a backstop, not the contract.
//
// GI OFF NOW LOOKS LIKE GI OFF. gibake::load already refuses to answer while
// the switch is off, but nothing asked it again: the viewport's cache key had
// scene / model-edit / bake-version in it and not the preference, and
// commitChange does not touch the viewport. Unticking the box left the baked
// light on screen until the scene changed. Measured: 66.7% of the viewport
// changes across the toggle now, 0.08% before.
//
// AND View > DISTANCE FOG IS PROJECT STATE, like the camera in 1.47.0 - the
// same report, and the same answer. It is the VIEWPORT's fog switch, not the
// scene's fogEnabled: it suppresses the preview of a fog the game still has, so
// you can author past it. Resetting it on every open reads exactly like a
// setting that was not saved. kFormatVersion 30 -> 31, purely additive.
//
// The Ambience Editor also loses two paragraphs it should not have had: the
// "What this does not do" wall in the GI tab (five lines of routing caveats
// that went stale the day the ground's route changed - that story lives in
// docs/global-illumination.md, which the in-editor assistant reads), and the
// read-only "Ambient occlusion - edit it in the Baked lighting tab" echo left
// behind by the 1.48.0 move. Moved is moved.
//
// 1.49.0 (a textured ground takes its GI as a MULTIPLY): the last third of the
// black-hills report - the peaks stopped being black in 1.47.1, the grid
// stopped clipping them in 1.48.0, and what was left was a ground that BANDED
// along contour lines because a volume probe grid was being asked to light a
// surface. A probe sample crosses a level as the terrain rises, and the probe
// just above the grass sees mostly ground bounce where the next one up sees
// sky. No amount of grid tuning fixes that; the surface wants a surface answer.
//
// It could not have one, because the ground's per-texel light is an ADDITIVE
// pass and a flat add over a texture blows out its dark texels. The way
// through is a frequency split, and its shape is forced by the hardware:
// GS_SET_ALPHA(A,B,C,D,FIX) computes (A-B)*C>>7 + D, and C may only be As, Ad
// or FIX - never a colour - so Cs*Cd is inexpressible and no pass can multiply
// the frame buffer by a coloured lightmap. But the OCCLUSION pass already
// multiplies by an alpha. So the bake writes the gathered light's luminance
// into that alpha (AoImage::giLumAlpha) and the terrain keeps its ordinary
// directional shade for colour: intensity per pixel, colour per vertex, no new
// table, no new pass, no pixels on the EE. SCENE_AO_MAP_GILUM says which
// meaning the channel carries, and it opens the occlusion pass on its own -
// the pass must run even with ambient occlusion switched off, because there
// the channel is light.
//
// THE TWO ROUTES ARE EXCLUSIVE, and the flags are where that is enforced
// (mapLit/mapGi are written off `&& !giLumAlpha`). Shipping both at once is not
// a subtle bug: LIT still on runs the additive pass over the texture and washes
// the ground to a flat wash with no texture left in it, which is exactly what
// the first console boot of this route showed. On the multiply route the map's
// RGB is never read, the emitters are not collected per chunk, and the point
// lights and emissive pools are skipped - the gather already contains them.
// The terrain's own AO goes with them, in the viewport too: that alpha channel
// is the light now, and the gather answered the sky-visibility question AO
// approximates.
//
// Verified on the reporter's saved showcase, rebaked: viewport and PCSX2 agree,
// the ground is textured and softly shaded across an eight-frame turn-and-walk,
// no black patch, no banding, 50.0/50 FPS held, VRAM 3.11/4 MB (+0.23 MB - the
// AO map, now uploaded in a scene whose ambient occlusion is off). The two GI
// examples are untextured ground and take the RGB route unchanged; their bakes
// are re-run only because the cache version moved 4 -> 5.
//
// 1.48.0 (the probe grid reaches the top of its terrain, and every bake is in
// one tab): the black hills, and the AO controls' new home.
//
// THE GRID. probeLevels was taken literally - anchored half a step above the
// LOWEST ground and rising a fixed levels*probeHeight from there, whatever the
// terrain did. On real relief the hills came out ABOVE the whole grid, the
// sampler clamped them onto its top layer (over a hill: buried inside that
// hill) and the ground shaded BLACK. Measured on examples/showcase: terrain
// -6.45..+7.88, grid -5.3..+0.7, 15.9% of the ground surface sampling to zero;
// after, 33x9x65 and 0.0%. The count is decided BEFORE the kMaxProbes cap, so
// a tall terrain thins X and Z rather than silently losing the levels that
// stopped the ground being black.
//
// SCENE AO MOVES to Ambience Editor > Baked lighting, beside model AO and
// pre-lit. It is still a per-PRESET setting and the section carries its own
// preset picker so that stays visible; the Presets tab keeps a one-line
// On/Off pointer. The tab's premise is rewritten with it - its sections do not
// share a scope and never did, they share the question "what is baked into
// this project's light".
//
// STILL WRONG, and written down in docs/global-illumination.md rather than
// left as a surprise: a volume probe grid lighting a SURFACE bands along
// contour lines. The ground sample crosses a probe level as the terrain rises,
// and a probe just above the grass sees mostly ground bounce where the next
// one up sees sky. The black is gone; the banding is not. (Fixed in 1.49.0 -
// by taking the ground off the probe grid entirely, not by tuning it.)
//
// 1.47.1 (the ground never takes probe light): reported as "with GI on the
// peaks are pitch black", and it was the editor preview alone - the generated
// game never had it.
//
// A TEXTURED terrain deliberately gets no GI lightmap: the ground pass is
// additive and would blow out the texture's dark texels, so gibake passes no
// light function for one, and with the scene's ambient occlusion also off
// terrainAOMap returns an EMPTY image. Correct so far. What was wrong is that
// the viewport only skipped the probe grid when a ground lightmap existed, so
// such a terrain fell through to giProbe - which REPLACES the shade instead of
// adding to it, and which is a grid built for objects, a few levels a few
// units apart. Handed a 192x192 landscape it has nothing to say, so every hill
// went black. Measured on the reporter's own saved project: viewport mean RGB
// 114/80/33 with GI off, 85/54/30 with GI on, and 114/80/33 after.
//
// The diagnosis is worth more than the fix. gibake::load returned valid=1 with
// terrain size 0 / hasLight 0, which is what said the map was absent BY DESIGN
// rather than broken - and the generated game reads terrainGi = terrainMapLit
// && SCENE_AO_MAP_GI and never consults the probes for ground, which is what
// said the console was fine. PATCH: no capability changes, a preview stops
// lying.
//
// 1.47.0 (the viewport remembers where you were looking, and stops repainting
// untextured models): two reports, both about the editor disagreeing with
// itself or with the console.
//
// THE VIEWPORT CAMERA IS NOW PROJECT STATE. The .tyra carried the render mode,
// the projection, the selection and the gizmo - everything about the viewport
// except where it was pointing - so every reopen started at a default 90 units
// out, which on a scene with distance fog ending at 82 is a flat wall of fog
// colour. It is the five numbers the orbit camera IS (yaw, pitch, distance,
// pivot), read off the viewport at save time exactly like viewMode, never
// dirtying the project and never entering undo. kFormatVersion 29 -> 30,
// purely additive: a file without the key opens at the viewport's own
// defaults, which is where it always opened.
//
// It also makes a scene SETTABLE from outside the GUI, which is what it was
// asked for: an agent or a script can put the camera on the thing it needs to
// photograph instead of describing where to drag.
//
// AND AN UNTEXTURED ANIMATED MODEL KEEPS ITS OWN COLOUR. AnimModelDraw::Part
// carried a mesh and a texture and nothing else, so glTF baseColorFactor was
// dropped and the model was drawn in the scene light alone. On
// examples/showcase - whose wobbler is teal by that factor and has no texture
// at all (baseColorFactor [0.15, 0.72, 0.62], images: none) - it came out
// ORANGE in the editor under a sunset preset while the console drew it green.
// Reported as exactly that. The parser already read the factor; only the
// viewport's own Part struct threw it away.
//
// MINOR: one new persisted key and a preview that changes colour.
//
// 1.46.0 (one occlusion model, two regimes, one constant): the response
// finished in 1.45.0 was a disc, and a disc has to be TOLD WHICH WAY TO POINT.
// Both ways of telling it fail on real geometry, and both were measured on
// examples/ambient-occlusion: aimed at the shape's nearest point the floor
// beside a wall reads 0.000 occluded (the wall touches it edge-on and the
// cosine falls out), and aimed at the shape's centre a crate standing on a
// 30x24 terrace reads 0.66 occluded ON ITS SIDES, because that terrace's
// centre is ten units sideways. The second one is what the 1.45.0 shipped, and
// this fixes it.
//
// Near and large, a shape is not a disc - it is a HALF-SPACE, and a half-space
// needs no aiming: it blocks the hemisphere behind its face, (1 + n.toOcc)/2.
// The two regimes blend on k = sin(alpha) = r/(r + dist), scaled by k*k, the
// solid angle. BOTH FACTORS ARE NEEDED: blending on k alone let a crate 0.6
// units away - 27 degrees, a speck - hand a horizontal surface the plane's
// 0.5, and a ring of neighbours summed to half the sky gone on a crate top
// with nothing above it (0.500 measured; 0.140 now).
//
// THE GROUND TERM TURNS OUT TO BE THAT SAME HALF-SPACE with toOcc pointing
// down - (1 + n.toOcc)/2 is (1 - n.y)/2, exactly the 0.5 - 0.5*n.y it always
// carried. It was never a separate model, only a separate spelling with its
// own constant, which is how the two drifted. One shape, one spelling, and one
// number left between the geometry and the picture: kAoBounce (0.7), applied
// once over everything, replacing the ground term's 0.7 AND the occluder
// term's unrelated 0.35 facing floor.
//
// The reported case, on the console at one frozen vantage - brightness of a
// crate with another crate on it against its uncovered neighbour: 0.87 with AO
// off (the natural difference), 0.78 before this branch, 1.04 with the disc
// alone, 0.98 now. 50 FPS. MINOR: every scene with AO looks different again.
//
// 1.45.0 (an occluder darkens you by how much sky it takes, not by how close
// it is): occluderOcclusionAt was (1 - dist/radius)^2 times a facing weight
// with a 0.35 FLOOR, so a surface turned away from a shape it can barely see
// kept a third of the term, and anything smaller than the AO radius darkened
// over its whole height as a lump. It is now the solid angle the shape
// subtends - cos(theta) * (r/d)^2 with r from the projected area and the disc
// placed tangent to the nearest surface point - and blockers combine as
// VISIBILITY, 1 - prod(1 - occ), instead of a clamped sum that saturates.
//
// Measured on the console, examples/ambient-occlusion: a crate with another
// crate resting on it read 0.78 of its uncovered neighbour's brightness where
// the AO-off scene reads 0.87 - a visible step between two crates 20 cm apart.
// It now reads 1.04. The covered crate's SIDES went 0.22 -> 0.000, which is
// the right answer rather than a suppression: the crate above lies entirely
// behind the plane of those faces. Contact shadows got stronger where they
// belong (floor beside a wall 0.247 -> 0.603). Existing scenes barely move:
// gi-showcase terrain alpha mean 60.9 -> 59.6.
//
// TWO OF MY OWN ERRORS, both caught by measuring rather than by reading the
// formula: aiming the disc at the shape's nearest point reads the floor beside
// a wall as 0.000 occluded (the wall touches it edge-on and the cosine falls
// out) - it is aimed at the midpoint of the nearest point and the centre; and
// an uncapped disc collapses a 26-unit wall into radius 5.15 against the
// surface for 0.70, where a half-plane at contact can block about 0.45 - r is
// capped at the AO radius, which is also the radius the bake prunes by.
//
// The GROUND term is deliberately untouched and is now what decides how dark a
// small prop gets; docs/backlog.md says why going fully physical there needs
// measuring first. MINOR: every scene with AO looks different.
//
// 1.44.0 (ambient occlusion: runtime blocks, and a terrain scan that stops
// shading bare slopes). Three things, and the number is a MERGE renumber - the
// branch stood at 1.35.0 while main took 1.42.0 and then 1.43.0, and the rule
// of this block takes the MINOR above both rather than picking a side.
//
// Runtime blocks self-occlude off the solid-cell field a Blocks Fill volume
// already publishes: 26 bit tests per block at generation time, reduced per
// visible face to four corner levels, riding the selfAo byte pushVert already
// takes - so the scene's own AO strength scales it and a scene with AO off
// computes none of it. THE TRAP WAS THE SHADING, NOT THE AO: generated chunks
// draw TyraShadingFlat, which takes one corner of a triangle and paints the
// whole triangle, so the first console build split every block face into two
// flat plateaus 42 levels apart. Hard adjacent-pixel steps over the frame read
// 2783 / 6964 / 2868 for AO-off / AO-on-flat / AO-on-Gouraud.
//
// The terrain horizon scan gets the term that stops a BARE SLOPE shading
// itself - the horizon measured above the surface's own tangent plane rather
// than above the horizontal - because every uphill sample is higher than the
// last and a smooth open hillside was darkening for being a hillside: 16% at
// 30 degrees, 30% at 60, no occluder anywhere. Both read open now while the
// foot of a step is unchanged. Also 16 azimuths instead of 8 (a lone spire's
// ring standard deviation 91% -> 30% of the mean) and an occluder GRID instead
// of scanning every occluder per sub-sample (32.0 s -> 61 ms on 1100 casters,
// and byte-identical output on every existing example).
//
// A per-texel azimuth rotation was implemented, measured and REMOVED - the
// scan is one sample per texel with nothing downstream to average it, so it
// decorrelates the error without reducing it. MINOR: behaviour changes for any
// project with sculpted terrain, and examples/ambient-occlusion is the first
// one in the tree that has any.
//
// 1.43.0 (the pre-release legacy comes out, and version::kMinFormatVersion is
// what replaces it): TyraX has never shipped publicly, so every translation the
// reader carried for a shape that changed on its way to v1 was weight nobody
// could ever spend - objects inline in the manifest instead of objects/<id>.json,
// a single "layout" dump, a project-level terrain block and flow graph, raw TTF
// paths where a font name now goes, "terrainTex", "stickDeadzone",
// "hudPostFxLayer", the one-day-old VU "programs" key, and the twelve retired
// Show*/Hide* flow-node types (flowLegacyNodes). Gone with them: the verbatim v1
// game templates kept only so matchesLegacy could recognise an unedited copy,
// the "Generated by tyra-editor" pre-rebrand ownership marker, the pre-rename
// TYRA assert banner, objparser's unused flat loader and the one-number
// "# tyra-glow" hint. The removal is DELIBERATE rather than silent: a file below
// kMinFormatVersion is refused by name, because a reader that recognises nothing
// in it would otherwise open an empty project and say nothing about why.
// Verified by resaving all 34 examples - byte-identical apart from the version
// stamps - and by A/B-ing --refresh-gen against a pre-change binary in the SAME
// directory (docker-compose.yml embeds the project path, so two directories
// manufacture a false diff): the only generated change anywhere is that a
// display-mode menu row now always carries its option->mode table instead of
// falling back to the positional map when the table was absent, and the table
// codegen emits for such a row is exactly that map. MINOR: the Cutscene
// Director's "Shot from" combo gains the Free shot entry it never had - free
// shots are what the take importer and the phone-camera recorder write, so
// calling them legacy and offering no way back to them was a one-way mis-click,
// not a deprecation. (Authored as 1.34.0 against a 1.33.0 main and RENUMBERED
// TWICE on the way in, which is this file's own rule and not an accident: main
// reached 1.33.1 and then 1.42.0 while this branch was open, so the MINOR
// strictly above both parents is 1.43.0. The format number moved under it the
// same way - see kMinFormatVersion's note.)
// 1.42.0 (the editor stops flattering you about lights): three preview
// gaps, all reported with a screenshot. The bulb gizmo is a small constant
// MARKER now instead of a unit-sized glow that hid the very point it marks;
// a spot light draws its actual CONE (apex at the light, opening down the
// aimed -Y for the reach) instead of a radius sphere that said nothing
// about direction; and the viewport lights shade the GAME's way - spots
// use the cone term with no N.L (exactly the VU1 slot's trade), and a
// dynamic light darkens only through its nearest FOUR Cast-shadow
// (projected) objects, hard-edged and quantized to the coarseness of the
// 64x64 silhouette the console samples. The editor used to raytrace
// nothing for scene lights and everything for emissives, which is how "it
// looks amazing in the editor, then surprise" happened. And Live Link
// learns lights: protocol v4 streams a DYNAMIC light's pose, color,
// brightness, radius, flicker and spot angle (the record's player-speeds
// slot, reused - types never collide - plus the tail pad), so aiming a
// lamp is a live drag instead of an amber chip. Baked lights still
// rebuild (vertex colors own them), as do the dynamic flag, the beam and
// the spot style (setup-time bags/textures).
//
// 1.41.0 (a scene light can be the flashlight's kind of light): dynamic
// point lights gain a SPOT style (format v29: "spot" + "spotAngle" in the
// light object, written only when on - old files resave byte for byte).
// The cone points down the object's local -Y (unrotated = straight down, a
// street lamp; the rotation gizmo aims it), lights nearby meshes per vertex
// through the same engine slot the camera torch uses (new
// RendererCore::addDynSpotLight - the registry entry always carried the
// cone constants, nothing changed on VU1), and its footprint on the ground
// is the flashlight's projection on a scene light: the pool patch takes
// the gobo's projective STQ from the LIGHT's frustum instead of the round
// corona, so a lamp's pool is per-pixel however coarse the ground is.
// night-walk's street lamp now actually lights its street (with a 0.12
// flicker and a corona). Spot pools march the cone axis to the ground and
// size the patch from the cone's footprint at the landing.
//
// 1.40.0 (a caster's shadow follows its shape, not its bounding box): a
// model now casts from up to three TIGHT sub-boxes fitted to its triangles
// - median split on the longest axis, twice, then leaves greedily merge
// back wherever the split bought nothing (a solid crate collapses to one
// box; an L-shape stays a pole and an arm). Built lazily per model asset,
// local space, shared by instances (g_shadowSubBoxes). This retires the
// volume pick's thin-skip: a tight thin box (a sign, a pole) casts its
// honest stripe now - the street lamp's shadow is its POLE again, not the
// pole-plus-arm slab of air that blotted out a facade. Each sub-box gets
// its own mask bracket because set/clear is only sound inside one CONVEX
// volume - the GS cannot count like a stencil, which is also why true
// mesh-shaped volumes (SH2's bed slats) need the era's full arrangement
// (count in a spare color channel with add/sub blending + a resolve pass)
// and are left as the named next step.
//
// 1.39.3 (thin things are transparent to the torch, in all three systems):
// stand exactly on the street lamp's axis and the light died completely -
// half a step sideways brought it back (reported, with the exact spot). The
// lamp is a thin POLE, but its AABB - pole plus arm - is a big slab of
// mostly air, and two systems still trusted that box: the volume pick cut a
// shadow from it (on-axis, a slab three units from the lens blots out the
// whole facade), and projWallHit called it "the wall the beam hits", which
// then stuffed it into a guaranteed receiver slot. The 0.25 thin rule the
// receiver scan already had now applies to all three: thin boxes cast no
// volume, projWallHit sees through them to the surface behind, and the
// guaranteed hit-slot inserts at its SORTED position (the interleaved walk
// merges the receiver and caster lists by distance - an unsorted insert
// drew a nearer light after farther volumes).
//
// 1.39.2 (nothing can shadow itself, and the toggle stops strobing the old
// look): two more reports from the same yard. The shed went black in the
// beam ("swallows the light like a black hole") because a model's AABB
// stands proud of its real walls - the roof overhang - so the shed's own
// volume's near cap floated in front of the wall the beam lit; no cap
// geometry fixes that (the radial push is tangent to a big face up close),
// so the ORDER does: casters and receivers walk together sorted by distance
// and each receiver's light draws BEFORE its own volume enters the mask
// (RendererCoreAlphaMask::beginKeep - one bracket per caster, only the
// first clears). A volume only shadows what is behind its caster, so
// nearest-first is the dependency order and self-shadowing is structurally
// impossible; the truck still carves the facade behind it. And spamming the
// torch toggle strobed the OLD per-vertex look for one frame per enable:
// the receivers' cone-off flags are computed in the light-pool pass, AFTER
// the scene has drawn, so the enable frame hit every big receiver with the
// full blocky cone once. The engine spot now arms one frame after the
// toggle - the projected pool lights the same frame, only the cheap cone
// waits, and on the props that keep it one frame is invisible. Verified in
// PCSX2: 12 toggles x 70 snapshots, four tight byte-size clusters, zero
// outliers - and the shed takes the full gobo in volumes mode.
//
// 1.39.1 (the volumes learn who actually casts, and the mask stops leaking
// onto the screen; renumbered from 1.38.1 when the lighting redesign took
// its slot): three reports from the reworked backlot. Volume slots
// go NEAREST-FIRST (they went in object-table order, and the scene's three
// merged facades - each huge enough to intersect the cone whenever the beam
// faced them - ate all of them, so the dumpster and the truck never cast:
// "no dynamic shadows at all"); a THIN receiver (the street lamp) no longer
// claims a light slot nor gives up its cone, and the box the beam actually
// HITS is guaranteed one (standing by the lamp used to unlight the facade
// behind it); and the destination-alpha mask is REPAINTED to neutral 0x80
// after the last DATE pass - the SDTV flicker filter blends its two read
// circuits by per-pixel framebuffer alpha, so a mask left in the channel
// was shown by the CRTC as translucent wedges (the "broken triangles" at
// torch toggles, caught by frame-stepping PCSX2). The repaint runs from its
// OWN packet2: sharing begin()'s buffer let a FINISH-parity slip rebuild a
// packet the GIF was still fetching, which killed the light entirely.
//
// 1.39.0 (the lighting redesign: baked light gets one home, and a textured
// model finally occludes itself; authored as 1.38.0 - the examples split
// took that number first, and the claim that arrives second renumbers).
// Lighting had accumulated four separate
// places - AO in the ambience presets, model AO by hand in the Material
// Editor, GI in its own tab, and a per-object pre-lit button in Properties -
// and the automatic half of that did the least for the thing a real game is
// mostly made of, TEXTURED MODELS. The engine's lightmap route refuses them
// (it is additive, and an additive term over a texture blows out its dark
// texels) and GI reaches them only as flat per-vertex probe light, so an
// imported model has never had any self-occlusion at all.
//
// AUTOMATIC MODEL AO (docs/ambient-occlusion.md, "Model AO", format v28 -
// authored as v26; this branch's base took v26 and then v27 while the
// redesign was in flight, and the claim that arrives second renumbers): the
// Material Editor's matbake AO, run per model ASSET without anybody asking,
// and multiplied into the texture that model ships anyway. Two properties are
// what make it affordable, and both fall out of WHAT is being baked rather
// than out of any cleverness: a model's own surface occlusion is
// TRANSFORM-INVARIANT, so every instance of the asset shares one map wherever
// it stands; and the pixels ride in an existing texture, so it costs ZERO
// extra GS VRAM - against one unique texture per object for the pre-lit route
// next to it. src/modelao.cpp owns the bake, the content-hash cache in
// .res-baked/modelao/ (the gibake rule: never mtimes, and never the texture's
// PIXELS - AO is a function of geometry and UVs, so repainting must not throw
// a bake away), and - the part that matters most - the MULTIPLY. That one
// function is called by texbake for the shipped PNG and by the viewport for
// the uploaded pixels, so what the editor shows and what the console draws
// cannot drift.
//
// WHAT IT REFUSES TO DO IS THE DESIGN. A texture referenced by more than one
// model asset is skipped and SAID SO, because two UV layouts over one image
// make a single multiply wrong for both; so is a pre-lit material, whose
// gather already contains occlusion and would be darkened twice. Both show up
// as a named row in the panel and a line in the build log - an AO map that
// silently is not there is indistinguishable from a broken feature. And
// litbake now multiplies the same map into the albedo it reads, so an object
// does not lose its self-AO the moment it goes pre-lit.
//
// PRE-LIT MANAGEMENT (docs/prelit-models.md, "Managing pre-lit objects", the
// same format v28): 1.35.0 gave a textured model per-pixel static light through
// one button per object, and left everything around that button to memory - no
// record of which objects were supposed to ship pre-lit, no way to know that a
// texture had stopped agreeing with the scene, no bulk operation, no way back.
// Three SceneObject fields close that: prelitWanted (the author's statement),
// prelitSig (what the last bake SAW) and prelitSource (the material to revert
// to, recorded on the FIRST bake only, an asset path that joins
// retargetAssetPath). All three are written only when they say something, so an
// object that never met the baker resaves byte for byte.
//
// THE SIGNATURE IS THE FEATURE, and the load-bearing decision in it is what it
// deliberately does NOT see. It mixes gibake's own scene signature, the
// object's transform, the model and its .mtl libraries by content, the bake
// parameters and - when Model AO resolves on for the asset - that map's
// signature, since it is multiplied into the albedo. But the scene half hashes
// the scene AS AUTHORED, with every pre-lit override normalized back to its
// source material: gibake::signature hashes each object's materialPath and that
// file's bytes, so without the normalization applying a bake would change the
// scene signature and make the object it just baked read STALE on the next
// frame, together with every other pre-lit object beside it. The price is that
// bounce light off a neighbour's new pre-lit texture stales nothing, a
// second-order term nobody would want a re-bake storm for.
//
// The batch baker builds and solves the gibake scene ONCE per scene and bakes N
// objects from it (that solve is nearly all of the wall clock), reports "2/7:
// crate-3", cancels, and lands as one undo step through App::litBakerPoll -
// polled from drawUI, so a batch started from the tab arrives whether or not
// the tab, the selection or Properties is still showing it. --bake-prelit is
// its headless twin: it re-bakes every stale wanted object and says `fresh` for
// the rest, so running it twice is the check that the tracking is honest. The
// three of them - the tab's button, the verb and the OPT-IN pre-build pass
// (ProjectSettings::prelitAutoBake, Preferences > Build) - are one loop,
// litbake::bakeStale, so a build cannot bake something the tab would have
// called fresh. Off by default: the gibake rule that an expensive bake is
// pressed, not implied, still stands, and only STALE objects are ever touched.
// GI gets the SAME opt-in (ProjectSettings::giAutoBake, gibake::bakeStale -
// stale scene caches re-baked before the pre-lit pass), because the silent
// alternative had already bitten twice: a stale cache drops a whole scene to
// the pre-GI lighting without a word. And gibake now reads a pre-lit object's
// SOURCE material (albedoMaterial) in both build() and signature(): a -lit
// texture is albedo x light, reading it as albedo doubled the light in the
// bounce, and every pre-lit bake used to stale the GI cache by repointing the
// object's materialPath.
//
// ONE HOME: a "Baked lighting" tab in the Ambience Editor, reachable from
// Tools > Baked Lighting..., which is where the scene's light was already
// authored - Model AO (per ASSET, free) and the pre-lit table (per SCENE, one
// texture each, with the VRAM line stating what that costs) as two sections of
// it. The Material Editor's manual bake is untouched and gains one line
// pointing at the automatic path.
//
// MINOR: capabilities appear (a textured model can occlude itself, for free;
// pre-lit objects gain staleness, batch baking and a Revert; --bake-model-ao
// and --bake-prelit are new headless verbs). No existing project's look moves -
// modelAo is false in the struct, which is what every file saved before it
// loads as, and true only for projects created from here on; the three pre-lit
// fields are pure bookkeeping and reach no codegen at all.
//
// 1.34.0 (the flashlight stops being drawn by the terrain's vertex grid, and
// the ground gets distance detail): two halves of one report - a torch on a big
// map looked bad, and the proposed cure was a finer heightmap near the player.
// The second half is built here as its own feature, because it is a good answer
// to a big map and NOT the answer to the torch.
//
// WHY NOT: a terrain cell can never be finer than one world unit
// (sceneGridDims caps cells at the map's own width in units), and the VU1 spot
// is per vertex with no N.L, so the cone on the ground is a Gouraud diamond
// whatever the detail cap says. A footprint two units across gets two vertices.
// Aiming at your own feet had lit nothing at all, which is why the ground POOL
// existed in the first place - a flat round patch under the beam's terrain hit,
// textured with the lens-flare corona, radius capped at 8 units.
//
// THE FIX IS PROJECTION, and every part of it was already in the tree: the
// receiver patch now takes its STs from the beam's own frustum, exactly the way
// renderProjShadows samples a silhouette through a light view-proj, so the
// light's SHAPE is a texture and the ground's tessellation stops being able to
// decide it. With that, three long-standing approximations go: the patch is
// laid out along the beam's ground run instead of axis-aligned (a grazing beam
// really does reach four times further than it is wide), the radius cap is gone,
// and it lands on placed geometry as well as terrain via projCollectReceivers -
// so a torch works in a room built out of floors, where a scene with no terrain
// at all used to have no pool by construction. The image itself is a baked
// 128x128 gobo (menubake::bakeFlashGoboRGBA - hotspot, penumbra, reflector ring,
// two low-frequency lobes so the circle is not perfect) instead of the corona
// sprite, gated by FLASHLIGHT_USED so a project without a flashlight pays no GS
// VRAM, and the authored Pool texture override keeps working - as a real gobo
// now, which is what its own documentation always claimed it was.
//
// DISTANCE DETAIL (docs/terrain-lod.md, ProjectSettings::terrainLodDistance,
// format v25): beyond the set range a terrain tile is built from every 2nd
// heightmap sample and beyond 2.2x it from every 4th - a quarter and a
// sixteenth of the triangles. The load-bearing decision is that the stride is a
// PURE function of the snapped view focus, so a tile can work out what its
// neighbours are doing without asking whether they are resident, and the finer
// side of a shared edge interpolates its vertices onto the coarser side's
// segment. That is what makes cracks impossible rather than merely rare, and it
// adds no geometry - skirts, the usual cure, add a quarter as much again to the
// tiles that can least afford it. The shade is interpolated with the height,
// or the closed hole leaves a colour seam in its place. Collision is untouched:
// every height query reads TERRAIN_HEIGHTS, never the mesh.
//
// MINOR: capabilities appear (a setting that did not exist, and a flashlight
// that can light a floor). The gobo is not a default change - it replaces a
// sprite that was never the right one - but the LOD key IS written into every
// project's settings block on its next save, hence the format bump.
//
// 1.38.0 (one example was proving two features, so now there are two): the
// night-walk example is split. deep-forest takes the scale story - the same
// 2048x2048 map in daylight with 2800 scattered spruces, held at 50 FPS by
// terrain detail distance + mesh LOD + chunk draw distance (2800 is measured:
// 3777 instances died in the chunk build's loading peak, 3100 ran at
// 30.7/32 MB, 2800 ships with headroom at 28.1). night-walk keeps the torch
// and becomes a dark kenney-kit backlot (CC0 Retro Urban Kit) built to be
// read by torchlight: brick facades, a dumpster and a truck for casters, the
// pre-lit shed pair, the west facade turned 24 degrees for the oriented-box
// receivers. The facades are kit tiles MERGED into one .obj each, because
// the torch lights the nearest three solids in its cone - a wall of twelve
// tile objects would light in patches. No engine or format change.
//
// 1.37.0 (the torch's shadows learn self-shadowing, and the technique becomes
// a choice): ProjectSettings::flashShadowVolumes (format v27) picks how the
// flashlight occludes. OFF keeps the silhouette slots below; ON is the
// survival-horror era's own arrangement, built on its own hardware trick:
// every occluder box in the beam is extruded away from the torch into a
// closed volume, the volume's camera-front faces SET the framebuffer's
// DESTINATION-ALPHA MSB where they beat the scene's depth and its back faces
// CLEAR it where they do - plain TestOnly z is the entire algorithm - and
// every torch light pass then draws with the GS's destination-alpha test
// (TEST.DATE), i.e. only where the mask says lit. The mask gates LIGHT;
// nothing ever paints darkness. Occlusion is exact per pixel against the
// real z buffer, for EVERY solid in the beam, self-shadowing included, with
// no caster flag and no four-slot budget; the price is the volume fill and
// box-shaped rather than mesh-shaped silhouettes. Engine: a new
// RendererCoreAlphaMask bracket (FBMSK to alpha-only + full-raster alpha
// clear with z writes masked) and PipelineInfoBag::dateLit riding the same
// in-band TEST qword every mesh already emits. Both re-render passes also
// gained a per-triangle FACING cull (orientation from the object's centre -
// an .obj's winding is nobody's promise), which is what stopped a box's far
// side sampling a lit texel and a wall's inner face taking the silhouette.
// MINOR: a capability and a setting appear; the default reproduces 1.36.0.
//
// 1.36.0 (the torch throws shadows, and its light stops picking favourites):
// the flashlight becomes a candidate light in the projected-shadow system - a
// caster in the beam renders its silhouette FROM THE TORCH'S POSITION into a
// shadow-map slot, the ground patch samples it as always, and the wall behind
// the caster is re-rendered with the silhouette through the light's view-proj,
// per pixel: the Silent Hill composition, on the machinery that was already
// there. Three findings paid for it: the torch needed a laxer elevation bar
// than fixed lights (it is carried level with everything, and its shadow's
// whole point is the WALL - the ground patch is simply skipped when the ray is
// too flat); it needed a LINE-OF-SIGHT check, because a light that walks
// around routinely stands on the wrong side of a wall from a caster, and the
// silhouette painted straight through; and the light pass had to stop lighting
// only the object the beam HITS - the wall behind a caster stayed dark (a
// shadow with nothing to be carved from), and a shed with the beam at its feet
// took no projected light at all and fell back to the per-vertex cone's hard
// triangles. Receivers are now the nearest three solids whose oriented boxes
// the CONE touches, drawn from one bag. MINOR: capabilities appear; no default
// moves; the format is untouched by it.
//
// 1.35.0 (per-pixel static light on a TEXTURED model; authored as 1.33.0 and
// renumbered on the merge below - main took 1.32.0 with #230 while this branch
// was away, so both entries here move up one, the standing arrive-second rule): the answer to "Silent
// Hill had textured models and it looked fine", which is a fair objection to
// everything the flashlight work had said up to then. The engine's lightmap
// route is per texel and refuses textured surfaces, and that refusal is
// hardware: the GS blend unit computes (A - B) * C + D with C always an ALPHA,
// so "texture times lightmap" cannot be expressed in a second pass at all, and
// the additive atlas this engine does have blows out a texture's dark texels.
//
// Which leaves the era's own answer: bake the light INTO the albedo and ship a
// unique pre-lit texture for that surface. Both halves of the machine were
// already here - gibake computes the light over a triangle BVH of the whole
// scene, matbake showed how to rasterize a model's UV space - and what was
// missing was the join. src/litbake.cpp walks the object's UV islands, turns
// each texel into a WORLD position and normal through the object's transform,
// asks gibake what arrives there, multiplies it into the albedo and writes the
// object its own material. SceneObject::prelit (format v26) then switches that
// object's vertex colours to neutral, because every term they used to carry is
// in the texture now and adding it again lights the surface twice.
//
// The dynamic half still lands on top, which is the whole arrangement: static
// light per pixel in the map, the flashlight's projected pool and cone added
// over it at run time.
//
// MINOR: a capability appears (--bake-object-light, and a route to per-pixel
// static light that textured geometry never had). No default moves - prelit is
// false everywhere until a bake sets it.
//
// 1.24.4 (--vu-check says when its two halves are not from one commit): every
// comparison it makes diffs a program GENERATED from the descriptions compiled
// into the binary against the HANDWRITTEN .vclpp on disk, so the two are only
// comparable at one revision - and the documented attribution trick of pointing
// it at another commit's engine swaps exactly ONE of them. Against a stale exe
// that MANUFACTURES failures rather than attributing them: measured, a pre-#218
// editor on post-#218 engine sources reports 7 DIFFERENT programs plus the
// matcap identity-at-zero, every one of which passes when each half runs against
// its own peer. It now prints `note: FOREIGN engine` when the engine is not the
// one beside the executable, `note: ... is NEWER than this executable` when a
// framework source outran the build, and a paragraph under FAIL naming the skew.
// PATCH: no capability appears, a failure becomes readable.
//
// 1.24.3 (the shipped default net is refitted, and CI stops asserting a
// property of one machine): the net embedded in the editor was fitted before
// examples/upscaler-lab was rebuilt on CC0 assets - and upscaler-lab is one of
// the seven projects in its corpus, so the recipe stopped producing the shipped
// bytes the moment the geometry changed. It was a KNOWN deferral (docs/
// backlog.md said so, and the PR description listed it as owed); what is new is
// that the blss-default-net workflow's first run collected the debt by failing.
// Refitted with the identical recorded command: md5 879146bd -> 6a93196c, final
// loss 0.042956, and the .meta came back byte-identical - the recipe never
// moved, only the corpus content under it.
//
// AND THE CHECK THAT CAUGHT IT CANNOT BE THE CHECK CI RUNS. Same tree, same
// command, three md5s: 6a93196c on MinGW g++ (Windows, twice - so the trainer
// IS deterministic) and d817c318 on the ubuntu runner, with losses 0.042956 and
// 0.042761 - 0.45 % apart. The bytes are toolchain-bound and the nets are
// equivalent, so a byte anchor pinned in a workflow that runs on ubuntu asserts
// a property of a machine that is not that runner.
//
// The assertion is aimed rather than deleted: CI now checks the final loss
// within 5 % (ten times the measured toolchain spread) plus the sidecar
// reproducing byte for byte - it is pure recipe text, so a changed corpus path,
// topology, epoch count or seed still lands there. Exact-byte identity stays a
// local check on the fitting machine, which is what caught this in the first
// place. Still owed: the published leave-one-project-out fold table was
// measured with the OLD net and needs its own round.
//
// 1.24.2 (a texture allocation is whole PAGES, and a menu stopped eating the
// HUD's letters): reported as "opening the menu makes some letters disappear -
// R is gone from VRAM", with a screenshot reading `V AM 3.81/4 MB`.
//
// getSize() counted a texture's PIXELS. The GS stores one in whole 8 KB pages,
// a row of pages at a time, so a texture that does not fill its last page row
// still OWNS those pages. The debug HUD font is 512x16 PSMCT32: 8192 words of
// pixels (+ upstream's 2048-word pad) against a footprint of ceil(512/64) x
// ceil(16/32) = 8 x 1 pages = 16384 words. The next texture was therefore
// placed 10240 words in - page 5 of the font's own 8 - and overwrote pages
// 5..7, which is every glyph from x=320 rightwards. R lives at x=480 and was
// the only glyph past that line on screen; T, at 496, was equally gone and
// simply not being drawn. The allocation that lands there is the menu's own
// font atlas, which is why opening a menu is what triggers it.
//
// THE DIAGNOSIS IS THE GLYPH POSITIONS. Every letter that still rendered (V, A,
// M, F, P, S, E, B) sits at x <= 288 and every one that did not sits past 320 -
// a boundary that falls exactly on a page edge, from one screenshot.
//
// getSize() now returns at least the page footprint. Upstream's
// "TODO: Without this hack, textures are overlapping ourselves" sat on the pad,
// which covers width 128 and nothing wider. Nothing changes for the frame and z
// buffers (page-aligned already - all five display modes come out identical) or
// for textures 32 rows or taller. Measured cost: the font grows 24 KB, which
// the HUD's own VRAM line shows as 3.21 -> 3.23 MB.
//
// 1.24.1 (the Display tab loses the VRAM line 1.23.0 gave it): the readout was
// correct and unwanted. It answered "what does this mode cost" as a wall of
// small print under the mode picker - three lines, two of them explaining that
// a DIFFERENT number elsewhere is a different pool - which is a footnote about
// the HUD parked in a settings dialog. Removed on request, without a
// replacement: the question it answered is a game-runtime question and the
// game's own HUD answers it (`VRAM 3.21/4 MB`, still there, docs/gs-vram.md).
// The per-mode figures it computed are not lost either - they are written down
// in that doc's table, where they can be read without switching modes to watch
// a number move. PATCH: one thing comes off the screen, no behaviour changes.
//
// 1.24.0 (the Debug button becomes a build-profile dropdown): "run and open the
// debugger" stops being a thing to remember and becomes what running a debug
// build does. The button was Run plus opening the Debugger panel, which meant
// the ordinary Run - the F5 every muscle memory reaches for - silently started
// a debug session with nowhere to watch it. Now every launch path opens the
// panel when the profile is debug, Live Debugger is on and the panel is closed;
// only when closed, so one shut mid-session stays shut, and never re-docking or
// stealing focus from one already open.
//
// THE HOOK IS ITS OWN CALL (App::openDebuggerForLaunch) BECAUSE THE PATHS DO NOT
// SHARE ONE. Putting it in runSelectedTarget looked complete and was not: the
// F5/F6 chords name their target rather than taking the toolbar's, so they reach
// runner_ directly and every one of them would have slipped past it. Caught by
// driving the built editor with --ui-script rather than by reading the diff.
// Ctrl+Shift+B is deliberately excluded - it builds without running.
//
// IN THE BUTTON'S PLACE, THE BUILD PROFILE. The toolbar was missing the switch
// that every chip to its right depends on - Live Link, the Live Debugger and
// Live Logic exist in debug builds only, and their dimmed tooltips all ended by
// naming a Preferences page. A labelled combo rather than another drawn glyph:
// this is the one control on the bar whose current VALUE has to be readable at
// a glance, and "why is there no LIVE chip" is answered by seeing "Release".
//
// AND NEW PROJECTS DOCK THE DEBUGGER BEHIND PROPERTIES, so the first run has
// somewhere to report. Properties stays the selected tab, which is decided by
// ImGui submission order (drawUI draws it first) and not by docking order -
// noted where a future reader would otherwise "fix" it by swapping two lines.
//
// 1.23.0 (three readings that answered the wrong question): all three came from
// one session of using the editor rather than from a test, and each is a
// surface saying something true about a quantity nobody asked about.
//
// MEM DID NOT MOVE WITH THE DISPLAY MODE, because it is the EE's 32 MB and a
// display mode never touches it. The pool that moves is the GS' separate 4 MB,
// where the two frame buffers and the z buffer live, and nothing showed it at
// all. Both surfaces now do: the game's debug HUD prints `VRAM 3.21/4 MB`
// under `MEM`, and Preferences > Display prints the same figure for the mode
// being picked, off `project::tripleBufferingFit`'s own arithmetic, before a
// build exists. Measured: 1080i leaves 0.79 MB for textures, 480p 1.24 MB -
// HD costs a third of the texture budget, which is worth seeing while choosing.
//
// THE FPS LINE READ 25 IN 576i AND 30-40 IN HD and looked like a counting bug.
// It was not: those modes refresh at 50 and 60 Hz, so one missed field halves
// to 25 and to 30 respectively. The rate is now printed over its cap
// (`FPS 25.0/50`, `FPS 30.0/60`) - the same result in both modes, and legible
// as such only with the denominator there.
//
// THE BOOT LOGO SAT LOW IN 1080i, and the first fix moved it 25 rows high
// instead, because the vertical centre of a raw sprite is neither half the
// buffer nor half the 448-row sprite space: render() shifts that space up by
// (renderHeight - 448) / 2, so the visible centre is (renderHeight + 448) / 4.
// The three candidates coincide at 448 rows and separate by 46 in 1080i, which
// is why it survived until a mode taller than the authored space shipped.
// Measured in PCSX2 with the HUD's 20-row line pitch as a ruler.
//
// MINOR rather than PATCH: two readouts that did not exist appear, in the game
// and in the editor. The logo alone would have been a PATCH.
//
// 1.22.0 (a deploy stops killing every other PS2 session on the machine):
// reported from use, and diagnosed the hard way an hour earlier - `Run on PS2`
// of one project froze another project's Live Debugger, its Live Link and its
// time machine, all at the same second, while the `[ps2]` log kept scrolling.
// Both halves of that are now fixed rather than merely diagnosable.
//
// THE CAUSE WAS ONE VERB, USED TWICE. `Runner::deployToPs2`, `stopPs2` and
// `clean` each ran `platform::killByName({"ps2client"})` - `taskkill /F /IM
// ps2client.exe`, machine-wide, by name - so a deploy of ANY project took down
// the file server of every OTHER ps2link session; and `launchPCSX2` /
// `stopEmulator` did the same to every emulator, which had already interrupted
// measurements repeatedly on this branch. This repo is routinely driven with
// several editors from several worktrees at once, so neither was an edge case.
// `killByName` is deleted, not narrowed: a by-name kill is the wrong primitive
// and leaving it in the platform layer leaves it there to be reached for.
//
// OWNERSHIP IS READ OFF THE PROCESS'S OWN COMMAND LINE, and the ordering is the
// design: the handle first (the Process we spawned, killed as a tree - the
// common case and the only certain one), then a search for what the handle
// cannot reach. `-h <ip>` says which console and `execee host:<name>.elf` says
// which game, so a stale server from a crashed run of the SAME project is still
// reaped - without that the fix would have traded one bug for "only one deploy
// per boot works" - while one a RUNNING editor owns is refused with that
// project named and one no editor claims at all is reaped as an orphan. Two
// rules hold it up: a process whose command line cannot be read is never a
// target, and "is that editor alive" is answered by devsession's heartbeat OR a
// live editor pid, because the heartbeat stops while an editor sits in a native
// file dialog and the direction that must not fail is the one ending in a kill.
// The discriminator is deliberately the command line and not the working
// directory, which would be sharper (the deploy runs its server with cwd =
// <project>/bin): Linux answers cwd with one readlink and Windows has no
// supported way to ask, and a key only one platform can compute would make the
// two behave differently.
//
// Measured against the reporter's own live session (their editor open on
// F:\Tyra-Projects\display-modes, its game running on the console at
// 192.168.100.150): a deploy of a different project REFUSED, naming that
// project and the remedy; their ps2client survived; their bin/livedbg.bin kept
// advancing across the whole operation - which is the exact file that froze in
// the original incident. In the same run a stale server of the deploying
// project and an orphan nobody owned were both reaped. On the emulator side,
// two instances differing only in their -elf path: the project's own was
// closed, the other left running.
//
// MINOR: a capability appears - the editor can tell one file server from
// another, name the session that owns one, and refuse rather than steal it, and
// `--debug-state` now prints that inventory (emulators and file servers with
// the console and game each is serving) instead of a PowerShell incantation for
// the reader to run. No default moves and the format is untouched (still v17).
//
// 1.21.2 (two silences: the Live Debugger reported nothing for a project with
// no flow graph, and the HD HUD was drawn above the picture). Two independent
// fixes, no setting moves, the format is untouched: PATCH.
//
// (1) The Live Debugger's whole runtime was gated on there being at least one
// instrumented flow-graph NODE (`liveDebugOn` = the preference AND a node to
// instrument), so a project without a graph generated an empty
// live_debug.gen.cpp, wrote no bin/livedbg.bin at all, and the panel waited
// forever. But the channel carries far more than node hits: the Stats tab's
// frame rate, bag flushes, GS VRAM and free EE RAM, the VU1 capture and the
// crash report are properties of the FRAME and have nothing to do with
// anybody's logic - and a bare fixture with no graph is exactly the kind of
// project somebody opens the Debugger on. Codegen now emits the runtime for
// any debug build with the preference on (`liveDebugEnabled`); the zero-cost
// rule is untouched, because that predicate still requires the debug profile,
// and a release build still gets the empty TU and the no-op header.
//
// (2) The same report had a second half nobody could separate from the first:
// the panel says the identical "No stats yet." whether the game has not
// booted, the build carries no runtime, or the file server died half an hour
// ago with the console still running. That last one is the ps2link failure
// mode - `bin/livedbg.bin` is a perfectly valid snapshot frozen at its last
// write - and it is now named as such, with the file's age and the remedy (a
// redeploy, not a retry), from ONE string both the Debugger's state block and
// the Stats tab read.
//
// 1.21.1 (the upscaler DELETED the terrain of any project with post fx): a
// second instance of the shrunken-z-buffer hazard 1.19.x fixed in
// RendererCoreBlss::configure, this time in the post-fx pass, and reported as
// "forcing the upscaler on in examples/showcase makes the ground disappear".
// Engine only, one register field, no setting moves, the format is untouched:
// PATCH.
//
// RendererCorePostFx::apply()'s restore block re-programmed ZBUF with a
// HARDCODED mask of 0 - "z writes enabled" - which was right for every project
// until the upscaler sized the z buffer from the raster instead of the display.
// ps2sdk's draw_enable_tests() on the next line writes TEST_1 and nothing else
// (disassembled from libdraw.a: one A+D qword at GS_REG_TEST_1), so that qword
// was the last word on ZBUF for the rest of the frame AND the next one - and
// the following frame's full-screen clearScreen sprite, which
// draw_disable_tests leaves at ZTE=1/ZTST=ALWAYS, then stamped 512x448 words of
// depth from ZBP at the display stride, across a 256x224 allocation, straight
// through the post-fx buffers, the env map, the camera feed, the low-res target
// and into the TEXTURE HEAP. Whichever textures landed in that window drew
// nothing at all: a zeroed 4-bit CLUT has alpha 0 and ATEST NOTEQUAL/AREF 0
// discards every fragment.
//
// So the projects it hit are exactly the ones that RUN post fx, which is why
// `examples/upscaler-lab` (bloom 0, grain 0) never showed it and
// `examples/showcase` (bloom 0.16 + grain 0.09 + colour grading) lost its whole
// ground - showcase has exactly one texture in the project, the terrain's, so
// the damage read as "BLSS deletes the terrain" rather than as texture
// corruption. Measured on a scratch copy of showcase with nothing changed but
// `blssEnabled`, PCSX2 software renderer, 2x2 neural: the ground is absent at
// every distance and every angle (looking straight down at ground 1.8 units
// away shows sky), the crosshair sprite is absent, and film grain reads
// sd 1.80/255 in the sky against 4.51 with the upscaler off. After the fix, on
// the same fixture: crosshair back (peak 253,239,195 against the control's
// 253,236,193), grain sd 4.20, and a ground patch means (112,100,24) - the
// control's value exactly. Two configurations that hid it and are now
// explained rather than mysterious: `blssScale` 1 (1x2) draws correctly because
// the twice-as-large low-res target absorbs the overshoot before the heap, and
// an untextured terrain draws because it has no texture to lose (its crosshair
// was still missing).
//
// 1.21.0 (main's animation and loading work arrives): three landings come in
// from origin/main - the terrain wrap fix (#211), the audsrv music-stream fix
// (#213, both PATCH there) and the animation/loading workflow set (#214, which
// main numbered 1.13.0). The merge takes ONE MINOR above both parents rather
// than picking a side, the 1.10.0 precedent below: the tree now carries a
// capability neither parent had on its own, and a number strictly greater than
// either is the only one that keeps "which editor wrote this file" answerable.
//
// What arrives: 3D finally owns the texture wrap mode, so a tiling ground
// texture repeats again instead of smearing its edge texels along the world
// axes (the CLAMP register is written once per frame by Path3::clearScreen and
// bracketed per bag for the render targets that genuinely want clamping);
// AudioSong::work polls audsrv_available() instead of blocking inside audsrv's
// single RPC handler, so a sound emitter beside a music track stops costing
// ~10 ms a frame; and the editor gains a project-opening screen, asynchronous
// model import with cached bounds, a Model faces selector, an Animation Editor
// preview camera, and IN-PLACE clips (below).
//
// Nothing on this branch moves. The one file both sides changed for real is
// RendererCoreGS - main added setTextureWrap/repeatWrap beside setAlpha while
// this branch rewrote the display queue around them - and the two are disjoint:
// the black-frame fix in 1.20.1 is intact, `(context + 1) % bufferCount` and
// all, and reallocateBuffers() still re-presents. Checked rather than assumed
// (see the commit message for the boot line and the capture counts).
//
// 1.20.1 (the upscaler with triple buffering presented BLACK frames): reported
// from use as the emulator "flickering badly", isolated to the PAIR of features
// - either one alone is clean - and fixed in the engine. No editor behaviour
// changes, no setting moves, the format is untouched: PATCH.
//
// One display buffer of the three was being PRESENTED without ever having been
// drawn - a fully black frame, no debug HUD either, at a third of the field
// rate. The cause is one expression. `RendererCoreGS::allocateVramBuffers()`
// arms the display queue with `displayedBuffer = context ^ 1`, which means "the
// other buffer" only when there are TWO. That function runs a SECOND time in
// any game that re-lays the permanent VRAM region after boot, and the neural
// upscaler is the only thing that asks for one (`configure()` sizes the z
// buffer from the raster, so it calls RendererCore::rebuildPermanentBuffers).
// By then the ~2 s boot banner has flipped ~120 times and `context` is wherever
// the three-buffer rotation left it; land on 2 and `context ^ 1` is **3**, one
// past the end of frameBuffers[]. flipBuffers' `3 - shown - finished` then goes
// negative and wraps in its u8 cast, so the rotation ran on indices 254 and 3
// and both DREW and PRESENTED through framebuffer_t's read past the array.
//
// That is why it needed both features and neither alone: triple buffering to
// make 2 a reachable value of `context`, and the upscaler to re-arm the queue
// after boot instead of only at init with `context` still 0. Measured on the
// reporter's fixture, progressive 480p, PCSX2 software renderer, one build
// changing only these lines: the boot log reads `drawing into 2, showing 3`
// before and `drawing into 2, showing 0` after; 12 back-to-back PrintWindow
// captures go from black (mean 0.64/255, PCSX2 losing its GS device seconds
// later) to 12 frames of the scene within 0.019 % of each other.
//
// Two smaller holes in the same seam went with it: `reallocateBuffers()` never
// re-programmed DISPFB although the third buffer's ADDRESS moves when the z
// buffer shrinks (602112 -> 458752 at 448x448 2x2), so the television scanned
// the texture heap for a frame; and a rebuild that comes back with FEWER
// buffers left `context` naming one that no longer exists.
//
// 1.20.0 (Project Preferences becomes a window, and triple buffering stops
// promising what the console will not do): two more defects reported from use,
// and as in 1.18.0 the fixes are structural.
//
// "CLICKING ADVANCED CLOSES PROJECT PREFERENCES COMPLETELY - COULD IT BE A
// WINDOW INSTEAD OF A MODAL?" The apply-and-close of 1.18.0 was the right answer
// FOR A MODAL - ImGui blocks every click behind one, so a window raised from
// inside it is untouchable - but it was a workaround for the modality rather
// than a fix, and the report is asking for the modality to go. It does, and both
// windows now sit open with both usable, which is the whole point.
//
// STAGING COULD NOT SURVIVE THAT, so OK/Cancel are gone and the window applies
// live like every other panel in the editor (rule 1: mutate, then
// commitChange()). This is the decision in the change, and it is forced rather
// than chosen: a non-modal window means the project can be edited underneath
// while staged edits wait - by undo, a session peer, the AI Assistant, or the
// very windows these buttons open - so an OK pressed afterwards would overwrite
// all of it with minutes-old values; and prefTerrain_ staged the ACTIVE SCENE's
// terrain, so a scene switch with the window open would have written scene A's
// size onto scene B. `prefSettings_` is a one-frame copy now, re-seeded from the
// model at the top of the body and compared back at the bottom, which covers
// every widget in every tab by construction. What Cancel bought is not lost so
// much as unified with the rest of the editor: nothing reaches disk until an
// explicit Save, and project-wide settings were never in the undo stack anyway
// (History::push carries the scenes only), so Cancel WAS the only way back and
// is now "close without saving" - said in the footer.
//
// ONE control in there is genuinely dangerous to apply per keystroke, and it is
// treated specially rather than holding the whole dialog modal for its sake: the
// terrain grid. Width, depth and the detail cap all change the heightmap's
// dimensions, and project::ensureHeightmap answers that with a
// NEAREST-NEIGHBOUR RESAMPLE - so typing "128" over "64" would pass through 1
// and 12 and flatten a sculpted map on the way, and dragging the detail slider
// to its left stop would destroy it outright. Those three keep a scratch written
// back only on IsItemDeactivatedAfterEdit, and re-seeded from the model on any
// frame the widget is not being edited so undo and a scene switch still show
// through. Verified: an uncommitted "2" in Width leaves the stored 100 alone;
// the slider applies once, on release (32 -> 431).
//
// AND TRIPLE BUFFERING IS GATED, ACROSS EVERY MODE THE PROJECT SUPPORTS. The
// checkbox stayed tickable in a display mode with no VRAM for a third buffer -
// the engine then silently stays double buffered and says so only in the game's
// log - and the amber warning that named the numbers was answering for
// bootDisplayMode ALONE. That is the substantive half: ProjectSettings::
// supportedModes declares the scan modes a player can switch INTO, and
// RendererCore::setDisplayOutput re-runs allocateVramBuffers on every such
// switch, so the engine grants a third buffer in one mode and refuses it in the
// next. The setting is a REQUEST, not a state, and the dialog now says so:
// project::tripleBufferingFit takes an explicit mode, project::
// tripleBufferingModes asks it of the boot mode plus every declared one, the
// tick is greyed out with the reason IN LINE when none of them has room (only
// the tick, never the untick - the BLSS x frame-extrapolation rule, so a project
// that arrives with the flag on can always clear it), and when the modes
// disagree it names them: "Fits in 512x224, not in 512x512 (boot mode 576i full
// PAL: does not)". Measured at a new project's defaults: room in 512x224 and
// 448x448, none in 512x448, 448x540 or 512x512. The way OUT is computed and
// never asserted, which is the trap this avoided: "turn the upscaler on" frees
// enough at 512x448 and is short by 0.12 MB at 512x512, so the dialog probes a
// blssEnabled copy of the staged settings rather than printing a general hint.
//
// MINOR: capabilities appear - Preferences and the upscaler window are usable at
// the same time, and the editor can now answer "does a third buffer fit in every
// mode this project supports", which nothing could ask before. No default moves
// and the format is untouched (still v16, no new field, no migration step; the
// window's open state rides the existing per-layout openWindows list as the new
// key "projectprefs").
//
// 1.19.0 (the FPS counter reads the wrong clock, and nobody said which frames
// it counts): reported from use as "the editor's debug panel shows 19 FPS while
// the game's own HUD shows 10". Both numbers were describing the same game at
// the same moment. The HUD was wrong, and by an exactly reproducible factor.
//
// `Info::calcFps` divided a hardcoded 15625.0 into a single frame's delta of EE
// **Timer 3** - the kernel's alarm timer, which is clocked by H-BLNK. T3 counts
// SCANLINES, so its rate is a property of the VIDEO MODE while the constant is
// PAL 576i's line rate. Measured on one fixture and one build, changing only
// displayMode: PAL 576i reads 15 626 Hz and the old formula was exact (48.00
// against a true 48.00); progressive 480p reads 31 470 Hz and it printed 29.76
// against a true 59.94, i.e. 0.4965x. Every progressive project read HALF its
// frame rate, on the HUD and in the Live Debugger's Stats tab, which relays the
// same field. NTSC interlaced is 15 734 Hz - the same bug at 0.7 %.
//
// It reads COP0 `Count` now - the clock the frame-timing rig uses, a property
// of the CPU and not of the signal - averaged over a STATED 0.5 s window and
// returned as a float rather than as a `const u32&` fed from one (19.6 used to
// display as 19). The constant was not assumed: on the PAL arm the H-BLNK and
// COP0 clocks independently give 48.00, and on the progressive arm COP0 (59.94)
// matches the editor's host clock over 8 s (59.94) and PCSX2's own status bar
// (59.92).
//
// AND THE COUNTER NOW SAYS WHICH FRAMES IT COUNTS, because with frame
// extrapolation on that is a second factor of two nobody was reporting. The
// warp presents a synthesised frame after endFrame() returns, so the game shows
// about twice the rate it renders; every counter in the repo counted rendered
// frames. `Info::getPresentedFps` counts buffer flips, the HUD prints
// `FPS 30.0 SHOWN 59.9` when the two differ and the plain line when they do
// not, and the Live Debugger carries both. Measured: 15 rendered against 30
// presented per window, PCSX2's own counter 59.89. On the reporter's
// configuration the old number was therefore **4x low** against what the eye
// sees - 2.014x of scanline clock times 2x of rendered-versus-presented, two
// independent faults that happened to stack.
//
// MINOR: a capability appears - the presented rate is measurable for the first
// time - while the format is untouched (still v16). The two tenths-of-a-frame
// fields ride the four spare bytes at the end of the Live Debugger's existing
// 64-byte stats block, so an older editor reads the block it always did and an
// older game leaves the zeros memset already put there; neither needs a
// snapshot version bump. No published figure moves: every headline on this
// branch (1.96x, 1.63x, 4.58 ms) came off the COP0 rig, and the three places
// that did quote this HUD were all PAL interlaced fixtures where its constant
// was correct. docs/profiling.md, "The three frame rate counters".
//
// 1.18.0 (Project Preferences gets a shape, and the refused pair becomes
// unreachable): two defects reported from use, and the fixes to both are
// structural rather than cosmetic.
//
// THE ADVANCED... BUTTON DID NOTHING, and so did Open Ambience Editor. A modal
// blocks every click on anything behind it, so raising a window from inside one
// leaves it visible, inactive and untouchable - the button looked broken because
// functionally it was. Open Loading Screens editor had the other half of the
// bug: it closed the dialog and silently DISCARDED the staged edits. All three
// are one helper now, over the same apply the OK button uses. It APPLIES rather
// than cancels, for a reason specific to what it opens: those windows edit
// project_.settings LIVE, so a cancelled dialog would show them the values on
// disk while the user is looking at the ones they just set, and the tick made on
// the way to pressing Advanced... would vanish. It says so on the line under
// each button, in the tooltip, and in the status bar afterwards.
//
// THE OK BUTTON WAS AT THE BOTTOM OF A VERY LONG SCROLL. The dialog was one
// vertical stack of a dozen sections with the footer inside it, so confirming
// meant scrolling past everything - and every setting anybody added made that
// worse. Two fixes, and the first matters more than the one that was asked for:
// the footer is PINNED OUTSIDE the scrolling region (each tab body reserves it),
// which is what stops the next setting putting the dialog back where it was; and
// then five TABS - Display, World, Rendering, Player, Build - derived from the
// sections that were already there. The old "Build" section was doing two jobs,
// the video signal and the ELF's contents, and splitting it is most of the
// regrouping: "how does a frame reach the screen" is now one tab, holding the
// signal, the presentation and BOTH reconstruction features. The dialog is also
// 720 px rather than 560, which is the cheap half of the wrapping complaint.
//
// AND THE INCOMPATIBLE PAIR IS NOW UNREACHABLE rather than refused four minutes
// later in Docker. BLSS x frame extrapolation is the only one of the five
// clashes that CAN be prevented - it is setting against setting, and both
// switches are in one block - so whichever is already on greys the other out
// with the reason in line (a greyed control that explains itself only on hover
// reads as a bug). The other four are setting against scene CONTENT and keep the
// warning; you cannot grey out a portal somebody placed. Only the TICK is ever
// blocked, never the untick, so a project that arrives with both on - a
// hand-edited .tyra, an older editor, a Set Frame Extrapolation node - can
// always turn one off. The build interlock STAYS as the backstop for exactly
// those three routes.
//
// The interlock's own two defects, also reported from use, are fixed with it: it
// was emitted into inc/scene_data.hpp, which fourteen translation units include,
// so one clash printed one 340-character paragraph forty-two times (GCC prints
// an #error three times over) and the reporter's whole build log was that wall;
// and the authored words "the upscaler's temporal pass" are an unterminated
// character constant to the preprocessor, so every one of those TUs also carried
// a bogus "missing terminating ' character" warning. The messages are one short
// line each now - the pair, the scene, one place to fix it, and the doc page for
// the why - errorSafe() covers the whole line rather than only the interpolated
// names, and the refusal lives in src/gen/blss_interlock.gen.cpp, a TU of its
// own that refreshGenerated DELETES when the project stops clashing. Measured:
// 42 diagnostic lines plus 14 warnings became 3 lines and no warning.
//
// MINOR: a capability appears - the editor now refuses to let an invalid
// combination be authored at all, which it previously only complained about -
// while no default moves and the format is untouched (still v16, no new field,
// no migration step). The reorganisation on its own would have been PATCH.
//
// 1.17.1 (the upscaler and frame extrapolation refuse each other): a user
// reported that turning both on makes the picture disintegrate, and it does -
// but only IN MOTION, which is why nothing on this branch had seen it. Every
// automated gate here freezes the camera and the emitters on purpose, because
// that is what made them reproducible, so a motion-only fault is precisely what
// they were built not to see.
//
// Reproduced on the reporter's own project (progressive, three display buffers,
// neural mode at 2x2), PCSX2 software renderer, player driven by --pad. Parked,
// all four arms are indistinguishable. Walking: the upscaler alone is clean,
// extrapolation alone is clean, and the pair tears the frame into cells that
// disagree - a second displaced copy of near geometry, hard rectangular seams
// across the sky, silhouettes pasted at 32-pixel granularity.
//
// The mechanism is not a bug in either feature's bookkeeping. Both rebuild a
// frame by reprojecting the previous one through the camera delta, and
// extrapolation presents twice per loop - so the world runs at half the field
// rate and the camera moves TWICE as far between two RENDERED frames, which is
// exactly the interval the upscaler's temporal pass reprojects across. Measured:
// BLSS' own per-corner reprojection offset peaks at 158 px of a 448 px raster
// with extrapolation off and 201 px with it on, while the warp's grid is
// displaced by the same doubled delta at the same time. Two approximations of
// one displacement, each fed twice its design input.
//
// TWO EARLIER THEORIES WERE DISPROVED BY MEASUREMENT AND ARE RECORDED SO THEY
// ARE NOT RE-OPENED. It is NOT the two-buffer history degeneration composite()
// guards: with three buffers the rotation was LOGGED frame by frame, and the
// history is always the previous RENDERED frame, intact and never a synthesised
// one - the guard is correct and simply never fires here. And it is NOT raster
// state leaking across the warp: a leaked SCISSOR/XYOFFSET/FRAME is static
// register state and would wreck a parked frame too, and parked frames are
// clean.
//
// Refused rather than degraded, because no partial measure fixed it: dropping
// the temporal pass - the strongest single contributor - reduced the tearing but
// left the warp's own grid coming apart under the same doubled delta. Either
// feature alone is clean, so the honest answer is that a project picks one.
// blssClashes() gains its fifth condition beside depth of field, portals and
// split view, per scene like the rest; the dialog says the same thing live, at
// both points of choice.
//
// AND FRAME EXTRAPOLATION MOVES TO WHERE THAT CHOICE IS MADE. It used to sit
// under "Build" beside triple buffering; it now sits in the same block as the
// upscaler, retitled "Frame delivery (upscaler, extrapolation)", because the two
// are siblings - each reconstructs part of what the player sees instead of
// rendering it - and a mutual exclusion is only useful said at the point of
// choice rather than discovered as a build error.
//
// PATCH: no capability appears and no default moves; a combination that never
// worked stops compiling, and a control moves. The format is untouched (still
// v16, no new field, no migration step) - a project carrying both switches
// still loads, and is refused with a sentence instead of a broken picture.
//
// 1.17.0 (the upscaler stops requiring a hacker): BLSS' user interface becomes
// two layers. Project > Preferences now asks the three questions a person
// switching the feature on actually has to answer - use it, which
// reconstruction, which raster - and states ONE LINE of verdict measured by
// blss::measureCoverage in about a second, with no network, no training and
// nothing written to disk. Everything else - Train, Evaluate, Cross-validate,
// Compare, Inputs, Training shots, Console probe - is behind an "Advanced..."
// button and is UNCHANGED IN SUBSTANCE. None of that instrumentation is
// deleted: every performance and quality number this feature has published came
// out of it, and removing it would make the feature unfalsifiable. It simply
// stops being what a user meets.
//
// The reduction is a consequence of plain mode and would have been wrong before
// it. Until 1.12.0, BLSS MEANT "fit a network to your scene", so the window had
// to be the whole feature. It is not the mainstream path any more: plain mode's
// break-even is 2.6 full-screen coverages against the neural path's 13.1, a
// trained default network ships embedded in the editor so no project is built
// with random weights, and on every project measured that net chooses nothing
// anyway (all three outputs 0.000, one bilinear pass). So training is genuinely
// advanced, and the ordinary interaction is a checkbox, a mode and a sentence.
//
// A SIMPLER UI MUST NOT BECOME A MORE CONFIDENT ONE, which is the specific
// failure this had to avoid: the one-line verdict goes through the same
// blssui::speedFrom() / blssui::recommend() the window's own answer does, so
// "TOO CLOSE TO CALL" still quotes no multiplier when the estimate is inside
// what the counter cannot see, the picture half is still named as UNMEASURED
// rather than assumed absent, and the emitter share is still labelled estimated
// rather than counted. The dialog also states, once and where it is being done,
// what MIXING costs: a project whose scenes disagree pins the z buffer at the
// full display raster and gives up the memory saving (measured free heap at
// 512x512: 0.375 MB native, 0.875 MB uniform, 0.125 MB mixed).
//
// MINOR: a capability appears - the project's speed verdict is reachable from
// Preferences, and project::blssUse() can now answer for a project default a
// modal has not committed yet - while no default moves and the format does not
// change (still v16 after the merge below, no migration step).
//
// (AUTHORED AS 1.14.0 AND RENUMBERED HERE. The frame-pacing branch reached
// three landings of its own from the same 1.13.0 parent, and the earliest of
// them - and every one of its three format numbers - was published before this
// one. One number per landing, and the branch that arrives second renumbers:
// the rule this file already applied to v8-v10 and to v14-v16 below. Nothing
// else moves; this landing never claimed a format version, so there is no
// on-disk consequence at all.)
//
// 1.16.0 (frame extrapolation, the ground plane): the synthesised frame takes
// its depth from the FLOOR instead of a fixed distance -
// ProjectSettings::frameExtrapolationGround, format v16. A view ray meets the
// ground at w = h / -dir.y, so depth grows toward the horizon on its own and a
// ray at or above it never meets the floor: the sky stops moving, which is the
// worst artefact of a single plane. MINOR because a capability appears (the
// third translation model, and the first analytic one); it is also the one
// default in this file's history that does NOT preserve what an older file was
// saved with, on the v9 blssJitter precedent - the behaviour it declines to
// preserve is a picture whose horizon slides.
//
// 1.15.0 (frame extrapolation, the translation model as a control):
// ProjectSettings::frameExtrapolationPlane and frameExtrapolationForce, format
// v15, plus the Set Frame Extrapolation flow node and the numeric flow
// parameter that can declare its own choices. The plane went to 0 - rotation
// only - after the fixed 12 units read as a lens zoom, and the force switch
// exists because the per-frame gate measures EE work and therefore stays shut
// on a GS-bound scene that would still like to be tested. MINOR: capabilities
// appear, both defaults reproduce what the previous version did.
//
// 1.14.0 (frame pacing and frame extrapolation, docs/frame-pacing.md and
// docs/frame-extrapolation.md): ProjectSettings::tripleBuffering presents from
// a vblank interrupt instead of stalling the EE on vsync, so a frame that
// overruns its field by a hair is shown one field late rather than halving the
// rate; ProjectSettings::frameExtrapolation makes the generated game present
// one synthesised frame - the last rendered one, re-drawn under a newer camera
// by the new renderer_core_warp - after each rendered one. Format v14. MINOR,
// and both default to false, so an existing project regenerates byte for byte.
//
// 1.13.0 (the upscaler is a property of a SCENE): BLSS gains a per-scene
// override - SceneOverrides::upscaler, format v13 - carrying blssEnabled and
// blssNetwork. A scene with a portal can refuse the upscaler while the scene
// next door keeps it, which is the half of this that matters most: the build
// interlock (blssClashes) was project-wide, so ONE portal anywhere disabled the
// feature for every scene in the project, including scenes that had neither.
// It is now asked per scene and the remedy is local too.
//
// The switch is FREE, and that is a measured claim, not a hopeful one. The
// blocker on the rejected per-frame toggle was that configure() re-lays the
// permanent VRAM region and evicts every texture - and a scene change does NOT
// re-lay VRAM (it frees and re-acquires per asset, ref-counted), so doing it
// there would have been the same problem in a quieter place. The fix is not to
// do it at all: a project whose scenes disagree pins the z buffer at the FULL
// display raster once, at init (RendererCoreGS::setZRasterScale), and
// RendererCoreBlss::setScene() then flips two flags and re-derives the
// projection. No eviction, no vram.reset(), no re-placement, nothing to
// measure at the transition.
//
// The price is paid only by a project that actually mixes, and it is the z
// saving: such a project keeps the low-res colour target as overhead (224 KB at
// 512x448, 2x2) instead of trading it for 672 KB of z. A project whose scenes
// all resolve alike is untouched and regenerates byte for byte - the per-scene
// tables, the eighth configure() argument and the setScene() call are emitted
// only when the resolved answers actually differ.
//
// MINOR: a capability appears, no default moves, and blssScale / blssJitter /
// blssSharpen / blssTemporal / blssDebugView stay project-wide on purpose (one
// project ships one net, and its provenance sidecar records the scale and the
// sampler it was fitted for).
//
// 1.12.1 (the flagship demo on assets we may actually ship): every art asset
// in examples/upscaler-lab is now CC0 1.0. The cottage and the animated spider
// went in with UNVERIFIED redistribution terms and a banner in the project's
// THIRD-PARTY-NOTICES.txt admitting it, which is not a state the feature's own
// demo should be in; they are replaced by buildings kit-bashed from Kenney's
// Retro Urban Kit (CC0) and by wobbler.glb, which five other examples already
// ship. PATCH by this file's own rule - no capability appears or disappears,
// the format does not move, and the editor is not touched. What DID move is
// measured rather than assumed: the GS fill the example exists to demonstrate
// is unchanged (--blss-coverage 72.63 -> 72.23, the emitters untouched at
// 6 x 32 haze billboards), the oracle ceiling went UP (+1.058 -> +1.108 dB,
// jitter off, 2x2) and the EE got 4 ms cheaper per frame in PCSX2, almost all
// of it the animated model (2 x 1092 spider vertices -> 2 x 123 wobbler ones).
// That last one moves the published hardware A/B, which CANNOT be re-measured
// here - the console is unreachable - so 52.95 -> 32.42 ms / 1.63x is now
// labelled as a measurement of the PREVIOUS geometry and the re-run is owed.
//
// 1.12.0 (the upscaler without the upscaler): BLSS gains a PLAIN mode -
// ProjectSettings::blssNetwork, format v12 - which keeps the reduced raster and
// the VRAM it hands back and deletes everything between: no bag proxies, no
// reprojection, no feature grid, no MLP, and one full-screen sprite instead of
// the Gouraud grid. It exists because on every project measured the trained net
// already asks for NOTHING (all three outputs under the deadzone, BLSSFILL
// 1.00 passes) while the frame pays the full EE bill to find that out. MINOR
// because a new mode appears in the editor and in the generated game; the
// default is unchanged, so an existing project regenerates byte for byte.
//
// 1.11.0 (the feature grid can describe particles): the SIXTH rule of the BLSS
// twin contract. An emitter bag used to contribute no proxy at all - a
// billboard bag runs frustumCulling None, so StaPipCore had no package bbox,
// fell to a radius-0 sphere and addBag threw it away, and bagList() only walked
// geometry - so on examples/upscaler-lab the network chose its kernels over
// 98.7 % of the frame's fill from the geometry behind it. Now an emitter is
// described by one box: the AABB over the centres it submits, grown by the
// widest quad they expand into. BOTH HALVES SHIP OFF (TYRA_BLSS_EMITTER_PROXY
// and --emitter-proxy), so no fold table and no shipped net moves; MINOR
// because --emitter-proxy is a new verb-level capability, not because anything
// changed by default. Measured before the flip and not after: it works
// (147 -> 224 of 224 covered tiles, texDetail finally reports puff.png) and it
// costs (coverage becomes a CONSTANT, +0.88 ms of EE, break-even 13.1 -> 15.3),
// so it stays off. The spatial-split follow-up this line used to point at has
// since been implemented on both twins, measured and REJECTED - it leaves all
// 224 tiles covered and both channels constant for another +1.18 ms - because a
// partition of a solid region is a tiling of it, and an emitter's pool is
// always solid. docs/blss-reconstruction.md section 2 and docs/backlog.md.
//
// 1.10.3 (three things that were wrong, none of them a new capability): a FOG
// emitter's Opacity survives a save (format v11 - it was written only inside
// the custom block, so the one non-custom kind that reads the value reloaded
// at the 0.6 default and the game was built with it); --blss-train and
// --blss-emit print ABSOLUTE paths for the net, its .meta and the emitted
// header; and an --blss-eval run on a project with enabled emitters ends in
// NO VERDICT rather than a confident sentence about a frame the corpus does
// not render. PATCH by this file's own rule, the 1.10.1 precedent: nothing new
// appears in the editor, three wrong behaviours become right. A format bump
// does not force MINOR - the two numbers are independent by design, and the
// semver is informational.
//
// 1.10.2 (the corpus says what it does not draw): a project with enabled
// emitters gets a warning from --blss-train / --blss-eval, because the corpus
// renderer draws none of them and the PSNR table therefore describes a frame
// the game never displays - on examples/upscaler-lab, measured at 1.63x on real
// hardware, it printed "THIS SCENE WILL NOT BENEFIT". Drawing them is filed in
// docs/backlog.md; this is the caveat, not the fix.
//
// 1.10.1 (two things hardware testing found, both fixes rather than features):
// `--blss-train <projectDir>` writes its net into the PROJECT instead of the
// current directory, so the documented "train, then rebuild" flow stops
// silently rebuilding with the shipped default; and the GS fill price is per
// PIXEL rather than one scalar measured at 512x512, which moves the published
// break-even to 13.1 coverages at an ordinary PAL raster. PATCH by this file's
// own rule - no capability appears, two published numbers become right.
//
// 1.10.0 (the neural upscaler, docs/neural-upscaler.md): the BLSS branch and
// main both climbed from 1.3.0 while they were apart and both arrived at 1.9.x
// - a collision, since 1.9.0 on one side names the widescreen/World Facts set
// and on the other the upscaler's last patch. The merge takes the MINOR above
// both rather than picking a side: the tree now carries a feature main did not
// have, which is what MINOR means, and a number that is strictly greater than
// either parent is the only one that keeps "which editor wrote this file"
// answerable.
// 1.55.2 (the clipper stops clipping what the scissor would crop -
// docs/vu1-clipping.md): the static pipeline classified a package against the
// VIEW frustum and read PARTIALLY_IN_FRUSTUM as "needs clipping", which it is
// not. VU1 cuts against the near/far pair and an X/Y band at 0.9 of w, and the
// projection divides by projectionScale 4096, so the screen edge is at 0.125 of
// w and the band is SEVEN times that - a triangle may hang ~1590 px past either
// edge of a 512x448 picture before anything is cut, and the GS scissor crops
// the raster during DDA. So a package straddling the screen border crossed no
// clip plane at all, and it was still split into thirds (3x the DMA chains and
// VU1 kicks), memcpy-ed stream by stream where the cull route hands VU1 a
// POINTER, and run through Sutherland-Hodgman with an empty plane mask.
//
// The packager already computed that mask; it now answers the routing question
// in the same pass (StaPipBagPackage::guardBandOnly) and such a package is
// culled whole and by pointer. Over EIGHT planes, not six: the cull programs'
// fcand 0x3FFFF tests z against +/-w too, while the guard band's near constant
// is deliberately looser (PlanesClipAlgorithm::clipMargin), and that gap is a
// thin shell in front of the near plane where the clipper draws a triangle the
// cull program would ADC away - a hole at point-blank range. The two exact
// near/far half-spaces live at indices 6..7, on the EE only, never uploaded.
//
// MEASURED on examples/large-terrain (2048x2048 terrain, 1181 props), PCSX2
// software renderer, a frame-indexed script camera, one line differing between
// the arms, 2922 PAIRED frames: work 6.887 -> 4.670 ms, d = -2.217 ms, 95% CI
// [-2.258, -2.175], 1.475x, 2864/2922 frames faster. Clip-routed packages
// 11 164 -> 2 127 per 50-frame window, clipped triangles 68 456 -> 13 264,
// qbuffer flushes 1 287 -> 756 - five sixths of the clipper's load was geometry
// that needed no clipping. The picture is unchanged, and the CONTROL is what
// says so: two boots of the same build differ on this fixture (it streams
// terrain chunks), and an A-arm boot and a B-arm boot came back BYTE-IDENTICAL
// over four parked poses - the arm is not what sorts the images.
//
// Also here: StaPipTelemetry gets its first reader after a year with none. The
// generated game enables it and prints an FTCLIP line beside FRAMETIME, but
// only under TYRA_FRAME_PROFILE (default 0), so a shipped build carries none of
// it. PATCH: no capability appears, frames get shorter, nothing on disk changes
// shape.

#define TYRAX_VERSION_MAJOR 1
#define TYRAX_VERSION_MINOR 78
#define TYRAX_VERSION_PATCH 0

#define TYRAX_STR2(x) #x
#define TYRAX_STR(x) TYRAX_STR2(x)
#define TYRAX_EDITOR_VERSION            \
    TYRAX_STR(TYRAX_VERSION_MAJOR)      \
    "." TYRAX_STR(TYRAX_VERSION_MINOR) "." TYRAX_STR(TYRAX_VERSION_PATCH)

namespace version {

inline constexpr const char* kEditorVersion = TYRAX_EDITOR_VERSION;

// Current on-disk project format. Files with no "formatVersion" field (every
// project saved before versioning existed) read as 0.
// v2 (Save Editor): the memory card appearance (saveTitle / saveIcon* /
// saveIconMotion*), the save behaviour (saveMenuWritesCheckpoint, saveAsync,
// saveSpinner*, saveAutosaveSlot, saveSlotCount, saveSlotsPerPage) and
// GameMenu::saveMenu. Purely additive with safe defaults, so no migration step
// - an older file opens silently and project::ensureSaveMenu backfills the
// save menu the same way ensureInputActions backfills the input map.
// v3 (menu stylesheets, docs/menu-styles.md): GameMenu::style names the
// menu-styles/*.menustyle file a panel is baked with, MenuEntry gains
// styleClass / description / icon / enabledWhen and the `label` action, and
// ProjectSettings::supportedModes declares which scan modes a game supports.
// Purely additive with safe defaults - an empty `style` IS the old look, byte
// for byte (checked by diffing the baked panels of every example against the
// previous baker), so no migration step.
// v4 (SPU2 reverb, docs/reverb.md): an Area's reverb zone (the "reverb" object
// on an Area: preset / amount / delay / feedback / priority) and the sound
// emitter's "reverb" send flag. Purely additive - an older file has no zones,
// which reads as a dry game exactly as it was - so no migration step. (This
// was authored as v3 on its own branch and renumbered on the merge: menu
// stylesheets took that number first.)
// v5 (sound priority, docs/sound.md): a sound emitter's "priority" - who
// keeps one of the eight emitter voices when more emitters are audible than
// there are channels. Purely additive and it defaults to 0, which is what
// every emitter in an older file gets, so the ranking then falls back to
// loudness alone - no migration step. (The Play Sound node's matching
// Priority parameter is a flow-node param and rides the existing num array,
// which needs no format bump of its own.)
// v6 (collision-box overlay, docs/collision-boxes.md):
// ProjectSettings::showCollision - the debug-profile preference that draws
// every collider's box in the running game, next to showAreas. Purely additive
// and it defaults to false, which is what every older file gets and what the
// game did before, so no migration step.
// v7 (World Facts, docs/world-facts.md): the "facts" section - the declared
// fact catalog, the named queries over it, the reaction rules and the saved
// test scenarios. Purely additive: a project with no facts writes no section
// and behaves exactly as it did, so no migration step. A fact's `id` is
// stamped by project::ensureFactIds on load, which is what lets a player's
// save survive renames and reordering later. (Authored as v4 on its own branch
// and renumbered TWICE on the way in - the reverb and sound-priority bumps took
// 4 and 5, then the collision-box overlay landed on main and took 6. A branch
// that lives a while renumbers rather than argues; the number means "what the
// file may contain", and only main gets to say which is which.)
// v8 (the neural upscaler, docs/neural-upscaler.md): ProjectSettings gains
// blssEnabled / blssScale / blssSharpen / blssTemporal / blssDebugView, the
// project-wide BLSS group. Purely additive - blssEnabled defaults to false, so
// an older file opens as "no upscaler", which is exactly what it was, and the
// codegen is byte-identical while the flag is off. No migration step.
// v9 (the upscaler's jitter kill switch): ProjectSettings gains blssJitter,
// the +-1/4-pixel per-frame raster jitter that is the confirmed cause of the
// screen shake (docs/neural-upscaler.md, "The oscillation"). Purely additive,
// and since 2026-08-08 it defaults to FALSE - so a file saved before the key
// existed opens with the jitter OFF rather than with the behaviour it was
// saved with. That is the one deliberate exception to "an older file opens
// byte-identical" in this list, and it is deliberate because the behaviour it
// declines to preserve is a visibly shaking picture. Nothing else about the
// project changes and no migration step is needed: the codegen difference is
// one constant, and a project that wants the samples back sets the key.
// v10 (the upscaler's training-shot plan, docs/neural-upscaler.md): Project
// gains blssShots - which of the six automatic camera moves the corpus shoots,
// how many frames each gets, whether Cutscene Director takes join, and the
// author's own vantages (typed, grabbed from the viewport, or bound to a placed
// Camera object). Purely additive, and additive in a stronger sense than the
// entries above: a DEFAULT plan writes nothing at all, so every project saved
// before the key existed round-trips byte-identically and every published fold
// table stays reproducible. No migration step.
// (v8-v10 were authored as v4-v6 on the upscaler's branch and renumbered on
// this merge - reverb, sound priority, the collision-box overlay and World
// Facts had taken 4 through 7 on main while it was away. Three numbers for one
// branch rather than one, because each was a separate landing with its own
// meaning and the list is what an older editor's refusal is read against; two
// features may never share a number. Nothing on disk changes: every one of them
// is additive, so a project written at the old v6 opens at v10 unchanged and no
// migration step is needed for the renumber either - a file claiming 6 now
// means "collision-box overlay", which a BLSS-less project is.)
// v11 (a fog emitter's Opacity is stored): save() wrote "opacity" only inside
// the custom (kind 5) block, but FOG reads it too - peak alpha = opacity x 60 -
// and the inspector offers it there, so an authored 0.3 came back 0.6 on the
// next load and the game was built with 0.6. It is now written for fog as well;
// the other four kinds have hardcoded peak alphas and still store nothing.
// Additive, and NO migration step - deliberately, because there is nothing to
// transform: the file never held the value, so 0.6 (the reader's default) is
// not a guess at what the author meant, it is exactly what that file has always
// meant to both codegen and the viewport. A step could only invent a number.
// The author's 0.3 was destroyed by the save that dropped it and no migration
// can bring it back; what the bump buys is that an older editor now refuses the
// file instead of dropping the key on ITS next save, which is the whole job of
// this number.
// v12 (the upscaler's plain mode, docs/neural-upscaler.md):
// ProjectSettings::blssNetwork - false renders at the reduced raster and blows
// it back up with one bilinear pass, with no network, no bag proxies, no
// reprojection and no feature grid. Purely additive and it defaults to TRUE,
// which is the only thing a project saved before the key existed can have
// meant: the reconstruction it shipped with is the one its blss.net was fitted
// for. So an older file opens as the neural mode it already was and regenerates
// byte for byte, and no migration step is needed. (Note the deliberate contrast
// with v9's blssJitter, which does NOT preserve what it was saved with - that
// exception was bought by a visibly shaking picture, and there is no equivalent
// argument here.)
// v13 (the upscaler per scene, docs/neural-upscaler.md, "Per scene"):
// SceneOverrides::upscaler plus a scene-local blssEnabled/blssNetwork pair.
// Additive AND INHERITING, which is a stronger property than merely additive: a
// scene with the flag off resolves to the project value, i.e. to exactly what
// the file meant before the key existed. So a v12 file opens as the project-wide
// setting it already was, and - because both the flag and the values are WRITTEN
// ONLY when the override is on - resaves byte for byte. There is nothing for a
// migration step to do: it could only write the inherited answer into every
// scene, which is the same behaviour spelled out at the cost of never being able
// to change a project default again.
// v14 (frame pacing + frame extrapolation, docs/frame-pacing.md and
// docs/frame-extrapolation.md): ProjectSettings::tripleBuffering, which decides
// whether the renderer presents from a vblank interrupt instead of stalling the
// EE on vsync, and ProjectSettings::frameExtrapolation, which makes the
// generated game present one synthesised frame after each rendered one. Both
// purely additive and both default to false - which is exactly what every
// project did before - so an older file opens unchanged and regenerates byte
// for byte while they are off. No migration step.
// (Authored as v5 and v6 on the frame-pacing branch and renumbered to ONE
// number on this merge, the same way v8-v10 above were: the upscaler had taken
// 4 through 12 while this branch was away. They collapse into one entry rather
// than two because they landed as one feature set with one meaning - "the
// pacing work" - which is the test this list applies. Nothing on disk changes;
// both are additive, so a project written at the old v6 opens at v14 unchanged.)
// v15 (frame extrapolation, the translation model as a control):
// ProjectSettings::frameExtrapolationPlane and frameExtrapolationForce. Both
// additive and both default to what the previous version did - plane 0 is
// rotation only, force off leaves the gate in charge - so an older file opens
// unchanged and regenerates byte for byte. No migration step.
// v16 (frame extrapolation, the ground plane):
// ProjectSettings::frameExtrapolationGround. Additive, and it defaults to TRUE
// - the one entry in this list that does not preserve what an older file was
// saved with, deliberately: the fixed plane it replaces moves the sky, and the
// ground plane is the same model with the horizon handled correctly. A project
// that wants the old look sets the key false. NO migration step, on the v9
// blssJitter precedent and for the same reason: a step could only write the
// old constant back into every file, which is exactly the look the default was
// changed to stop producing. The bump's job is done by an older editor now
// refusing the file rather than dropping the key on its next save.

// (This branch's three entries were authored as v13-v15 and renumbered to
// v14-v16 on this merge: the upscaler's per-scene work took 13 while this
// branch was away. Same rule as the v8-v10 renumber above - two features may
// never share a number, and every one of these is additive, so nothing on
// disk changes. Checked rather than assumed on the merge that brought them
// here: all five keys are read behind a find() with the default the entry
// names, none of them renames, moves or reinterprets an existing key, and
// migrations::stepsFor therefore has nothing to register for any of the three
// - which is what makes a v13 project open silently at v16.)
// v17 (Animation Editor in-place clips, docs/animated-models.md):
// AnimClipEdit::inPlace removes horizontal root motion during the .tskl bake.
// Purely additive: it is WRITTEN only when true and read behind a find() that
// defaults to false, so a project saved before the key existed keeps its exact
// authored animation and resaves byte for byte. No migration step - checked,
// not assumed: main's #214 changes nothing else that project::save() writes
// (the only other new state is the Model faces selector, which rides the
// existing per-object rotation, and the import/opening screens, which persist
// nothing), and src/migrations.cpp is untouched on both sides, so
// migrations::stepsFor has nothing to register and a v16 project opens
// silently at v17.
// (Authored as v8 on main and renumbered here. Two features may never share a
// number, and 8 has been this branch's neural-upscaler entry since well before
// #214 landed - it is quoted by the entries above, by docs/neural-upscaler.md
// and by an example .tyra on disk. So the claim that arrives second renumbers,
// which is the same rule the v8-v10 and v14-v16 notes above applied to this
// branch's own entries when main got somewhere first. Nothing on disk changes:
// the entry is additive, so a project written on main at v8 opens at v17
// unchanged, and no migration step is needed for the renumber either - a file
// claiming 8 now means "the neural upscaler", which an animation-only project
// simply does not use.)
// v18 (Player speed tiers, docs/player-speeds.md): SceneObject::playerRunSpeed
// and playerSprintSpeed, plus ProjectSettings::runSpeed for the fallback
// walker. All three are additive AND are written only when non-zero, so a
// project that never opens the new fields resaves byte for byte - checked, not
// assumed: `--resave` on examples/cube, showcase, two-players, weapons-arena
// and endless-runner produced no runSpeed/sprintSpeed key anywhere. (That
// weapons-arena was never a project - PR #203 committed three ignored build
// artifacts under the name and nothing else; the directory has since been
// removed. The other four still make the point.)
//
// No migration step, and the reason is the "0 = inherit" default rather than
// mere additivity: 0 resolves to the numbers the walkers used to compute
// inline (run = walk, sprint = walk x sprintMultiplier), so an old project is
// not merely readable, it MOVES identically. Verified on the generated side -
// examples/script-demo regenerated RUN_SPEED == WALK_SPEED == 0.4 and
// SPRINT_SPEED == 0.72 == 0.4 x 1.8.
// v19 (animation import, docs/animation-import.md): Project::animImports and
// its "animImports" manifest section - clips borrowed from another model file.
// A whole new section rather than a field, so a project that has imported
// nothing emits no key at all and resaves byte for byte; every retarget flag
// inside a row is likewise written only when it differs from its default.
//
// No migration step: nothing existing is renamed, moved or reinterpreted, and
// the feature is inert without a row. The one thing that DID change shape for
// every model is host-side only - SkelNode::name, which writeTskl does not
// serialize - so no .tskl version moved either and an unimported model bakes
// the same bytes it did before.
// v20 (sprint clip + live speeds, docs/player-speeds.md): SceneObject::
// playerSprintClip - the third-person avatar clip for the sprint tier, stored
// in the thirdPerson object and written only when set, so an untouched project
// resaves byte for byte. No migration step: "" means "the run clip covers
// sprinting", which is what every project did. The same commit moves the
// walkers onto PlayerCtl::speeds and streams speed edits over Live Link
// record v3 - channel-internal, not project format.
// v21 (bone mapping, docs/animation-import.md): AnimImport::boneMap - the
// hand-made donor->target bone pairs from the Map bones editor, an array of
// {s, t} objects written only when non-empty. Additive with a safe default
// (empty = pure name matching, the previous behaviour), so no migration step.
// v22 (the full retargeter, docs/animation-import.md): AnimImport::facing
// (world yaw of the source, -1 = auto from the rigs' feet) and ::mirror
// (left<->right flipped import). Written only when set and true respectively,
// so untouched projects resave byte for byte; no migration step. The
// retarget path itself is chosen automatically and stores nothing.
// v23 (posture fine-tune, docs/animation-import.md): AnimImport::lean -
// degrees of torso pitch applied by the retargeter. Written only when
// non-zero; no migration step.
// v24 (VRAM options, docs/gs-vram.md): ProjectSettings::colorDepth picks the
// frame buffers' pixel format (PSMCT32 or the half-size PSMCT16) and
// ProjectSettings::dither drives the GS's ordered dither. Both are written
// only when set away from their defaults, and those defaults are exactly what
// every older project already did, so no migration step. (The two optional
// render targets that landed with them are NOT in the format at all - they
// are derived at build time from what the project ships, not stored.
// Authored as v9 on this branch, renumbered to v18 at the first merge and to
// v24 at this one, the same rule every note above applied to itself: main's
// neural-upscaler batch took 8 through 17 and its speed/animation-import batch
// 18 through 23 while this branch was open, and the claim that arrives second
// renumbers.)
// v25 (terrain distance detail, docs/terrain-lod.md):
// ProjectSettings::terrainLodDistance - beyond it the ground is built from
// every 2nd heightmap sample, beyond 2.2x it from every 4th. It defaults to 0,
// which builds every tile at full detail, i.e. exactly what every project did
// before the key existed, so an older file opens and RUNS unchanged and no
// migration step is needed. It is written unconditionally, like the
// terrainViewDistance beside it in that flat settings block, so an untouched
// project does gain the key on its next save - which is precisely what this
// number exists to make safe. (Authored as v24; main's VRAM options took that
// number first, and the claim that arrives second renumbers.)
// v26 (pre-lit models, docs/prelit-models.md): SceneObject::prelit - the
// object's texture already carries its light, so its vertex colours go
// neutral. Written only when true, so a project that has never baked one
// resaves byte for byte; it defaults to false, which is what every existing
// object is. No migration step. (Authored as v25, renumbered with v25 above.)
// v27 (flashlight shadow volumes, docs/flashlight.md "The shadow"):
// ProjectSettings::flashShadowVolumes - written only when true, so an
// untouched project resaves byte for byte; false (the default) is the
// silhouette-slot behaviour every earlier file had. No migration step.
// v28 (the lighting redesign - automatic model AO + pre-lit management;
// authored as v26, renumbered twice as this branch's base took v26 and then
// v27 while the redesign was in flight - the arrive-second rule):
// ProjectSettings::modelAo / modelAoStrength / modelAoRays / modelAoDist - the
// project-wide bake knobs - plus the "modelAoMode" section, the per-asset
// force-on/force-off override keyed by a model's asset path; and the three
// pre-lit bookkeeping fields on SceneObject - prelitWanted (the author's
// statement that the object ships pre-lit), prelitSig (a hex-string hash of
// what the last bake saw) and prelitSource (the materialPath to revert to,
// recorded on the first bake only); plus ProjectSettings::prelitAutoBake and
// giAutoBake, the opt-in "re-bake what went stale before every build"
// switches for pre-lit objects and for the GI caches. Every one
// of them is written ONLY when it
// differs from its default and the modelAoMode section is omitted entirely
// while empty, so a project that never touches either feature resaves byte for
// byte; the struct defaults reproduce what an older file did (no model AO at
// all), while project::create turns modelAo on for new projects. Purely
// additive - no migration step.
// v29 (spot-style dynamic lights, docs/flashlight.md "A scene light with the
// same trick"): "spot" + "spotAngle" on a light object's light block -
// written only when the style is on, so an untouched project resaves byte
// for byte; off (the default) is the point light every earlier file had.
inline constexpr int kFormatVersion = 47;

// The OLDEST format this editor reads. v0 is "saved before versioning existed"
// - a handful of shapes that were renamed or moved on their way to v1 (objects
// inline in the manifest instead of objects/<id>.json, a single "layout" dump,
// a project-level terrain block and flow graph, raw TTF paths where a font name
// now goes, ...). TyraX has never shipped publicly, so nothing on anyone's disk
// is written that way and the translations for it were pure weight; they are
// gone. The gate exists so such a file is REFUSED by name rather than opening
// as an empty project - the reader would find no scene it recognises and say
// nothing about why.
//
// Raising this is the same kind of decision as a migration step and wants the
// same note above kFormatVersion: it drops support for everything below it.
inline constexpr int kMinFormatVersion = 1;

}  // namespace version
