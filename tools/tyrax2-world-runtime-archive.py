"""Archive qualified producer-owner net-cost A/B and correctness evidence."""
from pathlib import Path
base=Path(__file__).with_name('tyrax2-clip-ring-archive.py').read_text()
finish="exec(compile(source, str(Path(__file__)), 'exec'))"
assert base.rstrip().endswith(finish)
exec(compile(base.rstrip()[:-len(finish)],str(__file__),'exec'))
replace("f'clip-ring-{target}.json'","f'world-runtime-{target}.json'")
replace("assert data['valid'] and data['groupRasterEqual'] and data['consumerInputEqual']", """assert data['valid'] and data['groupRasterEqual'] and not data['consumerInputEqual']
 assert data['performanceCandidate'] and data['worldSourceDeliveryEqual'] and data['residualInputEqual']
 assert data['residualCoarseSourceDeliveryEqual'] and len(data['movingWorldOwners'])==6
 assert len(data['worldOwnerContracts']['WORLDOWNER'])==30""")
replace("assert len(data['ringContracts'])==len(data['heldTerrainContracts'])==30",
        "assert len(data['worldOwnerContracts']['RINGCONTRACT'])==len(data['heldTerrainContracts'])==30")
replace("for optional in ('host-capacity.log','build-missing-include.log'):",
        "for optional in ('host-capacity.log','build-missing-include.log','owner-reader-host.log','owner-switch-host.log'):")
replace("for name in ('tyrax2-clip-ring-fixture.py'", "for name in ('tyrax2-world-runtime-fixture.py','tyrax2-world-runtime-methods.inc','tyrax2-world-runtime.hpp','tyrax2-world-runtime-analyze.py','tyrax2-world-runtime-preflight.py','tyrax2-world-runtime-archive.py','verify-world-runtime-reader.py','verify-world-runtime-switch.py','tyrax2-world-strip-plan.hpp','tyrax2-world-body.hpp','tyrax2-world-owner.hpp','tyrax2-world-payload.hpp','verify-world-payload.cpp','verify-world-owner.cpp','verify-world-owner.py','tyrax2-coarse-strip-analyze.py','tyrax2-coarse-strip-archive.py','tyrax2-clip-ring-analyze.py','tyrax2-clip-ring-archive.py','tyrax2-clip-ring-fixture.py'")
exec(compile(source,str(__file__),'exec'))
