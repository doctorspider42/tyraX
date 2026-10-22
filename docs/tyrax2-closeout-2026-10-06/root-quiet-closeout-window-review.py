from pathlib import Path
import re,json
lab=Path('F:/Projects/tyrax2-lab-20261001')
runs={'tyrax2-closeout-emulator-v4-kind0-order0':'capture-final','tyrax2-closeout-emulator-v4-kind1-order0':'capture-final','tyrax2-closeout-emulator-v4-kind2-order0':'capture-final','tyrax2-closeout-emulator-quiet-motion-v4':'capture-status','tyrax2-closeout-emulator-quiet-showcase-v4':'capture-final'}
bad=re.compile(r'VEHCONTACT|VEH pos|FRAMEARENA frames|NATIVEVIF frames|ORDEREDFRAME frames|FRAMEPIPELINE starts|livepad\.bin|livedbg\.cmd|log\.txt')
for name,capture in runs.items():
 lines=(lab/name/capture/'runtime-emulator.log').read_text(errors='replace').splitlines()
 spans=[];start=None
 for i,line in enumerate(lines):
  if 'LOG: CLOSEOUTCONTEXT ' in line and 'offset=750 'in line:start=i
  if 'LOG: CLOSEOUTCONTEXT ' in line and 'offset=1155 'in line:
   assert start is not None
   unexpected=[l for l in lines[start+1:i]if bad.search(l)]
   assert not unexpected,(name,unexpected)
   warnings=[l for l in lines[start+1:i]if 'WARN:'in l]
   spans.append(dict(startLine=start+1,endLine=i+1,unexpectedHealthyTelemetry=[],warnings=warnings));start=None
 assert len(spans)==3,name
 (lab/name/'quiet-window-review.json').write_text(json.dumps(dict(status='PASS_NO_OBSERVED_NAMED_HEALTHY_TELEMETRY_IN_COLD_ENVELOPES',spans=spans,hardwarePerformanceQualified=False,limitations=['Log-family check cannot prove absence of all I/O or price common observer footprint.','Warnings are retained and audio is not qualified.']),indent=2)+'\n')
 print(name,'PASS')
