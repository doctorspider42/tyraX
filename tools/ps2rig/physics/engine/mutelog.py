"""engine/mutelog.py - RIG ENGINE ONLY (engine/setup.py): TYRA_LOG can be muted
(TyraProbe::muteLog) so a run's own periodic telemetry stops costing frames.
Stays an expression, like the release ((void)0)."""
import sys
p = sys.argv[1].rstrip('/') + '/inc/debug/debug.hpp'
lines = open(p, encoding='utf-8', newline='').read().split('\n')
out = []
for l in lines:
    if l.startswith('namespace TyraProbe {'):
        continue
    if l.startswith('#define TYRA_LOG(...) '):
        out.append('namespace TyraProbe { inline volatile bool muteLog = false; }')
        out.append('#define TYRA_LOG(...) ((void)(TyraProbe::muteLog || '
                   '(TyraDebug::writeLines("LOG: ", ##__VA_ARGS__, "\\n"), true)))')
        continue
    out.append(l)
open(p, 'w', encoding='utf-8', newline='').write('\n'.join(out))
print('mutelog ok')
