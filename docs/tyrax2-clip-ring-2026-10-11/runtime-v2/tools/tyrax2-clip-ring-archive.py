"""Archive A29 qualified sources, same-ELF runs and rejecting reader tools."""
from pathlib import Path
source = Path(__file__).with_name('tyrax2-clip-capacity-archive.py').read_text()
def replace(old,new):
 global source
 assert source.count(old)==1,(old,source.count(old))
 source=source.replace(old,new)
replace("f'clip-capacity-{target}.json'", "f'clip-ring-{target}.json'")
replace("assert data['valid'] and data['groupRasterEqual']", """assert data['valid'] and data['groupRasterEqual'] and data['consumerInputEqual']
 assert data['nativeActive'] and data['quietBothArms'] and data['residentWords']==2034
 assert len(data['ringContracts'])==len(data['heldTerrainContracts'])==30""")
replace("'game/Makefile','tyra/Makefile.base'", "'ring-shader-pins.json','game/Makefile','tyra/Makefile.base'")
replace("if (a.fixture/'host-capacity.log').exists():files.append('host-capacity.log')", """for optional in ('host-capacity.log','build-missing-include.log'):
 if (a.fixture/optional).exists():files.append(optional)""")
replace("for name in ('tyrax2-clip-capacity.py'", "for name in ('tyrax2-clip-ring-fixture.py','tyrax2-clip-ring-input.hpp','verify-clip-ring-input.cpp','tyrax2-clip-ring-analyze.py','tyrax2-clip-ring-archive.py','tyrax2-terrain-focus-analyze.py','tyrax2-fixed-header-analyze.py','tyrax2-clip-capacity.py'")
exec(compile(source, str(Path(__file__)), 'exec'))
