# Changelog

## 0.4.0

- **`.menustyle` menu stylesheets** ([docs](../../docs/menu-styles.md)):
  highlighting for selectors, the `:selected` / `:disabled` states, `menu#name`
  scopes, `--variables` and `var()`, colours, gradients, `url()`, `{{icons}}`,
  `@style` and `@transition`; snippets for a whole sheet, a selected-row
  highlight, a scrolling list, a description pane, a value bar, a transition and
  a per-menu override. The grammar deliberately lists no property NAMES - the
  authoritative list is `menustyle::propSpecs()` in the editor and a copy here
  would drift, so anything before a `:` inside a block highlights as a property.
- **`.vclpp` / `.vcl` / `.vsm` VU1 microprograms** reach a packaged build for the
  first time: the language went into the sources for 0.3.0 but that version was
  never packaged, so users kept getting 0.2.0. Both languages were invisible in
  practice until this release - see `package-vsix.py`, which exists so
  repackaging no longer needs node.

## 0.2.0

- `.flownode`: the per-parameter tip keys `tip0`..`tip3` and `tip_string`
  (`desc` says what the node does, a tip says what one knob does). Highlighted,
  hovered, completed next to the param they document, and diagnosed - a `tipN`
  whose `numN` is missing is flagged, since the editor drops it. Tips are
  deliberately NOT subject to the contiguity rule that `num*` params are.

## 0.1.0

- Initial release.
- Syntax highlighting for `.flownode` and `.screenfx` (header + embedded C++ body
  + `{placeholder}` overlay).
- Snippets, header/placeholder hovers, key/value/placeholder completion.
- Diagnostics: unknown/duplicate keys, enum & pin validation, contiguous
  `num*`/`param*`, body/separator checks, unknown/undefined placeholders.
