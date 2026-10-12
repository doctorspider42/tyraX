# troubleshoot/tyra-testing-ai.md

Verifying the AI Assistant (docs/ai-chat.md). Symptom first.

## An AI feature looks untestable

**Cause:** it looks like it needs a backend and tokens.
**Fix:** three of its four layers need no backend at all, and the fourth needs no tokens. Test them in order: docs search, the prompt, the pure half, the whole loop with a fake backend.

## 0. Is the documentation searchable?

**Cause:** a doc-facing change ("does anything still say `--max-turns`?").
**Fix:** `TYRAX --search-docs "<query>" [page]` runs the assistant's `search_docs` over the pages baked into the exe and exits 1 when nothing matches — one command, no project and no backend.

## 1. What is it told?

**Cause:** anything prompt-shaped ("is my new tool described?", "did that doc page join the index?", "how big is the prompt?").
**Fix:** `TYRAX --chat-prompt [projectDir]` prints the whole system prompt — the tool catalog built from `aichat::tools()`, the documentation index derived from `docs/*.md`, the object type/property tables and the live project context. With no project argument you get the no-project-open variant.

## 2. The pure half, from a harness

**Cause:** `aichat.cpp` has no ImGui, no GL and no `App`.
**Fix:** the doc index, the reply parser, argument validation, every read tool and the transcript trimming can be exercised from a host `main()`. The cheapest way to link one is to reuse a dev build's objects:

```bash
OBJS=$(ls build-dev/CMakeFiles/tyrax-editor.dir/src/*.o | grep -v /main.cpp.o)
g++ -std=c++20 -O0 -I src -I build-dev/generated -I vendor/imgui -I vendor/imguizmo/src \
    -I vendor/imnodes -I vendor/stb -I vendor/ufbx -I vendor/miniaudio \
    harness.cpp $OBJS build-dev/CMakeFiles/tyrax-editor.dir/vendor/*/*.o \
    build-dev/CMakeFiles/tyrax-editor.dir/vendor/ufbx/ufbx.c.o \
    build-dev/libimgui.a build-dev/vendor/glfw/src/libglfw3.a \
    -ldl -lrt -lm -lGLX -lOpenGL -lpthread -o harness
```

**On Windows** it is `src/*.obj`, no `main.cpp.obj`, and the libraries CMake links: `-static -lopengl32 -lgdi32 -lcomdlg32 -lshell32 -lole32 -luuid -lws2_32 -limm32` (miss `comdlg32` and the only error is `GetOpenFileNameW` from `platform.cpp`). That harness checked the procedural read tools without a backend — `runReadTool` for `get_proc_graph` / `list_proc_nodes` over `examples/procedural`, plus a `procGraphJson` -> `parseProcGraph` -> `procGraphJson` round trip over all six volumes, which must come out **byte-identical** or `set_proc_graph` loses the difference.

## 2b. A malformed reply is its own test

**Cause:** a bare `"` left in prose makes the whole document unparseable.
**Fix:** copy `repairJson` and `extractJsonObject` into a scratch file next to `src/json.cpp` and assert on the recovered `say` and on whether the tool calls SURVIVED (salvaging the prose while silently dropping the calls looks fine on screen and ends the turn). Cases: a clean envelope (untouched), a stray quote with and without calls, a lone backslash, a markdown fence, `\uXXXX` escapes (must come out as UTF-8, not `?`), and plain prose (must NOT parse as an envelope). Reuse ONE `json::Value` across the failed parse and the retry.

## 3. The whole loop, with a FAKE backend and no tokens

**Cause:** the backends are external commands found on `PATH`, so a shell script called `claude` earlier on `PATH` IS a backend.
**Fix:** have it drain stdin, count its invocations in a state file and answer from a canned sequence:

```sh
#!/bin/sh
cat > /dev/null                                     # the prompt arrives on stdin
n=$(cat "$STATE" 2>/dev/null || echo 0); n=$((n+1)); echo "$n" > "$STATE"
case "$n" in
1) echo '{ "say": "Placing it.", "calls": [ { "tool": "add_object", "args": { "type": "box", "name": "lever", "position": [4, 0, -6] } } ] }' ;;
*) echo '{ "say": "Done." }' ;;
esac
```

Drive the window with `--ui-script` and check the RESULT in the project rather than on screen:

```bash
STATE=/tmp/fake PATH=/tmp/fakebin:$PATH TYRAX --ui-script <proj> "
frames 20; click Tools; click 'AI Assistant'
click 'AI Assistant/Allow project edits'      # see the trap below
click 'AI Assistant/##chatinput'; text 'add a lever'; click 'AI Assistant/Send'
wait 12; key ctrl+s; shot /tmp/chat.png"
TYRAX --dump <proj>                            # is the lever there?
TYRAX --refresh-gen <proj>                     # did its graph compile?
```

A fake can drive paths a real backend reaches only after a long session: answer with a HUGE tool result (`read_doc` of the biggest page) to push the conversation over budget, and recognise the compaction request by its prompt (`grep -q "You are compacting a conversation"`) — 63.6k of context down to 5.8k, verified in the saved chat file (`role: ['summary', 'user', 'assistant']`). A canned `build_game` proves the park-and-resume; a canned `set_section` that sends an EMPTY list proves the shrink guard refuses it, then that `confirm_replace` gets through. Check the destructive ones against the SAVED project (`--dump`, `objects/*.json`, the manifest). Build canned replies that MISBEHAVE: a fenced envelope with prose around it, a `name`/`arguments` spelling instead of `tool`/`args`, an invented node type, an unknown property, a wrong object name.

## Two traps specific to testing anything that RUNS the game from a chat

**Cause:** both read as a broken feature.
**Fix:** a hermetic `XDG_CONFIG_HOME` isolates PCSX2 as well — its BIOS, memory cards and settings live under `$XDG_CONFIG_HOME/PCSX2`, so the emulator fails to start or comes up unconfigured; symlink the real one in (`ln -sfn ~/.config/PCSX2 $CFG/PCSX2`) and keep the editor isolated. And **a build compiles the IN-MEMORY model**: an assistant that added an object and built it produced a correct ELF while the `.tyra` on disk still knew nothing, so the NEXT session's build regenerated an empty debug runtime — a multi-session test has to save.

## Four traps this cost

**Cause:** each cost real time.
**Fix:** set `XDG_CONFIG_HOME` (or `LOCALAPPDATA`) to a scratch directory for any run that touches the chat (history files and `editor.ini` live there). **A popup's items live in a window of their own** (`##Popup_<hash>`), so `dump` lists them under that name — grep for the LABEL and confirm with `shot` before believing a popup is broken. uiscript's `text` needs the widget to have processed the keystrokes before the button that consumes them is clicked — `ImGui::BeginDisabled` on an empty field means a Send clicked too early is a no-op that still reports success, so put a `frames 5` between typing and clicking. `chatAllowEdits_` is persisted in `editor.ini`, so a scripted click on the checkbox is a toggle, not an assignment — assert with `expect-checked` right after (every edit reading `[failed]` is that switch being off). And the reply is consumed by `aiChatTick`, which runs from `drawUI` whether or not the window is open — a `wait` long enough for N backend invocations is what a multi-step turn needs.
