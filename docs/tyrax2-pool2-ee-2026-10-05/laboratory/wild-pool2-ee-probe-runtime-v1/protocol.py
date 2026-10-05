import re
def need(x,s):
 if not x:raise ValueError(s)
def verify(text,arm,vertices,repeats,clip=0):
 need('POOL2PROBE_REJECT' not in text,'probe rejection')
 config=re.findall(r'LOG: POOL2PROBE_CONFIG arm=(\d+) vertices=(\d+) members=(\d+) repeats=(\d+) package=75 alpha=128',text)
 need(config==[(str(arm),str(vertices),str(vertices//96),str(repeats))],'exact unique config')
 mode=re.findall(r'LOG: POOL2EEPROBE_CONFIG kind=(\d+) clip=(\d+) lazy=(\d+) oracle=(\d+)',text)
 need(mode==[('7',str(clip),str(arm),'0')],'kind7/lazy/cold oracle mode')
 frames=re.findall(r'LOG: POOL2PROBE_FRAME frame=(\d+) cold=(\d+) arm=(\d+) eligible=(\d+) applied=(\d+) compared=(\d+) mismatches=(\d+)',text)
 need(frames==[(str(i),str(int(i==1)),str(arm),'0','0','0','0')for i in range(1,repeats+1)],'exact disabled-counter frame rows')
 source=re.findall(r'LOG: POOL2EEPROBE_SOURCE clip=(\d+) ready=(\d+) total=(\d+) generation=(\d+) coldOracle=(\d+)',text)
 need(len(source)==repeats,'exact source row count')
 for epoch,row in enumerate(source,1):
  c,ready,total,g,oracle=map(int,row)
  need((c,total,g,oracle)==(clip,vertices,epoch,0),'source mode/total/epoch/oracle')
  need(0<=ready<=vertices and (ready==0 if not clip or not arm else ready>0),'source ready invariant')
 ready=re.findall(r'LOG: POOL2PROBE_READY arm=(\d+) vertices=(\d+) repeats=(\d+) completed=(\d+) warmBypass=0',text)
 need(ready==[(str(arm),str(vertices),str(repeats),str(repeats))],'exact completion row')
 # Preserve source-before-frame sequencing and READY only after final frame.
 events=re.findall(r'LOG: (POOL2EEPROBE_SOURCE|POOL2PROBE_FRAME|POOL2PROBE_READY)\b',text)
 need(events==['POOL2EEPROBE_SOURCE','POOL2PROBE_FRAME']*repeats+['POOL2PROBE_READY'],'source/frame/ready order')
 return dict(status='PASS_EXACT_LAZY_EPOCH_PROTOCOL',arm=arm,vertices=vertices,repeats=repeats,clip=clip,sourceRows=source,countersDisabled=True,coldOracleDisabled=True,outputFreshnessRequiresSavedVUValidation=True)
