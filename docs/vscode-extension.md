# The TyraX VS Code extension

Generated projects carry a bit of C++ that lives in plain text files:
[custom flow-graph nodes](custom-flow-nodes.md) (`.flownode`) and
[custom screen effects](custom-screen-effects.md) (`.screenfx`). Both are a
`key = value` header, a `---` separator, then a C++ body with `{placeholder}`
substitutions. The **TyraX VS Code extension** (`tools/vscode-tyrax`) turns those
files from plain text into a first-class editing experience.

## What it gives you

- **Syntax highlighting** — the header (keys, values, `#` comments) plus **full
  embedded C++ highlighting** of the body after `---`, with the placeholders
  (`{obj}`, `{num0}`, `{p0}`, …) overlaid on top.
- **Snippets** — `flownode-inline`, `flownode-call`, `screenfx`, and one per
  header key (type `title`, `param`, `call`, …).
- **Validation** (the Problems panel) — unknown or duplicate keys, bad enum
  values (`string`, the `in`/`out` pin kinds, `exec_out`), non-contiguous
  `num*` / `param*` params, a missing or empty body, `call = fn` with a stray
  inline body, and unknown or undefined `{placeholder}`s.
- **Hover & completion** — a description for every header key and placeholder,
  key/value completion in the header, and placeholder completion in the body.

The C++ body itself resolves against the project's engine headers through the
generated `.vscode/c_cpp_properties.json`, so open the **whole project folder**
(not a single file) for IntelliSense on `flow_nodes.hpp` and effect bodies. The
`call = fn` logic still belongs in `flow_nodes.hpp` — the `.flownode` stays a
thin manifest — but the extension now colours and checks that manifest too.

## VU1 microprograms (`.vclpp`, `.vcl`, `.vsm`)

The third language, and the one where the help matters most - a VU1 program is
an unbroken wall of three-letter abbreviations, which is most of why the
[VU framework](vu-framework.md) exists in the first place.

- **Syntax highlighting** - the vclpp layer (`#include`, `#define`, `#macro`,
  `Name{ }` calls, `#vuprog`), the VCL markers (`--enter`, `--cont`,
  `--barrier`), instructions with their `.xyzw` destination mask, registers
  (`vf00` and `vi00` highlighted as the hardwired constants they are),
  broadcast fields and labels.
- **Hover on every instruction** - 54 of them, each with what it actually does:
  what `madd` accumulates, why `sq.xyz` writing three words and not four
  matters, that `rsqrt` writes Q and therefore has to precede a perspective
  divide, what `xgkick` hands to the GS.
- **Hover and completion on the Tyra macro library** - with the macro's
  parameters, its comment block and how many instructions it expands to.
  Completing one inserts a filled-in `Name{ a, b }` call.
- **Hover and completion on the `VU1_*` addresses** with their values and the
  comments that explain them.
- **Two warnings the assembler will not give you**: a `div`/`rsqrt` whose Q is
  overwritten before anything reads it, and a macro call inside a macro body
  (vclpp does not nest macros - the line reaches the assembler verbatim).
- **Snippets** - `vuprog` for the whole per-buffer skeleton, plus `vuloop`,
  `vumvp`, `vupers`, `vufixcolor`, `vuscale`.

**The catalogue is read from the engine tree, not copied into the extension.**
On activation it scans `vendor/tyra/engine/.../shared/*.i` and the
`*_shared_defines.h` headers for `#macro` and `#define`, and a file watcher
re-scans when they change - so adding a macro to `tyra_macros.i` makes it
complete and hover immediately, with no extension change. Only the instruction
set is written down in `vu.js`, because that is fixed silicon. Open the whole
repo (not a single file) for the scan to find anything; without it the
instruction help still works and only the macro/address entries are missing.

**In a generated game project the scan follows a different pointer.** That is
where a project's OWN microprogram lives - `src/gen/vu_custom_*.vclpp`
([docs/vu-authoring.md](vu-authoring.md)) - and such a project has no
`vendor/tyra` to glob, so until 0.3.1 the file most likely to be read by
someone who does not already know the engine was the one getting the least
help. When the workspace globs find nothing, the extension reads the engine
path out of the project's own `.vscode/c_cpp_properties.json`, which the editor
already writes for IntelliSense. No new file, nothing to keep in sync.

## VU scripts (`src/vu/*.cpp`)

A project's own VU program written in C++ ([docs/vu-authoring.md](vu-authoring.md))
is an **ordinary `.cpp` file**, so none of the `.vclpp` help above applies to it -
what it wants is plain C++ IntelliSense over `vu::` and `vugen::`. The Scripts
panel lists these under *VU programs* and clicking one opens the project in
VS Code with that file focused.

It resolves through the same generated `.vscode/c_cpp_properties.json`, which
lists `${workspaceFolder}/vugen` - the copy of the framework the editor drops
into the project whenever it has a script (and removes when the last one goes).
That entry is what makes `#include "vushader.hpp"` resolve and
`c.` / `vugen::` / `vuir::M` complete; without it that one file was the only
one in the project with no IntelliSense at all. Open the **project folder**, not
the file.

The header the completions come from is `vugen/vushader.hpp` - `vu::Ctx` is the
authoring surface and `Ctx::raw()` is the escape hatch onto `vugen::Vu`, whose
whole method library (`sineApprox`, `transform`, `spotLight`, the masked
arithmetic) is documented in the header itself.

## Installing it

You normally don't have to do anything: the first time you use **Open in
VS Code** (from the Flow Graph *Custom nodes…* menu, the UI Editor *Custom
effects…* menu, or the Scripts panel) the editor installs the extension and
launches VS Code. If VS Code was already open, **reload the window once**
(Command Palette ▸ *Developer: Reload Window*) to activate it. To (re)install
without opening a project, use **Custom nodes… ▸ Install VS Code extension** —
it reports the outcome in the status bar.

Under the hood the editor runs `code --install-extension` on the prebuilt
`tools/vscode-tyrax/*.vsix` (resolved next to the `tyrax-editor.exe`, the same way
`c_cpp_properties.json` finds the engine headers). This is the **only** reliable
way: modern VS Code (≥ 1.74) loads only the extensions listed in its own
manifest cache, so an extension folder merely copied into `~/.vscode/extensions`
is silently ignored. It therefore needs VS Code's **`code` CLI on PATH** (in
VS Code: Command Palette ▸ *Shell Command: Install 'code' command in PATH*); if
it isn't, the status bar says so instead of failing silently. Generated projects
also get a `.vscode/extensions.json` recommending the extension (id
`tyrax.tyrax-flownode`), so an already-installed copy is not re-prompted.

### Installing it by hand

The extension is plain JavaScript with no build step, so you can also drop the
folder into `~/.vscode/extensions` yourself, or package and install a `.vsix`:

```sh
cd tools/vscode-tyrax
npx @vscode/vsce package
code --install-extension tyrax-flownode-*.vsix
```

## For maintainers

`tools/vscode-tyrax` is a self-contained extension:

- `package.json` — declares the two languages (`tyrax-flownode` → `.flownode`,
  `tyrax-screenfx` → `.screenfx`), their grammars and snippets.
- `syntaxes/*.tmLanguage.json` — TextMate grammars. The body is a begin/end
  region that starts at `---` and runs to end-of-file, sets
  `contentName: meta.embedded.block.cpp` (so VS Code injects the C++ grammar),
  and overlays a `{placeholder}` pattern.
- `snippets/*.json`, `language-configuration.json` — snippets and comment /
  bracket config.
- `extension.js` — diagnostics, hover and completion. The `SPEC` table (valid
  keys, enum values, placeholders) **must stay in sync** with the editor
  parsers `src/flownode.cpp` and `src/screenfx.cpp`; when you add a header key or
  placeholder to one, update the other and the docs above.
- `tyrax-flownode-<version>.vsix` — the **prebuilt package the editor installs**,
  committed to the repo. It is not rebuilt automatically, so after **any** change
  to the extension you must regenerate and re-commit it (bump the `version` in
  `package.json` first so `code --install-extension --force` picks up the new
  build): `cd tools/vscode-tyrax && npx @vscode/vsce package`, then delete the old
  versioned `.vsix`. A stale `.vsix` is a real trap — the source looks updated
  but users get the old build.

The extension is verified offline (no VS Code UI needed): the grammars are
tokenized with `vscode-textmate`, the `extension.js` logic runs against a mock
`vscode` module, and the manifest is validated by packaging a `.vsix` with
`vsce` — see the PROGRESS entry for the harness.
