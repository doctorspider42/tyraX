from pathlib import Path
import hashlib,json
lab=Path('F:/Projects/tyrax2-lab-20261001');r=lab/'tyrax2-closeout-road-two-cars-light-buildings-80k-shadow-batch-v1';p=r/'analyze-batch.py';s=p.read_text(encoding='utf8').replace("n(q,'retainedBatches')>0","n(q,'retainedBatches')==24");p.write_bytes(s.encode())
s=(lab/'test-batch-controls-v2.py').read_text(encoding='utf8').replace('parser-controls-v2','parser-controls-v3');needle=" trial('missing-batch'+str(order)";insert=""" trial('prebuild-snapshot'+str(order),s.replace('retainedBatches=24','retainedBatches=0'),baseart,False)
 trial('prebuild-owner-map'+str(order),s.replace('mappedOwners=50','mappedOwners=0'),baseart,False)
""";s=s.replace(needle,insert+needle).replace('PASS_TWENTY_STATIC_BATCH_PARSER_CONTROLS','PASS_TWENTY_FOUR_STATIC_BATCH_PARSER_CONTROLS').replace("print('PASS20')","print('PASS24')");(lab/'test-batch-controls-v3.py').write_bytes(s.encode())
source=r/'empty-car/src/terrain_game.cpp';s=source.read_text(encoding='utf8');b=s.index('  if (bootPhase < 2) {');depth=0;end=None
for i in range(s.index('{',b),len(s)):
 if s[i]=='{':depth+=1
 elif s[i]=='}':
  depth-=1
  if depth==0:end=i;break
switch=s.index('  const unsigned batchPhase=NightSampler::frame/1800;');assert switch>end
assert 'if(!BatchTrial::captured)'in s and 'staticBatches.size()!=24||initialOwners!=50'in s
proof={'status':'PASS_SOURCE_POSTBOOT_SNAPSHOT_ORDER','sourceSha256':hashlib.sha256(source.read_bytes()).hexdigest(),'bootPhaseBlockFirstLine':s[:b].count('\n')+1,'bootPhaseBlockLastLine':s[:end].count('\n')+1,'switchFirstLine':s[:switch].count('\n')+1,'explicitCapturedFlag':True,'oneTimeInitial24Batches50OwnersChecked':True,'runtimeProof':False};(r/'postboot-source-controls.json').write_bytes((json.dumps(proof,indent=2)+'\n').encode())
