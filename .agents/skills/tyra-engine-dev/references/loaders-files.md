# Loaders and file access (host:, cdrom0:)

Read when: you touch `loaders/` (`LeanObjLoader`, `TmdlLoader`, `TanmLoader`, `TsklLoader`, `png_loader`), `file/` (`FileUtils::fromCwd`), any asset path builder, or diagnose "Failed to load"/missing assets. Docs: docs/model-pipeline.md, docs/texture-atlasing.md, docs/ambient-occlusion.md, docs/emissive-materials.md.

## Loader contract: soft-fail
- New asset loaders: `TYRA_SOFT_ERROR` (debug/debug.hpp; logs the same delimited block the editor surfaces, no halt) + safe fallback; never `TYRA_ASSERT` on a missing file.
- `png_loader.cpp`: missing/empty texture → 8x8 magenta `makePlaceholderTexture`. `audio_song.cpp`: missing WAV → `load()` returns early, `play()` no-ops.
- Already non-fatal: `LeanObjLoader`/`TanmLoader`/`TsklLoader` return null (`nullptr` / empty `unique_ptr`) on a bad file (callers null-check); `AudioAdpcm::load` returns `nullptr`, `tryPlay` guards.
- Legacy `md2_loader` / TinyObjLoader `obj_loader` still assert — generated games don't use them.
- Placeholders/skips can silently shrink a scene's workload — see performance.md before benchmarking.

## `LeanObjLoader` (OBJ+MTL, host:/cdrom0:-safe) — now only the fallback path
Parsing semantics mirror the editor's `src/objparser.cpp` — **keep the two in sync**. Parses:
- `refl` sphere-map statement for reflective materials incl. TyraX `-rounded` (centroid-radial env normals for flat surfaces).
- `Ke` = emission floor of emissive materials (`# tyra-glow*` hint lines are editor-only; baked tables carry what the game needs).
- `# tyra-uvrect u0 v0 du dv` (written by the texture-atlas bake into baked .mtl): vertex UVs multiplied through it at load; `LeanMtlMaterial::uvRect` exposes it for the game's primitive builders.
- Sidecar `<model>.aov` (baked AO: "TXAO" + u32 count + one visibility byte per obj `v`) → per-vertex `vertexAo` bytes the game folds into its shade bake.

## `TmdlLoader` (`loaders/3d/tmdl_loader/`) — what generated games actually load
Binary static models (docs/model-pipeline.md). Reads the whole file sequentially and memcpys each part, because the stored layout IS `LeanObjMaterial::vertices`; triangulation, flat normals, material assignment, atlas UV rects, texture paths and LOD tiers were resolved by the editor's `templates::bakeStaticModels`. Layout lives in the editor's `src/tmdl.hpp` — **keep the two in sync**. Returns the same `LeanObjMesh` (one geometry path) with differences callers must know:
- texture names are already **cwd-relative** — do NOT prepend a directory;
- `LeanObjMaterial::lods` may carry decimated tiers (never from `.obj`);
- v3: `LeanObjMesh::shadowVertices`, a positions-only shadow proxy the flashlight's volumes extrude when the real mesh is over budget.
- 9216-vertex model: 286 ms → 39 ms load.
- Strip runs inside `.tmdl`: static-pipeline.md (`meshstrip::kRun`).

## Files over PS2 host fs / disc
- `fseek`/`ftell` are unreliable over `host:` — read into memory and parse there (the WAV parser walks RIFF chunks in memory).
- `cdrom0:` differs from `host:`: ISO9660 uses `\`, upper-case, `;1` suffix; `FileUtils::fromCwd` and the extension helpers convert. **Test asset-loading changes on BOTH boot paths** (host: via Build & Run; cdrom0: via Export PS2 ISO).
- **`getcwd()` may or may not end with a separator.** `fromCwd` was `cwd + file`, correct only for `host:/dir/bin/`; a current ps2sdk omits the slash → `.../binlivepad.bin` → PCSX2 "IopHLE: Denying access to path outside of ELF directory" (reads like the emulator sandbox, not a missing character), and the game opens NOTHING, its own log included. `fromCwd` inserts the separator itself and leaves a trailing `:` alone (`host:` + `file` is valid). Any new path builder owes the same care.
- `TYRA_LOG` never opens `cdrom0:LOG.TXT` for write (guard in `debug.cpp`; it wedged ISO boots).
- `isoexport` walks all of `bin/` — host-only caches must be skipped (build-and-toolchain.md, `RESDIR_SKIP`).

## Textures
Power-of-two sizes; PNG 32/24bpp or palettised 8/4bpp (palettised fastest on PS2). Residency: textures-vram.md.
