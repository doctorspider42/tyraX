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
#define TYRAX_VERSION_MAJOR 1
#define TYRAX_VERSION_MINOR 18
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
inline constexpr int kFormatVersion = 16;

}  // namespace version
