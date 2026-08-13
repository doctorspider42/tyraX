# Configurable buttons & keys (Input Map)

Before this, every gameplay button was a `#define` in the generated
`inc/controls.hpp`: jump was Cross, use was Square, and a player who disliked
that had no recourse. Now every button read in a generated game goes through a
named **action**, actions are bound in per-project **presets**, and the player
can **rebind them in-game** from a menu row.

Three layers, each optional:

| Layer | Who sets it | Where it lives |
| --- | --- | --- |
| **Actions** — what the game asks about ("jump", "sprint") | the project author | *Tools > Input Map* |
| **Presets** — which button/key each action is bound to | the project author (one or many) | *Tools > Input Map*, `"input"` in the `.tyra` |
| **Overrides** — one action moved to another **pad button** | the **player**, in-game | a menu *Rebind key* row → a save value on the memory card |

> **Scope note:** in-game rebinding covers the **pad** only. Keyboard and mouse
> bindings are authored in the Input Map and are not shown or captured by a
> rebind row — that support is still experimental
> ([docs/keyboard-mouse.md](keyboard-mouse.md): the hardware path is
> unconfirmed), and it is meant to get its own dedicated menu later.

The generated game reads bindings through `inputPressed(pad, action)` /
`inputClicked(pad, action)`, so all three layers land everywhere at once: the
walkers, the pause/save menus, the keyboard folding and the flow-graph *On
Action* trigger.

## Actions

An action is a name (the reference key), a label (what a rebind row shows), an
optional **role**, and a "player may rebind it" flag.

The **role** is what ties an action to built-in engine behavior. The generated
`inc/input_map.gen.hpp` emits one slot per role — `IA_ROLE_JUMP`,
`IA_ROLE_SPRINT`, … — and the game reads those:

| Role | What it drives |
| --- | --- |
| Jump | the walkers' jump (the old `BTN_JUMP`) |
| Use | usable objects, pick up / drop (the old `BTN_USE`) |
| Throw | throwing a carried pickable (the old `BTN_THROW`) |
| **Sprint** | new: multiplies walk speed while held |
| Fly up / Fly down | noclip ascend/descend |
| Menu confirm / back / Pause menu / Alternate | the menus and the 3-slot save menu (were Cross / Triangle / Start / Circle) |
| Menu up / down / left / right | menu navigation (was the d-pad) |
| Move forward / back / left / right | the left-stick deflection a keyboard asks for (was hardcoded WASD) |

An action with **no** role is a *custom* action: nothing built-in reads it, but
the *On Action* flow trigger does. That is how you add "crouch" or "horn"
without touching the editor's C++.

Only the FIRST action carrying a role reaches the game (roles are single slots);
the Input Map window says so when you create a duplicate. A role with no action
at all compiles to `-1` and simply never fires.

Defaults reproduce exactly what was hardcoded before, so an existing project
plays identically after the upgrade — `project::ensureInputActions()` seeds the
missing actions on load and only ever **adds**. The one addition is **sprint**
(pad R2 / Left Shift, ×1.8), which is new behavior by design.

## Presets

A preset is a named binding set. Each action can hold a pad button **and** a
keyboard key **and** a mouse button at once — all of them fire it.

- The preset the game boots with is *Preset at game start*.
- Switch presets at runtime with a menu **Choice** row bound to *Input preset*
  (its options are the presets, in order) or the **Set Input Preset** flow node.
- A new preset starts as a copy of the one you were editing.

Two presets are the classic "Default / Southpaw" pair; one preset is fine too.

## In-game rebinding

*Menu Editor* → a row with action **Rebind key**:

- **Rebind action** — which Input Map action the row moves. Only actions marked
  *Player may rebind it in-game* are offered.
- **Save value** — where the override is stored (created for you as
  `bind-<action>`). It holds an `inputCodes()` index; **0 = the project's preset
  binding**, so a fresh save plays exactly as authored.

In game: the cursor lands on the row, *confirm* arms capture mode (the row reads
`PRESS...`), and the next **pad button** the player presses becomes the binding.
**Start cancels** capture and *menu left* clears the row back to the preset
binding.

Cancel is deliberately the raw **Start** button rather than the *back* action:
back is Triangle by default, so checking it first made Triangle the one button
you could never bind - it cancelled instead of being captured. The cost is that
Start itself cannot be captured, which is why it is not offered as a rebindable
action.

The row's current binding is drawn as **runtime text** from the menu font's glyph
atlas — a baked option strip could not cover every possible binding name, so
`Project::atlasFontIndices()` bakes an atlas for any menu that has a rebind row.
When the project has an icon for the bound button (it does by default) the row
shows that **glyph** instead of the button's name.

The override replaces the action's **pad** slot and nothing else: the keyboard
key and mouse button the preset gave it keep working, since no in-game row can
put them back yet. An action with no pad binding at all shows `---`.

*Menu Editor > + Option block > **Key bindings (all rebindable actions)*** builds
the whole page in one click: one row per rebindable action, each with its own
save value, plus a preset picker when the project has several presets.

The scaffolded OPTIONS tree (*+ Options menu*) deliberately does **not** include
them - its CONTROLS page carries the stick settings only. Rebinding needs a save
value per action and most projects ship a fixed control scheme, so it stays an
explicit choice.

Because the overrides live in save values, they persist in memory-card saves
like every other menu state and are re-applied after a load
(`TerrainGame::applyInputBindings()`, called every frame next to
`applyMenuBindings()`).

*Input Map > Allow in-game rebinding* is the master switch: off makes every
rebind row read-only (it still shows the binding). Keep the menu-navigation
actions **not** rebindable — a player who moves *confirm* onto a button they
cannot reach is locked out of the menu that would fix it, which is why the
built-ins ship that way.

## Sprint

Holding the sprint action pins the player at their **sprint speed**. Where that
speed comes from is covered in full by [player-speeds.md](player-speeds.md); the
short version is that a Player object can state it outright (*Properties >
Sprint speed*), and if it does not, it falls back to
`ProjectSettings::sprintMultiplier` (*Input Map*, or *Preferences > Input >
Sprint speed*) applied to the run speed. **1.00× and no explicit sprint speed
turns sprinting off** without unbinding the button. It applies to all three
walker modes (walk / third person / noclip fly) and to both players.

The third-person avatar's locomotion clip is chosen from its speed as a fraction
of the **run** speed — the top of the stick ramp — so sprinting is above that
fraction by construction and pushes it over the run threshold (*Player > Run
at*); the run clip and its faster playback come for free.

## Flow graph

| Node | What it does |
| --- | --- |
| **On Action** | fires the frame the named action is pressed, whatever it is currently bound to (including a player's rebind). Its bool output is "held right now". |
| **On Key** | fires on a raw USB keyboard key going down. Bypasses the Input Map on purpose — for a fixed debug/cheat key, not for rebindable gameplay. |
| **On Button** | unchanged: the raw pad button. |
| **Set Input Preset** | switches the active preset by name (the player's overrides are re-applied on top). |

So "on jump" is an *On Action* node with `str = "jump"`, and it keeps working
after the player rebinds jump to Triangle.

Every one of the sixteen pad buttons can carry a **held** action, `L3`/`R3`/
`Start`/`Select` included. They could not until this branch: the engine's `Pad`
filled its `pressed` struct for twelve buttons only (the four with no pressure
channel were simply missing), while `getClicked()` read the full raw word — so
binding *Sprint* to L3 looked fine, fired nothing, and only the held actions were
affected. Fixed in `vendor/tyra/engine/src/pad/pad.cpp`, which means a **clean
engine rebuild** is what picks it up.

## Files

```
Tools > Input Map                     -> Project::input (InputMap, src/input.hpp)
      .tyra "input" section           -> project::Section::Input
              |
              v
inc/input_map.gen.hpp    action indices, role slots, preset tables, SPRINT_MULT
src/gen/input_map.gen.cpp inputPressed/inputClicked, preset+override resolution,
                          keyboard/mouse folding, rebind capture, binding labels
inc/controls.hpp         BTN_*/KEY_* derived from the DEFAULT preset (ownable)
```

`inputCodes()` (src/input.hpp) is the rebind code space — one dense index per
physical input, index 0 meaning "the preset's binding". Those numbers end up in
players' memory-card saves, so the table is **append-only**; its twin
`INPUT_CODES` is generated from it.

### controls.hpp still wins if you own it

`inc/controls.hpp` remains user-ownable, and its `BTN_*` / `KEY_*` macros are
now *generated from the Input Map's default preset* — so by default the two can
never disagree. If you delete the ownership marker and edit the macros yourself,
the generated runtime notices they differ from the default preset and lets your
file win for those roles. A player's own rebind still outranks both.

An older owned `controls.hpp` (from before the Input Map) also keeps compiling:
its `applyKeyboardMouseInput()` folds the keyboard the old hardcoded way, so
keyboard *rebinding* does nothing there while everything else works. Delete the
file and rebuild to get the current version.

## Related

- [docs/keyboard-mouse.md](keyboard-mouse.md) — the USB keyboard/mouse feature
  the key bindings ride on (drivers, PCSX2 setup, hardware caveats).
- [docs/multiplayer.md](multiplayer.md) — both players read the same bindings.
