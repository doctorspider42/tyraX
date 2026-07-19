# two-players example

Local co-op on one PS2: **two third-person avatars, runtime 1P/2P switching
and split-screen rendering** (docs/multiplayer.md). Player 1 is Cesium Man
(the classic skinned glTF sample, textured), player 2 is a cat.

Open `two-players.tyra` in the editor and Build & Run (`F5`), or build
headless: `tyrax-editor.exe --build <this folder> --run`.

## What to do

The game boots into a **TWO PLAYERS title menu**: pick `PLAYERS: 2 Players`
(dpad down, Cross cycles) and START. The screen splits — player 1 (pad 1,
top half) walks Cesium Man, player 2 (pad 2, bottom half) walks the cat;
each half is that player's own third-person camera. Bump into the wall and
pillars (box collision), jump with Cross.

With `PLAYERS: 1 Player` the game runs as normal single player — and player
2 can still **hot-join any time by pressing Start on pad 2** (plug the
controller in whenever). The pause menu (Start on pad 1) carries the same
PLAYERS row, so you can switch 1P/2P mid-game; dropping back to 1 Player
removes the cat and returns the full screen to player 1.

In PCSX2, give pad 2 keyboard bindings (Settings > Controllers > Controller
Port 2) to try both halves alone at one desk.

## How it is wired

- **Preferences > Multiplayer** — `Two players: Split screen (top / bottom)`
  with *Player 2 joins with Start on pad 2* enabled. Switch the combo to
  *Shared screen* and rebuild to get the one-camera co-op framing instead
  (the camera orbits the midpoint of the pair and pulls back as they
  separate).
- **`player-man`** — the scene's FIRST Player object = P1. Third person,
  avatar `res/models/cesium_man.glb` (its 1024x1024 embedded texture is
  downscaled to the PS2's 512 limit automatically at build).
- **`player-cat`** — the SECOND Player object = P2. Avatar
  `res/models/cat.glb` at scale 3 with a lower, closer camera (cat-sized
  boom). The committed .glb has its scene root wrapped in a +90° Y rotation:
  the source model was exported X-forward, the avatar drive expects
  Z-forward (the avatar turns to face where it walks).
- **Title + pause menus** — both carry a `PLAYERS` Choice row from the Menu
  Editor's **+ Option block > Player count (1P/2P)** (backed by the
  `opt_players` save value, bind `player-count`). The row, the pad-2 Start
  join and memory-card saves all stay in sync.
- Neither avatar maps explicit idle/walk clips — each model's first clip
  plays as the all-purpose locomotion loop (Cesium Man's walk cycle, the
  cat's armature action), the usual shortcut for single-clip models.
