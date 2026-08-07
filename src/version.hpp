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

#define TYRAX_VERSION_MAJOR 1
#define TYRAX_VERSION_MINOR 6
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
inline constexpr int kFormatVersion = 5;

}  // namespace version
