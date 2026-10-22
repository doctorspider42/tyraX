"""Read existing A02b physical evidence before assuming shared pass geometry."""
import argparse, collections, csv, hashlib, json
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('csv',type=Path);p.add_argument('out',type=Path)
a=p.parse_args();rows=list(csv.DictReader(a.csv.open()))
assert len(rows)==1027
def groups(keys):
 result=collections.defaultdict(list)
 for row in rows:result[tuple(row[k] for k in keys)].append(row)
 return [group for group in result.values() if len(group)>1]
prefix=['phase','offset','view','vertices']
shared=groups(prefix)
exact=groups(prefix+['sourceCount','version','stripped'])
report=dict(source=str(a.csv),sha256=hashlib.sha256(a.csv.read_bytes()).hexdigest(),rows=len(rows),
 sameAddressGroups=len(shared),sameRecordedGeometryKeyGroups=len(exact),
 groups=[dict(pose={k:g[0][k] for k in prefix},counts=[int(r['sourceCount']) for r in g],
              passes=[int(r['pass']) for r in g],clips=sum(int(r['callsClip']) for r in g),
              culls=sum(int(r['callsCull']) for r in g)) for g in shared],
 scope='Historical nine-frame sample, not current A25 pricing; no complete model/view/program-capacity contract in CSV')
a.out.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='groups'},indent=2))
