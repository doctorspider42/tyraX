"""Derive default-OFF paint images without modifying their baseline bodies."""
import argparse
import re
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('generated', type=Path, help='buildPaintExperiment output directory')
parser.add_argument('engine', type=Path)
parser.add_argument('--check', action='store_true')
args = parser.parse_args()
programs = args.engine/'src/renderer/3d/pipeline/static/core/programs'
stems = ['cull/stapip_cull_tce_vu1', 'cull/stapip_cull_tce_envn_vu1',
         'clip/stapip_clip_tc_vu1', 'clip/stapip_clip_tc_fold_vu1']
for stem in stems:
    base = ('cull/stapip_cull_tce_vu1' if stem.startswith('cull/') else
            'clip/stapip_clip_tc_vu1')
    generated = (args.generated/(base+'_paint.vclpp')).read_text()
    prepass = re.search(r'^    ilw\.y\s+paintMode,.*?^paintDone:\n', generated, re.M|re.S)
    assert prepass, base
    source = (programs/(stem+'.vclpp')).read_text()
    assert 'paintMode' not in source, stem
    begin = re.search(r'begin:\n    xtop\s+buffer\n', source)
    assert begin, stem
    result = source.replace(begin[0], begin[0]+prepass[0])
    name = re.search(r'^\.name (\w+)$', result, re.M)[1]
    result = result.replace('.name '+name, '.name '+name+'_PAINT')
    result = re.sub(r'^(#vuprog \w+)$', r'\1Paint', result, flags=re.M)
    result = ('; Generated paint variant: tools/emit-paint-images.py; do not edit.\n'
              '; Retains the baseline body and generator-owned prepass.\n'+result)
    dest = programs/(stem.replace('_vu1', '_paint_vu1')+'.vclpp')
    if args.check:
        assert dest.read_text()==result, dest
    else:
        dest.write_bytes(result.encode())
    print(dest)
