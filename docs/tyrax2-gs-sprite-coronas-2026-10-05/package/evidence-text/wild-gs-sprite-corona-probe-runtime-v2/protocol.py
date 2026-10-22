import re
def need(x,s):
 if not x:raise ValueError(s)
def verify(text,arm,case,repeats,clip=0):
 need(clip==0 and 'POOL2PROBE_REJECT'not in text,'probe rejection/clip')
 count=96 if case==4 else 72
 package=75 if case==4 else 72
 config=re.findall(r'LOG: CORONAPROBE_CONFIG arm=(\d+) case=(\d+) vertices=(\d+) repeats=(\d+) package=(\d+) fog=1 light=1 oracle=0 depth=16 capability=1',text)
 need(config==[(str(arm),str(case),str(count),str(repeats),str(package))],'unique exact corona config')
 frames=re.findall(r'LOG: CORONAPROBE_FRAME frame=(\d+) arm=(\d+) case=(\d+) generation=(\d+) counters=0',text)
 need(frames==[(str(i),str(arm),str(case),str(i))for i in range(1,repeats+1)],'exact source epoch/frame rows')
 ready=re.findall(r'LOG: CORONAPROBE_READY arm=(\d+) case=(\d+) repeats=(\d+) completed=(\d+)',text)
 need(ready==[(str(arm),str(case),str(repeats),str(repeats))],'exact final READY')
 events=re.findall(r'LOG: (CORONAPROBE_CONFIG|CORONAPROBE_FRAME|CORONAPROBE_READY)\b',text)
 need(events==['CORONAPROBE_CONFIG']+['CORONAPROBE_FRAME']*repeats+['CORONAPROBE_READY'],'strict lifecycle order')
 return dict(status='PASS_EXACT_CORONA_SOURCE_EPOCH_PROTOCOL',arm=arm,case=case,repeats=repeats,count=count,package=package,countersDisabled=True,sourceEpochFreshnessRequiresSavedVUValidation=True)
