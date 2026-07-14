# Object scripts (Unity-style components)

Custom game logic is written in C++ and compiled into the ELF by the normal
build - there is no interpreter on the PS2, so scripts run at native speed.
An **object script** is a class you write once and *attach* to any number of
scene objects in the editor, the way components work in Unity: every
attachment becomes its own instance at scene load, with its own member
variables and a `self` pointer to the object it hangs on. Attach `Spinner`
to five crates and five independent spinners run; detach it and the class
costs nothing.

```
Properties > Scripts (attach "Spinner" to box-1, box-2)
   │  stored per object in <name>.tyra ("scripts": ["Spinner"])
   ▼
src/scripts/object_scripts.gen.cpp        ← regenerated on every build:
   (scene, object, class-name) table  +  a driver that owns the instances
   ▼
scene load: driver looks each name up in the TYRA_OBJECT_SCRIPT registry,
news one instance per attachment, calls onStart; every frame it refreshes
`self` and calls onUpdate (and onUsed when the player USEs the object)
```

Your `.cpp` files in `src/scripts/` are **never regenerated or parsed for
compilation** - the editor only scans them (read-only) for
`TYRA_OBJECT_SCRIPT(...)` registrations to populate the attach list.

## Quick start

1. Select an object and click **New script...** in **Properties > Scripts**
   (also available in the Project panel's *Scripts* section). The editor
   writes `src/scripts/<name>.cpp` from an attachable stub and attaches the
   class to the selected object.
2. Click **Open in VS Code** (Project panel > Scripts) - the generated
   `.vscode/c_cpp_properties.json` gives working IntelliSense against the
   engine and PS2SDK headers.
3. Edit the class:

```cpp
// bobber.cpp - created by TyraX. This file is yours.
#include "scripts/script.hpp"

namespace Mygame {

class Bobber : public ObjectScript {
 public:
  void onStart(ScriptContext& ctx) override {
    baseY = self->data.position[1];      // remember where we were placed
  }

  void onUpdate(ScriptContext& ctx) override {
    t += g_frameDt;                      // wall-clock seconds, PAL == NTSC
    self->data.position[1] = baseY + 0.5F * sinf(t * 2.0F);
    self->dirty = true;                  // geometry changed - rebuild
  }

 private:
  float baseY = 0.0F;
  float t = 0.0F;
};

TYRA_OBJECT_SCRIPT(Bobber);

}  // namespace Mygame
```

4. Attach the class to more objects with the **Attach script...** combo -
   each gets an independent instance. **Build & Run** (`F5`).

## The API

Everything lives in the generated `inc/scripts/script.hpp` (regenerated
while its ownership-marker line is intact, so the API tracks the editor).

### Lifecycle

| Override | Called |
|---|---|
| `onStart(ScriptContext&)` | Once per scene (re)load, before the first `onUpdate`. `self` is already valid. |
| `onUpdate(ScriptContext&)` | Every frame while the owning scene is active (paused while a pausing menu is open, like all scripts). |
| `onUsed(ScriptContext&)` | The frame the player pressed USE on `self` (objects with **Usable** checked - same dispatch as the flow graph's *On Used* trigger). |

Instances are created at scene load and deleted when the scene is left -
member variables reset on every scene switch or reload. For state that must
survive scenes or power-off, write `ctx.saveValues` (see *Save data* in the
Project panel).

### `self` - the attached object

`self` is a `RuntimeObject*` pointing at the object this instance is
attached to (`selfIndex` is its index in `ctx.objects`); the driver
refreshes it every frame. Mutate the object through it, then set
`self->dirty = true` so the geometry rebuilds:

| Field | Meaning |
|---|---|
| `self->data.position/rotation/scale` | The transform (rotation in degrees, X→Y→Z order). |
| `self->data.color` | RGB 0..1 - mesh tint on solids, particle tint on emitters, free parameter on empties. |
| `self->visible` | Show/hide (also mutes sound emitters, disables particle emitters). |
| `self->velocityY` | Vertical velocity used by object physics. |
| `self->dirty` | Set true after changing any of the above so the frame's rebuild picks it up. |
| `self->anim*` | Animated-model playback state - prefer the `playAnimation`/`stopAnimation`/`animationFinished` helpers (see [animated-models.md](animated-models.md)). |

### Registration

`TYRA_OBJECT_SCRIPT(ClassName);` goes at file scope **inside your project
namespace** (the stub places it correctly). The stringized class name is
what the editor's attach list shows and what is stored in the project file -
rename the class and existing attachments show a red *not found* until you
re-attach or rename them back. One file may register several classes.

### `ScriptContext` - everything a script can see

The same context global scripts receive; the fields you will actually use:

| Field | Meaning |
|---|---|
| `ctx.engine` | The Tyra engine: `ctx.engine->pad` for buttons/sticks, renderer, audio. |
| `ctx.playerPosition` | Camera/player position this frame. |
| `ctx.objects`, `ctx.objectCount` | All runtime objects of the active scene (your `self` is `ctx.objects[selfIndex]`). |
| `ctx.skyColor` | Write to retint the sky/clear color. |
| `ctx.teleport*` | Set `teleport = true` + `teleportPos`/`teleportYaw` to move the player. |
| `ctx.usedObject` | Index of the object USEd this frame (-1 none) - `onUsed` is the ergonomic form. |
| `ctx.saveValues`, `ctx.saveTexts` | Named values persisted in memory-card saves. |
| `ctx.openMenu`, `ctx.menuEvent` | Open a game menu / react to a menu entry's flow event. |
| `ctx.scene`, `ctx.requestScene` | Active scene index; write an index to switch scenes after this frame. |
| `ctx.sceneGeneration` | Bumps on every scene (re)load - object scripts rarely need it (their lifetime already tracks it). |

Handy globals from `scene_data.hpp` (included via `script.hpp`):
`g_frameDt` (seconds per frame - multiply all speeds by it so PAL and NTSC
play identically), `g_frameScale`, the `SCENE_*` accessor macros.

## Empty objects

**+ Add object > Empty** inserts a pure transform: a small sphere marker in
the editor viewport, *nothing* in the game - no geometry, no collision, no
USE target. It exists exactly so scripts and flow graphs have something to
hang on:

- an anchor for an object script that manages a spot in the world (spawner,
  trigger zone via distance check, cutscene camera target),
- a waypoint: flow-graph *Move Object* nodes and scripts can read/write its
  position like any object,
- its editable **color** never renders in-game, which makes it a free
  per-object parameter (`self->data.color`) - e.g. one "SkyTint" script
  attached to differently-colored empties per scene.

Rotation, scale and *Save state* work like on any object; collision is
always off.

## Global scripts

The pre-existing flavor still works and is the right tool for logic that is
not about one object: derive from `Script`, override `init`/`update`,
register with `TYRA_SCRIPT(MyScript);` (note: at file scope, *outside* or
inside the namespace with a qualified name - see the FPP example script).
One instance for the whole game, `update` runs every frame in every scene.
Existing projects keep compiling unchanged; the flow-graph compiler and the
object-script driver are themselves global scripts under the hood.

## Performance

- Scripts are compiled MIPS code, not interpreted - an empty `onUpdate` is
  one virtual call per attached instance per frame (nanoseconds on the EE).
- Class-name lookup happens **only at scene load**; the per-frame path
  touches no strings.
- Scenes with no attachments cost an empty loop; unattached classes cost
  only their code size.
- The usual costs are what the script *does*: `self->dirty = true` rebuilds
  that object's vertex buffers this frame (fine for a handful of moving
  objects - the flow graph's Move/Recolor nodes do the same), and per-frame
  math over `ctx.objects` scales with scene size.

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| Class missing from the **Attach script...** combo | No `TYRA_OBJECT_SCRIPT(Name);` in a `src/scripts/*.cpp` file (the scan is textual and per-file - check spelling and that the file is saved). |
| Attached entry shows red *"not found"* | The registration disappeared (file deleted, class renamed). The game will skip it with a log line; remove the entry or restore the class. |
| `Object script not registered: X` in the game log (Debug panel, `bin/log.txt`) | Same as above, seen at runtime - scene loads fine, that attachment is skipped. |
| Script does nothing | Is it attached to an object *in the active scene*? Attachments are per object per scene; the class running in scene A does not run in scene B. |
| State resets when the scene changes | By design - instances are recreated per scene load. Persist through `ctx.saveValues` or a `static` member (statics survive scene switches but not power-off). |
| Movement speed differs between PAL and NTSC | Multiply per-frame deltas by `g_frameDt` (seconds) or `g_frameScale` (50 Hz-relative) - never hardcode per-frame steps. |
| `onUsed` never fires | The object needs **Usable** checked (Properties), the player must be close and press USE; menus swallow input while open. Markers (spawn, player, lights, empties) cannot be usable. |
| Nothing moves while a menu is open | Menus with *Pause gameplay* checked pause all scripts - that is the pause menu working as intended. |
| Editing the `.cpp` does nothing in the running game | Scripts compile at build time - **Build & Run** again (there is no hot reload on the console). |
