"""Preserve A26 with original SDK byte oracle and both complete A/B runs."""
from pathlib import Path
source=Path(__file__).with_name('tyrax2-clip-capacity-archive.py').read_text()
source=source.replace("f'clip-capacity-{target}.json'","f'fixed-header-{target}.json'")
source=source.replace("assert data['valid'] and data['groupRasterEqual']","assert data['valid'] and data['groupRasterEqual'] and data['consumerInputEqual']")
source=source.replace("for name in ('tyrax2-clip-capacity.py'","for name in ('tyrax2-fixed-header.py','tyrax2-fixed-header-analyze.py','tyrax2-fixed-header-archive.py','tyrax2-clip-consumer.py','tyrax2-clip-consumer-analyze.py','tyrax2-clip-capacity.py'")
exec(compile(source,str(Path(__file__)),'exec'))
