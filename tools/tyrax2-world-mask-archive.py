"""Preserve the all-plane cold gate; no timing acceptance."""
from pathlib import Path
base=Path(__file__).with_name('tyrax2-world-body-archive.py').read_text()
finish="exec(compile(source,str(__file__),'exec'))"
assert base.rstrip().endswith(finish)
exec(compile(base.rstrip()[:-len(finish)],str(__file__),'exec'))
replace("f'world-body-{target}.json'","f'world-mask-{target}.json'")
replace("assert data['worldCompiledExecution']", """assert data['worldAllPlanes'] and len(data['worldMasks'])==30
 assert data['worldCompiledExecution']""")
replace("for name in ('tyrax2-world-strip-plan.hpp'", "for name in ('tyrax2-world-mask.hpp','verify-world-mask.cpp','tyrax2-world-mask-fixture.py','tyrax2-world-mask-analyze.py','verify-world-mask-reader.py','tyrax2-world-mask-archive.py','tyrax2-world-strip-plan.hpp'")
exec(compile(source,str(__file__),'exec'))
