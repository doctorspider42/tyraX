# Changelog

## 0.3.1

- `.vclpp` in a **generated game project** now gets the macro and `VU1_*`
  address catalogue too. It is read from the engine tree named by the project's
  own `.vscode/c_cpp_properties.json`, which the editor already writes for
  IntelliSense - so there is no new file and nothing to keep in sync. This
  matters because that is where a project's OWN microprogram lives
  (`src/gen/vu_custom_*.vclpp`, docs/vu-authoring.md): the file most likely to
  be read by someone who does not already know the engine was the one getting
  the least help.
- The two addresses a generated program reads, `VU1_CUSTOM_PARAMS_ADDR` and
  `VU1_CUSTOM_TIME_ADDR`, hover with their meaning. Nothing was added to the
  extension for them - they are picked up by the same scan as every other
  address, which is the point of scanning rather than tabulating.

## 0.3.0

- `.vclpp` / `.vcl` / `.vsm` / `.i` become a first-class language: syntax
  highlighting, hovers for all 54 VU instructions, the registers and the
  directives, plus the MACRO and `VU1_*` address catalogue scanned live from the
  engine tree in the workspace. The opcodes are tabulated (a fixed ISA); the
  macros and addresses are read from the files, because copying them would be a
  second source of truth that rots the first time someone adds one.

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
