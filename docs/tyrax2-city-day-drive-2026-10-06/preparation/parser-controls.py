from pathlib import Path
import subprocess,sys,json,re,hashlib
r=Path(__file__).parent;emu=r.parent/'tyrax2-city-day-drive-emulator-v1/capture-final';out=r/'parser-controls';assert not out.exists();out.mkdir()
log=(emu/'runtime-emulator.log').read_text();artifact=emu/'bin-closeout-samples.txt';cmd=[sys.executable,str(r/'analyze-day-drive.py')]
def replace_field(text,label,field,value):
 pattern=r'(^.*LOG: '+label+r' .*?\b'+field+r'=)\S+'
 result,n=re.subn(pattern,lambda m:m.group(1)+value,text,count=1,flags=re.M);assert n==1;return result
variants={'valid':log,'capture_mask':replace_field(log,'DAYSELECT','captured','15'),'selected_wrong':replace_field(log,'DAYSELECT','chunk','4'),'pose_ticks':replace_field(log,'DAYPOSE','workTicks','0'),'not_parked':replace_field(log,'DAYPARK','parked','0'),'park_frame':replace_field(log,'DAYPARK','frame','800')}
lines=log.splitlines();line=next(l for l in lines if 'LOG: CLOSEOUTCONTEXT phase=1 'in l)
variants['park_camera']=log.replace(line,replace_field(line,'CLOSEOUTCONTEXT','positionX','00000000'))
variants['park_speed']=log.replace(line,replace_field(line,'CLOSEOUTCONTEXT','vehicleSpeed','41200000'))
variants['park_drift']=log.replace(line,replace_field(line,'CLOSEOUTCONTEXT','vehicleX','42c80000'))
variants['duplicate_selection']=log+'\n'+next(l for l in lines if 'LOG: DAYSELECT 'in l)+'\n'
results=[]
for name,text in variants.items():
 p=out/(name+'.log');p.write_text(text)
 run=subprocess.run(cmd+[str(p),'--artifact',str(artifact),'--out',str(out/(name+'.json')),'--route','vehicle'],capture_output=True)
 assert (run.returncode==0)==(name=='valid'),name
 results.append(dict(name=name,exitCode=run.returncode,expectAccepted=name=='valid'))
(out/'controls.json').write_text(json.dumps(dict(status='PASS_10_REPLAY_PARSER_CONTROLS',parserSha256=hashlib.sha256((r/'analyze-day-drive.py').read_bytes()).hexdigest(),referenceLogSha256=hashlib.sha256((emu/'runtime-emulator.log').read_bytes()).hexdigest(),results=results,measurements=False),indent=2)+'\n')
print('PASS_10_REPLAY_PARSER_CONTROLS')
