"""Preserve qualified combined A31+A32 first-stage runtime evidence."""
from pathlib import Path
base=Path(__file__).with_name('tyrax2-world-runtime-archive.py').read_text()
finish="exec(compile(source,str(__file__),'exec'))"
assert base.rstrip().endswith(finish)
exec(compile(base.rstrip()[:-len(finish)],str(__file__),'exec'))
replace("f'world-runtime-{target}.json'","f'prepared-runtime-{target}.json'")
replace("assert len(data['worldOwnerContracts']['WORLDOWNER'])==30", """assert len(data['worldOwnerContracts']['WORLDOWNER'])==30
 assert data['combinedWorldOwner'] and data['preparedFrontendStage']=='owned-validation-capacity-bounds'
 assert len(data['preparedFrontendContracts'])==30 and len(data['movingPreparedFrontend'])==6
 assert len(data['negativeControls'])==47""")
replace("for name in ('tyrax2-world-runtime-fixture.py'",
    "for name in ('tyrax2-prepared-runtime-fixture.py','tyrax2-prepared-runtime-methods.inc','tyrax2-prepared-runtime.hpp','tyrax2-prepared-front.hpp','tyrax2-prepared-runtime-analyze.py','tyrax2-prepared-runtime-archive.py','verify-prepared-front.cpp','verify-prepared-front.py','verify-prepared-runtime.cpp','verify-prepared-runtime.py','tyrax2-world-runtime-fixture.py'")
exec(compile(source,str(__file__),'exec'))
