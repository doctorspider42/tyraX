# Changelog

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
