"""Archive A35 early-Core preflight correctness, explicitly not hardware price."""
from pathlib import Path
base=Path(__file__).with_name('tyrax2-world-runtime-archive.py').read_text()
finish="exec(compile(source,str(__file__),'exec'))"
assert base.rstrip().endswith(finish)
exec(compile(base.rstrip()[:-len(finish)],str(__file__),'exec'))
replace("f'world-runtime-{target}.json'","f'world-front-preflight-{target}.json'")
replace("assert data['performanceCandidate'] and data['worldSourceDeliveryEqual'] and data['residualInputEqual']",
 """assert not data['performanceCandidate'] and data['worldSourceDeliveryEqual'] and data['residualInputEqual']
 assert data['worldFrontStage']=='early-Core-preflight-only' and data['originalHeads'] and data['commonWorldHot']
 assert len(data['earlyFrontWitness'])==6 and len(data['actualWholeHeadOracle'])==30
 assert 'contrasts' not in data and 'firstOrderGate' not in data""")
replace("for name in ('tyrax2-world-runtime-fixture.py'",
 "for name in ('tyrax2-world-front.hpp','tyrax2-world-front-methods.inc','tyrax2-world-front-fixture.py','tyrax2-world-front-preflight.py','tyrax2-world-front-archive.py','verify-world-front.cpp','verify-world-front.py','verify-world-front-producer.inc','verify-world-front-reader.py','verify-prepared-front.cpp','verify-prepared-front.py','tyrax2-prepared-front.hpp','tyrax2-world-runtime-fixture.py'")
replace("'tyrax2-world-front-methods.inc',","'tyrax2-world-front-methods.inc','tyrax2-world-front-admission.inc',")
exec(compile(source,str(__file__),'exec'))
