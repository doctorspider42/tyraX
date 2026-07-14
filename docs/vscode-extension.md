# The Tyra VS Code extension

Generated projects carry a bit of C++ that lives in plain text files:
[custom flow-graph nodes](custom-flow-nodes.md) (`.flownode`) and
[custom screen effects](custom-screen-effects.md) (`.screenfx`). Both are a
`key = value` header, a `---` separator, then a C++ body with `{placeholder}`
substitutions. The **Tyra VS Code extension** (`tools/vscode-tyra`) turns those
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

## Installing it

You normally don't have to do anything: the first time you use **Open in
VS Code** (from the Flow Graph *Custom nodes…* menu, the UI Editor *Custom
effects…* menu, or the Scripts panel) the editor copies the extension into
`~/.vscode/extensions` and launches VS Code. **Reload the VS Code window once**
after the first install to activate it. To (re)install without opening a
project, use **Custom nodes… ▸ Install VS Code extension**.

Under the hood the editor copies `tools/vscode-tyra` (resolved next to the
`tyra-editor.exe`, the same way `c_cpp_properties.json` finds the engine
headers) into `~/.vscode/extensions/tyra.tyra-flownode-<version>`, clearing any
older install of the same id. Generated projects also get a
`.vscode/extensions.json` that recommends the extension (id
`tyra.tyra-flownode`), so an already-installed copy is not re-prompted.

### Installing it by hand

The extension is plain JavaScript with no build step, so you can also drop the
folder into `~/.vscode/extensions` yourself, or package and install a `.vsix`:

```sh
cd tools/vscode-tyra
npx @vscode/vsce package
code --install-extension tyra-flownode-*.vsix
```

## For maintainers

`tools/vscode-tyra` is a self-contained extension:

- `package.json` — declares the two languages (`tyra-flownode` → `.flownode`,
  `tyra-screenfx` → `.screenfx`), their grammars and snippets.
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

The extension is verified offline (no VS Code UI needed): the grammars are
tokenized with `vscode-textmate`, the `extension.js` logic runs against a mock
`vscode` module, and the manifest is validated by packaging a `.vsix` with
`vsce` — see the PROGRESS entry for the harness.
