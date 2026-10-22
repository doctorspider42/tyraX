"""Archive A28's bound sources and qualified held-focus/moving evidence."""
from pathlib import Path
source = Path(__file__).with_name('tyrax2-clip-capacity-archive.py').read_text()
source = source.replace("f'clip-capacity-{target}.json'", "f'terrain-focus-{target}.json'")
source = source.replace("assert data['valid'] and data['groupRasterEqual']",
    "assert data['valid'] and data['groupRasterEqual'] and data['consumerInputEqual'] and data['quietBothArms'] and len(data['heldTerrainContracts'])==30")
source = source.replace("for name in ('tyrax2-clip-capacity.py'",
    "for name in ('tyrax2-terrain-focus.py','tyrax2-terrain-focus-analyze.py','tyrax2-terrain-focus-archive.py','tyrax2-log-gate.py','tyrax2-fixed-header-analyze.py','tyrax2-clip-capacity.py'")
exec(compile(source, str(Path(__file__)), 'exec'))
