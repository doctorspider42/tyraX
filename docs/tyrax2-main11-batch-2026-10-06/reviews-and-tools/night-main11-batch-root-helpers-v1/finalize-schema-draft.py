from pathlib import Path
import json,hashlib,shutil
b=Path(__file__).parent.parent;h=Path(__file__).parent;c=b/'night-main11-batch-controls-v1';fixture=b/'night-main11-batch-physical-v1'
p=c/'batch_controls.py';s=p.read_text(encoding='utf8')
s=s.replace("for name in('fallbackGroups','demotedGroups','eligibilityRefused','dirtyDemotions'):need(c[name]<=c['sourceGroups'],'fallback/demotion source subset')", "for name in('fallbackGroups','demotedGroups'):need(c[name]<=c['sourceGroups'],'fallback/demotion group subset')\n  for name in('eligibilityRefused','dirtyDemotions'):need(c[name]<=c['sourceMembers'],'fallback/demotion member event bound')")
s=s.replace("need(c['dirtyDemotions']<=c['demotedGroups'],'dirty demotion subset');covered=set();admittedMembers=actualMembers=0;originals=grouped=reflectUses=0", "covered=set();admittedMembers=actualMembers=0;originals=grouped=reflectUses=carriers=oracleVertices=0")
s=s.replace("need(sum(n(mm[j],'count')for j in owned)==v['count'],'group vertex source denominator')", "if v['ready']:\n    need(sum(n(mm[j],'count')for j in owned)==v['count'],'ready copied group vertex source denominator');oracleVertices+=v['count']\n   else:need(v['count']==0,'unready group has no copied vertex denominator')")
s=s.replace("reflectUses+=m['reflectionUses']", "reflectUses+=m['reflectionUses'];carriers+=m['ready']")
s=s.replace("need(c['reflectionCarriers']<=c['sourceMembers'],'carrier source subset')", "need(c['reflectionCarriers']==carriers,'actual ready original reflection carrier sum');need(c['geometryCompared']==oracleVertices,'full-word oracle covers every ready copied group')")
p.write_text(s,encoding='utf8')
p=h/'freeze-completed.py';s=p.read_text(encoding='utf8').replace('sha=lambda f:', 'from runtime_guard import verify_native_binding\nsha=lambda f:')
anchor="build=path(native['build_log']);"
s=s.replace(anchor,"nativeProof=path(release['native']);verify_native_binding(load(nativeProof),native,sha(archive/'target-source-manifest.json'),sha(archive/'vehicle-playground.elf'),sha(archive/'vehicle-playground.elf.sym'),sha(nativeProof),release['nativeSha256']);authorityPins[str(nativeProof)]=sha(nativeProof)\n"+anchor)
s=s.replace("'corona_controls.py','source-controls", "'corona_controls.py','batch_controls.py','source-controls")
s=s.replace("for name in ('review','host'):", "for name in ('review','host','native'):")
p.write_text(s,encoding='utf8')
shutil.copy2(fixture/'tyra/engine/inc/debug/night_plan.hpp',c/'source-controls/night_plan.hpp')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
p=h/'host-authority.json';d=json.loads(p.read_text(encoding='utf8'));d['sourcePins']={k:sha(Path(k))for k in d['sourcePins']};d['draftProducerManifestSha256']=sha(fixture/'draft-source-manifest.json');d['pricingSourceManifestSha256']=None;d['draftUnreleased']=True;p.write_text(json.dumps(d,indent=2)+'\n',encoding='utf8')
print('DRAFT_SCHEMA_UPDATED_NO_RUNTIME_AUTHORITY')
