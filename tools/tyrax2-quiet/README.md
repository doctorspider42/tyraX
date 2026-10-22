# Ordinary-clock quiet fixture

Prepare a fresh generated FPP project and current engine copy with:

```text
python tools/tyrax2-quiet-fixture.py --project /absolute/project --engine /absolute/vendor/tyra --editor /absolute/tyrax-editor --out /absolute/new-fixture --order 0
```

Preparation only invokes the editor's `--refresh-gen` on the private copy. It does not build, launch an emulator, operate PS2 hardware or modify the source project/engine. Existing output directories, unsupported templates, ambiguous or changed source anchors are rejected. Rejected partial preparation is preserved with its error. Do not regenerate a prepared copy: generation would remove the private hooks.

Authored output quality, triple buffering, pipeline request, product HUD, save values, scripts, portals, camera, audio and ordinary clocks remain unchanged. Diagnostic overlays and link/debug/logic polling are disabled. Authored remote-pad, input-recorder and keyboard controls remain enabled as authored, including any polling cost they carry. Explicit `--disable-control-apparatus` disables only `remotePad` and `inputRecorder` in the private copy and records these named keys as an apparatus control. Mood and script semantics remain opaque; `night=0` is an unknown placeholder, never a save-slot assignment. Initial support covers the generated FPP `TerrainGame` family and sparse contexts from scene 0.

`quiet-control.cfg` selects Off/On/Off (0) or On/Off/On (1), three 1800-loop phases in the same ELF. Both profilers are compiled out. Common samples use 6144 bytes and chunks use 420 bytes. Five 64-loop chunks span entries 800–1120, using six common reads. On primes 899 and samples 900–1027, using two reads per loop with shared endpoints (262 reads during the tax interval). No sampled/tax frame performs export; authoritative `quiet-cadence.log` is exported at 1200–1327 and closed before explicit completion at 5400 loops.

The entry/exit hooks surround Pad/Game/Info. Whole-loop elapsed minus the existing pacing counter retains other waits, deferred rendering, audio/preemption and interrupts. The sample's final timestamp precedes observer bookkeeping; the common tax chunks include it. Flip hooks reuse existing timestamps and actual owner fields, keeping synthetic/compatibility sequence unknown where appropriate. No wait, fence, register read or third per-loop timestamp is added.

Run the strict analyzer with explicit environment:

```text
python tools/tyrax2-quiet/analyze-quiet.py STDOUT --artifact quiet-cadence.log --expected-order 0 --environment ps2 -o report.json
```

It accepts complete identities only; missing UDP/file records are not reconstructed. Initial pipeline requests may be 0 or 1; an explicit false and a legacy v94 missing request remain synchronous. Sparse actual request changes are retained rather than assumed equal to boot. Compatibility/synthetic completions retain sequence0 as unknown instead of inventing overlap. It retains zero/multiple presentations and synthetic events; periods are eligible only for exactly one corresponding completion. Flip-return periods are distinct from actual TV photons and, under triple buffering, can represent queue/submission boundaries. Ordinary physics and adaptive selector responses can change between arms, so the net tax includes induced choices and noise. No pure CPU bill, isolated observer instruction cost, uniform correction or 60 FPS certification is accepted by this tool.

Source manifests cover generated code, engine C/C++/VU/VCL/IRX/Makefile inputs and project settings; fixture metadata records authoring/baked assets. Before a device launch, preserve exact native build/source/ELF/symbol provenance and verify current baked runtime assets (WAV-to-ADPCM conversion is an explicit transformation). Archive configuration and input hashes before each boot; use unique output directories and preserve rejected evidence. Both orders must use the same ELF, source and runtime assets. Count modulo chunks assume no entire missed wrap between endpoints; independent duration sanity remains an explicit limitation.

Host checks execute the actual headers/runtime (`header-oracle.cpp`, `cfg-oracle.cpp`, `host-runtime.cpp`, `host-controls.py`). Their supplied render/state inputs are specification-only. `verify-source.py` checks two independent final-editor generated maps, exact hook output, quality/save/clock preservation and malformed/repeated anchors. These checks provide no target timing, DMA/cache, pixel or native ABI proof.

The source identity hashes every file in engine/game source and include trees, engine resources and the optional game `vugen` framework, without an extension whitelist (including embedded `.irx-em`). Root build recipes/helpers and project data are also covered; generated `obj`/`bin` outputs are excluded. Runtime assets are recorded separately. PS2DEV/SDK/VCL/compiler binaries and SDK IRX modules named by `.irx-em` recipes remain external build dependencies and require separate native provenance; the manifest does not claim to hash those installed tools. Missing required source trees or recipes reject preparation.

Host VU dependency validation follows the native recipe exactly: only direct lowercase `.cpp` files in `game/src/vu` or `game/src/vu0` activate compilation of `game/vugen/*.cpp`. Active recipes require framework implementations and the literal quoted include closure resolved through source-local paths or `game/vugen`; unresolved or out-of-manifest local includes reject preparation. Empty, nested-only and name-only VU directories do not impose a framework requirement. Installed standard-library/compiler dependencies remain external native provenance.
