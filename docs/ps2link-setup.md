# Running and debugging on a real PS2 — the TyraX ps2link

The console side of this editor is **always our own ps2link**: a pinned upstream
checkout plus [`tools/ps2link/tyrax.patch`](../tools/ps2link/tyrax.patch), built
here in Docker and flashed onto the memory card once. Stock ps2link is not a
supported target — we keep patching ours (the USB HID stack today, more later),
and every "how does the console behave" answer in these docs assumes the TyraX
build.

Consequently **nothing downloads a ps2link for you**: `setup.ps1` / `setup.sh`
fetch [ps2client](../tools/ps2client/README.md) (the PC side) and stop there.
You build the console side, which takes one command and about a minute.

## What you need

| | |
|---|---|
| A PS2 with ethernet | SCPH-700xx and later have it built in; a fat PS2 needs the official Network Adaptor in the expansion bay. |
| A way to boot homebrew | FreeMcBoot / FreeDVDBoot / uLaunchELF / a disc-swap exploit — whatever already runs `.ELF` files off your memory card. |
| A memory card | It holds `PS2LINK.ELF` + `IPCONFIG.DAT`. |
| A wired LAN | Console and PC on the **same subnet**; the PS2 has no Wi-Fi and ps2link has no DHCP. |
| Docker Desktop on the PC (plain `docker` on Linux) | The ps2link build runs inside the `ps2dev/ps2dev` toolchain image. |

## 1. Build the TyraX ps2link

```powershell
tools/ps2link/build.ps1
```

On Linux: `tools/ps2link/build.sh` (the same script, same pins). Add `-Clean` /
`--clean` to throw the work tree away first.

It clones ps2link pinned at `0c6138c`, applies `tyrax.patch`, runs `make ee`
inside `ps2dev/ps2dev:latest` and drops **`tools/ps2link/ps2link.elf`** (~280 KB)
next to the script. The first run pulls the toolchain image; later runs reuse the
clone in `tools/ps2link/build/`. The ELF is gitignored — the patch is the source
of truth, not the binary.

What the patch does to upstream today:

| File | Change |
|---|---|
| `ee/Makefile` | bakes `usbd.irx`, `ps2kbd.irx`, `ps2mouse.irx` (prebuilt in `$PS2SDK/iop/irx/`) into the ELF |
| `ee/irx_variables.h` | externs the three embedded buffers |
| `ee/ps2link.c` | loads them in `loadModules()` — **`usbd` first**, the other two import its symbols — brands the welcome screen, and guards `PS2_DISABLE_AUTOSTART_PTHREAD` so newer ps2sdk versions still compile |

The USB stack has to come up on ps2link's **own freshly reset IOP**: a game
deployed over the network runs on that same IOP without resetting it, so it
cannot bring the stack up itself (the full story is in
[keyboard-mouse.md](keyboard-mouse.md)). Baking it into ps2link is what makes a
keyboard and mouse work over an F6 deploy at all.

## 2. Put it on the console

1. Copy `ps2link.elf` onto the memory card as **`PS2LINK.ELF`** (uLaunchELF over
   USB or the network, a PS2 memory-card manager on the PC, whatever your
   FMCB setup already uses to install homebrew).
2. Put an **`IPCONFIG.DAT`** in the *same directory* — ps2link opens it by a
   relative path, so it reads the one next to itself. One line, three
   space-separated fields, `ip netmask gateway`:

   ```
   192.168.1.42 255.255.255.0 192.168.1.1
   ```

   Give the console a **static address outside your router's DHCP pool**;
   the editor stores that IP and the deploy is UDP, so a changing address just
   looks like a dead console.
3. Boot it. The screen must read:

   ```
   Welcome to TyraX ps2link (USB keyboard + mouse)
   based on ps2link
   ...
   Net config: 192.168.1.42  255.255.255.0  192.168.1.1
   ```

   Plain **"Welcome to ps2link"** means you booted a stock build — nothing below
   is supported on it. And if `Net config:` reads **192.168.1.10 /
   255.255.255.0 / 192.168.1.0**, those are the values compiled into ps2link as
   a fallback: your `IPCONFIG.DAT` was not found or not readable, and it is
   *not* using the address you configured.

## 3. Point the editor at it

*Edit > Preferences > Real PS2 (network deploy) > **PS2 (ps2link) IP***. This is
a **machine-global** editor setting (`editor.ini`), shared by every project —
not project data, so it never travels in a `.tyra` file. Leave it empty to
disable the F6 path entirely.

Headless, the IP can also come from the command line:

```powershell
tyrax-editor --build <projectDir> --run-ps2 192.168.1.42
```

## 4. Deploy and run

**F6** = build && run on PS2, **Ctrl+F6** = run without building. The run
toolbar's target dropdown has the same thing as *PlayStation 2 (ps2link)* — the
run triangles turn **blue** when the console is the target and green for PCSX2.

What the editor does, in order:

1. kills any previous `ps2client` (one file server at a time);
2. clears the stale devkit channel files in `bin/` (`log.txt`, `livedbg.bin`,
   `livetime.bin`…) so the panels do not read yesterday's session;
3. writes the **`bin/ps2link.run`** marker — the game probes it over `host:` to
   learn it was network-deployed and must keep the IOP resident (ps2link's
   `execee` does not deliver argv reliably, so the `-ps2link` argument alone
   cannot be trusted; a PCSX2 launch deletes the marker);
4. `ps2client -h <ip> -t 10 reset`, waits ~3 s for ps2link to reload;
5. `ps2client -h <ip> execee host:<name>.elf -ps2link`, with the working
   directory set to the project's `bin/` — that is how the game's `host:` maps
   onto `bin/` exactly like a PCSX2 run.

That last `ps2client` **is the file server for the whole session**: the ELF,
every texture, every model and every devkit channel file is read from your PC
over the network while the game runs. Its output — including the console's
`printf`/`TYRA_LOG` — is pumped into the *Output* panel as `[ps2]` lines.

> **Keep the editor open while the game runs.** Closing it kills `ps2client`,
> and the game loses `host:` mid-session: the devkit files freeze exactly where
> they were while the console happily keeps running. The cure is a redeploy, not
> a retry.

Ports, if a firewall is in the way:

| Port | Direction | What |
|---|---|---|
| TCP 18193 | PC → console | the file-request socket ps2link listens on; `ps2client` connects to it |
| UDP 18194 | PC → console | commands (`reset`, `execee`) — fire-and-forget, no ack |
| UDP 18194 | console → PC | the console's `printf` output (udptty) — this is what the `[ps2]` lines are |

Because the commands are fire-and-forget, a dead or wrong IP makes `reset` and
`execee` both "succeed". The editor therefore treats **the first log line** as
the liveness signal and gives up after 15 s.

## 5. Debug it

Everything in the [devkit](devkit.md) rides the same `host:` filesystem channel
on hardware as it does in PCSX2, so it all just works over ps2link:
[Live Link](live-link.md), the [Live Debugger](live-debugger.md) (breakpoints on
flow-graph nodes, pause/step/step-node, watches, the timeline),
[Live Logic](live-logic.md) and the [time machine](time-machine.md). Build in
the **debug** profile — release builds carry none of it.

Differences on real hardware:

- **Slower polling.** The game re-reads the channels every 25 frames instead of
  6 (~0.5 s): each file operation is a network round-trip, and hammering the
  ps2link file server starves the game.
- **`ps2client` must be patched.** The editor ships one
  ([`tools/ps2client`](../tools/ps2client/README.md)) with `TCP_NODELAY` on the
  request socket. Without it, Nagle plus the PS2's delayed ACKs stall every
  exchange ~200 ms — measured ~4 KB/s, a 10-minute game boot.
- **No process to query.** `--debug-state` finds a PCSX2 session by reading the
  emulator's command line; on hardware there is none, which is why the editor's
  session pointer records the transport (`ps2link`) instead.
- **Keyboard and mouse work**, because this is the TyraX ps2link: tick
  *Project > Preferences > Build > Keyboard & mouse controls* and the
  *Also over ps2link* sub-option under it is already on — the game reuses
  ps2link's resident USB stack instead of loading one it cannot load. See
  [keyboard-mouse.md](keyboard-mouse.md).
- **The EE crash handler** ([devkit.md](devkit.md#the-ee-crash-handler-experimental-opt-in))
  is still opt-in and unproven on hardware; the heartbeat post-mortem needs
  nothing from the game and works here.

**Stop on PS2** kills the file server, sends `reset`, then runs
`host:silencer.elf` for a few seconds — the SPU2 keeps looping voices
independently of the IOP, so the reset alone would leave the last sound effect
playing forever.

## When it does not work

| What you see | What it means |
|---|---|
| `[editor] Could not reach ps2link at <ip>` | `ps2client reset` failed outright — wrong IP, console not booted into ps2link, cable/link down. |
| `[editor] No response from <ip> within 15s` | The commands went out and nothing came back. Check the IP against the console's `Net config:` line, then the PC firewall (inbound **UDP 18194** for `ps2client` — without it the game may actually be running with its log going nowhere). |
| Boot screen says "Welcome to ps2link" | Stock ps2link. Rebuild from `tools/ps2link/` and reflash. |
| `Net config:` shows `192.168.1.10` | `IPCONFIG.DAT` was not found next to `PS2LINK.ELF`; ps2link fell back to its compiled-in default. |
| Assets crawl in, boot takes minutes | An unpatched `ps2client` on `PATH` is being used instead of `tools/ps2client/bin`. |
| `KbdMouse: mouse skipped (no resident USB stack…)` | The running ps2link has no USB stack — a stock one. |
| Both drivers "ready" but nothing responds | The devices. `ps2kbd`/`ps2mouse` only speak the USB HID **boot protocol**; test them in uLaunchELF first. |
| Devkit panels frozen, game still running | The editor (and with it `ps2client`) was closed. Redeploy. |

## Changing the patch

We expect to keep changing our ps2link, so the loop is:

1. Run a build once — `tools/ps2link/build/` is then a full ps2link checkout at
   the pinned commit with `tyrax.patch` applied.
2. Edit the sources there.
3. **Regenerate the patch before rebuilding**, because both build scripts start
   with `git checkout -- .` and would throw your edits away:

   ```powershell
   git -C tools/ps2link/build diff --output=../tyrax.patch
   ```

   `--output` rather than `>` on purpose: git writes the file itself, so the
   bytes stay LF and UTF-8. Windows PowerShell's `>` would hand you a UTF-16
   file that `git apply` refuses. (The path is relative to the `-C` directory,
   hence the `../`.)

4. Rebuild (`tools/ps2link/build.ps1`, or `build.sh` on Linux) and reflash
   `PS2LINK.ELF`.

Keep the patch **LF-only** (`.gitattributes` enforces it) — it is applied to a
Unix checkout inside a Linux container, and a CRLF patch fails `git apply`. To
move to a newer upstream, bump the pinned commit in **both** `build.ps1` and
`build.sh` and re-generate the patch against the new tree. And if a change
alters what the console reports on boot, update the expected banner in step 2 above — that
banner is how anyone tells the two builds apart.
