#!/usr/bin/env python3
"""Engine-only rig instrumentation, for any generated project.

usage: make_engine_probe.py <project> <engine-root-copy> [--lean] [--opt=Os]

make_arm.py also edits the Motor District's generated game (night switch, pad
neutralisation, drive cycle); its anchors exist only in that example. This
script applies just the engine half: the probe in Engine::realLoop (CLD win
rows: loop period, work = period minus renderer stalls, p50/p95, late frames),
quiet TYRA_LOG, the core/submit scopes and the optimisation flags. The game is
left exactly as generated, so the windows sample whatever the project does from
boot (its authored camera, tour or idle state). Use with arm.sh:
    MAKE_ARM=make_engine_probe.py bash tools/ps2rig/arm.sh S1 --lean
"""
import sys, shutil, pathlib

proj = pathlib.Path(sys.argv[1])
eng_root = pathlib.Path(sys.argv[2])
eng = eng_root / 'engine'
opt = next((a.split('=', 1)[1] for a in sys.argv if a.startswith('--opt=')), None)
rig = pathlib.Path(__file__).parent


def edit(path, old, new, count=1):
    t = path.read_text(encoding='utf-8-sig')
    n = t.count(old)
    if n != count:
        raise SystemExit(f'anchor count {n}!={count} in {path}: {old[:70]!r}')
    path.write_bytes(t.replace(old, new).encode())


def include(path):
    t = path.read_text(encoding='utf-8-sig')
    if 'cld_probe.hpp' not in t:
        path.write_bytes(('#include "debug/cld_probe.hpp"\n' + t).encode())


shutil.copy(rig / 'cld_probe.hpp', eng / 'inc/debug/cld_probe.hpp')
edit(eng / 'inc/debug/debug.hpp',
     '#define TYRA_LOG(...) TyraDebug::writeLines("LOG: ", ##__VA_ARGS__, "\\n")',
     '#define TYRA_LOG(...) ((void)0) // rig: quiet')
edit(eng / 'src/renderer/core/paths/path1/vif1_queue.cpp',
     'if (++frames % 120 == 0) {', 'if ((++frames % 120 == 0) && false) {')
p = eng / 'src/engine.cpp'
include(p)
edit(p, 'void Engine::realLoop() {',
     'void Engine::realLoop() {\n  Cld::begin(renderer.core.getStallTotal());')
p = eng / 'src/renderer/3d/pipeline/static/core/stapip_core.cpp'
include(p)
edit(p, 'void StaPipCore::renderNow(StaPipBag* bag, StaPipLightReception reception) {',
     'void StaPipCore::renderNow(StaPipBag* bag, StaPipLightReception reception) {\n  Cld::S cldCore("core");')
p = eng / 'src/renderer/core/paths/path1/vif1_queue.cpp'
include(p)
edit(p, 'u32 Vif1Queue::submit(const void* chain, u32 qwords, bool* sourceCopied) {',
     'u32 Vif1Queue::submit(const void* chain, u32 qwords, bool* sourceCopied) {\n  Cld::S cldSub("q.submit");')
flags = '-D_EE -G0 -Wall ' + ('-' + opt if opt else '-O3') + (' -DCLD_NOPMU=1' if '--lean' in sys.argv else '')
edit(eng_root / 'Makefile.base', 'CFLAGS      += -D_EE -G0 -Wall -O3', 'CFLAGS      += ' + flags)
print('engine probe', proj, eng_root, flags)
