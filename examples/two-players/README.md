# two-players example

Local co-op on one PS2: **two third-person avatars, runtime 1P/2P switching
and split-screen rendering** (docs/multiplayer.md). Both players are cats —
player 1 the ginger one, player 2 the gray one.

Open `two-players.tyra` in the editor and Build & Run (`F5`), or build
headless: `tyrax-editor.exe --build <this folder> --run`.

## What to do

The game boots into a **TWO PLAYERS title menu**: pick `PLAYERS: 2 Players`
(dpad down, Cross cycles) and START. The screen splits — player 1 (pad 1,
top half) walks the ginger cat, player 2 (pad 2, bottom half) walks the gray
cat; each half is that player's own third-person camera. Bump into the wall
and pillars (box collision), jump with Cross.

With `PLAYERS: 1 Player` the game runs as normal single player — and player
2 can still **hot-join any time by pressing Start on pad 2** (plug the
controller in whenever). The pause menu (Start on pad 1) carries the same
PLAYERS row, so you can switch 1P/2P mid-game; dropping back to 1 Player
removes the gray cat and returns the full screen to player 1.

In PCSX2, give pad 2 keyboard bindings (Settings > Controllers > Controller
Port 2) to try both halves alone at one desk.

## How it is wired

- **Preferences > Multiplayer** — `Two players: Split screen (top / bottom)`
  with *Player 2 joins with Start on pad 2* enabled. Switch the combo to
  *Shared screen* and rebuild to get the one-camera co-op framing instead
  (the camera orbits the midpoint of the pair and pulls back as they
  separate).
- **`player-cat-ginger`** — the scene's FIRST Player object = P1;
  **`player-cat`** — the SECOND = P2. Both use the same avatar
  (`res/models/cat.glb` at scale 3, a low close camera — cat-sized boom)
  and differ only by the object **color tint**, which multiplies the
  model's texture: ginger vs gray. The committed .glb has its scene root
  wrapped in a rotation: the source model was exported X-forward, the
  avatar drive expects Z-forward (the avatar turns to face where it walks).
- Both avatars map **Idle clip** to the model's `reference|…|EmptyAction`
  (the rest pose) and leave **Walk clip** at `(first)` — the cat's armature
  action is its walk cycle. Without the idle mapping the walk cycle also
  plays while standing (the "clip 0 covers everything" fallback), which
  reads as the cat pacing in place.
- **Title + pause menus** — both carry a `PLAYERS` Choice row from the Menu
  Editor's **+ Option block > Player count (1P/2P)** (backed by the
  `opt_players` save value, bind `player-count`). The row, the pad-2 Start
  join and memory-card saves all stay in sync.
- **Performance**: split screen renders the scene twice, and on real
  hardware every separate object bag pays a fixed ~1 ms submit cost per
  view (PCSX2 hides this — see docs/multiplayer.md > Performance for the
  measured budget). The project keeps *Mesh LOD distance 4* for the avatars
  and relies on **static batching** (on by default since PR #120) to fold
  the pillars/wall/step into shared bags.
