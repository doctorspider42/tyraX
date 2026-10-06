from pathlib import Path
import json,re,subprocess,hashlib
lab=Path('F:/Projects/tyrax2-lab-20261001');root=lab/'tyrax2-closeout-road-two-cars-light-buildings-80k-shadow-v1';out=root/'parser-controls-v2';assert not out.exists();out.mkdir()
source=lab/'tyrax2-closeout-ps2-two-cars-light-buildings-80k-v2/stdout.utf8.log';artifact=lab/'tyrax2-closeout-ps2-two-cars-light-buildings-80k-v2/bin/closeout-samples.txt';s=source.read_text(encoding='utf8').replace('route=9','route=10');lines=[]
for line in s.splitlines():
 if line.startswith('LOG: EMPTYRENDER '):
  line=re.sub(r'acceptedBags=(\d+)',lambda m:'acceptedBags='+str(int(m[1])+1),line);line=re.sub(r'acceptedVertices=(\d+)',lambda m:'acceptedVertices='+str(int(m[1])+54),line)
  phase=re.search(r'phase=(\d+)',line)[1];offset=re.search(r'offset=(\d+)',line)[1];lines.append(f'LOG: BLOBRENDER phase={phase} offset={offset} owner=1 acceptedBags=1 acceptedVertices=54 valid=1')
 lines.append(line)
s='\n'.join(lines)+'\n'
cases={'positive':(s,True),'missing-blob':('\n'.join(l for l in s.splitlines()if not l.startswith('LOG: BLOBRENDER '))+'\n',False),'wrong-owner':(s.replace('owner=1 acceptedBags=1 acceptedVertices=54','owner=3 acceptedBags=1 acceptedVertices=54'),False),'no-accepted':(s.replace('owner=1 acceptedBags=1','owner=1 acceptedBags=0'),False),'wrong-input':(s.replace('acceptedVertices=54 valid=1','acceptedVertices=53 valid=1'),False),'invalid-blob':(s.replace('acceptedVertices=54 valid=1','acceptedVertices=54 valid=0'),False),'high-owner-loss':(s.replace('refs49=7','refs49=0').replace('refs49=5','refs49=0'),False),'partition-mismatch':(s.replace('acceptedVertices=80187','acceptedVertices=80186'),False)}
proof=[]
for name,(text,expect)in cases.items():
 p=out/(name+'.log');p.write_bytes(text.encode());dest=out/(name+'.json');r=subprocess.run(['python',str(root/'analyze-shadow.py'),str(p),'--artifact',str(artifact),'--out',str(dest)],capture_output=True);passed=r.returncode==0;assert passed==expect,(name,r.stderr.decode(errors='replace')[-500:]);proof.append({'name':name,'expectedAccepted':expect,'actualAccepted':passed,'inputSha256':hashlib.sha256(p.read_bytes()).hexdigest()})
(out/'proof.json').write_bytes((json.dumps({'status':'PASS_EIGHT_SHADOW_PARSER_CONTROLS','syntheticNotRuntime':True,'parentCompletedLogSha256':hashlib.sha256(source.read_bytes()).hexdigest(),'parentArtifactSha256':hashlib.sha256(artifact.read_bytes()).hexdigest(),'parserSha256':hashlib.sha256((root/'analyze-shadow.py').read_bytes()).hexdigest(),'cases':proof},indent=2)+'\n').encode());print('PASS8')

