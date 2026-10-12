"""Archive completed A30 source/delivery/raster and same-ELF travel evidence."""
from pathlib import Path
base=Path(__file__).with_name('tyrax2-clip-ring-archive.py').read_text()
finish="exec(compile(source, str(Path(__file__)), 'exec'))"
assert base.rstrip().endswith(finish)
exec(compile(base.rstrip()[:-len(finish)],str(__file__),'exec'))
replace("f'clip-ring-{target}.json'","f'coarse-strip-{target}.json'")
replace("for optional in ('host-capacity.log','build-missing-include.log'):",
        "for optional in ('host-capacity.log','build-missing-include.log','main-loop-switch-host.txt'):")
replace("assert data['valid'] and data['groupRasterEqual'] and data['consumerInputEqual']", """assert data['valid'] and data['groupRasterEqual'] and not data['consumerInputEqual']
 assert data['coarseSourceDeliveryEqual'] and data['residualInputEqual'] and data['baselinePostRouteInputEqual']
 assert set(data['coarseContracts'])=={'COARSESOURCE','COARSEDELIVERY','COARSERESIDUAL'}
 assert all(len(rows)==30 for rows in data['coarseContracts'].values())""")
replace("for name in ('tyrax2-clip-ring-fixture.py'", "for name in ('tyrax2-coarse-strip-fixture.py','tyrax2-coarse-strip-input.hpp','verify-coarse-strip-input.cpp','verify-coarse-strip-reader.py','verify-coarse-strip-switch.py','tyrax2-coarse-strip-analyze.py','tyrax2-coarse-strip-archive.py','tyrax2-clip-ring-analyze.py','tyrax2-clip-ring-archive.py','tyrax2-clip-ring-fixture.py'")
exec(compile(source,str(__file__),'exec'))
