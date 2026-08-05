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
#define TYRAX_VERSION_MINOR 1
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
inline constexpr int kFormatVersion = 2;

}  // namespace version
