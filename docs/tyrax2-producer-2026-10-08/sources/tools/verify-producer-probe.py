"""Compile the real private ledger on host with only the EE clock substituted."""
import subprocess
import copy
import importlib.util
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
header = (root/'tools/tyrax2-producer-probe.hpp').read_text()
old = 'inline U ticks(){U t;asm volatile("mfc0 %0,$9":"=r"(t)::"memory");return t;}'
assert header.count(old) == 1
header = header.replace(old, 'inline U ticks(){static U fake=100;return fake+=10;}')
test = r'''
#include <cassert>
#include "probe.hpp"
int main(){
 using namespace MotionProbe;
 static_assert(sizeof(names)/sizeof(names[0])==MetricCount);
 phase=1;offset=360;begin(0);
 unsigned saved;
 {Owner a(Roads);Camera b(Reflection);saved=tag();add(DeferredPush);}
 {Owner a(Objects);Restore b(saved);assert(producer==Roads&&view==Reflection);
  add(DeferredPop);add(ClipChunks,7);add(SnapshotBytes,32);}
 assert(producer==Other&&view==Main);
 assert(values[saved][ClipChunks]==7&&values[saved][DeferredPop]==1);
 assert(values[Unassigned*ProducerCount][SnapshotBytes]==32);
 {CoreScope ignored;Wait ignoredWait;}
 assert(values[0][CoreCalls]==0);
 renderBegin();end(0);assert(rows[0].c[ClipChunks]==7&&rows[0].errors==0);
 phase=4;offset=360;begin(0);
 {Owner a(Roads);CoreScope core;{Wait outer;{Wait nested;}}add(Bags);}
 assert(values[Roads][CoreCalls]==1&&values[Roads][WaitCalls]==1);
 assert(values[Roads][WaitTicks]>0&&values[Roads][WaitTicks]<values[Roads][CoreTicks]);
 assert(values[Roads][Bags]==0&&coreDepth==0&&waitDepth==0);
 {Owner a(Terrain);CoreScope ignored;Wait ignoredWait;}
 assert(values[Terrain][CoreCalls]==0);
 phase=0;offset=1200;begin(0);
 {Owner a(Roads);CoreScope ignored;Wait ignoredWait;add(ClipChunks);}
 assert(values[Roads][CoreCalls]==0&&values[Roads][ClipChunks]==0);
 phase=1;offset=360+120;assert(angle()==0&&bin()==1);
 offset+=359;assert(angle()==359&&bin()==12);
 ++offset;assert(angle()==0&&bin()==13);
 phase=11;offset=1320;current.z=108;assert(gas()&&bin()==10);
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='producer-ledger-') as name:
    tmp = Path(name)
    (tmp/'probe.hpp').write_text(header)
    (tmp/'test.cpp').write_text(test)
    subprocess.run(['g++', '-std=c++17', '-O2', str(tmp/'test.cpp'), '-o', str(tmp/'test')], check=True)
    subprocess.run([str(tmp/'test')], check=True)
print('Producer ledger: deferred identity, view nesting, unknown copies, observer gates, nested waits, workload bins PASS')

if len(sys.argv) > 1:
    spec = importlib.util.spec_from_file_location('audit', root/'tools/tyrax2-producer-analyze.py')
    audit = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(audit)
    evidence, logfile = Path(sys.argv[1]), Path(sys.argv[2])
    rows, bins = audit.read(evidence/'producer-probe.csv'), audit.read(evidence/'producer-bins.csv')
    log = logfile.read_text(errors='replace')
    audit.analyze(rows, bins, log)
    cases = [('scope leak', 'errors', 1), ('vehicle leak', 'vehicles', 3),
             ('angle drift', 'angle', 1), ('clock leakage', 'coreTicks', 1),
             ('frame accounting', 'whole', 1), ('missing completion', None, None)]
    for name, key, value in cases:
        rs = copy.deepcopy(rows)
        if key:
            rs[0][key] = value
        try:
            audit.analyze(rs, bins, log if key else log.replace('PRODUCERDONE valid=1', ''))
        except AssertionError:
            pass
        else:
            raise AssertionError('Accepted malformed evidence: '+name)
    for key in ('clipChunks', 'snapshotBytes', 'deferredPush'):
        bs = copy.deepcopy(bins)
        bs[0][key] += 1
        try:
            audit.analyze(rows, bs, log)
        except AssertionError:
            pass
        else:
            raise AssertionError('Accepted malformed ledger: '+key)
    print('Complete evidence accepted; 9 malformed observer/pose/accounting/ownership/log inputs rejected')
