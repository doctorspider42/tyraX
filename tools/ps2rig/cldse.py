#!/usr/bin/env python3
"""cldse.py <log> [skipcycles]: per (state, window index, arm) the median whole-frame
work with a 95% bootstrap CI - the summary every arm decision in the 2026-10-20
architecture audit was made with."""
import sys,re,collections,statistics,random
rows=collections.defaultdict(list); last=0; idx=collections.Counter()
for line in open(sys.argv[1],encoding='latin-1'):
    m=re.search(r'CLD win (.*)',line)
    if not m: continue
    kv=dict(re.findall(r'(\w+)=(\d+)',m.group(1))); f=int(kv['f'])
    if not ((f<=2400) if last==0 else (0<f-last<=1200)): continue
    last=f; key=(int(kv['cyc']),int(kv['st'])); w=idx[key]; idx[key]+=1
    if int(kv['cyc'])<1: continue
    rows[(int(kv['st']),w,int(kv['arm']))].append(int(kv['work'])/1000)
random.seed(1)
def ci(x):
    b=sorted(statistics.median(random.choices(x,k=len(x))) for _ in range(400)); return b[10],b[389]
for k in sorted(rows):
    x=rows[k]; lo,hi=ci(x)
    print(k,'med %.3f  95%%ci %.3f..%.3f  sd %.3f'%(statistics.median(x),lo,hi,statistics.pstdev(x)))
