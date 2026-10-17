# The Flow Graph window

The Flow Graph window is where an object gets its behaviour: a graph of
triggers (On Start, On Used, On Button...) wired to actions (Play Sequence,
Display Text, Set Save Value...), compiled into the game's C++ on every build.
Every graph belongs to one object; the window always shows one of them.

![The Flow Graph window on the showcase's instrument-console: the scene's graphs on the left, the compact graph in the middle with the pin legend and the zoom card, and the node inspector on the right editing a Display Text node with its 512x448 placement preview.](img/flow-graph-window.png)

## Which graph you are looking at

- **The header's switcher** shows the object's name; click it for a filterable
  list of the scene's objects, the ones with logic first.
- **Follows selection** (on by default): selecting an object in the viewport or
  the Outliner opens its graph. Turn it off to keep a graph open while you
  select other things. While the [Live Debugger](live-debugger.md) has the
  game halted it reads **Follows the halt** and opens the graph that stopped.
- **Graphs in** *scene* (left column, when the window is wide enough)
  lists only objects that have logic, with their trigger and node count, and a
  filter. *List objects without logic* adds the rest. Opening a graph frames
  all of its nodes.

The header also counts nodes and links and carries two status pills:

- **Live Logic** for this graph: *Edits go live* when [Live Logic](live-logic.md)
  can hot-patch it into a running game, *Edits need a build · N nodes* when
  some node types cannot be patched (hover for which), *Live Logic off* when
  the preference is off. Nodes Live Logic cannot patch carry a small amber dot.
- **Problems** in the whole scene's graphs — an In Area with no area, an
  unnamed event, an input action that no longer exists, a renamed fact or save
  value, an autosave checkpoint without an autosave slot. Click it for the
  list; a row opens that graph with the node selected.

*Generate* writes or edits the graph from a sentence ([AI flow
graphs](ai-flow-graph.md)); the `</>` button holds the
[custom node](custom-flow-nodes.md) actions.

## Adding nodes

Press **Space** on the canvas, right-click empty canvas, or **drag a wire from a
pin and drop it on empty canvas**. The palette opens with the search field
focused: type part of a name ("sound", "save value"), a category or a word
from what the node does. Category chips narrow it; Up/Down picks, Enter places
the node where the palette opened. With nothing typed, the nodes you added most
recently come first. A palette opened from a dropped wire lists only nodes
that can take that wire, and wires the new one up.

An object without logic shows **On Start** and **On Used** buttons on its empty
canvas, the two triggers most graphs begin with.

## Nodes and the inspector

Nodes on the canvas are compact: a coloured square for the kind (green
triggers, blue actions, violet logic gates, amber other data), the title, and
the values that make the node what it is as chips (`self`, `lenses`,
`Seconds 2`). A pin with nothing wired to it is drawn hollow. The pin legend in
the canvas's top-left says what each shape carries (exec, object, position,
bool, text, number).

Select a node and the **inspector** on the right shows everything about it:
what it does, a problem if it has one, every setting with its tip on the
caption, the wiring of every pin (each end is a link that selects that node),
and whether Live Logic can patch it. Display Text adds a **placement
preview** — the 512x448 screen with the text where it will appear; drag in it
to move the text. *Delete node* removes it (Delete on the canvas does too). A pad-button setting (On
Button's *Button*) is the editor's controller picker: the button's glyph in the
field, and a small DualShock 2 to click above the 16 names.

## Moving around

The card in the canvas's bottom-left zooms (− / + or the mouse wheel),
**Frame all** (F) fits the whole graph, and **Tidy** (L) lays it out in
columns — triggers first, each action one column right of what runs it, data
nodes left of what reads them — then frames it. Tidy is one undo step. Ctrl+C /
Ctrl+V copy and paste selected nodes between graphs.

## While the game runs

With a debug build reporting, the canvas becomes a live instrument —
glowing titles, lit exec links, hit counts, breakpoints in the gutter left of
each title, a transport bar while halted, and a **Live** tab in the inspector.
See [Live Debugger](live-debugger.md).
