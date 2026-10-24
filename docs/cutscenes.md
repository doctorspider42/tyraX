# Cutscenes

A cutscene is a keyframe timeline that poses scene objects and, optionally,
takes over the game camera for a while — authored in *Tools > Cutscene
Director*, compiled into the PS2 game as a runtime player, and started from a
flow graph with **Play Sequence**. This page covers what a cutscene does to the
rest of the game while it runs: the HUD, the USE prompt, and how the player
gets out of one early.

For the camera side of the Director — importing a phone-recorded 6DoF move as
keyframes, or using the phone as a live viewfinder — see
[camera takes](camera-takes.md) and [the phone camera](phone-camera.md).
`examples/cutscene-demo` is the worked example for all of it.

## The Director window

![The Cutscene Director on cutscene-demo's "The Reveal", played by the pedestal's Play Sequence: on the left the transport and the timeline - the camera lane as named clips per camera, the Fades & bars lane, and the hero track opened into Position, its X, Y and Z rows (each with its value at the playhead and a key-here diamond), the closed Rotation row and the + Scale / + Color / + Visibility chips, then the closed obelisk, sparks, cam-dolly and cam-crane tracks; the key at 5 s is selected on the Position X row, so it is highlighted on the X, Position and hero rows. On the right the shot at the playhead (cam-wide, FOV 65, Cinema bars) over the Key tab: hero · Position X, key 2 of 4 on Position X, also keyed here Position Y Z and Rotation, Time 5.00 s and the easing.](img/cutscene-director.png)

The header picks the sequence (and adds one), says which graphs start it —
*played by pedestal (Play Sequence)*, or *not started by any graph yet* —
shows the phone camera link's state (click it for *Tools > Phone Camera*,
where a phone move is recorded into the sequence) and imports a take.

The window is two columns. On the left, under the transport, the **timeline**
takes the whole height; on the right the **shot** sits over the
**inspector**. Drag the line between them to trade room. The **Cinema**
workspace docks the Director along the bottom half of the editor, with the
Project and Properties panels slid away (Ctrl+B / Ctrl+Alt+B bring them
back). *Layout > Reset to built-in arrangement* brings that to a project whose
Director layout predates it.

- **The shot** at the playhead is rendered through the sequence camera, as the
  game draws it (no grid, markers, gizmos or selection outline), with the bars
  and fades the console will composite and a label naming the camera, its FOV
  and shake. The arrow on its header folds it away when the inspector needs
  the room. *Shot* leaves the main viewport to you; *Also drive the viewport*
  makes it film through the sequence camera too. Opening the Director, or
  picking another sequence, parks the playhead at the end of the fade-in - the
  first frame with something to see - instead of the black 0:00 (the saved
  sequence is not touched; rewind still goes to 0).
- **The timeline** shows the camera lane as **clips** — one per shot, named
  and coloured by camera, as long as it holds; a Smooth or Linear blend fades
  into the next shot's colour, a Cut is a hard edge. Click a clip to select
  the shot, drag its left edge to retime the cut, double-click to park the
  playhead on it. **Fades & bars** draws the fade and bar ramps at both ends.
  The time ruler stays at the top however far the rows are scrolled, so the
  playhead can always be scrubbed, as the names stay at the left.
- **Object tracks are a tree.** A track row shows every key of the object and,
  closed, its animated channels as a dim `P R S C V` summary. Its arrow opens
  one row per animated channel (Position, Rotation, Scale, Color, Visibility),
  and the arrow of Position, Rotation or Scale opens one row per axis. Each
  row shows its value at the playhead, and an open track ends in a row of
  **+ Channel** chips for the channels it does not animate yet.
- **The inspector** has two tabs. *Key*: the selected camera shot (*Shot
  from*, *Into next* — Cut / Smooth / Linear, the three easings named for what
  they do to a camera —, FOV, Shake, Roll, Time, *Look through*) or object key
  (Time, easing, the values it sets, Re-snapshot, Delete), plus the selected
  track's channels. *Sequence*: everything below.

### Per-channel keys

A key on a row stands for **what that row shows**. The same key at 4.98 s can
be one diamond on the track row, one on the Position row and one on the
Position X row - and what you do to it depends on which one you touch:

| On the... | Drag retimes / Delete removes |
|---|---|
| track row | the whole key, every channel it sets |
| channel row (Position) | position X, Y and Z of that key |
| axis row (Position X) | position X only - Y and Z stay where they were |

Retiming part of a key splits it: the dragged components move to their own
key, the rest stay. Dropped within a frame of another key, they join it.
Double-click a row's lane to key that row's components there; the diamond at
the right of a row keys them at the playhead from the object's pose, or, when
they are already keyed there, removes that key. Right-click a key for its
easing and the three deletes (this row's components, the channel at this
time, the whole key); right-click a channel row to key it, delete all its
keys or stop animating it.

Each component interpolates between the keys that set it, and nothing else.
Position Y keyed at 0 s and 9 s glides over those 9 s, whatever X and Z do in
between. A component no key sets keeps the object's own value. The easing
belongs to the key - to everything it sets at that time.

Projects saved before per-channel keys (format 119 and older) open
unchanged: each of their keys sets every component, which is what a key meant
then. A key that sets only some components is saved with a `"ch"` mask
(format 120). The generated player carries the mask, and the masked sampler,
only when the project has such a key; projects that have none generate the
same player byte for byte.

## The sequence options

![The Director with the shot folded away: the Sequence tab fills the side column - Name, Duration, Loop, Skippable with On skip set to Ask first through the skip-cutscene screen, Camera track, Hide player and Hide HUD as switches, then Bars & fades - beside the timeline with every track closed.](img/cutscene-sequence-tab.png)

Every cutscene carries a handful of settings on the inspector's **Sequence** tab:

| Option | What it does |
|---|---|
| Duration / Loop | How long the timeline is, and whether it restarts at the end |
| Skippable | The player may end it early — see [Skipping](#skipping) |
| Camera track | Drive the game camera from the camera lane's shots |
| Hide player | Hide the third-person avatar (no effect in FPP — there is no body) |
| **Hide HUD** | Take the HUD, the USE prompt and USE itself off — see below |
| Widescreen bars | A letterbox mask, sliding in and out over its own times |
| Fade in / out | Ramp from and to black |

## Hide HUD

A cinematic with a health bar and a minimap in the corner is not a cinematic.
**Hide HUD** takes the whole HUD off for the duration of the cutscene and puts
it back exactly as it was afterwards:

- every HUD image in the screen stack,
- the live bars,
- the baked on-screen texts,
- the **USE prompt** — and, which is the part that matters, **the USE
  interaction itself**.

That last one is the reason the option exists in the shape it does. Use
targeting is a plain distance-and-facing test run from the **player** every
frame, before the cutscene camera is applied, and it knows nothing about
cutscenes — so a player left standing in front of a usable prop keeps *press to
use* on screen over the cinematic, and the button still works. That is the
common case rather than an edge one: the prop is usually the very one whose
**On Used** started the cutscene, and the walker keeps running underneath it.

![The same cutscene frame twice, on the console. Left, Hide HUD off: the word USE is drawn over the letterboxed cinematic because the player is still standing at the usable prop. Right, Hide HUD on: the same frame with no prompt.](img/cutscene-hide-hud.png)

**It is tied to this flag and not to "a cutscene is playing"**, deliberately. A
cutscene that animates a door, a lift or a piece of scenery while the player
keeps the camera and keeps playing is an ordinary thing to author, and there the
HUD and USE must keep working. Leave the box off for those.

**Runtime text is not hidden.** The *Display Text* flow node keeps drawing,
because that is what a cutscene's subtitles are written with. If you want a HUD
element to survive a hidden HUD, make it a Display Text rather than a baked
one.

Under the hood the player writes `ScriptContext::hudSuppressed` every frame and
clears it when the cutscene ends. That is a second flag rather than a write to
`hudVisible` on purpose: `hudVisible` belongs to the **Set HUD Visible** flow
node, and a cutscene restoring it to `true` on release would switch a HUD back
on that the game had deliberately hidden.

## Skipping

Tick **Skippable** and the player's `menu` action — START on a default pad —
ends the cutscene early. Two things about that are worth knowing.

**A skippable cutscene owns that button for its duration.** While it plays, the
press skips instead of opening the pause menu. Without that rule a skippable
cutscene is unskippable in any project that has a pause menu, which is most of
them: the press opened the menu, the open menu paused the scripts, and the
cutscene player never ran to see the button at all. There is nothing to
configure — ticking Skippable is what claims the button, and releasing the
cutscene hands it straight back.

**A skip can ask first.** The *On skip* dropdown, which appears next to the tick
box, chooses between:

- **Skip instantly** — the press ends the cutscene. The default, and what every
  project authored before this existed keeps doing.
- **Ask first** — the press opens the project's **skip screen** and the
  cutscene freezes on the frame it was on. The camera, the bars and the fade all
  hold where they were.

### The skip screen

![A SKIP CUTSCENE? panel over a frozen cutscene frame: the widescreen bars are still up and the world behind is dimmed, with the cursor on "NO, KEEP WATCHING" above "YES, SKIP".](img/cutscene-skip-screen.png)

The skip screen is an ordinary menu, so it is authored, styled and previewed
like every other one — *Tools > Menu Editor*, with the whole
[stylesheet](menu-styles.md) language available. One menu per project carries
the role, marked with the **Cutscene skip screen** switch on the *Opens* tab.

The fastest way to get one is **New > + Skip screen** in the Menu Editor's menu list.
It scaffolds a two-row confirmation already marked with the role:

```
SKIP CUTSCENE?
  NO, KEEP WATCHING      (Close menu)
  YES, SKIP              (Skip cutscene)
```

`NO` is first so the cursor starts on it and a stray Cross does not throw the
cutscene away. Note that **declining needs no action of its own**: any way of
dismissing the menu — a *Close menu* row, the back button — leaves the cutscene
where it froze and it carries on. Only the **Skip cutscene** row action ends it.

If a cutscene is set to *Ask first* and the project has no skip screen, the
press skips on the spot. Swallowing it and doing nothing would read as a broken
button; the Cutscene Director says so in amber next to the dropdown, and the
Menu Editor flags an unreachable *Skip cutscene* row in amber ("Unreachable: this is not the skip screen").

### Ending a cutscene from a graph

**Stop Sequence** does the same thing a skip does, from a flow graph, with no
button involved — and **On Sequence Finished** fires either way, so a graph that
cleans up after a cutscene does not need to know how it ended.

## What a cutscene does NOT take over

- **The walker keeps running.** A camera-track cutscene overrides the camera,
  not the player's movement; the player can still walk about behind it. Freeze
  them with a flow graph if that matters (or use *Hide player*, which at least
  takes the avatar out of shot).
- **The pause menu still exists** — a cutscene that is not skippable leaves the
  `menu` action exactly where it was, so START still pauses.
- **Split screen suspends itself** for the duration of a camera override: a
  cutscene owns the whole frame (see [multiplayer](multiplayer.md)).

## Where it lives in the code

| Piece | File |
|---|---|
| The data model + the shared interpolation math | `src/sequence.hpp` |
| The Director window, the track tree and camera lane | `src/cutscene_ui.cpp` |
| The generated PS2 player | `sequencesSource` in `src/templates.cpp` → `src/gen/sequences.gen.cpp` |
| The skip interception and the confirm row | `updateGameMenu` in `src/templates.cpp` |
| The HUD / USE suppression | `ScriptContext::hudSuppressed`, read by the frame loop and `updateUseTarget` |

The easing, sampling (`seqSampleComponent` / the player's `sampleObj`), bars,
fade, shake and camera-basis functions in `src/sequence.hpp` are **mirrored**
in the generated player so the editor's scrub preview and the console agree.
Change one, change both.
