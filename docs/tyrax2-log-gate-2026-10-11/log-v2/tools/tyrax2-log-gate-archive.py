"""Archive A27's qualified byte/raster/log-window evidence and source inputs."""
from pathlib import Path
source=Path(__file__).with_name('tyrax2-clip-capacity-archive.py').read_text()
source=source.replace("f'clip-capacity-{target}.json'","f'log-gate-{target}.json'")
source=source.replace("assert data['valid'] and data['groupRasterEqual']",
    "assert data['valid'] and data['groupRasterEqual'] and data['consumerInputEqual'] and len(data['ordinaryLogWindows'])==6")
source=source.replace("files.append(report.name)",
    "files.append(report.name)\n if target=='emulator':\n  assert sha(a.fixture/folder/'log.txt')==data['ordinaryLogSha256']\n  files.append(folder+'/log.txt')")
source=source.replace("for name in ('tyrax2-clip-capacity.py'",
    "for name in ('tyrax2-log-gate.py','tyrax2-log-gate-analyze.py','tyrax2-log-gate-archive.py','tyrax2-fixed-header.py','tyrax2-fixed-header-analyze.py','tyrax2-clip-capacity.py'")
exec(compile(source,str(Path(__file__)),'exec'))
