# troubleshoot/tyra-testing-model-import.md

Accepting a model-import / model-picker change. Symptom first. Feature guide:
`docs/model-import.md`; the importer's own traps are in
`tyra-editor-dev-model-import.md` (same folder).

## Proving an import ran, with no GUI

**Cause:** the importer is host-only and headless-capable, but the Import model
window needs a desktop session to open its native file picker.
**Fix:** drive the core from the CLI:

```
TYRAX --import-model <proj> <file> [--split none|top|mesh] [--pivot keep|bottom|center]
      [--keep f] [--max-texture px] [--animated] [--no-static] [--textures dir]
      [--name stem] [--dry-run]
```

`--dry-run` prints the objects, top-level groups, missing textures and the plan
(triangles, list vs strip vertices); otherwise it writes into `res/models`
(split: `res/models/<name>/`) and never touches the project file.

## The object count is wrong for a big `.fbx`

**Cause:** the 256-slot matrix-palette bug (see `tyra-editor-dev-model-import.md`).
**Fix:** count objects vs the source - a 546-mesh `.fbx` must report 545-546, not
256; read one written `.obj`/`.mtl`. Then `--refresh-gen` a project using it:
static -> `.tmdl`, animated -> `<stem>__ovr*.tskl` (sibling `.mtl` override).

## Driving the Import model window with no display

**Cause:** on Linux the native picker is `zenity`, which needs a display.
**Fix:** fake it:

```bash
printf '#!/bin/sh\ncat /tmp/pick.txt\n' > /tmp/fakebin/zenity; chmod +x /tmp/fakebin/zenity
echo /path/to/models.fbx > /tmp/pick.txt
PATH=/tmp/fakebin:$PATH TYRAX --ui-script <proj> "frames 20; click 'Import model...'; wait 3;
  click 'Import model/##split'; click 'One per top-level object (465)';
  click 'Import model/Add to the scene'; click 'Import model/Import'; wait 6; key ctrl+s"
ls <proj>/objects | wc -l      # one object per imported model
```

Picker tiles are labelled by file name
(`click 'Choose model/##modelgrid/cat.glb'`).

## A static `.glb` must leave its `.obj` twin and `.mtl` untouched

**Fix:** pick a `.glb` from **Model...** and assert `<stem>.obj` appears beside it
and the existing `<stem>.mtl` is byte-identical (`md5`).
