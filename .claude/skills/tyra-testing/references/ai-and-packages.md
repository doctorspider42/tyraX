# Acceptance recipes — AI Assistant (docs/ai-chat.md), packages and the update check (docs/updates.md)

Read when: you change the AI Assistant (prompt, tools, reply parsing, compaction, chat UI) or the installer / Linux packages / update check / self-update.

## AI Assistant — four layers, three need no backend, none need tokens

0. **Docs searchable?** `TYRAX --search-docs "<query>" [page]` runs the assistant's `search_docs` over docs baked into the exe; exit 1 when nothing matches ("does anything still say `--max-turns`?").
1. **What is it told?** `TYRAX --chat-prompt [projectDir]` prints the whole system prompt: tool catalog (`aichat::tools()`), doc index from `docs/*.md`, object type/property tables, live project context; no-project variant without an argument. Check "is my tool described / did the page join the index / how big is the prompt".
2. **Pure half from a harness**: `aichat.cpp` has no ImGui/GL/`App` → doc index, reply parser, argument validation, every read tool, transcript trimming from a host `main()` (link recipe: host-harnesses.md, the "reaches project.cpp" variant). Used to check `runReadTool` for `get_proc_graph`/`list_proc_nodes` over `examples/procedural`, and `procGraphJson` → `parseProcGraph` → `procGraphJson` round trip on all six volumes (must be **byte-identical**, or `set_proc_graph` loses the difference).
   - **Malformed replies (2b)**: copy `aigen::repairJson` + `extractJsonObject` beside `src/json.cpp`; assert recovered `say` AND that tool calls SURVIVED (salvaging prose while dropping calls looks fine and ends the turn). Cases: clean envelope (untouched), stray quote with/without calls, lone backslash, markdown fence, `\uXXXX` (must become UTF-8, not `?`), plain prose (must NOT parse as an envelope). Reuse ONE `json::Value` across failed parse and retry (surfaced the stale-partial-parse bug in `json::parse`).
3. **Whole loop with a FAKE backend**: backends are commands on `PATH`, so a script named `claude` earlier on PATH IS a backend:
```sh
#!/bin/sh
cat > /dev/null                                     # prompt arrives on stdin
n=$(cat "$STATE" 2>/dev/null || echo 0); n=$((n+1)); echo "$n" > "$STATE"
case "$n" in
1) echo '{ "say": "Placing it.", "calls": [ { "tool": "add_object", "args": { "type": "box", "name": "lever", "position": [4, 0, -6] } } ] }' ;;
*) echo '{ "say": "Done." }' ;;
esac
```
```bash
STATE=/tmp/fake PATH=/tmp/fakebin:$PATH TYRAX --ui-script <proj> "
frames 30; wait 1; click 'TyraX menu'; click Tools; click 'AI Assistant'
click 'AI Assistant/Edit project'
click 'AI Assistant/##chatinput'; text 'add a lever'; frames 5; click 'AI Assistant/Send'
wait 12; key ctrl+s; shot /tmp/chat.png"
TYRAX --dump <proj>          # is the lever there?
TYRAX --refresh-gen <proj>   # did its graph compile?
```
   Check the RESULT in the project, not on screen.
   - Broken replies on demand: unescaped quote (`{ "say": "the "main" scene" }`), lone backslash → window must show prose, not JSON.
   - Long-session paths: answer with a HUGE tool result (`read_doc` of the biggest page) to exceed budget; recognise the compaction request (`grep -q "You are compacting a conversation"`) → compact-then-continue runs deterministically (verified 63.6k → 5.8k context, saved chat `role: ['summary', 'user', 'assistant']`).
   - Expensive/destructive tools: canned `build_game` proves park-and-resume (turn stops, Runner runs for real, outcome appended to the existing tool result, loop continues — measured with one Docker build); canned `set_section` with an EMPTY list proves the shrink guard refuses, then `confirm_replace` passes. Check against the SAVED project (`--dump`, `objects/*.json`, manifest).
   - Misbehaving replies: fenced envelope with prose, `name`/`arguments` instead of `tool`/`args`, invented node type, unknown property, wrong object name — transcript tool rows show which honest path fired.

Traps:
- Hermetic `XDG_CONFIG_HOME` (or `LOCALAPPDATA`) for any chat run (history + `editor.ini` live there). It also isolates PCSX2 (BIOS, memcards, settings under `$XDG_CONFIG_HOME/PCSX2`) → symlink `ln -sfn ~/.config/PCSX2 $CFG/PCSX2`.
- **A build compiles the IN-MEMORY model**: an assistant that added + built produced a correct ELF while the `.tyra` knew nothing → next session regenerated an empty debug runtime, no channel ("broken devkit"). Multi-session tests must save.
- `chatAllowEdits_` persists in `editor.ini`; the checkbox click is a toggle → `expect-checked` right after (every edit `[failed]` = that switch).
- Popup items live in `##Popup_<hash>`; grep `dump` for the label, confirm with `shot`.
- `frames 5` between `text` and *Send* (`BeginDisabled` on empty input → silent no-op).
- Replies are consumed by `aiChatTick` from `drawUI` whether or not the window is open: `wait` long enough for N backend invocations; closing mid-turn doesn't strand it.

## Packages and the update check

- **Pure half**: `update::compareVersions`, `update::parseRelease` (no ImGui/GL/Project) — harness link line in host-harnesses.md. Feed `parseRelease` a REAL payload (`curl -sL https://api.github.com/repos/<any>/releases/latest`). `fetchLatest()` against a repo with no releases → *no releases have been published yet* (a state, not a failure).
- **UI half**: a failed check opens the modal too:
```bash
./build/tyrax-editor --ui-script "frames 30; wait 1; click 'TyraX menu'; click Help; click 'Check for updates'; wait 3; shot upd.png; dump"
```
  Automatic startup check is skipped while a UI script runs (`uiScriptActive_`).
- **Windows installer — verify by installing** (the LAYOUT is what breaks). A silent install from a console-attached shell hangs → `Start-Process -Wait`:
```powershell
./installer/build-installer.ps1 -SkipBuild
Start-Process dist\TyraX-Setup-<v>.exe -Wait -ArgumentList `
  '/VERYSILENT','/SUPPRESSMSGBOXES','/CURRENTUSER',"/DIR=$env:TEMP\tyrax-test","/LOG=$env:TEMP\ins.log"
$env:TEMP\tyrax-test\bin\tyrax-editor.exe --vu-check           # finds <exe>/../vendor/tyra
$env:TEMP\tyrax-test\bin\tyrax-editor.exe --new t $env:TEMP    # then:
Select-String tyrax-test $env:TEMP\t\docker-compose.yml        # engine path in the generated compose
```
  Finish with `unins000.exe /VERYSILENT`; the directory, Start Menu folder and `HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall` key must be gone.
- **Linux packages** (one staged tree; unpack + RUN the tarball):
```bash
./installer/build-package.sh --skip-build --all --out-dir /tmp/pkg
tar xzf /tmp/pkg/tyrax-<v>-linux-x86_64.tar.gz -C /tmp/pkg
/tmp/pkg/tyrax-<v>/bin/tyrax-editor --new t /tmp/pkg
grep engine-src /tmp/pkg/t/docker-compose.yml   # must name the UNPACKED tree's vendor/tyra, not your checkout
```
  `--deb` needs `dpkg-deb` (`apt-get install dpkg-dev`), `--rpm` needs `rpmbuild` (`apt-get install rpm`). Inspect metadata/deps/files: `dpkg-deb -I`/`-c`, `rpm -qip`/`-qRp`/`-qlp` — a missing `zenity | kdialog` = an Open button that does nothing.
- **macOS app** (`installer/build-macos.sh`; docs/updates.md#macos): mount the dmg (`hdiutil attach -nobrowse -readonly -mountpoint <dir>`), check `.DS_Store`/`.background.tiff`/`Applications` link, `codesign --verify --deep --strict` + `codesign -dvv` (Developer ID, `Runtime Version`, `Timestamp`), `ditto` the app out, then prove it needs nothing from the host - an ISOLATED config and a bare PATH:
```bash
env -i HOME=$HOME USER=$USER XDG_CONFIG_HOME=/tmp/cfg PATH=/usr/bin:/bin:/usr/sbin:/sbin \
  /tmp/apps/TyraX.app/Contents/MacOS/tyrax-editor --build <copied project>
```
  The log must show `prerequisites are ready (bundled tools)`, `relinked N binaries`, `installing the bundled OpenVCL`, `gmake: Entering directory`. Pre-seed `/tmp/cfg/tyra-editor/toolchain/ps2dev/.sources/` with the PS2DEV archive to skip the 235 MiB download. Note the dev machine's toolchain flips between the app's and Homebrew's libraries (a reinstall, ~30 s) when the same config dir serves both. After notarizing: `spctl -a -vv -t install <dmg>` and `spctl -a -vv <app>` must say `Notarized Developer ID`.
- **Self-update without a release**: put the harness binary at `<root>/bin/`, a `.tyrax-package` marker beside it, call `update::installKind`/`selfInstallBlocked` for `tarball` (empty = button appears), `deb`, `rpm`, no file (source checkout), `tarball` in a `chmod a-w` dir. Then build a throwaway "new release" tarball, `update::runInstaller` it: new files present, a file the release lacks SURVIVED (overlay is deliberate), binary relaunched.
