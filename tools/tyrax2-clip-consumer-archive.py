"""Preserve A25 using the shared A24 evidence layout and complete tool closure."""
from pathlib import Path
source=Path(__file__).with_name('tyrax2-clip-capacity-archive.py').read_text()
assert source.count("f'clip-capacity-{target}.json'")==1
source=source.replace("f'clip-capacity-{target}.json'","f'clip-consumer-{target}.json'")
source=source.replace("assert data['valid'] and data['groupRasterEqual']","assert data['valid'] and data['groupRasterEqual'] and data['consumerInputEqual']")
source=source.replace("for name in ('tyrax2-clip-capacity.py'","for name in ('verify-clip-consumer.cpp','tyrax2-clip-consumer.py','tyrax2-clip-consumer-analyze.py','tyrax2-clip-consumer-archive.py','tyrax2-clip-capacity.py'")
source=source.replace("if (a.fixture/'host-capacity.log').exists():files.append('host-capacity.log')","if (a.fixture/'host-consumer.log').exists():files.append('host-consumer.log')")
exec(compile(source,str(Path(__file__)), 'exec'))
