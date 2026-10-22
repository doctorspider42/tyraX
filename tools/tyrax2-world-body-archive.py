"""Archive A31 cold compiler/execution gates, not performance acceptance."""
from pathlib import Path
base=Path(__file__).with_name('tyrax2-coarse-strip-archive.py').read_text()
finish="exec(compile(source,str(__file__),'exec'))"
assert base.rstrip().endswith(finish)
exec(compile(base.rstrip()[:-len(finish)],str(__file__),'exec'))
replace("f'coarse-strip-{target}.json'","f'world-body-{target}.json'")
replace("assert set(data['coarseContracts'])", """assert data['worldCompiledExecution'] and not data['performanceCandidate']
 assert len(data['worldBodies'])==30 and 'contrasts' not in data and 'firstOrderGate' not in data
 assert set(data['coarseContracts'])""")
replace("for name in ('tyrax2-coarse-strip-fixture.py'", "for name in ('tyrax2-world-strip-plan.hpp','verify-world-strip-plan.cpp','tyrax2-world-body.hpp','verify-world-body.cpp','tyrax2-world-body-fragment.inc','tyrax2-world-body-fixture.py','tyrax2-world-body-analyze.py','verify-world-body-reader.py','tyrax2-world-body-archive.py','tyrax2-coarse-strip-analyze.py','tyrax2-coarse-strip-archive.py','tyrax2-coarse-strip-fixture.py'")
exec(compile(source,str(__file__),'exec'))
