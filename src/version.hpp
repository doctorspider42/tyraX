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
#define TYRAX_VERSION_MINOR 10
#define TYRAX_VERSION_PATCH 2

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
inline constexpr int kFormatVersion = 10;

}  // namespace version
