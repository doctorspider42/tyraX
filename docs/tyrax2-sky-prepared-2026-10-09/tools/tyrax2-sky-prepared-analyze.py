"""A04 extends A03 validation with complete prepared-bound consumption."""
import csv
import copy
import json
import re
import subprocess
import sys
from pathlib import Path
ROOT=Path(__file__).resolve().parent
root,logfile=map(Path,sys.argv[1:3])
subprocess.run([sys.executable,str(ROOT/'tyrax2-sky-bounds-analyze.py'),str(root),str(logfile)],check=True)
rows=list(csv.DictReader((root/'drive-probe.csv').open()))
log=logfile.read_text(errors='replace')
def validate_prepared(data,text):
    for r in data:
        hits,misses=int(r['skyPreparedHits']),int(r['skyPreparedMisses'])
        assert misses==0 and hits==(int(r['skyEligible']) if int(r['phase']) in (1,4) else 0)
    builds=re.findall(r'SKYPREP phase=(\d+) offset=(\d+) vertices=(\d+) maximum=(\d+) boxes=(\d+) bytes=(\d+)',text)
    assert {int(b[0]) for b in builds}==set(range(6))
    assert all(int(o)<1000 and int(v)>0 and int(m)>0 and int(n)>0 and int(size)>0 for p,o,v,m,n,size in builds)
    return builds
builds=validate_prepared(rows,log)
controls=[]
for name,key,value in [('missing prepared hit','skyPreparedHits','0'),('metadata miss','skyPreparedMisses','1')]:
    bad=copy.deepcopy(rows);next(r for r in bad if int(r['phase'])==1)[key]=value
    try:validate_prepared(bad,log)
    except AssertionError:controls.append(name)
    else:raise RuntimeError('Accepted '+name)
try:validate_prepared(rows,re.sub(r'(SKYPREP phase=\d+ offset=)\d+',r'\g<1>1400',log,count=1))
except AssertionError:controls.append('late preparation')
else:raise RuntimeError('Accepted late preparation')
result=dict(builds=builds,rows=len(rows),zeroMisses=True,fullCandidateCoverage=True,negativeControls=controls)
(root/'sky-prepared-analysis.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
