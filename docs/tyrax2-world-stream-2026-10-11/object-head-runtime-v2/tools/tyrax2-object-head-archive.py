"""Preserve cold whole-head execution gates, explicitly excluding FPS pricing."""
from pathlib import Path
base=Path(__file__).with_name('tyrax2-world-runtime-archive.py').read_text()
finish="exec(compile(source,str(__file__),'exec'))"
assert base.rstrip().endswith(finish)
exec(compile(base.rstrip()[:-len(finish)],str(__file__),'exec'))
replace("f'world-runtime-{target}.json'","f'object-head-{target}.json'")
replace("assert data['performanceCandidate'] and data['worldSourceDeliveryEqual'] and data['residualInputEqual']",
    """assert not data['performanceCandidate'] and data['hotOriginalHead']
 assert data['objectHeadStage']=='cold-actual-writer-only' and len(data['objectHeadContracts'])==30
 assert 'contrasts' not in data and 'firstOrderGate' not in data
 assert data['worldSourceDeliveryEqual'] and data['residualInputEqual']""")
replace("for name in ('tyrax2-world-runtime-fixture.py'",
    "for name in ('tyrax2-object-head.hpp','verify-object-head.cpp','verify-object-head.py','verify-object-head-reader.py','tyrax2-object-head-runtime.inc','tyrax2-object-head-fixture.py','tyrax2-object-head-analyze.py','tyrax2-object-head-archive.py','tyrax2-world-runtime-fixture.py'")
exec(compile(source,str(__file__),'exec'))
