# The start page, New Project and opening a project

The start page is what the editor shows while no project is open: the ways
into a project on the left, the bundled examples on the right, and a strip of
setup checks at the bottom. It replaces the docked layout until a project
opens - the panels would only say "No project open" - and *File > Close
Project* brings it back.

![The start page: New project and Open folder, the recent projects with when
each was last saved, the bundled examples as cards, and the setup chips at the
bottom.](img/start-page.png)

## Recent projects

**New project** opens the [New Project](#new-project) dialog (Ctrl+N, Cmd+N on
a Mac); **Open folder...** picks a folder holding a `.tyra` file (Ctrl+O).
Each button shows its shortcut inside it.

Below them, the projects you opened, most recent first, each with its folder
(your home folder shown as `~`) and when it was last saved. Click a row to open
it. As soon as there are two, a **Filter** field beside *Recent* narrows the
list by name or folder. The **...** on a hovered row offers *Open*, *Locate...*, *Reveal in
folder* and *Forget* - forgetting only drops the entry, the project stays on
disk.

A project whose folder is gone (an unplugged drive, a renamed folder) stays in
the list with a **moved or deleted** badge: clicking it asks where it is now
(*Locate...*) instead of failing, and the found folder replaces the entry.

The list lives in `editor.ini` (one `recentProject=` line per folder, at most
ten); the "last saved" time is the project file's own date, so it needs no
bookkeeping of its own.

## Examples

Every package ships the example projects of the [examples/](../examples)
folder. The right-hand side lists them as cards - the picture from the
example's README when it has one, its title and first sentence, and a category
tag. **Featured** shows six of them; **All** and the category chips
(Rendering, Gameplay, Vehicles, Cinema & UI, Performance) show the rest.

Clicking a card **opens a copy**: the example folder is copied into your
projects folder (*Edit > Preferences > Projects*, `~/TyraProjects` by default)
as `<example>`, and the copy opens. The shipped example is never edited - an
installed package may not even be writable, and the next update would
overwrite your changes.

When the projects folder already has that example (a `<example>` folder with a
`.tyra` in it), a dialog asks first:

- **Open it** (the default, Enter) - carry on with the copy you have.
- **Copy with a new name** - a fresh copy beside it as `<example>-2` (`-3`...);
  yours stays as it is.
- **Overwrite** - deletes that folder and copies the example fresh; your
  changes there are lost.
- **Cancel** (Esc).

## New project

*New project* on the start page, *File > New Project* or Ctrl+N (Cmd+N).

![The New Project dialog: the three start types as cards on the left, name,
location and the world on the right, a summary of what Create makes, and
Create project.](img/new-project.png)

**Start as** (left) is the one choice that cannot change later: it picks the
game sources the project generates, and those are yours to edit, so switching
afterwards would overwrite your work or orphan it. Each card's picture is the
same small floor-and-horizon scene: seen down a crosshair (First person), with
the player standing in it seen from behind (Third person), or empty (Empty).

| Card | What you get |
|---|---|
| **First person** | A Player you walk and look through. |
| **Third person** | A camera on a boom behind the Player; the avatar is the Player object's own animated model (assign a `.glb`/`.fbx` in Properties - the rig works without one). |
| **Empty** | No player and a camera that stays put - drive it from a cutscene, a Camera object or your own code. |

On the right:

- **Project name** - letters, digits, `-` and `_`. **Location** - the parent
  folder (*...* to browse; the default is *Edit > Preferences > Projects*).
  The line under them shows the folder Create will make, joined with this
  system's separator, or in red why it cannot: no name, a bad character, or a
  folder of that name that already holds files. **Create project** stays off
  until that line is green, and the reason is repeated in the footer.
- **World** - *Ground* (the *Create terrain* switch: off makes a scene with no
  ground at all, for interiors and platformers - see
  [terrain](terrain.md)), *Size* (64, 100, 256, 512 or Custom for any width
  and depth, with the size in meters beside it) and *Scale* (how many units
  are a meter - [world scale](world-scale.md)).
- **AI coding assistants** - *Claude Code*, *Codex*, *GitHub Copilot*: guidance
  files for an assistant opened in the project ([AI support](ai-support.md)).
  *Claude Code* starts selected; click it to leave its files out.
- The **summary** card says what Create makes: the scene, its ground and
  player, and that it builds in the debug profile with Live Link on. A player
  with no ground gets a warning - it would start in mid-air.

The project opens as soon as it is created.

## Opening a project

Opening a project (from the start page, *File > Open*, the recent list or the
command line) shows a card with the project's name - the one in its `.tyra`
file, not just the folder's - where it is and how many scenes it has, and the
steps of loading: **Project file**, **Objects** (with the count), **Terrain**
(heightmaps) and **Workspace** (the viewport's meshes and textures). Finished
steps get a tick, the running one a spinner, the ones still to come an empty
ring - the steps follow the reader as it goes, and while it reads the object
files *Objects* counts them (`245 / 489`) and the bar moves with it. Inside the
other steps the bar does not pretend to know a percentage. The time so far is
in the corner.

![The Opening card: project name and folder, the four loading steps and
Cancel.](img/opening-project.png)

**Cancel** (or Esc) stops the load and returns to the start page - or, when
another project was open, to that project, untouched. Reading the files
(*Project file*, *Objects*, *Terrain*) runs beside the card, so Cancel works in
the middle of it: the reader stops before its next object file, the card says
*Cancelling* for that moment, and the half-read project is thrown away along
with the custom nodes, screen effects and menu styles it had registered. The
last step, *Workspace*, builds the viewport's meshes and textures on the
editor's own thread and cannot be stopped, so Cancel is greyed out once it
starts.

When opening fails (a damaged `.tyra`, a project from a newer editor, a
migration you declined), the card turns into the error: what failed, and
**Back to start**, **Show in folder** and **Try again** - instead of an OS
dialog over a screen that no longer says what was being opened. A project in
an older format still asks before migrating, as before
([format versioning](format-versioning.md)).

## Setup chips

The strip at the bottom says what the first Run will need, before it fails:

| Chip | Says | Click |
|---|---|---|
| TyraX *version* | up to date, an update is available (amber), or nothing yet | opens the update dialog, or checks now |
| PCSX2 | found (the configured path, else the same auto-detect Run uses) or not found (amber) | Edit > Preferences |
| Native toolchain | ready, or set up on the first build (amber - a one-time download); *Build: Docker fallback* when that backend is picked | Edit > Preferences |
| Real PS2 | the ps2link IP, or "no IP set" | Edit > Preferences |

The PCSX2 and toolchain checks look at the disk every few seconds, so
installing PCSX2 while the page is open turns the chip green without a restart.
On the right: links to Preferences, these docs and the release notes.
