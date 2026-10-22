from pathlib import Path
import json,hashlib,re,importlib.util,subprocess,sys,argparse
ap=argparse.ArgumentParser();ap.add_argument('--out',type=Path,required=True);args=ap.parse_args()
b=Path('F:/Projects/tyrax2-lab-20261001');h=Path(__file__).parent;c=b/'night-main11-batch-controls-v1';syn=args.out;syn.mkdir(exist_ok=False);sp=importlib.util.spec_from_file_location('n',c/'analyze-night.py');n=importlib.util.module_from_spec(sp);sp.loader.exec_module(n);checks=[]
def cold_rows(p,o,on,active=True):
 candidate=int(on and active);admit=int(active);out=[]
 agg=dict(invocations=1,sourceGroups=3,sourceMembers=11,groups=admit,members=4*admit,candidateGroups=candidate,candidateMembers=4*candidate,controlMainSubmits=11-4*candidate,candidateMainSubmits=candidate,reflectionCarriers=11,reflectionUses=11,fallbackGroups=3-admit,demotedGroups=0,eligibilityRefused=11-4*admit,dirtyDemotions=0,geometryCompared=396,geometryMismatches=0,duplicateMainSubmits=0,verticesCompared=396,colorBytesCompared=6336)
 def record(label,row):return 'LOG: '+label+' '+' '.join(k+'='+str(v)for k,v in dict(phase=p,offset=o,enabled=int(on),**row).items())
 out.append(record('NIGHTMAINBATCH',agg))
 for i,(first,amount)in enumerate(((0,4),(4,4),(8,3))):
  out.append(record('NIGHTMAINBATCHGROUP',dict(group=i,ready=1,admitted=admit if i==0 else 0,demoted=0,submitted=candidate if i==0 else 0,first=first,count=36*amount,members=amount,geometryHash=10+i,infoHash=9,lightKey=7,spotEnabled=0)))
 for j in range(11):
  g=0 if j<4 else 1 if j<8 else 2;grouped=candidate if g==0 else 0
  out.append(record('NIGHTMAINBATCHMEMBER',dict(ordinal=j,object=100+j,idLo=1000+j,idHi=2,group=g,ready=1,admitted=admit if g==0 else 0,mainOriginal=1-grouped,mainGrouped=grouped,reflectionUses=1,count=36,positionHash=100+j,colorHash=200+j,copiedPositionHash=100+j,copiedColorHash=200+j,infoHash=9,lightKey=7,spotEnabled=0)))
 return '\n'.join(out)+'\n'
for order in(0,1):
 e=b/f'night-ablation-ps2-object-route-order{order}-20261006-evidence';s=(e/'stdout.log').read_text(encoding='utf8').replace('kind=22','kind=29');art=(e/'night-ablation.log').read_text(encoding='utf8');flags=[int((p==1)!=(order==1))for p in range(3)]
 def prod(match):
  r=dict(x.split('=')for x in match[0].split()[2:]);
  for k in('calls','units1','units2','units3','units4','timedCalls','reads','ticks'):r[k]='0'
  return 'LOG: NIGHTPROD '+' '.join(k+'='+v for k,v in r.items())
 base=re.sub(r'LOG: NIGHTPROD [^\r\n]+',prod,s);s=base+'\n'+''.join(cold_rows(p,o,flags[p])for p in range(3)for o in(750,1155));r=n.analyze(s,art,'host',29,order,0,0);assert r['mainBatchColdControls']['candidateActivatedInCold'];checks.append('positive_active'+str(order))
 fallback=base+'\n'+''.join(cold_rows(p,o,flags[p],False)for p in range(3)for o in(750,1155));r=n.analyze(fallback,art,'host',29,order,0,0);assert r['mainBatchColdControls']['fallbackOnlyCompletedCapture']and not r['mainBatchColdControls']['candidatePricingActivationQualified'];checks.append('positive_completed_inactive_no_price'+str(order))
 unready=fallback.replace('geometryCompared=396','geometryCompared=0').replace('verticesCompared=396','verticesCompared=0').replace('colorBytesCompared=6336','colorBytesCompared=0').replace('demotedGroups=0','demotedGroups=3').replace('dirtyDemotions=0','dirtyDemotions=11')
 unready=re.sub(r'(LOG: NIGHTMAINBATCHGROUP [^\r\n]*?)ready=1 admitted=0 demoted=0',r'\1ready=0 admitted=0 demoted=1',unready)
 unready=re.sub(r'(LOG: NIGHTMAINBATCHGROUP [^\r\n]*? count=)\d+',r'\g<1>0',unready)
 assert n.analyze(unready,art,'host',29,order,0,0)['mainBatchColdControls']['fallbackOnlyCompletedCapture'];checks.append('positive_unready_original_carriers_member_demotion'+str(order))
 mutations=[('geometryMismatches=0','geometryMismatches=1'),('duplicateMainSubmits=0','duplicateMainSubmits=1'),('candidateMainSubmits=1','candidateMainSubmits=2'),('candidateMembers=4','candidateMembers=3'),('colorBytesCompared=6336','colorBytesCompared=6335'),('controlMainSubmits=11','controlMainSubmits=10'),('ordinal=0 object=100','ordinal=1 object=100'),('idLo=1001','idLo=1000'),('object=101','object=100'),('group=1 ready=1 admitted=0 demoted=0 submitted=0 first=4','group=1 ready=1 admitted=0 demoted=0 submitted=0 first=3'),('copiedPositionHash=100','copiedPositionHash=99'),('mainOriginal=0 mainGrouped=1','mainOriginal=1 mainGrouped=1'),('reflectionUses=11','reflectionUses=10'),('group=0 ready=1 admitted=1','group=0 ready=0 admitted=1'),('reflectionCarriers=11','reflectionCarriers=10'),('eligibilityRefused=7','eligibilityRefused=12')]
 for old,new in mutations:
  # Semantic candidate mutations must touch an actual enabled window.
  if old in('copiedPositionHash=100','mainOriginal=0 mainGrouped=1'):
   firstOn=min(p for p in range(3)if flags[p]);prefix=s.index(f'LOG: NIGHTMAINBATCH phase={firstOn} offset=750');bad=s[:prefix]+s[prefix:].replace(old,new,1)
  else:bad=s.replace(old,new,1)
  try:n.analyze(bad,art,'host',29,order,0,0)
  except ValueError:checks.append('rejected'+str(order)+old)
  else:raise AssertionError(old)
 for label in('NIGHTMAINBATCH','NIGHTMAINBATCHGROUP','NIGHTMAINBATCHMEMBER'):
  first=next(x for x in s.splitlines()if x.startswith('LOG: '+label+' '))
  for name,bad in [('missing',s.replace(first+'\n','',1)),('duplicate',s+first+'\n')]:
   try:n.analyze(bad,art,'host',29,order,0,0)
   except ValueError:checks.append('rejected'+str(order)+name+label)
   else:raise AssertionError(name+label)
 (syn/f'order{order}.stdout').write_bytes(s.encode());(syn/f'order{order}.artifact').write_bytes(art.encode());(syn/f'order{order}.fallback.stdout').write_bytes(fallback.encode())
 report=syn/f'order{order}.cli.json';r=subprocess.run([sys.executable,str(c/'analyze-night-cli.py'),'--stdout',str(syn/f'order{order}.stdout'),'--artifact',str(syn/f'order{order}.artifact'),'--environment','host','--kind','29','--order',str(order),'--joint','0','--restored','0','--report',str(report)],capture_output=True);assert r.returncode==0,r.stdout+r.stderr;checks.append('cli'+str(order))
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();proof=dict(status='PASS_KIND29_HOST_SYNTHETIC_AND_CLI_ONLY',checks=checks,nativeRuntimeAccepted=False,actualSourceOracleQualified=False,actualVUOrGSOutputQualified=False,completedInactiveCaptureRemainsUnpriced=True,syntheticFiles={p.name:sha(p)for p in syn.iterdir()if p.is_file()});(syn/'host-controls-proof.json').write_bytes((json.dumps(proof,indent=2)+'\n').encode());print(proof['status'],len(checks))
