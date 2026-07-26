# Live collaboration sessions

One editor **hosts** its open project; other editors **join** over the LAN and
everyone edits the same project at the same time. The host is authoritative:
it applies every edit in a single order, decides who is in the session (kick /
close), and is the **only participant who saves to disk and commits to git**.
Joined editors work on a synced copy that lives in a local cache.

This is the real-time layer on top of the existing git-based collaboration
(the `objects/<id>.json` split format + `COLLABORATION.md` in every generated
project remain the async story).

## Using it

**Host** (has the project open): *Session > Host Session...* — pick a display
name and port (default 7797), share the shown `IP:port` and the generated
6-digit join code. Windows Firewall asks for permission on the first start.

**Join**: *Session > Join Session...* — enter the host's address, port and
code. The whole project transfers on the first join (a progress bar shows
files/bytes); re-joins only fetch what changed since. When the sync finishes,
the project opens and every edit streams both ways live.

While in a session:

- The toolbar shows a **SESSION (n)** chip on the host and a blue **JOINED**
  chip on clients; clicking it opens the *Session* window (participants with
  per-peer colors, the scene each one is editing, Kick buttons for the host).
- Other participants' selections appear as **colored outlines** in the
  viewport and colored dots in the Project panel's object list.
- A client's **Save is disabled** (title bar shows `[joined]`) — the host owns
  persistence. Everything else works, including local Docker builds, PCSX2
  runs and Live Link: the synced copy is a complete, real project.
- Undo works on every side; undoing rewinds remote edits batch-by-batch (it
  is a snapshot undo, and remote edits land as snapshots between yours).
- *Session window > Refresh project files* (client) re-syncs assets/sources
  mid-session — use it after the host imports new models/textures/audio.
  Scene edits never need this; they stream live.

Leaving (or being kicked from) a session keeps the synced project open as a
local copy; Save re-enables and writes to the cache folder.

## What syncs, and how conflicts resolve

Everything in the `.tyra` model syncs live: objects (with flow graphs and
script attachments), scene structure (add/remove/rename/reorder), terrain
sculpting, and the project-wide sections (preferences, HUD/UI, menus,
sequences, gradings, ambience, loading screens, audio lists, save data,
texture quality).

Conflict policy is **last-write-wins per unit**, where a unit is one object,
one scene-layout change, one terrain heightmap, or one project-wide section.
The host orders all edits; concurrent edits to *different* objects merge
cleanly, concurrent edits to the *same* object resolve to whichever the host
applied last. Presence highlights exist precisely so people see who is where
before stepping on each other.

Known v1 limitations:

- Material Editor **paint strokes write .mtl/.png files directly** (assets,
  not model data) — clients' paint edits do not propagate; the host's reach
  clients via *Refresh project files*.
- No per-object locks; LWW means simultaneous edits of the same field pick a
  winner instead of merging.
- A kick is a disconnect, not a ban — the kicked editor could rejoin while
  the session is up (share a new code if that matters).

## The wire, the cache, and the trust model

- Transport: raw TCP on the LAN (`src/wire.cpp`), length-prefixed frames of
  JSON + an optional binary trailer (file chunks, heightmap grids). The
  `wire::Transport` interface is the seam where an internet transport
  (tunnel/WebSocket) can be added without touching the protocol.
- Join transfer: the host serves a content-hash **manifest** (the live
  in-memory model + the project's files, minus `bin/`, `obj/`, `.git/`,
  `.res-baked/`, `*.history`); the client fetches only hashes it lacks.
  Host-side hashes are memoized across sessions
  (`hash-cache.json` in the editor config folder - `%LOCALAPPDATA%\tyra-editor`
  on Windows, `~/.config/tyra-editor` elsewhere), so hosting a huge project
  does not rehash unchanged assets every time.
- Client cache: joined projects materialize under
  `<editor config folder>/remote-cache/<projectId>/project` (override in
  *Edit > Preferences > Collaboration sessions*), with `cache.json` recording
  size/hash/mtime per synced file. Only files that cache recorded are ever
  deleted by a re-sync — local build outputs are never touched.
- Live sync: edits are detected by diffing the model against a shadow of the
  last-broadcast state and shipped as per-unit messages; the host applies and
  rebroadcasts everything (including to the sender), which is what makes
  concurrent editing converge.
- Trust model: the join code is a shared secret, not cryptography, and the
  stream is not encrypted — this is a LAN/VPN feature for people who already
  trust each other with the project. For remote friends today, a mesh VPN
  (Tailscale/ZeroTier) makes the session look like LAN; a built-in internet
  transport is deliberately deferred.

Settings that persist in `editor.ini`: `displayName=` (name shown to peers)
and `sessionCacheDir=` (cache root override) — *Edit > Preferences*.

## Files

| File | Role |
|---|---|
| `src/wire.cpp/.hpp` | Frame codec + `Transport` interface + TCP impl + hashing |
| `src/session.cpp/.hpp` | Session worker thread (handshake, transfer, cache, relay) + the model diff/apply engine |
| `src/app.cpp` (`sessionTick`, `drawSession*`) | Per-frame event drain, modals, Session window, chip, presence |
| `src/project.cpp` (`manifestFiles`, `sectionJson`, `objectJson`, `scenesLayoutJson`) | The model's wire representations |
