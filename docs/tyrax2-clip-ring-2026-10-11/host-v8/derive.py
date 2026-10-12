"""A29 private actual-generator clip output rings; no production shader edits."""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('out', type=Path)
p.add_argument('--toolchain', type=Path, help='Also schedule/assemble private images')
a = p.parse_args()
repo = Path(__file__).resolve().parents[1]
assert not a.out.exists(), a.out
a.out.mkdir(parents=True)
names = ['vugen.cpp', 'vugen.hpp', 'vuir.cpp', 'vuir.hpp', 'vusim.cpp',
         'vusim.hpp', 'vuasm.cpp', 'vuasm.hpp']
pins = {}
for name in names:
    src = repo/'src'/name
    pins[name] = hashlib.sha256(src.read_bytes()).hexdigest()
    shutil.copy2(src, a.out/name)
source = (a.out/'vugen.cpp').read_text()
tag_rule = 'bool tagsPerBuffer(const Desc& d) { return d.clip && d.sharedClipTexDir; }'
assert source.count(tag_rule) == 1
source = source.replace(tag_rule, '''bool privateRingTags = false;
bool tagsPerBuffer(const Desc& d) {
    return d.clip && (d.sharedClipTexDir || privateRingTags);
}''')
start = source.index('void buildClipBody(')
end = source.index('// The .vclpp emitter', start)
body = source[start:end].replace('void buildClipBody(', 'void buildRingBody(', 1)
body = body.replace('    Vu b(prog);', '''
    const bool previousRingTags = privateRingTags;
    privateRingTags = true;
    Vu b(prog);''', 1)
body = body.replace('    b.finish();', '''
    b.finish();
    privateRingTags = previousRingTags;''', 1)

def edit(old, new):
    global body
    assert body.count(old) == 1, old
    body = body.replace(old, new)

edit('    emitTagBlock(b, prog, d, k, hdr.primTag, destAddress);', '''
    // Private clip-only metadata uses the tail after the scratch polygons.
    b.isw(destAddress, b.izero(), 1016, 0);
    b.isw(destAddress, b.izero(), 1016, 1);
    b.isw(buffer, b.izero(), 1016, 2);
''')
edit('    b.bind(triLoop);', '''    b.bind(triLoop);
    ilwInto(prog, destAddress, b.izero(), 1016, 1);
    ilwInto(prog, sceFlag, b.izero(), 1016, 2);
    loadInto(prog, hdr.primTag, sceFlag, 1);
    emitTagBlock(b, prog, d, k, hdr.primTag, destAddress);
    b.iaddiuInto(outCount, b.izero(), 0);
''')
edit('    b.bind(triNext);', '''    b.bind(triNext);
    // One complete clipped polygon per ring; never split a fan.
    ilwInto(prog, sceFlag, b.izero(), 1016, 1);
    b.iaddiuInto(buffer, outCount, 0x4000);
    b.iaddiuInto(buffer, buffer, 0x4000);
    b.isw(buffer, sceFlag, primQuad, 0);
    b.xgkick(sceFlag);
    b.barrier();
    // The second kick completes the preceding ring before it is reused.
    ilwInto(prog, buffer, b.izero(), 1016, 0);
    b.iaddInto(buffer, buffer, buffer);
    b.iaddiuInto(buffer, buffer, tagQuads(d) + 21 * outStride);
    b.isubInto(buffer, buffer, sceFlag);
    b.isw(buffer, b.izero(), 1016, 1);
''')
kick = body.index('    // --- the kick ')
finish = body.index('    b.cont();', kick)
body = body[:kick] + body[finish:]
source += '\nnamespace vugen { namespace {\n' + body + '\n}}\n'
strip = body.replace('void buildRingBody(', 'void buildStripRingBody(', 1)
strip = strip.replace('    b.isw(buffer, b.izero(), 1016, 2);', '''
    b.isw(buffer, b.izero(), 1016, 2);
    ilwInto(prog, sceFlag, buffer, 1, 3);
    b.isw(sceFlag, b.izero(), 1016, 3);
''')
strip = strip.replace('    loadInto(prog, hdr.primTag, sceFlag, 1);', '''
    loadInto(prog, hdr.primTag, sceFlag, 1);
    b.subInto(hdr.primTag, b.zero(), b.zero(), MW);
''')
strip = strip.replace('    b.iaddInto(vertexCounter, b.izero(), vertexCount);',
                      '    b.iaddiuInto(vertexCounter, vertexCount, -2);')
anchor = '    auto loadColors = '
assert strip.count(anchor) == 1
strip = strip.replace(anchor, '''
    auto loadCorner = [&](Val value, IVal pointer, int corner,
                          uint8_t mask = MALL) {
        if (corner == 2) {
            loadInto(prog, value, pointer, 2, mask);
        } else {
            ilwInto(prog, sceFlag, b.izero(), 1016, 3);
            if (corner == 0) b.iaddInto(sceFlag, pointer, sceFlag);
            else {
                b.isubInto(sceFlag, pointer, sceFlag);
                b.iaddiuInto(sceFlag, sceFlag, 1);
            }
            loadInto(prog, value, sceFlag, 0, mask);
        }
    };
''' + anchor)
strip = strip.replace('loadInto(prog, color[i], colorData, i)',
                      'loadCorner(color[i], colorData, i)')
strip = strip.replace('loadInto(prog, vertex[i], vertexData, i, MALL,\n'
                      '                 i == 0 ? "object space - the MVP multiply is below" : nullptr)',
                      'loadCorner(vertex[i], vertexData, i)')
strip = strip.replace('loadInto(prog, st[i], stqData, i)',
                      'loadCorner(st[i], stqData, i)')
strip = strip.replace('loadInto(prog, normal[i], normalData, i, MXYZ)',
                      'loadCorner(normal[i], normalData, i, MXYZ)')
for pointer in ('vertexData', 'stqData', 'colorData', 'normalData'):
    strip = strip.replace(f'b.iaddiuInto({pointer}, {pointer}, 3);',
                          f'b.iaddiuInto({pointer}, {pointer}, 1);')
strip = strip.replace('    b.iaddiuInto(vertexCounter, vertexCounter, -3);', '''
    ilwInto(prog, buffer, b.izero(), 1016, 3);
    b.iaddiuInto(sceFlag, b.izero(), 1);
    b.isubInto(sceFlag, sceFlag, buffer);
    b.isw(sceFlag, b.izero(), 1016, 3);
    b.iaddiuInto(vertexCounter, vertexCounter, -1);
''')
source += '\nnamespace vugen { namespace {\n' + strip + '\n}}\n'
hybrid = strip.replace('void buildStripRingBody(', 'void buildHybridRingBody(', 1)
hybrid = hybrid.replace('    b.isw(sceFlag, b.izero(), 1016, 3);', '''
    b.iaddiuInto(buffer, b.izero(), 1);
    b.iandInto(buffer, sceFlag, buffer);
    b.isw(buffer, b.izero(), 1016, 3);
    b.iaddiuInto(buffer, b.izero(), 2);
    b.iandInto(buffer, sceFlag, buffer);
    b.iaddiuInto(sceFlag, b.izero(), 3);
    b.isubInto(sceFlag, sceFlag, buffer);
    b.isw(sceFlag, b.izero(), 1017, 0);
''', 1)
hybrid = hybrid.replace('    b.iaddiuInto(vertexCounter, vertexCount, -2);', '''
    ilwInto(prog, sceFlag, b.izero(), 1017, 0);
    b.iaddInto(vertexCounter, vertexCount, sceFlag);
    b.iaddiuInto(vertexCounter, vertexCounter, -3);
''')
for pointer in ('vertexData', 'stqData', 'colorData', 'normalData'):
    hybrid = hybrid.replace(f'b.iaddiuInto({pointer}, {pointer}, 1);',
        f'{{ ilwInto(prog, sceFlag, b.izero(), 1017, 0);\n'
        f'      b.iaddInto({pointer}, {pointer}, sceFlag); }}')
hybrid = hybrid.replace('    ilwInto(prog, buffer, b.izero(), 1016, 3);\n'
                       '    b.iaddiuInto(sceFlag, b.izero(), 1);', '''
    const Lbl keepListParity = b.label("keepListParity");
    ilwInto(prog, buffer, b.izero(), 1017, 0);
    b.iaddiuInto(buffer, buffer, -1);
    b.branchIfGtz(buffer, keepListParity);
    ilwInto(prog, buffer, b.izero(), 1016, 3);
    b.iaddiuInto(sceFlag, b.izero(), 1);''')
hybrid = hybrid.replace('    b.iaddiuInto(vertexCounter, vertexCounter, -1);', '''
    b.bind(keepListParity);
    ilwInto(prog, sceFlag, b.izero(), 1017, 0);
    b.isubInto(vertexCounter, vertexCounter, sceFlag);''')
source += '\nnamespace vugen { namespace {\n' + hybrid + '\n}}\n'
(a.out/'vugen.cpp').write_text(source)

# The normal simulator records only final RAM. Private per-kick snapshots
# prevent reused output rings from hiding earlier corrupt output.
sim = (a.out/'vusim.cpp').read_text()
anchor = '                res.kicks.push_back('
assert sim.count(anchor) == 1
sim = sim.replace(anchor, '                res.kickMem.push_back(m.mem);\n'+anchor)
(a.out/'vusim.cpp').write_text(sim)
header = (a.out/'vusim.hpp').read_text()
anchor = '    std::vector<int> kicks;'
assert header.count(anchor) == 1
header = header.replace(anchor, '    std::vector<std::vector<uint32_t>> kickMem;\n'+anchor)
(a.out/'vusim.hpp').write_text(header)
shutil.copy2(repo/'tools/verify-clip-ring.cpp', a.out/'verify.cpp')
command = ['g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-I'+str(a.out),
           str(a.out/'verify.cpp')] + [str(a.out/name) for name in
           ['vuir.cpp', 'vusim.cpp', 'vuasm.cpp']] + ['-o', str(a.out/'verify')]
subprocess.run(command, check=True)
result = subprocess.run([str(a.out/'verify'), str(a.out)], check=True,
                        capture_output=True, text=True)
(a.out/'host-result.txt').write_text(result.stdout + result.stderr)
print(result.stdout, end='')
if a.toolchain:
    sizes = {}
    for shader in a.out.glob('*_ring*.vclpp'):
        vcl = shader.with_suffix('.vcl')
        vsm = shader.with_suffix('.vsm')
        obj = shader.with_suffix('.o')
        subprocess.run([str(a.toolchain/'bin/vclpp'), str(shader), str(vcl)], check=True)
        with vsm.open('w') as stream:
            subprocess.run([str(a.toolchain/'bin/vcl'), str(vcl)], stdout=stream, check=True)
        subprocess.run([str(a.toolchain/'dvp/bin/dvp-as'), str(vsm), '-o', str(obj)], check=True)
        kicks = [line for line in vsm.read_text().splitlines() if 'xgkick' in line.lower()]
        assert len(kicks) == 1 and re.match(r'\s*nop\s+xgkick\s+VI\d+\s*$', kicks[0]), kicks
        nm = subprocess.check_output([str(a.toolchain/'dvp/bin/dvp-nm'), str(obj)], text=True)
        addresses = {m[2]: int(m[1], 16) for m in re.finditer(
            r'^([0-9a-fA-F]+)\s+\w\s+(\w+_Code(?:Start|End))$', nm, re.M)}
        stem = next(n[:-9] for n in addresses if n.endswith('CodeStart'))
        sizes[shader.name] = (addresses[stem+'CodeEnd'] - addresses[stem+'CodeStart'])//8
    (a.out/'assembled-sizes.json').write_text(json.dumps(sizes, indent=2)+'\n')
    print('Actual scheduled instruction words:', sizes)
(a.out/'source-pins.json').write_text(json.dumps(pins, indent=2)+'\n')
print('PASS private generator prototype; no production files changed')
