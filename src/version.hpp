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
#define TYRAX_VERSION_PATCH 1

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
// v4 (the neural upscaler, docs/neural-upscaler.md): ProjectSettings gains
// blssEnabled / blssScale / blssSharpen / blssTemporal / blssDebugView, the
// project-wide BLSS group. Purely additive - blssEnabled defaults to false, so
// an older file opens as "no upscaler", which is exactly what it was, and the
// codegen is byte-identical while the flag is off. No migration step.
// v5 (the upscaler's jitter kill switch): ProjectSettings gains blssJitter,
// the +-1/4-pixel per-frame raster jitter that is the confirmed cause of the
// screen shake (docs/neural-upscaler.md, "The oscillation"). Purely additive,
// and since 2026-08-08 it defaults to FALSE - so a file saved before the key
// existed opens with the jitter OFF rather than with the behaviour it was
// saved with. That is the one deliberate exception to "an older file opens
// byte-identical" in this list, and it is deliberate because the behaviour it
// declines to preserve is a visibly shaking picture. Nothing else about the
// project changes and no migration step is needed: the codegen difference is
// one constant, and a project that wants the samples back sets the key.
inline constexpr int kFormatVersion = 5;

}  // namespace version
