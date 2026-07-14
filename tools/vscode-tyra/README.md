# Tyra Flow Nodes & Screen Effects — VS Code extension

Language support for the two text-file formats [tyra-editor](https://github.com/doctorspider42/tyra-editor)
projects use for custom logic:

- **`.flownode`** — [custom flow-graph nodes](../../docs/custom-flow-nodes.md)
- **`.screenfx`** — [custom screen effects](../../docs/custom-screen-effects.md)

Both are a `key = value` header, a `---` separator, then a C++ body with
`{placeholder}` substitutions. The extension gives each of them:

- **Syntax highlighting** — the header (keys / values / comments) plus full
  **embedded C++** highlighting of the body after `---`, with the `{obj}`,
  `{num0}`, `{p0}`, … placeholders overlaid.
- **Snippets** — `flownode-inline`, `flownode-call`, `screenfx`, and one per
  header key.
- **Validation** (Problems panel) — unknown/duplicate keys, bad enum values
  (`string`, `in`/`out` pin kinds, `exec_out`), non-contiguous `num*`/`param*`,
  a missing/empty body, `call = fn` with a stray inline body, and unknown or
  undefined `{placeholder}`s.
- **Hover & completion** — descriptions for every header key and placeholder,
  key/value completion in the header, and placeholder completion in the body.

The C++ body itself resolves against the project's engine headers via the
generated `.vscode/c_cpp_properties.json`, so open the **whole project folder**
(not a single file) for IntelliSense on `flow_nodes.hpp` / effect bodies.

## Installing

The tyra-editor installs this automatically the first time you use
**Open in VS Code** (it copies this folder into `~/.vscode/extensions`; reload
the window once). To install it by hand, either copy this folder there yourself,
or package and install a `.vsix`:

```sh
cd tools/vscode-tyra
npx @vscode/vsce package
code --install-extension tyra-flownode-*.vsix
```

## Development

Plain JavaScript, no build step. Press **F5** in this folder to launch an
Extension Development Host, then open a `.flownode` / `.screenfx` file.
