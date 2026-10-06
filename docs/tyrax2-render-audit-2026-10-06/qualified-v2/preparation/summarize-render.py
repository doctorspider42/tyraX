from pathlib import Path
import json
r=Path(__file__).parent.parent/'tyrax2-closeout-ps2-render-audit-v1'
d=json.loads((r/'strict-result.json').read_text());b=d['balance']
print('BALANCE',json.dumps(b['byPhase'],indent=2))
for label,entry in [('driving',d['byPhase']['0']),('heavy',d['heavy'])]:
 print(label,'rows',entry.get('enabledRows',entry.get('count')))
 for kind in ['stageStats','workTypeStats']:
  print(kind,[(k,round(v['meanMs'],6))for k,v in sorted(entry[kind].items(),key=lambda x:x[1]['meanMs'],reverse=True)])
print('READS',d['additionalRenderCountReads'])
print('GAPS',[(p,x['interLoopGapStats'],x.get('gapOnMinusOffMs'))for p,x in d['byPhase'].items()])
print('COUNTERS',d['byPhase']['0']['counterTotals'])
