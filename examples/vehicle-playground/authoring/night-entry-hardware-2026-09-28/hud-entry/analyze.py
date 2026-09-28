from pathlib import Path
import csv, json, statistics, hashlib, sys

p = Path(sys.argv[1])
elf = Path(sys.argv[2]) if len(sys.argv)>2 else None
rows = [{k: float(v) for k, v in r.items()} for r in csv.DictReader((p/'frame-cost.csv').open())]
attrs = [{k: float(v) for k, v in r.items()} for r in csv.DictReader((p/'frame-attrib.csv').open())]
traffic = [{k: float(v) for k, v in r.items()} for r in csv.DictReader((p/'traffic.csv').open())]
assert len(rows) == len(attrs) == len(traffic) == 960
assert [r['frame'] for r in rows] == [r['frame'] for r in attrs] == [r['frame'] for r in traffic] == list(range(960))
assert all(r['speed'] == 0 and r['x'] == 0 and abs(r['z'] + 74.303497) < 1e-5 for r in rows)
assert all(r['driver'] == -1 for r in rows[:180]) and all(r['driver'] == 0 for r in rows[180:])
assert all(r['pica_speed'] == r['strix_speed'] == 0 for r in traffic)
assert all(max(r[k] for r in traffic) == min(r[k] for r in traffic) for k in ['pica_x','pica_z','strix_x','strix_z'])
for r in rows: r['work'] = r['update_ms'] + r['submit_ms'] + r['finish_ms']
def summary(lo, hi):
    w = rows[lo:hi]; a = attrs[lo:hi]; vals = sorted(r['work'] for r in w)
    d = {k: statistics.median(r[k] for r in w) for k in ['work','update_ms','vehicles_included_ms','submit_ms','finish_ms','vif_wait_included_ms','bounds_included_ms','dispatch_included_ms','packet_included_ms','uploads','reuploads','triangles','fps']}
    d.update({k: statistics.median(r[k] for r in a) for k in ['dmHud_ms','rsTotal_ms','rsObjects_ms','rsObjSubmit_ms','rsTerrainDraw_ms','rsWheels_ms','rsEnvProbe_ms']})
    d.update(count=len(w), p95=vals[int(.95*(len(w)-1))], maximum=max(vals), over20=sum(v>20 for v in vals))
    return d
log = (p/'ps2client-stdout.txt').read_text(errors='replace')
assert log.count('LOG: HUDENTRY fix ') == 1
fix = int(log.split('LOG: HUDENTRY fix ',1)[1].split()[0])
d = {'fix':fix, 'walking':summary(120,180), 'firstEntry':summary(180,181), 'warm':summary(240,480), 'elfSha256':hashlib.sha256(elf.read_bytes()).hexdigest() if elf else (p/'elf-sha256.txt').read_text(encoding='utf-8').strip()}
d['fontReads'] = log.count('open name host:/fonts/atlas-default.png')
d['iconReads'] = log.count('open name host:/hud/icons.png')
assert d['fontReads'] == 1
if fix: assert d['iconReads'] == 0
else: assert d['iconReads'] == 1
(p/'summary.json').write_bytes((json.dumps(d,indent=2)+'\n').encode())
print(json.dumps(d,indent=2))
