# tyra-editor-dev — menus, stylesheets, fonts, text icons, credits

From `### 3` (menus & option blocks, a menu's look is a stylesheet, inline text
icons, anything that draws text, credits rolls). See `docs/menu-styles.md`,
`docs/text-icons.md`, `docs/credits.md`, `docs/hud-animation.md`.

## A menu change misses part of its chain
**Cause/rule:** `GameMenu`/`MenuEntry` (project.hpp; project-wide, not per scene)
are baked to a panel sprite at build (menubake.cpp — the PS2 has no font), edited
in the *Menu Editor* (`drawMenusWindow` in app.cpp), and driven at runtime by
`updateGameMenu`/`renderGameMenu` (generated in `TPL_GAME_CPP_SCENE`, data in
`menu_data.gen.hpp` via `MenuEntryData`/`MenuData`).
**Fix:** a menu change can touch `MenuEntry` (+ `==`) → menu JSON in project.cpp
→ `MenuEntryData` codegen + `applyMenuBindings` in templates.cpp → the runtime
setting site (audio call, `axis`/`axisValue`, `applyVideoRequests`) → the Menu
Editor UI. Stateful **Toggle/Choice** rows store an option index in a named save
value (cycled by the pad, label drawn from a baked value strip). **Option blocks:**
`MenuEntry::settingBind` (a `Setting` enum, 0=none) marks a stateful row as
driving a built-in engine setting; codegen emits it as the `bind` column and
`TerrainGame::applyMenuBindings()` (called each frame in both loops before
`applyVideoRequests`) maps the row's option index onto the setting — music/sfx
volume, deadzone (`g_deadzoneL/R`), stick curve (`g_stickCurve`), display mode /
widescreen. The **display-mode row** (bind 5) is special twice: each option
carries an explicit engine mode (`MenuEntry::optionModes` → `MenuEntryData::optModes`;
codegen fills a short/absent table positionally; the Menu Editor edits these as a
dropdown of the five `Tyra::DisplayMode`s + a "Default (project)" sentinel (-1,
resolved to `g_defaultDispMode`, the boot mode latched at init, which main.cpp may
have promoted from Interlaced to Pal576i on a PAL region via
`ProjectSettings::palFullHeight`) + free label; a new engine mode must bump the
-1..4 clamps in project.cpp load, the templates.cpp emitter and the app.cpp
lists/seeds), and with an **Apply video mode** row (`MenuEntry::ApplyVideo`,
action 9) anywhere in the project (codegen'd `MENU_HAS_APPLY_VIDEO`) the row
defers: cycling only stages the save value, the APPLY row commits the switch
(`updateGameMenu` case 9), and outside a menu the row snaps back to the live mode
each frame. A **`MenuEntry::RebindKey`** row (action 10) is the in-game
key-assignment row: `bindAction` names the Input Map action, `param` the save
value holding the override as an `inputCodes()` index (0 = the preset's binding).
It is deliberately **pad-only** — `inputCapture` ignores keyboard/mouse and
`inputBindLabel` omits them — so an override replaces the action's `pad` slot
alone. It draws as **runtime text** from the menu font's glyph atlas, which is why
`Project::atlasFontIndices()` bakes an atlas for any menu carrying such a row and
`updateGameMenu` grows a capture mode (`menuRebindRow`). `bind` 8 =
`BindInputPreset` cycles the Input Map presets from a Choice row. The "+ Option
block" popup and "+ Options menu" scaffolder (`addOptionBlock`/`addOptionsMenuPages`/
`addRebindRows`, app.cpp) create pre-configured rows + backing save values.

## A new menu style property is not enough / the empty sheet stops being byte-identical
**Cause/rule:** a menu's LOOK is a stylesheet (docs/menu-styles.md); most of the
chain is data. A new **style property** is one row in `menustyle::propSpecs()` +
one `case` in `applyDecl` + one entry in `menustyle_ui.cpp`'s `propsFor()` (where
it shows up) + wherever `menulayout`/`menubake` reads it — the parser, writer,
widget and its tooltip are all derived from that row. `stage()`/`unstage()` are
the preview's door into the registry (one lookup path).
**Fix:** three decisions to keep: **the built-in `defaults()` ARE the Classic
look** (the literals that used to sit in menubake.cpp), so "no sheet" and "the
classic sheet" are the same thing; the **`classic` built-in sheet is deliberately
EMPTY of rules**, because a rule restating a default freezes a value the MENU is
supposed to supply (it re-froze every project's `accent`); and `write(parse(t))`
must be STABLE, since the Style tab saves through it. A property that only
changes PIXELS never reaches codegen; one that changes where a sprite goes is what
`menu_data.gen.hpp` carries, and `renderGameMenu` is a **compositor over that table
and decides nothing about the look**. The bake side owes a **stale-file delete**:
`refreshGenerated` writes the state/list/description textures only when the sheet
needs them and REMOVES them when it stops. A menu is authored in the logical
512x448 space and the runtime scales by `(width/512, height/448)` per display
mode (`project::displayModes()` is the ONE geometry table, host twin of
`RendererSettings::updateGeometry`). **Widescreen is anamorphic, so it is a 2D
problem**: `renderGameMenu`'s HORIZONTAL factor divides
`RendererSettings::getWindowAspect()` back out over 4:3, resolved per frame (Set
Widescreen and the DISPLAY menu's widescreen row flip it while the game runs — the
cutscene letterbox masks ship a STYLE and a runtime `barsFractions` twin,
`seqBarsFractions` in src/sequence.hpp, not baked fractions); runtime text inside a
compensated panel carries `drawFontText`'s `sx` squeeze; the Menu Editor's preview
applies the identical factor plus an **Aspect** override. The sheet's editor
support lives in `tools/vscode-tyrax` and **a change there is not done until the
`.vsix` is repackaged** (bump `version` in package.json, run
`python3 tools/vscode-tyrax/package-vsix.py`) — a source-only change is invisible
to every user with no error. **Motion is sprite properties, and only sprite
properties** (`@transition` reacts, `@animate` loops): a scroll bakes two copies of
the tile along each scrolled axis so the window never leaves the texture, and
anything drawn over the panel must be cropped by hand. The compatibility rule:
**an empty `GameMenu::style` bakes byte-identically to the pre-stylesheet editor**
(check with `--refresh-gen` on an example with both binaries and diff
`res/menus/*.png`).

## Inline text icons (`{{cross}}` / `{{action:jump}}`)
**Cause/rule:** `Project::textIcons` (`TextIcon`: name + PNG + scale, seeded per
pad button by `project::ensureTextIcons`, edited in *UI Editor > Button icons*).
The parser is **shared, header-only** (`parseTextIcons` in project.hpp returns
`TextRun`s) but the two renderers are TWINS: `textWidth`/`drawText` in
menubake.cpp composite icons into baked sprites, while the generated game blits
them from `res/hud/icons.png` via `resolveIconToken`/`drawFontText` with rects from
`inc/icon_data.gen.hpp`.
**Fix:** both sides take their geometry from `menubake::iconAtlasLayout`, and the
advance formulas (`iconAdvance`/`iconAdvanceFor`) are the pair to change together,
or the same string measures differently baked vs runtime. Icons are NOT tinted
with the text color (the face buttons carry the DualShock palette) and are skipped
in shadow passes. A `{{token}}` naming nothing stays literal on purpose.

## Anything that draws text
**Cause/rule:** `Project::fonts` (`GameFont`) is the single font registry — *Tools
> Font Manager* (`drawFontManagerWindow`). Text carries a font **name**, never a
path (`HudText::font`, `GameMenu::font`); only `GameFont::fontPath` names a real
file, resolved in one place (`menubake::resolveFontPath`, with a Consolas Bold
fallback chain).
**Fix:** `fonts[0]` is the fallback for an empty/stale name (`Project::findFont`)
and the Font Manager refuses to delete the last entry. Pick a font with
`App::fontCombo`; `fontSourceCombo` (the TTF picker) belongs to the Font Manager
alone. Renames must follow into texts, menus AND `FontName` node params
(`App::renameFont`). Two paths hang off one entry: **static text** (HUD texts,
menus, loading screens) rasterizes from the TTF into a sprite at build
(`menubake::bakeText*`) — the font never reaches the PS2; **runtime text** (the
Display Text node) bakes a **glyph atlas** (`atlasLayout`/`bakeAtlasPNG` →
`res/fonts/atlas-<name>.png`, metrics into `inc/font_data.gen.hpp` via
`fontDataHeader`, both from the same `atlasLayout()` call — change the layout math
and BOTH move or every glyph misplaces). Only fonts `Project::atlasFontIndices()`
returns get one; atlases bake white and are tinted at runtime. One runtime slot per
node: `dynTextSlots()` is walked identically by `fontDataHeader` and
`flowGraphScript`.

## Credits rolls
**Cause/rule:** `CreditsRoll`/`CreditsBlock` (project.hpp, docs/credits.md) are the
reference for **"a whole screen the game hands over to"**. The roll owns the
frame: the loop hook (in BOTH game-cpp loop templates, after the boot-splash block)
ticks `credits::tick` and `return`s while `credits::playing()` — no walker, no
scripts, no scene render behind it. `playing()` is `inline` over one extern int.
**Fix:** anything that owns the screen this way must ALSO own the pad, or the frame
it ends the same press reaches gameplay. The roll carries where to go afterwards
(`CreditsRoll::Finish`, resolved to indices in `credits_data.gen.hpp`), the player
REPORTS it (`credits::Result`) and the loop turns it into requests the loop already
serves (`scriptCtx.requestScene`/`openMenu`/`pendingEvent`); `pendingEvent` must be
promoted in `updateGameMenu` (the single place that clears `menuEvent`) or it is
wiped before any script sees it. A SKIP runs the same finish action. **`On Credits
Finished` cannot be a falling edge** — a roll freezes every graph, so no node ran to
latch "it was playing"; the runtime counts finished rolls (`credits::endCount()`)
and the node fires when its own copy falls behind, re-synced to the LIVE count on a
scene reload. The look is baked, not drawn: `menubake::bakeCreditsStripRGBA` (in
menubake.cpp) lays out and rasterizes the blocks into a strip of pow2 PAGE textures
(`menubake::kCreditsMaxPages` = 16; the GS pins every texture it draws, so the
window reports pages / duration / VRAM and says *content clipped* rather than
silently cutting a roll); the preview uploads THOSE pixels and positions them with
the generated player's own arithmetic. `creditsPreviewRefresh()` re-bakes when the
roll changes, compared with `CreditsRoll::operator==` plus the fonts' TTF paths.
The layout is settings-over-preview with a **height splitter** (`creditsSplit_`,
the matEdSplit_ idiom: InvisibleButton + `MouseDelta` + `saveGlobalConfig()` on
release, persisted in editor.ini); the **Jump to** list inverts the roll's own
timing arithmetic. Rolls are outside undo like Loading Screens, but still edited
with `commitChange()` (the file used to call `saveAll()` per widget).

## Reference facts (upstream `game-screens-menus-text.md`)
- VRAM rule: the engine uploads on a texture's FIRST RENDER (`RendererCoreTexture::useTexture`) and pins it forever - no LRU, an all-or-nothing flush, ~1.33 MB budget (+8 KB per allocation). `RendererCoreGSVRam::free` is a bump-pointer pop (`pointer = address`), so freeing anything but the newest allocation rewinds past live textures. Prefer 4-bit for runtime-tinted textures.
- Menus/option blocks: baked to a panel sprite (menubake.cpp), edited in the Menu Editor (`drawMenusWindow`), runtime `updateGameMenu`/`renderGameMenu` with `menu_data.gen.hpp` (`MenuEntryData`/`MenuData`). `MenuEntry::settingBind` (`Setting` enum, 0 = none) -> `bind` column -> `TerrainGame::applyMenuBindings()` (before `applyVideoRequests`). Display-mode row (bind 5) uses `MenuEntry::optionModes` -> `optModes`; five `Tyra::DisplayMode`s + "Default (project)" (-1 -> `g_defaultDispMode`, `ProjectSettings::palFullHeight`); `MenuEntry::ApplyVideo` (action 9, `MENU_HAS_APPLY_VIDEO`) defers commit. `MenuEntry::RebindKey` (action 10) uses `bindAction` + `param` (an `inputCodes()` index; `Project::atlasFontIndices()`; `MenuData::font`; capture mode `menuRebindRow`); bind 8 = `BindInputPreset`. Scaffolders `addOptionBlock`/`addOptionsMenuPages`/`addRebindRows`. Menu roles `titleScreen`/`pauseMenu`/`saveMenu`/`skipMenu`.
- Stylesheets `menustyle.{hpp,cpp}` (~400+1.7k): `propSpecs()` is the ONE property table; `defaults()` ARE Classic; the `classic` built-in sheet is deliberately EMPTY; `write(parse(t))` stable; `stage()`/`unstage()`. `menulayout.{hpp,cpp}` (~150+540): `compute(menu, project)`, `baseFor()`, `kMaxRows` = 32; read by menubake/editor/templates. `menustyle_ui.cpp` (~1.5k): own undo stack; `propsFor()`; `applyDecl`. Widescreen is anamorphic (horizontal factor divides `RendererSettings::getWindowAspect()` over 4:3); `seqBarsFractions`; `drawFontText`'s `sx`. Motion is sprite properties only (`@transition`/`@animate`). 512x448 logical space; `project::displayModes()`.
- Text/fonts/icons: `Project::fonts` (`GameFont`), `drawFontManagerWindow`; `menubake::resolveFontPath` (Consolas Bold fallback, `platform::systemFontPath`); `Project::findFont`; `App::fontCombo`/`fontSourceCombo`; `App::renameFont`. Static text baked (`menubake::bakeText*`); runtime text via a glyph atlas (`atlasLayout`/`bakeAtlasPNG` -> `res/fonts/atlas-<name>.png`, `fontDataHeader` -> `inc/font_data.gen.hpp`); `Project::atlasFontIndices()`; `dynTextSlots()`; `drawFontText` (centres on x, trailing `alpha`); `fontGlyphSprite`. Inline icons `{{cross}}`/`{{action:jump}}`: `Project::textIcons` (`TextIcon`, `project::ensureTextIcons`), `parseTextIcons` -> `TextRun`s; twins `textWidth`/`drawText` (menubake.cpp) and `resolveIconToken`/`drawFontText` (`res/hud/icons.png`, `inc/icon_data.gen.hpp`); `menubake::iconAtlasLayout`; `iconAdvance`/`iconAdvanceFor`; `menubake::optionalBuiltinIconNames()` / `project::ensureStickIcons`.
- HUD: `hud_ui.cpp` (~5.8k); `HudAnim`/`HudTransition`/`HudBar` in project.hpp; `hudanim.hpp` (~130) is the host twin of runtime `hudAnimEval` (change both); `HudAnim::kind` order is format (append only).
- Credits `credits_ui.cpp` (~980): the roll owns the frame (`credits::tick`, `credits::playing()`), `CreditsRoll::Finish` -> `credits_data.gen.hpp`; `credits::Result`; `credits::endCount()` for `On Credits Finished`; `menubake::bakeCreditsStripRGBA`; `menubake::kCreditsMaxPages` = 16; `creditsPreviewRefresh()` (`CreditsRoll::operator==`); height splitter `creditsSplit_`.
- Save system `savebake.cpp/.hpp` (~930+130): `icon.sys` (964 B), `list.icn`; `iconInfo`/`iconIcn` geometry (flat quad, `res/models` `.obj`, or `.glb` sampled <= 8 morph shapes via `glbparser::bake`); weight envelopes; `iconMotions()`/`applyMotion()`; `iconPreviewFrames()` / `buildGeo` / `modelFallbackTexture`. Save menu is a `GameMenu` (`project::ensureSaveMenu`, `saveMenuIndex`, `Project::saveSlotsPerPage`, `menubake::asBaked()`); async `SAVE_ASYNC` via `saveWriteBegin`/`saveWritePoll` (`mcSync(MC_NOWAIT)`: 0 = still, 1 = finished, -1 = nothing). Busy spinner sheet `MODE_REPEAT`/`MODE_STRETCH` (POT sides); `savebake::spinnerInfo()`; `busyText()`. `save_assets.cpp` (~540).
- Bakes produce `bakeTextRGBA`/`bakeTextPNG` and `credits_ui.cpp`/`hud_ui.cpp` ship these; `icon.sys`; save paths `saveEnsureIcons`.

## Menu/text identifiers (upstream)
- Logical 512x448 space scales at runtime by `(width/512, height/448)` per display mode via `Tyra::Sprite` per-axis `drawSize`. `drawFontText` CENTRES on x (left-align = add half of `fontTextWidth`).
