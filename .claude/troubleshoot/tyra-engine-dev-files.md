# troubleshoot/tyra-engine-dev-files.md

Asset loading, path building, the OBJ/TMDL loaders and the soft-error contract.
Symptom first.

## `fseek`/`ftell` lie over the PS2 host filesystem

**Cause:** the PS2 host filesystem does not honour them reliably.
**Fix:** prefer read-into-memory parsing — the WAV parser walks RIFF chunks in memory
for this reason.

## Textures must be power-of-two sized

**Cause:** the GS swizzle assumes it.
**Fix:** PNG 32/24bpp or palletized 8/4bpp (palletized is fastest on PS2).

## `cdrom0:` and `host:` paths differ

**Cause:** ISO9660 uses `\`, upper-case and a `;1` version suffix.
**Fix:** `FileUtils::fromCwd` (in `file/`) and the extension helpers handle the conversion. Test
asset-loading changes on BOTH boot paths (host: via normal Build & Run, cdrom0: via
Export PS2 ISO).

## Every path became `.../binlivepad.bin` and nothing opened

**Cause:** whether `getcwd()` ends with a separator is not a guarantee. `fromCwd` used
to be a plain `cwd + file`, correct only while the SDK returns `host:/dir/bin/`; a
current ps2sdk returns it without the slash. PCSX2 answered *"IopHLE: Denying access to
path outside of ELF directory"* — which reads as the emulator's host: sandbox refusing
you, not as a missing character. The game then opens NOTHING, its own log included.
**Fix:** `fromCwd` inserts the separator itself now, and leaves a trailing `:` alone
(`host:` + `file` is a valid path). Any new path builder owes the same care.

## A missing texture/model/sfx trapped the game

**Cause:** legacy `TYRA_ASSERT` on a missing file. `md2_loader` / TinyObjLoader
`obj_loader` still assert (fine, generated games don't use them).
**Fix:** when adding a new asset loader, follow the soft-error pattern (soft-error +
safe fallback), don't `TYRA_ASSERT` on a missing file. `TYRA_SOFT_ERROR`
(`debug/debug.hpp`) logs the *same* delimited `====== TYRA ======` block a fatal
assert does — so the editor surfaces it identically — but with a `Non-fatal error (game
keeps running)!` header and **no halt**. The asset loaders use it: `png_loader.cpp`
returns an 8x8 magenta placeholder (`makePlaceholderTexture`) on a missing/empty
texture, and `audio_song.cpp` skips a missing music WAV (`load()` returns early,
`play()` no-ops when unloaded). The fork's other loaders were already non-fatal —
`LeanObjLoader`/`TanmLoader`/`TsklLoader` return `nullptr` on a bad file (callers
null-check — an empty `unique_ptr`), and `AudioAdpcm::load` returns `nullptr` + `tryPlay` guards.

## `LeanObjLoader` (OBJ+MTL)

**Cause:** host:/cdrom0:-safe parsing that must mirror the editor's
`src/objparser.cpp` — keep the two in sync.
**Fix:** it parses the `refl` sphere-map statement for reflective materials incl. the
TyraX `-rounded` flag (centroid-radial env normals for flat surfaces); parses `Ke` as
the emission floor of emissive materials (`docs/emissive-materials.md` — the
`# tyra-glow*` hint lines are editor-side only); parses the TyraX
`# tyra-uvrect u0 v0 du dv` hint the texture-atlas bake writes into baked .mtl files
(docs/texture-atlasing.md) — model vertex UVs multiply through it at load and
`LeanMtlMaterial::uvRect` exposes it for the generated game's primitive builders; and
quietly picks up the TyraX `<model>.aov` baked-ambient-occlusion sidecar — "TXAO" + u32
count + one visibility byte per obj `v` entry, docs/ambient-occlusion.md — into
per-vertex `vertexAo` bytes.

## `TmdlLoader`

**Cause:** `loaders/3d/tmdl_loader/`, docs/model-pipeline.md — the binary static-model
format the generated game actually loads; the ASCII `LeanObjLoader` above is now only
the fallback path. It reads the whole file sequentially and memcpys each part, because
the stored layout IS `LeanObjMaterial::vertices`; everything else (triangulation, flat
normals, material assignment, atlas UV rects, texture paths, LOD tiers) was resolved by
the editor's `templates::bakeStaticModels`; the layout lives in the editor's
`src/tmdl.hpp`, **keep the two in sync**.
**Fix:** it returns the same `LeanObjMesh` so the game keeps one geometry path, with two
differences the caller must know: texture names are already **cwd-relative** — do NOT
prepend a directory — and `LeanObjMaterial::lods` may carry decimated tiers, which an
`.obj` never has (and `LeanObjMesh::shadowVertices`, v3, a positions-only shadow proxy
the flashlight's volumes extrude when the real mesh is over budget). Loading a
9216-vertex model went 286 ms -> 39 ms. Strip runs inside a `.tmdl` are the run
contract in `tyra-engine-dev-packages.md` (`meshstrip::kRun`).

## `Texture::sourcePath`

**Cause:** the full load path, set by `TextureRepository::add` — `name` keeps only the
basename. The generated texture hot-reload poller `live_tex.gen.cpp` matches repainted
files against it and re-uploads via `RendererCoreTexture::updateTextureInfo` to the SAME
VRAM address — see docs/live-link.md.
**Fix:** NOTE the engine always constructs the clut `TextureData`, a non-paletted
texture just has `clut->data == nullptr` — test data presence, not the object pointer.

## Missing resources can invalidate a performance comparison

**Cause:** native compilation alone does not populate a missing `.res-baked` mirror.
PNG soft-error placeholders and skipped missing TMDLs can leave a game running with much
less work than the intended scene.
**Fix:** validate deployed asset hashes and actual GS images, not just successful ELF
linking or equal triangle counts between two broken fixtures. The full-asset gate and
September 14 correction are recorded in docs/performance-hardware-recheck.md.
