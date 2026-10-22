"""Archive qualified A34 net head A/B, retaining common owner and fallback gates."""
from pathlib import Path
base=Path(__file__).with_name('tyrax2-world-runtime-archive.py').read_text()
finish="exec(compile(source,str(__file__),'exec'))"
assert base.rstrip().endswith(finish)
exec(compile(base.rstrip()[:-len(finish)],str(__file__),'exec'))
replace("f'world-runtime-{target}.json'","f'head-hot-{target}.json'")
replace("assert data['performanceCandidate'] and data['worldSourceDeliveryEqual'] and data['residualInputEqual']",
    """assert data['performanceCandidate'] and data['worldSourceDeliveryEqual'] and data['residualInputEqual']
 assert data['commonWorldHot'] and data['headNetCompileCost']
 assert not data['admittedHotOriginalHead'] and data['unsupportedOriginalHead']
 assert data['objectHeadStage']=='direct-semantic-hot-owner' and len(data['objectHeadContracts'])==30
 assert len(data['movingHeads'])==6""")
replace("for name in ('tyrax2-world-runtime-fixture.py'",
    "for name in ('tyrax2-head-hot.hpp','tyrax2-head-hot-methods.inc','tyrax2-head-hot-fixture.py','tyrax2-head-hot-analyze.py','tyrax2-head-hot-archive.py','verify-head-hot.cpp','verify-head-hot-facts.cpp','verify-head-hot.py','verify-head-hot-reader.py','tyrax2-object-head.hpp','verify-object-head.cpp','verify-object-head.py','tyrax2-object-head-analyze.py','tyrax2-world-runtime-fixture.py'")
exec(compile(source,str(__file__),'exec'))
