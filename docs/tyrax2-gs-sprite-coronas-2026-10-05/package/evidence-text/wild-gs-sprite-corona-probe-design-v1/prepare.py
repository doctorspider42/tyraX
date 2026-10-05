"""Private source/helper generation; does not invoke build or lifecycle helpers."""
from pathlib import Path
import hashlib,json,py_compile
ROOT=Path(__file__).resolve().parent;LAB=ROOT.parent;RUNTIME=LAB/'wild-gs-sprite-corona-probe-runtime-v1';OLD=LAB/'wild-pool-lattice-probe-runtime-v1';PRICING=LAB/'wild-gs-sprite-corona-physical-v2'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();pins={}
def replace(s,a,b):
 assert s.count(a)==1,a
 return s.replace(a,b)
for name in ['savestate-reader-pinned.py','launch-probe.py','run-case.py','capture-ready.py','verify-capture.py','finish-probe.py']:
 p=OLD/name;pins[str(p)]=sha(p);s=p.read_text()
 s=s.replace('wild-pool-lattice-probe-physical-v1','wild-gs-sprite-corona-probe-physical-v1').replace('pool-lattice-probe-','corona-probe-')
 s=s.replace('502','501').replace('frozen501 lattice correctness','frozen501 corona correctness')
 s=s.replace('vertices','case').replace('choices=(96,192)','choices=(1,2,3,4)').replace('n in(96,192)','n in(1,2,3,4)')
 s=s.replace('POOL2PROBE_READY','CORONAPROBE_READY')
 if name=='run-case.py':
  s=replace(s,'assert 184<=a.slot<=195','assert 208<=a.slot<=219')
  s=replace(s,'stem=f\'corona-probe-{"a" if a.case==96 else "b"}{a.case}', 'stem=f\'corona-probe-case{a.case}')
 if name=='verify-capture.py':
  s=replace(s,"d.decode(vu,'A96'if n==96 else 'B192',arm,repeats)","d.decode(vu,n,arm,repeats)")
  s=replace(s,'finalEpochRGBAValidated=True,','sourceFinalEpochValidated=True,actualFogLightUniformsValidated=True,actualCompletedOutputRouteValidated=True,')
  s=replace(s,'finalPackageCounts=sorted(bank),','finalPackageCounts=sorted(bank),actualOutputWitnesses={str(k):dict(marker=v[\'marker\'],sprite=v[\'sprite\'],fogUnique=sorted(set(v[\'fogValues\'])),rgbaUnique=sorted(set(v[\'rgbaValues\'])))for k,v in bank.items()},')
  s=s.replace('Final requested color epoch is independently validated in every saved VU output RGBA','Final requested source epoch is validated from saved actual input colors alongside completed output routing')
  s=replace(s,"limitations=['Native", "limitations=['Latest source colors identify the final epoch bank; an old other bank is excluded for single-package cases.', 'Native")
 if name=='finish-probe.py':
  s=s.replace("v['finalEpochRGBAValidated']","v['sourceFinalEpochValidated']")
  s=s.replace('Final requested color epoch is proven in saved VU output','Final requested source epoch is proven in actual saved VU input colors and completed output route')
  s=s.replace('finalColorEpoch=', 'finalSourceEpoch=')
 (RUNTIME/name).write_bytes(s.encode())
for arm in(0,1):
 for case in(1,2,3,4):
  for r in(1,2,3):
   p=ROOT/f'configs/arm{arm}-case{case}-repeat{r}.cfg';p.parent.mkdir(exist_ok=True);p.write_bytes(f'{arm} {case} {r}\n'.encode())
for p in RUNTIME.glob('*.py'):py_compile.compile(str(p),doraise=True)
for p,h in pins.items():assert sha(Path(p))==h
manifest=PRICING/'target-source-manifest.json';pins[str(manifest)]=sha(manifest);assert len(json.loads(manifest.read_text())['files'])==500
report=dict(status='SOURCE_ONLY_V6_CORONA_ACTUAL_OUTPUT_PROBE_PREPARED_NOT_BUILT',pricingManifestSha256=sha(manifest),pricingSourceFiles=500,expectedDiagnosticFiles=501,fixture='wild-gs-sprite-corona-probe-physical-v1',slots='208..219',inputs=pins,outputs={str(p.relative_to(LAB)):sha(p)for folder in(ROOT,RUNTIME)for p in folder.rglob('*')if p.is_file()and '__pycache__'not in p.parts and p.name!='preparation-proof.json'},limits=['No native build or device execution.','Actual source inputs, completion, output primitive marker/type/count, negative witnesses and paired payload remain runtime gates.','Static microprobe positivity does not establish full-night workload activation, raster or pricing gain.'])
(ROOT/'preparation-proof.json').write_bytes((json.dumps(report,indent=2)+'\n').encode());print(report['status'])
