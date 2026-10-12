# tyra-editor-dev — the Runner, PCSX2/ps2link, collaboration transport

From upstream `devkit-runner-and-network.md` (Runner + network halves). Devkit
channels themselves are in `tyra-editor-dev-live.md`,
`tyra-editor-dev-debugger.md` and `tyra-editor-dev-devkit.md`.

## The Runner (`runner.cpp/.hpp` ~1.95k+170)
- Worker-thread pipeline (states Idle/Running/Success/Failed): `buildAndRun(p,
  run, rebuild)`, `runEmulatorOnly`, `buildAndRunPs2`, `exportIso`, `deployToPs2`,
  `launchPCSX2`. Default backend is native PS2DEV + vendored OpenVCL
  (`ProjectSettings::buildBackend = "native"`; `tools/toolchain/native-build.sh` +
  `.ps1` bridge); Docker (`docker compose`, image `${TYRAX_IMAGE:-h4570/tyra}`) is
  the fallback. `--build --rebuild` is the escape hatch. Engine/game rules track
  real `bin/` outputs and the game depends on `libtyra.a`; `Makefile.base` globs
  `src/**.cpp`/`src/**.vclpp`.
- Pre-flight `project::checkScriptNamespaces`: user-owned scripts live in the
  namespace derived from the project NAME; `TYRA_SCRIPT`'s qualified argument is
  the check; the ONE refresh-time failure treated as FATAL.
- Incrementality: anything that WRITES a file the compiler reads must not write
  when nothing changed (PS2SDK headers, the audsrv overlay; `cp` sets an mtime).
- Every "clear the field" step is per PROJECT: `claimPs2Channel` (ps2link file
  server) and `killEmulatorsFor` (PCSX2, keyed on `-elf <path>`); the `-elf`
  argument is an absolute native-separator path; PCSX2's `host:` loader refuses
  ELF paths over ~145 chars. ps2link ownership: handle first, then search
  `platform::processesNamed("ps2client")` reading each command line
  (`-h <ip>` = console, `execee host:<name>.elf` = game); `devsession` heartbeat
  or a live editor pid decides ownership.
- `devsession.cpp/.hpp` (~180+75): each editor publishes the project it debugs;
  `--debug-state` reads it. `sessionlog.cpp/.hpp` (~120+60): `<project>/logs/ps2-<stamp>.log`
  bounded to the last N lines / last K files (name order IS age; `_2` collision
  suffix). `pcsx2_config.cpp/.hpp` (~195+30): finds `PCSX2.ini` (portable dir next
  to the exe, then Documents/XDG/flatpak), forces `HostFs = true`, and with
  `ProjectSettings::keyboardMouse` points USB ports at host devices
  (`ensureUsbKbdMouse`: `[USB1] Type=hidkbd`, `[USB2] Type=hidmouse`).
  `iso9660.cpp/.hpp` + `isoexport.cpp/.hpp`: ISO9660 writer + disc layout.
- Hardware traces expose `Live_debug`, `Live_debug_poll`, `Live_debug_flush`;
  a leftover channel file is deleted before launch in BOTH Runner clean-up blocks.
  `menuActive`/`menuOwnsPad`/`g_gameplayPaused`, `tickFromLoop`/`tickFromScript`.

## Collaboration and network transport
- `session.cpp/.hpp` (~1.33k+215): `Session` (Host/Client), one worker thread
  (Runner idiom); the UI drains `drainEvents()` once per frame in
  `App::sessionTick()`. Host scans+hashes the project (`project::manifestFiles()`)
  and serves a content-hash `manifest`; the client diffs against
  `remote-cache/<projectId>`, fetches misses in 256 KiB chunks. Handshake: proto
  version (`kProtoVersion`) + 6-digit join code, `deny`/`bye`, ping/timeout,
  kick/close; `broadcastFrame`/`sendFrameToHost` + `AppEvent::Frame` carry live
  sync (LWW per `Section`, edits via `modelEditSerial_`). A client REFUSES to
  migrate a host's project.
- `wire.cpp/.hpp` (~830+140): the only place sockets live. Frame codec
  `[u32 jsonLen][u32 binLen][json][bin]` LE with per-part caps + incremental
  `FrameDecoder`; `wire::Transport` (listen/connect/poll/send/kick) with
  `makeTcpTransport()` (Winsock2 + WSAPoll) and `makeWebSocketTransport()` (RFC
  6455 SERVER: SHA-1 + base64 upgrade, unmasking, ping/pong, fragmentation). A WS
  peer is announced on UPGRADE; a dying codec sets `closeAfterFlush`. Also
  `fnv1a64`/`hashFile`/`hashFileEolAgnostic`/`localIPv4()`, a per-connection
  `WsCodec`, and `encodeFrame`; an unmatched `Disconnected`. `project::inputLayoutHash`
  warns on a coarse world mismatch. `project::checkScriptNamespaces`.
- Command IR shared with Live Logic: `BlockKind`/`OpCode`/`CondOp`/`PosKind`;
  `boolInputsOr`; `finalize`/`encodeFrame`; `hashFile`/`hashFileEolAgnostic`;
  `kStaleFrames` (Remote Pad overlay expiry); `LL_VERSION` (Live Link poller);
  `live_tex.gen.cpp`; `templates::livePadSource`/`inputReplaySource`;
  `padFromCli`/`drawRemotePadWindow`; `dbgReadVuCapture`/`dbgReadCrashReport`;
  `captureVu`/`measureRam`; `presented` counts flips. `platform::copyImageToClipboard`;
  `TYRA_SCRIPT`; `kMaxNodes`/`kMaxBreakpoints`/`kMaxForced`/`kMaxEvents`; `Timeline`.
