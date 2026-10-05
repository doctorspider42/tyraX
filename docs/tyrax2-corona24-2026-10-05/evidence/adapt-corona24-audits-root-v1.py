from pathlib import Path
import shutil
lab=Path('/mnt/f/Projects/tyrax2-lab-20261001')
p=lab/'audit-corona-probe-native-v2-root-v1.py';t=p.read_text()
t=t.replace("len(m['files'])==501","len(m['files'])==502").replace("F.name=='wild-gs-sprite-corona-probe-physical-v2'","F.name=='corona24-probe-physical-v1'")
start=t.index('design=read(');end=t.index("probeSource=")
replace='''delta=['game/src/terrain_game.cpp','game/inc/pool2_static_probe.hpp','tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp','tyra/engine/src/renderer/3d/pipeline/static/core/programs/cull/stapip_cull_tc_vu1.vclpp','tyra/Makefile.base','tyra/engine/src/renderer/3d/pipeline/static/core/programs/cull/corona_flag_interlock.py']
assert set(m['files'])==set(reference['files'])|{delta[1],delta[5]}
assert {k for k,h in m['files'].items()if h!=reference['files'].get(k)}==set(delta)
assert m['files']['game/src/main.cpp']==reference['files']['game/src/main.cpp'], 'original Hybrid output retained'
'''
t=t[:start]+replace+t[end:]
t=t.replace("assert 'options.colorDepth = Tyra::ColorDepth::Bits16;' in (F/delta[2]).read_text()","assert 'options.colorDepth = Tyra::ColorDepth::Bits16;' not in (F/'game/src/main.cpp').read_text()")
t=t.replace("RendererCoreDepth::bits!=16","RendererCoreDepth::bits!=24").replace('len(mapping)==492','len(mapping)==493')
lines=t.splitlines()
lines=[s for s in lines if not (('pricing[' in s or 'pricing=' in s) and ('actualVUPrograms' in s or 'proof.json' in s))]
t='\n'.join(lines)+'\n'
# This diagnostic deliberately changes TC; every other image stays baseline-equal below.
t=t.replace("'allActualVUImagesEqualPricing':True","'allActualVUImagesEqualPricing':False")
t=t.replace("'sourceFiles':501","'sourceFiles':502").replace("'mirroredInputs':492","'mirroredInputs':493").replace('NATIVE_501','NATIVE_502')
needle="assembled=[]"
extra='''vsm=pin(Path(str(tc)+'.vsm'));schedule=vsm.read_text();assert len(re.findall(r'abs\\.[xyzw]+ VF00, VF18\\s+fmand',schedule))==6
assert programs['StaPipVU1Cull_TC_CodeStart']['sha256']!=read(P.parent/'wild-gs-sprite-corona-native-root-review-v3/proof.json')['actualVUPrograms']['StaPipVU1Cull_TC_CodeStart']['sha256']
'''
t=t.replace(needle,extra+needle)
(lab/'audit-corona24-probe-native-root-v1.py').write_text(t)
t=(lab/'compile-corona-abi-v6-root-v1.py').read_text().replace("a.fixture.name=='wild-gs-sprite-corona-physical-v2'","a.fixture.name=='corona24-probe-physical-v1'")
(lab/'compile-corona24-abi-root-v1.py').write_text(t)
root=lab/'corona24-probe-runtime-v1';assert not root.exists()
shutil.copytree(lab/'wild-gs-sprite-corona-probe-runtime-v2',root,ignore=shutil.ignore_patterns('__pycache__'))
for p in root.glob('*.py'):
 t=p.read_text().replace('wild-gs-sprite-corona-probe-physical-v2','corona24-probe-physical-v1').replace('==501','==502').replace('frozen501','frozen502').replace('220<=a.slot<=231','232<=a.slot<=243').replace('corona-probe-v2-case','corona-probe-v3-case').replace('20261005-v2','20261005-v3').replace('depth=16','depth=24').replace('32767.5','8388607.5').replace('actual 16bit depth scale','actual 24bit depth scale')
 if p.name=='decode-vu1-output.py':
  t=t.replace('0xffff0','0xffffff0')
  t=t.replace("'Z/F/ADC bounds');xy=", "'Z/F/ADC bounds');need(int(float(xyz[0][2]))==xyz[0][2], 'reachable Z exactly convertible');xy=")
 p.write_text(t)
print('Prepared source-bound audit/runtime overlays; device execution pending')
