# tyra-editor-dev — vehicles: damage, exhaust, lamps, controls, suspension

From `## Vehicle damage (1.138.0)`, `## Vehicle exhaust pipes (1.167.0)`,
`## Vehicle lamp glow (1.141.0)`, `## Vehicle controls card (1.169.0)` and
`## Vehicle bank and suspension invariants`. See `docs/vehicles.md`.

## Vehicle damage (1.138.0)
**Cause/rule:** vehicle exit clamps the authored door-side offset outside half the
track plus tyre radius and player clearance, then floors the player at terrain
height; Use and Exit Vehicle share `exitAtDoor`. `damageVisual` gates dents, loose
pieces, broken lamps, damage dust and engine smoke while the meter still
accumulates if mechanical damage is enabled; with both switches off, skip impact
tracking. `damageMechanical` gates power loss and wreck immobilisation, including
AI and nitrous. `damagePerfCurve` shapes partial loss.
**Fix:** keep `vehiclesim::damagePerformance` and the generated runtime formula
aligned. The high-rev enabled switch retains the authored sample and curve but
codegen emits a silent high slot, and host audition must use only idle. These
fields join normal tuning inheritance and format. Three rules. (1) A hit is the
velocity change the frame's collision stages imposed (`VehicleRt::dmgPreV` is
taken at the loop top for sleepers and again right after the drive integrates); a
new contact path dents for free as long as it runs before `updateVehicleDamage`,
which is called after the car-vs-car pass — never write a speed change that is not
a collision between those two points. (2) `vehiclesim::impactFromDelta`/`dentHash`/
`applyDent` are the host source and `vehDentHash`/`vehicleDentApply` in
`vehicleImpl` the twin; the dent is a function of the REST position only (welded
corners cannot split), and the rest copy lives in `VehDamageGeo`, re-captured
whenever a part's `baseStamp` moves — anything else that bumps `baseStamp` without
rebuilding would be captured as the undamaged pose. (3) Dents write the
matrix-path LOCAL vertices through `BagArray::span` and must bump
`baseStamp`/`bboxVersion` (the package boxes move); they skip the lamp and glass
colours (`renderVehicleGlow` owns the lamp colours every frame — `lampBroken` is
how damage speaks to it). The `"damage*"` DriveSpec keys are shown on the Damage
tab and skipped by the Driving tab by that prefix.

## Loose pieces (1.139.0)
**Cause/rule:** a piece is a vertex RANGE of a body part's tier-0 array
(`VEHICLE_PIECES`, measured into `VehicleDef::pieces` by the bake), and the range
is only safe to collapse because the bake gave the piece whole strip runs of its
own.
**Fix:** anything that re-orders or re-strips a vehicle body (a new weld key, a
merge of parts, a change to meshstrip's run packing) must keep that — or
collapsing a bonnet tears a triangle out of the fender next to it.
`vehiclePiecesCollapse` must run after every write that could re-grow a lost piece
(a dent, a re-capture). Debris lives in `vehDebris_`/`vehDebrisBatches_` (one
world-space bag per texture, rebuilt only while dirty), kicked by cars, turned off
collision boxes and deleted past 60 units from the camera. The same reorder puts a
shiny textured part's matte (near-black texel) triangles LAST, and
`applyVehicleEnvLimits` shortens that part's env bag to the prefix every frame
(`VEHICLE_ENV_LIMITS`) — a rebuild or tier swap resets the count, which is why it
is re-asserted. The editor mirrors it by splitting the part in `viewportBody`
(vehicle_ui.cpp).

## Vehicle exhaust pipes (1.167.0)
**Cause/rule:** `vehbake::build` reads every geometry-free node named `exhaust*`
(case-insensitive) as {opening, direction} in the canonical body frame — direction
= the node's +Y for glTF, +Z for FBX, an exactly vertical arrow = an unrotated
empty = straight back. `adoptMeasured` carries `VehicleDef::exhausts` (format v93)
into `VEHICLE_EXHAUSTS`; the runtime's `vehicleExhausts()` is the ONE pipe list the
nitrous flame, the upshift backfire and `updateVehicleExhaustSmoke` read, falling
back to the two guessed rear pipes.
**Fix:** `kVehExhaustMax` (runtime) must stay >= `vehbake::kMaxExhausts`. Exhaust
smoke rides the tyre-smoke pool: every spawn site takes its slot with
`VehFx::takeSmoke()`, which resets the per-puff `smokeScale`/`smokeFade` — a new
spawn site that bumps `smokeNext` by hand inherits the last puff's size. The
`exhaust*` spec keys are on the Effects tab (skipped by Driving, section "effects"
in `visitVehicleTuning`). Headless refresh/build adopt the markers in memory only;
the GUI tick (or a hand edit + `--resave`) persists them.

## Vehicle lamp glow (1.141.0)
**Cause/rule:** the lamps are measured per LAMP by the bake (connected components
of the lamp part → `VehicleDef::lampGlows`), emitted as `VEHICLE_LAMP_GLOWS` only
for definitions with `drive.lampGlow` > 0, and drawn by `renderVehicleLampGlow`
through `beamCoronaTex` — which is why `projectUsesBeams` returns true for such a
project (it gates both the bake of hud/flare-corona.png and its load).
**Fix:** the arrays are fixed-size, sized in `setupVehicles` and written by slot
(a previous frame's DMA may still read them). The "lamp*" spec keys live on the
Effects tab, skipped by the Driving tab.

## Speed feel
**Cause/rule:** the "feel*" keys, on the Effects tab. `vehiclesim::speedFeel` is
the curve and the generated `updateVehicleSpeedFeel` is its twin (called from the
driver-camera block, with nullptr on foot so everything eases back and the FOV is
put back). It talks to the frame loop through two prolog globals: `g_vehShake`,
applied after the Camera Shake node's shake, and `g_vehBlurFix`, a floor under
`g_motionBlurBase`.
**Fix:** both game templates carry the two consumers, so edit them as a pair. The
nitrous flame is extra quads in `renderVehicleLampGlow` (`VEHICLE_NOS_FLAME_USED`).
An upshift's backfire uses that textured corona batch too: `projectUsesBeams` must
keep the texture for every vehicle project, and `backfireT` must tick outside the
skid-smoke branch or a handbrake slide can leave the flash frozen.
`vehiclesim::rpmFor` and generated `vehRpmFor` are twins: in final gear they allow
RPM up to `nosTopSpeed` times redline, while the sound's high-rev crossfade stays
capped at full volume. `DriveSpec::powerFade` and the generated longitudinal step
must apply the same speed-squared fade, including nitrous relief. The high-rev
sound has its own onset and pitch endpoints in VehicleDef; the idle loop must
pitch from launch, while the second loop fades in only above its onset. The rev
limiter (`DriveSpec::revLimiter`/`revLimiterRate`) is another twin:
`vehiclesim::revLimiterStep`/`revLimiterDipRpm`/`revLimiterOnset` against the
generated `vehRevLimiter*`. It is presentation only — the dip is kept beside `rpm`
(`limiterDip`), never subtracted from it. A car's bumper push into a physics body
divides EVERY component by the body's mass, the hop included.

## Vehicle controls card (1.169.0)
**Cause/rule:** `VehicleDef::tutorialSeconds` (a Driver tuning key, JSON
`"tutorial"`, written only when non-zero) emits `tutorialFont`/`tutorialSecs` at
the END of `VehicleDefData`. The card is runtime text: `kVehTut` in the vehicles
template is the ONE row table (role, fallback pad button, label) and must mirror
the fallback buttons `updateVehicles` reads when an `IA_ROLE_VEH_*` is -1.
**Fix:** keep the empty `VEHICLE_DEFS` initializer in step; a new driving button
needs a row there or the card silently omits it. Glyphs come from `g_inputBind`
every frame (never baked); rows a car cannot use are filtered by
`vehTutRowApplies`. `drawFontText` gained a trailing `alpha` (128 = the old
output). The stick glyphs are `menubake::optionalBuiltinIconNames()` — drawable
built-ins NOT seeded by ensureTextIcons (that would add two icons to every
project); `project::ensureStickIcons` adds them when the option is ticked. A
rebuild that REUSES a GeoPart (Repair Vehicle, a damage re-bake) resets
`envColors` to the 128 placeholder, so it must also clear `envPaintValid` and
`paintMapSrc`: the paint pass's quantised view key otherwise reads "same view" and
the car shows full reflection until the camera turns.

## Vehicle bank and suspension invariants
**Cause/rule:** `vehiclesim::bodyRotation` and the generated `vehBodyRotation` are
twins: vehicle-local pitch and roll are applied before heading, then converted to
ordinary object XYZ Euler angles. Use that conversion for both body and wheel
transforms; assigning roll to world Z makes the same bank heading-dependent.
Ground-plane fitting uses the hardpoints' projected spacing. Clearance probes are
position constraints only, never a spring target or a velocity source.
**Fix:** the wheel batch composes three transform columns per wheel outside its
vertex loop. Keep these rules in step between vehiclesim.cpp, vehicle_ui.cpp and
the vehicle runtime in templates.cpp (docs/vehicles.md explains the regression).
The generated runtime may sleep a grounded car only after 25 stopped updates, when
it is neither driven nor assigned an AI route and all looping audio channels are
already silent. Keep prompt/enter handling before the gate and car-to-car collision
after it: the collision response assigns motion and wakes the car on the next
frame. `Vehicles_update` and `Vehicle_sleep` are the hardware-trace proof that the
gate saves work without deleting traffic collisions.

## Reference facts (upstream `vehicles-and-roads.md`, vehicle half)
- Files: `vehiclesim.cpp/.hpp` (~1.7k+560) host wheel detection + drive model (twins `bodyRotation`/`vehBodyRotation`, `damagePerformance`, `impactFromDelta`/`dentHash`/`applyDent` vs `vehDentHash`/`vehicleDentApply`, `rpmFor`/`vehRpmFor`, `revLimiterStep`/`revLimiterDipRpm`/`revLimiterOnset` vs `vehRevLimiter*`, `speedFeel`/`updateVehicleSpeedFeel`). `vehbake.cpp/.hpp` (~2.3k+320, `kMaxExhausts` = 6; body `.tmdl` + wheel `.tmdl`); `vehcheck.cpp/.hpp` (`--vehicle-check`); `vehicle_ui.cpp` (~1.7k, `viewportBody`).
- Geometry: `glbparser` batches by material; `vehbake::meshNodes`/`collect` split per palette owner; wheel batch is a TRIANGLE STRIP with `meshstrip::Weld::kNoNormal` (body separate `kFull` attempt; `TYRA_STRIP_VEHICLE_BODIES_BAKE=0` control). Special parts `lamps`, `merged-matte`, `glass` (`glassOpacity < 1`, `GeoPart::translucent`, `renderVehicleGlass`, compartment `lodHidden`). Far tiers: `farPart`, `VehicleDef::farModel`, `collectFarModel`, `farHideMask`, `GeoPart::lodHidden`, `vehicleLodTier`, `trafficDistance`, `{{VEHICLE_LOD_TIER}}`/`{{VEHICLE_LOD_HIDE}}`, `applyGeoLod`, `lodStripVerts`. `wheel_blur` reserved node.
- Textured vehicles: `vehbake::Result::textures`, `bakeProject`, `vehicleRefreshBake`, `ModelPart::bakedTextureRel`, `textureQuant`, `pngquant::quantizeRGBAToMemory`, `finish_ms`; `-palette.png` exempt; reuse resets `envColors`/`envPaintValid`/`paintMapSrc`.
- Runtime: bank/suspension via `bodyRotation`/`vehBodyRotation`; sleep after 25 stopped updates (`Vehicles_update`/`Vehicle_sleep` scopes); exit clamps door side + `exitAtDoor`; damage `damageVisual`/`damageMechanical`/`damagePerfCurve`/`VehicleRt::dmgPreV`/`VehDamageGeo`/`baseStamp`/`BagArray::span`/`renderVehicleGlow`/`lampBroken`; loose pieces `VEHICLE_PIECES`/`VehicleDef::pieces`/`vehiclePiecesCollapse`/`vehDebris_`/`vehDebrisBatches_`/`applyVehicleEnvLimits`/`VEHICLE_ENV_LIMITS`; exhausts `exhaust*`, `VehicleDef::exhausts` (v93), `VEHICLE_EXHAUSTS`, `vehicleExhausts()`, `updateVehicleExhaustSmoke`, `kVehExhaustMax`, `VehFx::takeSmoke()`, `smokeNext`; lamp glow `VehicleDef::lampGlows`, `VEHICLE_LAMP_GLOWS`, `renderVehicleLampGlow`, `beamCoronaTex`, `projectUsesBeams`, `VEHICLE_NOS_FLAME_USED`, `backfireT`; speed feel `g_vehShake`/`g_vehBlurFix`; RPM/sound `nosTopSpeed`, `powerFade`, `revLimiter`/`revLimiterRate`/`limiterDip`; controls card `VehicleDef::tutorialSeconds`, `tutorialFont`/`tutorialSecs`, `VEHICLE_DEFS`, `kVehTut`, `vehTutRowApplies`, `g_inputBind`; `collideVehicleCamera`; `TPL_GAME_VEHICLES`/`vehicleImpl`.
- Tuning inheritance: `Project::vehicleDefaults` in `Section::Vehicles` (`writeVehicleArray`/`readVehicleArray`), `VehicleDef::inheritDefaults`, `visitVehicleTuning`, `tuningOverrides`, `vehicleGeometryKey`, `applyVehicleDefaults`. Preview/sound: separate `Viewport`; `audiopreview::EngineLoop`; sound roles `engineSound`/`engineHighSound`/`screechSound`; `inc/vehicle_sound_loops.gen.txt`; legacy `-loop.wav`.
