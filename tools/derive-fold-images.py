"""Derive the shipping light-fold clip images from their generated bases.

stapip_clip_c_fold_vu1.vclpp and stapip_clip_tc_fold_vu1.vclpp are the images
TYRA_VU1_EXP_EE_LIGHT_FOLD links (stapip_vu1_experiments.hpp): the base program
from `tyrax-editor --vu-emit` with the lit peer path's light-matrix multiply
removed, because the EE uploads the light directions already multiplied by it.
Re-run this after copying new clip bases into the engine:

    python tools/derive-fold-images.py vendor/tyra/engine [--check]
"""
import argparse
import re
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('engine', type=Path)
parser.add_argument('--check', action='store_true')
args = parser.parse_args()
clip = args.engine / 'src/renderer/3d/pipeline/static/core/programs/clip'

FOLDED = '  ; CalculateTyraDirectionalLights (folded: EE uploads D*M)'
MATRIX_LOADS = ('    lq.xyz     lightMatrix[0], 4(vi00)  ; LoadTyraDirectionalLights\n'
                '    lq.xyz     lightMatrix[1], 5(vi00)\n'
                '    lq.xyz     lightMatrix[2], 6(vi00)\n')


def derive(stem, path_name, matrix, dirs, corners, drop_loads):
    out = (clip / f'{stem}.vclpp').read_text()
    # The light-matrix multiply on each corner goes; the first direction
    # product inherits the macro's comment.
    for c in corners:
        old = (f'    mula.xyz   acc, {matrix[0]}, {c}[x]  ; CalculateTyraDirectionalLights\n'
               f'    madda.xyz  acc, {matrix[1]}, {c}[y]\n'
               f'    madd.xyz   {c}, {matrix[2]}, {c}[z]\n'
               f'    mula.xyz   acc, {dirs}, {c}[x]\n')
        assert out.count(old) == 1, (stem, c, out.count(old))
        out = out.replace(old, f'    mula.xyz   acc, {dirs}, {c}[x]{FOLDED}\n')
    if drop_loads:  # the C image loads a matrix it no longer reads
        assert out.count(MATRIX_LOADS) == 1, (stem, 'loads')
        out = out.replace(MATRIX_LOADS, '')
    name = re.search(r'^\.name (\w+)$', out, re.M)[1]
    out = out.replace('.name ' + name, '.name ' + name + '_FOLD', 1)
    out = re.sub(r'^(#vuprog \w+)$', r'\1FOLD', out, count=1, flags=re.M)
    out = (f'; Added by TyraX: EXPERIMENT IMAGE - (b) light matrix folded on the EE (the {path_name} path)\n'
           '; Linked only with its toggle on (stapip_vu1_experiments.hpp);\n'
           f'; derived from {stem}.vclpp - re-derive, do not edit.\n' + out)
    dest = clip / f'{stem.replace("_vu1", "_fold_vu1")}.vclpp'
    if args.check:
        assert dest.read_text() == out, f'{dest} is stale'
    else:
        dest.write_bytes(out.encode())
    print(dest)


derive('stapip_clip_c_vu1', 'D',
       ['lightMatrix[0]', 'lightMatrix[1]', 'lightMatrix[2]'],
       'lightDirections[0]', ['normal1', 'normal2', 'normal3'], True)
derive('stapip_clip_tc_vu1', 'TD', ['envBasisX', 'envBasisY', 'envBasisZ'],
       'spotPos', ['color1', 'color2', 'color3'], False)
