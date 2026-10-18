# Editor Preferences

Editor Preferences (*Edit > Preferences*, or *Preferences* on the start page)
holds the settings that belong to this machine rather than to a project: the
theme, where new projects go, where PCSX2 is, the PS2's address, the build
backend, your name in sessions, the AI backend and the update check. They live
in `editor.ini` in the editor's config folder and are shared by every project;
project settings are in *Project Preferences* instead.

![Editor Preferences on the Running games page: the auto-detected PCSX2 path,
the ps2link IP with its "not set" state and Test connection, the console log limits, and Done.](img/editor-preferences.png)

## One rule: changes apply at once

Every change takes effect the moment it is made and is saved to `editor.ini`
straight away - a switch or a pick when you click it, a text field when you
press Enter or leave it. There is no Save and no Cancel, only **Done**, the
same model as Project Preferences. (The dialog used to stage the text fields
for Save while the theme, title bar and update switches saved themselves, so
Cancel undid only half of it.) An open project picks up the new emulator path,
PS2 address and build backend immediately.

## Pages

The sidebar has seven pages; **Search settings** above it filters the rows of
every page by name and description (try `ip`, `docker`, `model`).

| Page | Settings |
|---|---|
| **Appearance** | The theme as cards with their colours (applies on click - see [the editor's look](editor-theme.md)); *Use the native title bar* (macOS and Windows; a *Restart TyraX to apply* badge until the next start); *Windows can leave the main window* (a panel dragged out of the editor becomes its own OS window - [details](editor-theme.md#windows-outside-the-main-window); applies at once, on by default except in a Wayland session, where it is unavailable); the interface font and scale in use. |
| **Projects** | *Default folder* for New Project and for opened examples (empty = `~/TyraProjects`, shown in the field). |
| **Running games** | *PCSX2*: empty means auto-detect, and the field shows what was found ("Auto-detected: ...") or that nothing was; a configured path that does not exist is flagged. *ps2link IP* for Run on PS2 (F6) - [setup](ps2link-setup.md) - with **Test connection**, which says whether ps2link listens at that address (answered in N ms / no answer within 2 s / refused - not ps2link) without deploying anything. *Console log*: lines per session file and how many files to keep. |
| **Build** | *Build backend*: Native (default) or Docker fallback. The Docker-only *Compiler image* and *Image name* appear only when the fallback is picked ([toolchain image](toolchain-image.md)). |
| **Collaboration** | *Display name* in [live sessions](collaboration.md) (empty = your system user name) and the *Joined-project cache* folder. |
| **AI assistant** | *Backend*, *Model* (or Custom with a free *Model id*) and *Extended thinking*, used by the [AI Assistant](ai-chat.md) and [Generate with AI](ai-flow-graph.md). |
| **Updates** | *Check for updates at startup*, *Check now*, and *Stop skipping* for a skipped version ([updates](updates.md)). |

## Moving the camera without a middle button

*View > Navigation controls...* picks the mouse scheme (Tyra, Blender, Maya,
Unity), the fly keys and the sensitivities - all machine settings in
`editor.ini`. Every scheme pans with a middle-button drag, which a laptop's
trackpad does not have, so two more ways in:

- **Two-finger scroll pans** (*Trackpad*, on by default on macOS): the scene
  follows your fingers, **Cmd+scroll** (or Ctrl+scroll) zooms and
  **Option+scroll** orbits. Turn it off to zoom with a mouse wheel again.
- **Shift + right-drag pans** in every scheme - on a trackpad, a two-finger
  press and drag with Shift held.

Pinch-to-zoom is not available: the window library the editor uses does not
pass the gesture on.

## Project Preferences looks the same

*Project > Preferences* (Ctrl+,) holds the project's own settings with the same
model - changes apply at once and are saved with the `.tyra`. It is five pages
by what you are trying to do:

| Page | Up front | Under **Advanced** |
|---|---|---|
| **Game** | the preset, the player's height and speeds (or the orbit speed), two players, gravity and jump | sprint, stick deadzones and response curves |
| **Picture** | TV region, picture mode, widescreen, colours | PAL picture, supported resolutions, dithering, triple buffering, VSync |
| **World** | world scale, terrain size, detail, view distance and material, the loading screen | terrain detail distance, the AI navigation grid |
| **Graphics** | texture colours, shadows, usable-object highlight, the Ambience Editor | performance (batching, occlusion culling, atlasing...), detail at a distance, reflections, the upscaler and frame extrapolation |
| **Build & debug** | debug or release, on-screen overlays, keyboard & mouse, the live tools, AI support | ps2link keyboard, bakes before a build, how often the live tools check in, the crash handler |

Each page shows its essentials in one card; the **Advanced** fold under it is
closed until you open it. Rows are short: the explanation is on the caption's
hover, and only a few essential rows carry a one-line hint. A setting changed
from what a new project starts with gets a **blue dot** in front of it - click
the dot to reset it - and each page in the sidebar shows how many it has.
**Show what I changed** lists all of them (click one to jump there), and
**Search settings** finds a setting by its name or an older one (*Display mode*,
*devkit channels*, *BLSS*) and jumps to it, opening Advanced when it lives there.

![Project Preferences on the Build & debug page: the essentials card, the live tools with their one-line hints, and the closed Advanced fold.](img/project-preferences-build.png)
