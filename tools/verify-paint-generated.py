"""Target-compiler syntax checks of freshly generated FPP/orbit paint code."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('fixture',type=Path)
args=parser.parse_args();root=args.fixture.resolve()
binding=json.loads((root/'native-binding.json').read_text())
stage=Path('/home/spider/.cache/tyrax/native')
key=hashlib.sha256(b'/mnt/c/Users/pawel/AppData/Local/tyra-editor/toolchain').hexdigest()[:24]
toolchain=stage/'toolchains'/key
cc=toolchain/'ee/bin/mips64r5900el-ps2-elf-g++'
includes=[Path(binding['engineMirror'])/'inc',toolchain/'ps2sdk/ee/include',toolchain/'ps2sdk/common/include',toolchain/'ps2sdk/ports/include']
for kind in ('fpp','orbit'):
    game=root/('generated-'+kind)
    for enabled in (0,1):
        command=[str(cc),'-fsyntax-only','-D_EE','-G0','-Wall','-O3',f'-DTYRA_VU1_EXP_PAINT={enabled}']
        command+=['-I'+str(p) for p in [game/'inc']+includes]
        command+=[str(game/'src/gen/game_physics.gen.cpp')]
        subprocess.run(command,check=True)
        print(f'{kind} target physics/header syntax: PAINT={enabled} passed',flush=True)
