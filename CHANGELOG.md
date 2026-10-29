# Changelog

What changed in each TyraX release, newest first. Every release is also on
[GitHub Releases](https://github.com/doctorspider42/tyraX/releases) with its
installers; releases before 1.170.0 are described only there and in the git
history.

A heading names the version only: the release workflow publishes the section
under it as that release's notes, and the release's own page carries the date.

## 2.2.1

- Dense physics scenes reject disjoint swept hull bounds before expensive contact tests. Rigid-body contact storage and vehicle prop candidates grow as needed instead of silently truncating at 384 contacts or eight props.

## 2.2.0

- Moving and sleeping colliders participate in spatial filtering, and rigid-body pairs use swept bounds instead of scanning the entire physics population.
- Mesh collision uses a host-baked 3D BVH, including camera and carry sphere sweeps through actual mesh openings. Legacy projects without a collision sidecar retain runtime construction.
- Convex rigid bodies detect crossed-edge contacts, spheres use exact hull corner distances, and selective linear sweeps catch fast crossings of thin obstacles. Hull transforms have a batched VU0 implementation with a scalar comparison path.

## 2.1.1

- The release notes on GitHub, and in the editor's update dialog, are now this changelog's entry for the version instead of a bare "Full Changelog" link.

## 2.1.0

- **Point-sampled textures.** *Preferences > Graphics > Texture filtering* chooses how the PS2 samples your textures: **Smooth** (the PS2 look), **Pixel** (crisp texels, the PS1 look) or **Pixel close-up** (crisp up close, smooth in the distance, so far walls do not shimmer). Any texture can override it in the Asset Browser, and every model and material using that texture follows. The viewport shows the same look; its view menu can also show everything smooth or everything pixel without touching the project. Texture atlas pages can mix pixel and smooth textures, so it costs no VRAM. Lightmaps, terrain, the sky and particles stay smooth. Projects are saved as format 123.
- The `texture-atlas` example is now **texture-atlas-filtering**: the same thirty crates on one shared page, now in three bands - Smooth, Pixel and Pixel close-up - and their numbers stand upright again.
- *Properties > Material* can open the Material Editor again: the *Edit...* button sat past the panel's right edge, out of reach. It now shares the Material row with a narrower picker.

## 2.0.1

- The Assets header and selected asset's **Open in Asset Browser** buttons no longer share an ImGui ID. UI scripts now fail on duplicate widget IDs without requiring a hover, and the Linux release job exercises the Assets actions before packaging.

## 2.0.0

- **Project > Audio** is laid out by what loads each sound. Sounds are grouped into Always loaded, the scene, each streaming layer, each sound bank, other scenes and never loaded. Selecting one opens an inspector with what loads it, what plays it, its banks and the Always loaded switch. A card on top shows the most sound RAM a scene can need at once, split by group, with a scene picker. Music has its own tab.
- Sound effects follow streaming layers, and you can make sound banks. The emitters and vehicles of a streaming layer bring their sounds with them, so an open-world scene keeps only the sounds of the districts you are in. Named banks (**Project > Audio > Banks**) group sounds a mission or cutscene needs, and the new **Set Sound Bank Loaded** node loads and unloads them. Sounds still playing when their layer or bank goes away are cut off cleanly, and anything else keeps playing, including through a scene switch. The *SPU2 sample RAM* meter now shows the worst moment it can find: zones close enough to be in together, layers your graphs load, and the banks they load.
- Sound effects now load per scene. A scene keeps only the sounds it uses in the PS2's 2 MB of sound RAM, and frees them when another scene starts. Sounds every scene uses, sounds in prefabs, and sounds you mark **Always loaded** in **Project > Audio** stay loaded for the whole game. Sounds nothing plays are no longer loaded at all. The *SPU2 sample RAM* meter shows the shared sounds plus the heaviest scene, which is the most that is ever loaded at once. *Play Sound* nodes no longer load a second copy of a sound that an emitter also uses.
- Walking through trees with distant impostors no longer drops frames. Every tree switching back to its full model used to be rebuilt in one go, about 13 ms on a PS2, so the impostor grove held 60 FPS standing still but fell to 50, or to 40 at a run. The full model is now baked over a few frames while the impostor is still shown, and models lit only by the sun bake several times faster (which also shortens scene loading). On the console the worst frame went from 27 ms of rebuilding to under 3 ms.
- Camera drags in the viewport no longer stop at the edge of the window. Orbiting, panning or pushing forward with the right and middle buttons together wraps the cursor to the other side of the viewport when it reaches an edge, so a drag lasts as long as your desk does.
- Shining the flashlight on a big block no longer switches off the lamps already lighting it. Large boxes skip only the torch's per-vertex cone (the beam lands on them as a per-pixel pool), and they used to lose every dynamic light with it.
- A flashlight held in the eye no longer takes a shadow away. It used to win the shadow slot of whatever it lit from the moon or a lamp, and a torch shadow cast from the eye falls exactly behind its caster, so the shadow simply vanished. A torch held off the eye (*Held right* / *Held below eye*) casts its shadows as before.
- A prop lit by a lamp keeps that lamp's light when the flashlight sweeps over it. Each mesh takes one dynamic light, and the torch used to take it whenever it scored brighter, so a ball next to the pulsing crystal turned torch-white and lost its blue. A lamp that properly reaches a mesh now keeps it, and the torch still lights what it is aimed at through its per-pixel pass.
- The flashlight lights physics objects per pixel like everything else, so a crate under a lamp shows the torch's hotspot on top of the lamp's light.
- The flashlight's pool on the floor no longer jumps when the beam's centre crosses the edge of a crate or another small object. It used to snap to the object's foot, or lie flat at the height of its lid, and back again, which looked like someone shaking the torch. Only walls and floors big enough to hold the pool move it now.
- A projected shadow no longer switches off when you walk past its caster. Only casters in view could hold a shadow slot, so once an obelisk left the frame the long shadow a lamp threw from it vanished from the screen. A caster whose shadow is in view now keeps its slot.
- The flashlight's ground pool no longer paints a bright band across the bottom of walls. It floated up to 1.2 units over the ground to clear bumps in the terrain, so on flat ground it lit the foot of every block it ran into. It now floats only as high as the ground under it needs.
- The lighting example is a night plaza now: a moon that throws the hero objects' shadows, thin fog, a street lamp carving shadow volumes over the crates, and a flashlight held in the right hand with torch shadows on. The dusk ambience is still in the project.
- *Drop to floor* moved from the Scene menu to the Edit menu.
- The start page's *Open folder...* button is now just *Open*.
- The Cutscene Director is easier to find your way around. The timeline takes the whole height of the window, and the shot preview (which can be folded away) sits above the inspector in a column on the right, so the inspector no longer scrolls in a small box. Object tracks open like a tree: a track into its channels (Position, Rotation, Scale, Color, Visibility), and Position, Rotation and Scale into X, Y and Z. Each row shows its value at the playhead and has a diamond that keys that row at the playhead or removes the key there. The time ruler stays at the top however far you scroll the rows, so you can always scrub. The Cinema workspace starts with the Project and Properties panels hidden, so the viewport and the Director get the full width (Ctrl+B and Ctrl+Alt+B bring them back; *Layout > Reset to built-in arrangement* applies this to an existing project). Keys are per channel now: dragging or deleting a key on the Position X row moves or removes position X only, and each axis interpolates between its own keys. Older projects open unchanged. Projects are saved as format 120.
- The procedural node inspector is a form instead of a long list of fields. Each node has a header with its icon, inputs and outputs, and buttons to preview the node, bypass it and show its help. Values that belong together share a row, min..max pairs are one range with draggable ends (with the falloff drawn on it), short choices are segments and on/off settings are chips. Safety caps sit in a collapsed *Advanced* row. Settings that do nothing in the current mode are hidden. Noise and Terrain masks show their result, Remap Mask shows its curve and Filter by Attribute shows a histogram of the incoming values. Pool rows are cards that show each model's share of the points. Output lists every property it can set on the generated objects as a tick list.
- The side panels slide away to give the viewport the room. Click the open page's icon on the left rail again (or press `Ctrl+B`) to hide the Project panel, and click any rail icon to bring it back. `Ctrl+Alt+B` hides the Properties panel. The two panel switches at the right end of the status bar and the View menu do the same. A hidden panel returns to the same place and size.
- Each window layout remembers which side panels it has hidden, so the Procedural layout can keep the Project panel out of the way while Default keeps both. New projects get Procedural with the Project panel hidden. In existing projects, *Layout > Reset to built-in arrangement* applies the new default. Projects are saved as format 119.
- The workspace switcher on the top bar ends in a **...** button that lists the other layouts (Material Designer, Procedural and your own) and *New layout...*.
- Text in the Output and Debug panels selects directly with the mouse, colours and all: drag, Shift+click, double-click for a line, Ctrl+A, then Ctrl+C or right-click *Copy*. The *Select text* switch is gone.
- The Debugger's **Build & Run** follows the PCSX2 | PS2 switch like the top bar's Run. With PS2 selected it used to launch PCSX2 anyway.
- **Copy image** in *Debugger > Screen* now pastes into Claude and other Electron apps on Windows; it used to fail there unless the PNG was copied from Explorer.
- The built-in layouts can no longer be deleted. They can still be renamed or reset. A project that lost one in an earlier version gets it back on open.
- Instanced scatter budgets count shared near/far geometry instead of flagging the sum of every placement as an over-budget mesh. The full placement sum remains informational, and the memory label identifies geometry-only estimates.
- Shared procedural object settings now live in the Output inspector instead of a disconnected graph card. Existing settings are preserved. Instance detail clearly shows that it is unused in Instanced mode, and its help explains the visual cost of simplification.
- Dense procedural previews keep their models above 4,000 instances instead of replacing the entire layout with green crosses. Pick Asset previews use the pool's distant models, Output draw distance and camera-frustum culling. Unassigned graph points retain their markers.
- The upscaler (BLSS) can draw **only the particle emitters** at the reduced resolution (*Draw reduced: Particles only*): smoke, haze, fire and rain lose three quarters of their fill while the rest of the scene, the HUD and every texture stay sharp, and depth of field, portals, split screen and frame extrapolation keep working beside it. On a physical PS2, upscaler-lab's haze yard went from 34.8 ms to 13.8 ms of frame work. See docs/upscaler.md.
- New **Set Upscaler (BLSS)** flow node (and `ctx.upscaler` for scripts) switches the upscaler while the game runs - Scene default, Off, On or Automatic - so a level can run native and drop to the reduced raster only in the zone where the smoke and sparks are. The switch moves no VRAM.
- BLSS's neural reconstruction is removed - the network, its trainer, the Neural Upscaler window and the `--blss-*` commands. It measured +0.3 dB over plain bilinear for ~5 ms of EE a frame. Projects keep the plain upscaler; the old settings are dropped on the next save (format 118).
- Tree Generator adds Forest oak and Hero oak presets with Blender-baked broadleaf sprays for fuller, muted foliage. Existing tree recipes keep their original appearance.
- deep-forest uses three fuller broadleaf oak variants and a textured Blender fieldstone (56 triangles near / 12 far). Includes editable assets and a reproducible physical-PS2 density/LOD benchmark; 7,000 trees and rocks hold 50 FPS on the sampled hardware route with oak LOD at 25 units and finer spatial culling.

### Real PS2

- PS2Link r8 fixes a startup hang observed on an SCPH-70004 booted through FreeDVDBoot/uLaunchELF by configuring the sound hardware's bus mapping and timing before muting leftover audio.
- `tools/ps2link/build.*` builds with the editor's native toolchain when it is installed (Docker stays the fallback, or `-Docker` / `--docker`), and the revision the boot banner shows is set in one place, `TYRAX_REV` in the patch. `build.sh` no longer ignores `--no-usb` and `--packed`.

### Examples

- Fixed distant holes in the cube example: procedural room shells and decorations no longer disappear independently at a short draw-distance cutoff.
- The cube map now has Blender-authored industrial room details: bevelled panels, hatch frames, vents, deck grips and safety markings. Editable Blender sources and a shared texture atlas are included; the PS2 detail mesh uses 1,048 triangles per room. All four doorways now have the same upright opening.
- deep-forest now scatters **8,692 instanced Blender spruces** (three variants, each with a 12-triangle distant version) instead of 2,800 merged pines, still at 50 fps on a PS2 and in 12.5 of 32 MB.
- The cube's rooms switch to a 240-triangle far-room model past 30 units (the Pick Prefab rows' new distant version), instead of drawing every distant room in full.

### TyraX2 and mainline integration

- The redesigned editor and the TyraX2 frame pipeline now ship together, including ordered frame submission, reflection and shadow improvements, and the accompanying qualification tools. **TyraX2 is the default renderer** for every project, 1.x projects included; the old synchronous renderer stays as *Preferences > Picture > Advanced > Renderer: Legacy*.
- Collision queries share mainline's grid or octree broad phase, including per-scene overrides and refreshed bounds for streamed models, scripts and spinners. Camera sweeps retain TyraX2's cached narrow phase while using the shared candidate query.
- Project format 114 combines the additive fields from all integration branches; explicit pipeline settings, Outliner visibility and collision choices survive saving and reopening.
- This is the 2.0 integration candidate. Cross-platform and release qualification still listed in the backlog remains outstanding.

### Fixes and speed-ups in games

- **Streamed music no longer turns into a buzz** a few seconds into a song: audsrv's EE callback thread ran below the game thread and starved. [Sound](https://github.com/doctorspider42/tyraX/blob/main/docs/sound.md)
- **Split screen runs at 50 fps instead of 25** on a PS2 (two-players): the per-half packets record into the frame instead of draining the queue. [Multiplayer](https://github.com/doctorspider42/tyraX/blob/main/docs/multiplayer.md)
- **The cube example on a PS2: 25 -> 50 fps** (scene 25.4 -> 8.8 ms) - a prefab a runtime procedural volume places now merges into chunks of its own cell, height included, instead of by each member's position, which built chunks spanning every floor that the EE had to clip. [Runtime procedural generation](https://github.com/doctorspider42/tyraX/blob/main/docs/procedural-runtime.md#how-it-draws)
- **Showcase on a PS2: 28.5 -> 50 fps** - each projected-shadow caster below its floor keeps its own clamped copy, a portal view culls against the opening's rectangle, and matcap passes are replayed from stamps.
- A static model picks its LOD level by the distance to its box, not its origin, so a big floor no longer swaps to its far level (and changes brightness) under the player's feet.
- A blob shadow lies on the top of a solid object (a model floor), not only on terrain and roads.
- Roads no longer swim on a console, and a headlight beam, blob shadow or lamp pool no longer shows holes at a road's edge.
- Roads and other 3D no longer smear into streaks for one frame when a texture uploads mid-frame.
- The bumper camera rides the car's body instead of swinging the bonnet through the lens.
- Projects without roads build again.

### The same project bakes the same files on every OS

- **Games have a built-in default font, Geist Mono SemiBold.** A font left on *Default* used to be the OS's own (Consolas Bold on Windows, Arial Bold on macOS, DejaVu Sans Bold on Linux), so every machine baked a different glyph atlas, HUD text sprites and save-busy sprite. Text in games that use the default font looks slightly different from 1.x.
- Model LODs, AO atlas layouts, physics hulls and the day/night sun no longer depend on the compiler or C library: no fused multiply-add on Apple Silicon, ties in sorts broken explicitly, and the trigonometry that ends up in committed files or bake signatures computed by the editor itself. A Mac no longer reads a Windows-made GI cache as stale (the Showcase lost its light probes that way).

### Editor fixes

- **Properties > Shadow** is one visible choice - *Auto · Off · Blob · Live · Baked* - with a line saying what it costs. The ambient-occlusion switch below it, which read "Cast shadow" and was taken for the same thing, is now **Contact shadow (AO)**.
- **Blob shape** is *Round* (vehicles: *Automatic*), *From mesh* or *Image*, with an image picker over `res/textures` and an import, instead of a list of every PNG in the project.
- Collision: the mode reads **Box** (it is not the mesh AABB once the model has its own box), a line says the collider is drawn red and the yellow outline is the selection, and *Fit to post* is **Fit to base**.
- **The Vehicle Editor says what is inherited.** *Editing: Defaults | <vehicle>* on top of the settings, a line saying how many values are the car's own, and on every row a fixed slot on the right: *Own* with a revert button (and a tick on the slider at the Defaults value), *Defaults* that turns into *Make own* on hover, *Model* for measured values, or why the row is faded ("needs a sample"). In Defaults the slot shows which vehicles follow, with a *Follow* per vehicle; a legacy vehicle gets *Adopt Defaults*. The dot and reset arrow that overlapped long captions are gone. Tabs are grouped by goal - Handling, Engine & sound (with a pitch-against-revs chart and the samples' length and size), Camera & HUD, Damage, Look, Model & cost - each counting the car's own values, and *Show: Own only* filters to them.
- **Loading Screens: animations, press START, no logo.** A new **animation** element plays sprite frames - as a **spinner** on its own speed, or as **progress**, where the load picks the frame and the last one (the crash) lands at 100% - from PNGs or a **sliced sprite sheet**. A screen can **wait for a button** after loading (Any / START / Cross) with a blinking or pulsing **continue prompt**. Clicking the **TyraX logo** card in the First power-on strip skips the engine's 2-second logo. **+** starts a screen from a template (Minimal bar, Dots, Corner spinner, Animated progress, Press START); *Play load* is now **Preview** and ends on the prompt like the game does. [Loading screens](https://github.com/doctorspider42/tyraX/blob/main/docs/loading-screens.md)
- **Loading Screens: Assignments.** One table says which load shows which screen - **Game start** (the first load at power-on: the start scene's screen, or a screen of its own such as a PRESS START title), the **Default**, then every scene (*Default*, a screen, *Built-in* or *None*) - and what each one resolves to. A screen now only reports who uses it (*Used by*, *Edit in Assignments*, *Make default*); the top strip is **First power-on** with a Game start pick, beside a **Between scenes** strip. A 115 screen that waited "only after the boot load" becomes the Game start screen on load, its scenes getting a copy that does not wait (format 116). [Loading screens](https://github.com/doctorspider42/tyraX/blob/main/docs/loading-screens.md#assignments)
- **The Project panel's Assets, Audio and Scripts pages are rebuilt.** Assets: thumbnails with use counts, type chips, folder, Unused, Grid | List, drag or double-click to place, a selection drawer. Audio: an SPU2 budget bar, a play button on every sound and track, lengths, sizes, who uses each, build settings on the selected track and one-click fixes (Convert, Edit in Drone Generator, Make music). Scripts: flow graphs with their triggers, C++ files with the classes they register and the objects using them, Attach for an unused class, Create for a missing one, and the last build's script error on top. [The editor's look](https://github.com/doctorspider42/tyraX/blob/main/docs/editor-theme.md)
- **Instanced procedural scatter.** The Output node's new *Draw as: Instanced* keeps one copy of each scattered model on the console and draws every visible copy as another matrix of that one mesh (one baked VIF body, an MVP and one DMA REF per copy) instead of merging world-space copies. A forest is no longer capped by EE RAM: 2,800 spruces on a PS2 use 11.4 instead of 24.2 MB. Copies are lit by the sun and ambient only. [Procedural generation](https://github.com/doctorspider42/tyraX/blob/main/docs/procedural-generation.md#instanced)
- **A distant version per pool row in procedural graphs.** Every Pick Asset and Pick Prefab row can name another model and a distance: past it, that model replaces every instance of the row - for a prefab, all of its merged members at once - while collision stays the near geometry's. Runtime volumes switch per chunk on the console; baked Pick Asset chunks get a merged `-far.obj` as their distant version (format 117). [Procedural generation](https://github.com/doctorspider42/tyraX/blob/main/docs/procedural-generation.md#distant-version-per-row)
- A vehicle's Properties have **Edit in Vehicle Editor**. The Vehicle Editor's *Drive it* is hidden for 2.0.
- **Opening an example you already have** asks first: *Open it*, *Copy with a new name* or *Overwrite*, instead of quietly making `<example>-2`, `-3`... [Start page](https://github.com/doctorspider42/tyraX/blob/main/docs/start-page.md#examples)
- **One mouse standard for every 3D preview**: right-drag orbits, middle-drag pans, the wheel zooms, a middle double-click re-centres - in the Vehicle Editor, the Asset Browser, the Material, Tree and Animation Editors, the model import preview and the Debugger's VU mesh view. The left button no longer turns the camera; it picks, paints and drags handles. [Preview navigation](https://github.com/doctorspider42/tyraX/blob/main/docs/editor-theme.md#preview-navigation)
- On Windows the Run button no longer sits under the window's caption buttons.
- Opening a bundled example shows the loading card at once and copies the example behind it (the vehicle playground used to freeze the window for seconds first).
- Opening the Asset Browser no longer stalls: thumbnails and model parses are spread over frames by time, and the 1.5-second rescan no longer re-reads every model.
- Every bundled example has a fresh `preview.png` - its first frame in PCSX2 on the 2.0 renderer, captured from the GS - at the top of its README and on its start-page card.
- The rail's tooltips no longer show "##rail"; the Menu Editor's *Install a copy...* no longer runs off the panel.

### Repository and installer

- The TyraX2 performance investigation is closed: its raw evidence (about 134,000 files, 10.6 GB under `docs/tyrax2-*`) and lab scripts are gone from the tree; the write-ups stay in `docs/`, the evidence in git history.
- Installers ship only the tools an installed editor runs (`ps2client`, `toolchain`, `ps2link`, `vscode-tyrax`), not the developer scripts in `tools/`.
- Updating on Windows lays the bundled examples down fresh, so files an example no longer has do not stay behind in the install and in the copies you open.


### A redesigned main window

- **The editor has its own typeface, Geist** (Geist Mono for numbers and logs), built in - the same letters on every platform, with the system fonts as a fallback for symbols Geist lacks.
- **A new look**: a deeper default palette, renamed *Graphite* (the *Face buttons* name and the PlayStation button symbols in the editor's own chrome are gone; the editor now carries a trademark notice in Help and on the welcome screen), every panel a rounded card on a darker background, inset value fields, and the interface font's heavier cut for titles. A panel docked on its own shows its own header (the Project panel reads "Outliner 81" with a + button) instead of a tab strip and a ▼ menu - drag the header to move it; panels sharing a dock node keep their tabs, drawn as quiet pills. [The editor's look](https://github.com/doctorspider42/tyraX/blob/main/docs/editor-theme.md)
- **The title bar is part of the editor** on macOS and Windows: the traffic lights (or the minimize / maximize / close buttons) sit in the top bar; drag its empty parts to move the window, double-click to zoom. *Edit > Preferences > Appearance > Use the native title bar* brings the system bar back after a restart; Linux keeps its own.
- **The menus moved behind the TyraX logo** at the top left (click it, press F10 or tap Alt); menu paths like *Tools > Remote Pad* are unchanged. The top bar now holds *project / scene* (the scene picker switches scenes; hovering the project shows its location, terrain and target), a "3 unsaved" marker that saves when clicked, four **workspaces** - Scene, Logic, Cinema, UI & HUD, one click each to a window layout - a wide **search** field, and on the right the build profile, a **PCSX2 | PS2** target switch, Build, a green **Run** and Stop.
- **Command palette** - Ctrl+K (Cmd+K on a Mac) or the search field: find and run commands, jump to an object, a scene, a window, a workspace or a documentation page by typing; Ctrl+Enter (Cmd+Return) hands what you typed to the AI Assistant as a draft.
- **A status bar** along the bottom with the BUILDING / BUILD FAILED, LIVE, DBG, LOGIC and SESSION chips, the last message, and the scene's object and selection counts.
- **An icon rail on the left** switches the Project panel between Outliner, Layers, Assets, Audio and Scripts - one page at a time, so the object list gets the full height - with the AI Assistant and Project Preferences at the bottom.
- **Outliner**: an icon per object type, filter chips (All, Models, Primitives, Lights, Areas, Audio, Cameras, Other), layers / prefabs / groups as folders with counts, an eye on every row to hide an object in the editor for the session, and a strip of layer chips that toggle a layer's visibility.
- **The viewport's controls float on frosted cards** that blur the scene behind them: a tool palette with icons and shortcut keys (Move, Rotate, Scale, Sculpt, Paint, Measure) at the top left; a view pill at the top centre with the projection, the look-through camera, the render mode, Surface snap and World | Camera; the selection at the bottom left - name, type, triangles and size in metres - with **Frame** (new hotkey **F**); and **Editor | As PS2** at the bottom right. *Center view* moved into the gear menu.
- **Properties** is headed by the object - its colour tile, its name in large type, its type - with a ... menu holding Duplicate and Delete object. Fields are rows with the caption on the left; every on/off option is a switch whose "(...)" text shows as a dim second line; help moved onto the caption (hover the underlined label). Solid objects get **Object / Material / Physics** tabs, kept while you select other objects. Position, rotation and scale name their axis inside each field (red X, green Y, blue Z, like the gizmo) in a fixed-width face; colour is a swatch with a picker and a #RRGGBB field.
- **The console drawer**: in the Default layout Output, Debug and the Debugger are tabs of one bottom panel, and a debug launch brings the Debugger forward (saved layouts keep their arrangement - *Layout > Reset to built-in arrangement* applies it). Logs use a fixed-width font with a time per entry, a gutter mark on error and warning lines, coloured level pills on one header row, and a count badge on the Output and Debug tabs.
- The Debugger layout (the Logic workspace) now docks the Project panel on the left instead of leaving it floating.
- `--ui-script`: menus are reached with `click 'TyraX menu'`, but a step naming a menu (`click Tools`) opens it by itself, so existing scripts keep working; `key ctrl+X` now reaches Ctrl shortcuts on macOS.
- Dear ImGui is now kept in the repository (`vendor/imgui-tyrax`) with small, documented patches, instead of being downloaded by the setup scripts.

### Every tool window, redesigned

Each tool now shows what it makes as the hero, puts its one main action in plain sight, says what an action will do before you click, and shows who uses what. Tools that change the scene open docked beside the viewport instead of floating over it.

- **Start page** (no project open): recent projects with when they were last saved, the 44 bundled examples (opened as a copy), and setup checks. **New Project** picks First person / Third person / Empty from cards and checks the name and folder before Create; the macOS path no longer shows a backslash. **Opening a project** shows real loading steps, Cancel, and keeps errors on screen with Back / Show in folder / Try again.
- **Edit > Preferences**: seven pages with search; every change applies at once and there is one Done button (Cancel used to undo only some changes); the PCSX2 field shows the auto-detected path. **Project Preferences**: a sidebar with search and a section index, label-left rows, segmented choices.
- **Flow Graph**: press Space (or right-click, or drop a wire on empty canvas) to search every node type; a dropped wire offers only nodes that take it and wires the new one. The graph follows the selected object (and the halted node while debugging), a node inspector holds every setting, and Frame all (F) / Tidy (L) lay it out.
- **Debugger**: a section rail instead of 13 tabs, a header with the game's state, real transport icons with F10/F11 (no more "? Pause"), a checklist with Build & Run when nothing runs, a "Stopped at" card, one time strip instead of Timeline/Rewind/Replay, Watch with a Changed column. **Remote Pad**: a drawn controller, "Game listening", shift-click to hold, the moment as a `--pad` line. **AI Assistant**: each edit turn ends in a change card with Undo; Ask only / Edit project / + Build & run.
- **UI Editor**: clicking a HUD bar opened the Motion blur panel - fixed. A list in true draw order, a 512x448 preview to drag and resize elements in, and a show/hide transition preview. **Loading Screens**: the boot sequence as a strip, "used by", assignment to scenes, Play load. **Input Map**: a binding matrix by mode, a DualShock 2 diagram, Press to set, clash check.
- **Menu Editor / Menu Preview**: the preview stays visible (Menu Preview shows every resolution with a fit verdict); a Flow event row says which graph handles it. **Credits**: the preview opens on the first block instead of black, with a timeline. **Save Editor**: the card's browser preview, who writes and reads each slot, card space.
- **Cutscene Director** previews the shot in its own window instead of taking over the viewport; shots are clips on the timeline. **Phone Camera** explains itself unlinked, shows the phone's view, and now holds Record.
- **Asset Browser**: folders | grid | inspector, Unused / Recently changed / Generated views, one main action, "Used by". **Import model**: the model fills the window, every option says what it writes, and a skinned .glb/.fbx no longer also writes a bind-pose .obj. **Material Editor**: the preview never shrinks, Look / Paint / Bake tabs. **Texture Atlas**: the VRAM verdict first, the page as a large view, settings for one texture (the clipped page combo is gone).
- **Animation Editor**: clips show who plays them, a trim timeline, "Set as Walk". **Map bones**: each rig in its own card, one "Apply N pairs". **Tree Generator**: a preset gallery, an impostor view, Save to project, and saved trees re-open from a `.tree` file. **Font Manager**: a specimen drawn from the real atlas with its VRAM cost.
- **Prefabs**: a thumbnail grid and one "Place in scene". **Particle Editor**: the preview in the middle, a layer stack, tabs. **Drone Generator**: opens on the project's own tracks, warns about chords the track never reaches (and *Fit length*), says when a patch changed since its render. **Vehicle Editor**: each value shows inherited vs custom with a per-value reset; Drive it in the header.
- **Terrain Editor**: tool tiles with what the next drag does, layers in paint order with coverage %, Paint creates the first layer. **Ambience Editor**: a new default **Simple** view - pick a look (Clear day, Sunrise, Golden hour, Overcast, Foggy, Night), drag the time of day, and set the sun, brightness, warmth, shadows, fog and "time passes" in plain words; the full editor (a drawn sky with a playable 24-hour track, keyframes, arcs, moon) is the **Detailed** view, one click away and remembered; **Global illumination** with Draft/Final presets and an always-visible Bake column. **Color Grading**: a before/after split preview and each look's GS cost. **Disc Layout**: Build first, then files by load stage.
- **Static Batches**: summary tiles, batch | detail, one-click fixes for objects drawn solo. **World Facts** opens in front with "used by and how". **VU Programs**: the micro-memory budget always on top. **Procedural**: per-node point counts, Auto-arrange, a bake-state card; canvas zoom now scales node text. **Neural Upscaler**: the verdict and its action first, the three checks as cards.
- **Live session**: one Host | Join dialog with a copyable invite that fills Join when pasted, the right firewall note per OS, and a panel showing who is where. **Update dialog**: rendered notes, what installing does, a real download bar.
- The showcase example's *Neon Dusk* drone now fits all its chords in its one minute.

### Finishing touches

- Cutscene Director's shot, Color Grading's before/after and the UI Editor's scene background show the scene as the game draws it - no grid, markers, gizmos or selection; Color Grading's preview also works with the viewport hidden.
- **Asset Browser**: .glb/.fbx thumbnails (vehicle bodies, for example) no longer stay empty; drag the inspector's preview to orbit a model or material, wheel to zoom, double-click to reset.
- **Vehicle Editor**: a Near | Far toggle shows the far body the console swaps to past the far distance. **Global illumination**: *Show probes in viewport* draws the baked probe grid, each probe coloured by its light.
- **Import model**: play, pause and scrub an animated file's clips before importing. **Animation Editor**: *Bones* draws the skeleton over the preview.
- **Material Editor**: the smart-mask controls are label-left with a two-handle Range, and each layer row shows a thumbnail of the layer. **Drone Generator**: every tab in the new look (dials, layer pills, voice chips, switches), and dial rows wrap in a narrow window.
- **Drone Generator**: a Simple view, now the default - start from a mood (Dark cave, Calm wind, Tense...), six plain sliders (Brightness, Pitch, Movement, Space, Wind, Chimes), Feeling, Over the track, Length in whole passes and Loudness, a waveform hero with Play/Stop, Another take with one-step Undo, *Save to project*, and *In the game*: which scenes play the track, Open in Flow, and *+ Play in another scene...* which wires On Start -> Play Music for you. The full editor is one click away as Detailed (remembered per machine); .drone files are unchanged.
- **UI Editor**: drop PNG files from Finder or Explorer onto it to add image elements where you drop them.
- **World Facts**: Queries, Rules, Scenarios and Blackboard follow the catalog's list | detail shape, with live true/false dots, a readable "Why?" tree and "Used by" for queries; drag rules to reorder them.
- A Viewport or Properties panel alone in its dock area shows a header strip; drag it to undock the panel.
- Hiding an object with the Outliner's eye is saved with the project and can be undone (editor only - the game still draws it).
- Menus and popups are fully opaque in the Graphite, Boot screen and Memory card themes.
- UI scripting: a `drop <target> <file> [dx,dy]` step simulates a file dropped from the OS.
- **Windows can leave the main window**: drag a panel's tab out of the editor and it becomes its own window (onto another monitor, for example); drag it back onto a panel to dock it. Turn it off in *Edit > Preferences > Appearance* (on by default; unavailable under Wayland). *Layout > Bring all windows back* docks every panel back where it was (windows that were never docked come back inside, floating), and a layout saved with a window on a monitor that is gone opens with that window inside the editor.
- UI scripting: `undock`, `dock` and `mainwindow` steps, and `shot` also writes every panel that is in a window of its own.
- **Run** in the top bar reads "Running 00:42" while your game is up in PCSX2 or on the PS2; hover it to Stop.
- The Debugger no longer says the game "stopped reporting" after you close it, relaunch it, or open a project whose game ran earlier.
- **Live session**: Host and Join keep separate port and join-code fields; the Session panel can **Follow** a participant's camera.
- **Edit > Preferences**: *Test connection* checks the ps2link IP without deploying.
- Opening a project can be cancelled while it loads (Cancel or Esc).
- **Input Map**: *Press to set* for the mouse button.
- The **command palette** (Ctrl/Cmd+K) finds every menu command, with its menu path and shortcut.
- **One controller drawing everywhere**: Remote Pad, the Input Map and every pad-button picker now show the same DualShock 2 - two round lobes, the four-key d-pad, the face buttons in their real colours, L2/L1 and R1/R2 as stacked tabs, Select/Start/Analog with its LED and big ridged sticks.
- **Remote Pad**: a pressed button fills and glows whatever pressed it (mouse, keys, a `--pad` script or the AI); the P1/P2 badge and the Pad 1/Pad 2 dots show the port; the stick gauges show the deadzone and the numbers sit under the pad; key caps sit beside the buttons they drive; the card lists what is held. With no game running the pad dims and its LED goes out.
- **Input Map**: the controller labels every bound button in tidy columns (leaders never cross), rings the selected action's button and marks two actions on one button in amber; click a button to select its action. *Press to set* makes the pad pulse - click the button on the drawing when no controller is connected. The Pad column shows each button's symbol.
- **Pad-button pickers** (Input Map, the Flow Graph's On Button, the flashlight's Toggle button) show the button's symbol and open over a small pad you can click.
- **AI Assistant**: a `press_pad` step shows small controllers with what the assistant pressed and for how long.

### Checked against the designs

Every tool and the main window were opened next to their approved designs; what differed is fixed.

- **Sliders** everywhere are a thin track with a round knob and the value at the row's end; click the value or double-click to type a number. Color Grading's Temperature track runs blue to orange.
- **Main window**: Properties keeps X/Y/Z values readable in the docked column and wraps long lines and button rows; the command palette no longer opens an empty "Debug" window; the examples open on the redesigned default layout.
- **Ambience Editor** and **Color Grading** open beside the viewport at a normal window size instead of on top of it. **Project Preferences > Frame delivery**: *Use the upscaler* is a switch with the reason a build would refuse it right under it. **World Facts** keeps fact names readable when docked. **Neural Upscaler**: the Compare card has an empty state instead of a temp-folder path.
- **Flow Graph**: the graph list keeps its width when docked, long names end in "..." and the footer switch is *Show all objects*. **Input Map**: *Press to set* sits beside its field. **Save Editor**: "Used by" wraps; the card-space meter lines up with its label. **Phone Camera**: no stray "P" past the window edge. **Cutscene Director** opens on the first visible frame instead of the black start of the fade-in.
- **Animation Editor**: *Borrow clips from a file > Import a file...* adds the file's clips (and opens Map bones, now in front) instead of stopping after the import. **Material Editor**: "used by" counts models that use their own .mtl; the preview readout no longer clips. **Particle Editor**: the "2D approximation" note shows at the default size. **Drone Generator**: the Root dial names the note (B1); the Simple view's Track swatch shows the track's sky.
- **Import model** opens framed on the model whatever its size; the VU1 cost line wraps. **Vehicle Editor**: *Drive it* is the green go button; opening a project with baked vehicles no longer marks it Unsaved.
- **Debugger**: the Hardware timeline is one card (what is loaded, a chip per frame, Load capture, *Arm next boot...*, an empty state, lanes coloured by meaning); the Watch Kind column shows chips; *Fire & continue* works on actions too (it fires the trigger that reaches them) and *Remove breakpoint* moved into the card's "..." menu; the armed-timer and VRAM lines no longer clip. **Remote Pad**: in a short window the pad shrinks so *Release all* stays visible.
- **Command palette**: typing "bake" lists every bake the editor can start - global illumination (this scene / all scenes), model AO, pending pre-lit models, shadow decals (this scene / all scenes), stale procedural volumes - new **Build** menu items that run the same code as the tab buttons.
- **Procedural**: pin and link colours follow the theme and the legend (points blue, masks pink, curves green); a *Procedural layout* button switches to the docked layout. **Neural Upscaler**: the *Project settings* tab is now a link to Project Preferences > Display > Frame delivery, which also holds the network's tuning. **Project Preferences**: sections are cards, every row shows a one-line meaning under its label, and Devkit channels are one table with each channel's polling interval in an *Every* column.
- **VU Programs** and the **Particle Editor**: nothing overlaps in a narrow window any more (legends wrap, long notes end in "...", rows rearrange).
- **Opening a project** shows which step is running and counts object files as they load. **Start page**: the Recent filter shows from two projects; New project / Open folder show their shortcuts. **New Project**: Claude Code starts selected; the start-type cards have new pictures - a crosshair view, the player from behind, an empty floor. **Sculpt / Paint**: the brush settings moved from a loose row at the top of the viewport to a card at the bottom centre, with a line saying what a drag does. **Drone Generator**: Detailed shows the Simple | Detailed switch at the top right too, not at the tab row. **Particle Editor**: with no effects yet it shows all six presets as cards (Fire, Smoke, Fog, Sparks, Rain, Custom) instead of an empty library and a "New fire effect" button. **Texture Atlas**: *Turn it on in Rendering prefs* (and *Rendering prefs*) opens Project Preferences on top of the Texture Atlas, scrolled to the *Texture atlasing* switch and ringed for a moment - before, the Preferences opened as a tab hidden behind it. **Project Preferences, reorganised**: five pages by what you are doing - *Game*, *Picture*, *World*, *Graphics*, *Build & debug* - each with its essentials up front and an **Advanced** fold for the rest; the line of explanation under every setting is gone (hover the caption), captions are plain words (*TV region*, *Picture mode*, *Colours*, *Torch shadows*), and a setting changed from what a new project starts with gets a blue dot - click it to reset - with a count per page and **Show what I changed**. Search also finds the old names. Walk, Run and Sprint speed (Preferences and a Player's Properties) and the animation frame rate are caption-left rows like the rest, with the metres in the field. **Trackpad navigation**: two-finger scroll pans the viewport (on by default on macOS; *View > Navigation controls > Two-finger scroll pans*), Cmd+scroll zooms, Option+scroll orbits, and Shift+right-drag pans in every scheme - no middle button needed. **GPU GI bake on a Mac**: *Bake on the GPU* (Ambience Editor > Global illumination) and `--bake-gi --gpu` now work on macOS through Metal - about 3x faster than all CPU cores on gi-showcase, and within 0.003% of the CPU bake. **One GPU switch**: *Edit > Preferences > Build > Use the GPU for bakes* (on by default) now decides for every bake that can use the GPU - global illumination and impostor captures; the Ambience Editor shows which engine it will use and links there. [Editor preferences](https://github.com/doctorspider42/tyraX/blob/main/docs/editor-preferences.md) **AI Assistant**: pad-script and build rows say how they ended. **Update dialog**: names the package in words with its size.

## 1.172.2

### Releases

- A brief network drop at Apple while the macOS image is finished (stapling the notarization ticket) no longer costs a release its `.dmg`: the step retries for a few minutes, an image whose ticket still cannot be attached ships anyway (it is notarized, and the app inside carries its ticket), and re-running a failed macOS job now adds its image to the release that already went out.

## 1.172.1

### macOS

- **The macOS download is here**: 1.172.0 went out without its `.dmg` (the packaging step missed a source file); this release carries both `tyrax-1.172.1-macos-arm64.dmg` and `-macos-x86_64.dmg`.
- From source on a Mac, the first Build & Run no longer stops at the toolchain setup with `machorelink.c: No such file or directory`.

## 1.172.0

### The macOS app

- **TyraX for macOS as a download**: `tyrax-<version>-macos-arm64.dmg` (Apple silicon) and `-macos-x86_64.dmg` (Intel), macOS 15 or newer. Open it, drag TyraX into Applications - signed with a Developer ID and notarized by Apple. [Installing and updating](https://github.com/doctorspider42/tyraX/blob/main/docs/updates.md#macos)
- **Builds PS2 games with nothing else installed** - no Homebrew, no Xcode. The app brings GNU make, the libraries the PS2 compilers need and ready-built OpenVCL/vclpp/bin2s; the first Build & Run downloads PS2DEV and is ready seconds later. Only a project with its own VU programs asks for the Xcode Command Line Tools.
- **It updates itself**: *Install* swaps the whole app for the new one and reopens it.
- Started from Finder, TyraX now also finds tools in `/usr/local/bin` and `/opt/homebrew/bin` (VS Code's `code`, Docker, a Homebrew PCSX2).
- From source, the PS2 toolchain no longer breaks when Homebrew upgrades `libmpc` and friends: it keeps its own copies.

## 1.171.0

### macOS

- **The editor runs natively on macOS** (Apple silicon and Intel, built from source): `xcode-select --install`, `./setup.sh --deps` (Homebrew, no root), `./build.sh --run`. Native file dialogs, Finder reveal, image copy to the clipboard and the system fonts (Helvetica Neue for the interface) all work; the interface is drawn at full Retina density.
- **Games build natively on a Mac with PS2DEV + OpenVCL**, no Docker: the first Build & Run installs the pinned PS2DEV v2.0.0 macOS archive, builds the in-tree OpenVCL (and runs its tests), vclpp, bin2s and audsrv, exactly as on Linux. A first game build took 43 seconds on an Apple silicon Mac; an unchanged one under a second. [docs/native-toolchain.md](https://github.com/doctorspider42/tyraX/blob/main/docs/native-toolchain.md#macos)
- PCSX2 is found as `PCSX2.app` in `/Applications` or `~/Applications`, and the generated `run.sh` launcher looks there too.
- No macOS release package yet - see [Installing and updating](https://github.com/doctorspider42/tyraX/blob/main/docs/updates.md).

## 1.170.2

### AI support in game projects

- The guides **Add AI support** installs into a game project (Claude Code, Codex, Copilot) are up to date again: which files the editor rewrites (now including `src/main.cpp`, `Makefile`, `.vscode/c_cpp_properties.json` and the split `src/gen/game_*.gen.cpp` sources) and which you can own (`inc/scripts/flow_nodes.hpp` added), object vs global scripts, the flow-graph JSON (`number` links, `pin`/`fpin`), the one model importer and the Model / Animated model entities, and the bakes a build reads vs. re-bakes.
- They now list the CLI verbs an assistant needs: `--migrate`, `--bake-status`, `--capture-frame`, the full `--import-model`, `--build --run-ps2` / `--rebuild` - and `--search-docs`, so an assistant can read the editor documentation the guides cite.
- `--help` shows `codex` for `--add-ai-support`, and lists `--bake-shadows`, `--bake-particles`, `--bake-object-light` and `--capture-frame --timeout`.
- `--list-nodes` (and so the AI flow-graph generator and assistant) names the three outputs of **On Fact Changed** (changed / became true / became false) - it used to show one - and describes the wired number of Clamp, Lerp and similar nodes as the value they operate on rather than as overriding the first parameter.
- `docs/vu-authoring.md`: project VU code is compiled by the build machine's own `g++` in the default native build, in the container only with `--docker`.

## 1.170.0

### One model importer

- **One importer for `.obj`, `.glb` and `.fbx`**, with a live preview before anything is written (right drag orbits, middle drag pans, wheel zooms; missing textures show as white surfaces). [docs/model-import.md](https://github.com/doctorspider42/tyraX/blob/main/docs/model-import.md)
- **Always the same result**: a static model is `.obj` + `.mtl` + PNG textures, an animated one keeps its `.glb`/`.fbx` + `.mtl` + PNGs. Every import writes a `.mtl`, even for a model without materials, and extracts every texture (embedded ones included) as PNG.
- **Split a pack** into one model per top-level object or per mesh, untick the ones you do not want, click a piece in the preview to find it in the list, and place them all at the file's own layout as one editor group.
- **Pivot, real-world size, clean-up** (degenerate and duplicate triangles), **triangle reduction** and **max texture size**, with the VU1 vertex count the build will actually ship.
- **Folder**: file imports into a catalogue (`res/models/terrain/rocks`); remembered between imports.
- **Search folder...** finds textures that are not where the file says (packs that point at the artist's own disk).
- Headless: `tyrax-editor --import-model <project> <model> [--split top|mesh] ...`.

### Model and Animated model

- **Add object > Object > Model... / Animated model...** replace the long list of files. Each opens a **model picker**: folder tree, thumbnails, search, *Import new model...*.
- The entity decides static or animated, not the file: a `.glb`/`.fbx` placed as a static Model gets an `.obj` twin.
- The picker lists models from **anywhere under `res/`**, so projects made before this release (models in `res/aster/`, hand-made folders) show all of their models. Nothing to migrate.

### Properties

- Models, primitives and save points have **collapsible sections**: Model, Transform, Material, Gameplay, Distance and LOD, Performance, Lighting and shadows, Scripts.
- The impostor controls became one **Distant version** choice (None / Billboard / Hull proxy) with one Bake button and *Switch at*. The checkbox labelled "Impostor" (really "capture on the GPU") is gone; GPU capture with CPU fallback is automatic.
- **Material > Edit...** is always there on a model and opens the model's own `.mtl` when no override is assigned. The per-material list moved to the Material Editor; Properties only warns about missing textures. The Material field lists every `.mtl` under `res/`.

### Fixes

- A `.fbx` with more than 256 meshes lost every object past the 256th (the skeletal parser's palette index is a byte). Static `.fbx` geometry is now read directly: 545 of 546 objects on a real pack.
- `--ui-script`'s `drag` takes an optional `left|right|middle` button.
