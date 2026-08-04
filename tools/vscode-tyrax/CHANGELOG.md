# Changelog

## 0.4.0

- **A project's own VU program (`src/vu/*.cpp`) gets language support**, which
  is the file a VU1 program is actually WRITTEN in. Until now the help lived on
  `.vclpp` - the generated OUTPUT, the one file nobody types. Scoped by PATH
  (`**/src/vu/*.cpp`), so ordinary game code is untouched.
  - **Diagnostics for the rules a header cannot state**, each of which used to
    cost a Docker build or a console run: a script writing **Q** (grey stipple
    on hardware, invisible on the host), `scratch(n)` past the four that exist,
    a geometry slot without `movesGeometry()`, `movesGeometry()` on `Slot::Ndc`
    where it is a cost with no benefit, a displacement claiming a SUBSET of the
    material classes (an object's passes separate), and writing the position at
    `Slot::Color` / `Slot::Texture`, where it has already become a 12.4 integer.
  - **Hover that bridges C++ to the machine**: `b.mulInto` shows `mul` and what
    it does, `sineApprox` its seventeen instructions, `MXYZ` which fields it
    writes. The instruction descriptions come from the same catalogue the
    `.vclpp` hover uses - one table, two languages.
  - **A `vuprogram` scaffold** with the five overrides and the two rules that
    are easy to get wrong already spelled out.
  - It deliberately does NOT repeat the headers: `vu::Ctx` and `vugen::Vu`
    hover out of `vugen/vushader.hpp` through cpptools, and duplicating them
    would be a second source of truth for the thing this framework exists to
    stop duplicating. The editor now puts `${workspaceFolder}/vugen` on the
    generated includePath, which is what makes that work.
- **The packaged `.vsix` was two feature releases stale** - 0.2.0, predating
  `vu.js` entirely, so everyone who installed through the editor got an
  extension with NO VU1 support while the source and the docs described it. The
  editor now installs the newest `.vsix` in the folder rather than the first one
  the directory hands back.

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
