"""A31 owner source/residual/actual-moving activation and same-ELF travel gates."""
import importlib.util
from pathlib import Path
base=Path(__file__).with_name('tyrax2-coarse-strip-analyze.py').read_text()
finish="exec(compile(source,str(__file__),'exec'))"
assert base.rstrip().endswith(finish)
exec(compile(base.rstrip()[:-len(finish)],str(__file__),'exec'))
spec=importlib.util.spec_from_file_location('world_preflight',Path(__file__).with_name('tyrax2-world-runtime-preflight.py'))
preflight=importlib.util.module_from_spec(spec);spec.loader.exec_module(preflight)
ownerContracts=preflight.contracts
replace("plan['experiment']=='A30' and plan['coarseStrip']",
        "plan['experiment']=='A31-owner' and plan['coarseStrip'] and plan['worldMovingWitness'] and plan['worldCompiledPayload'] and plan['worldNetProducerCost']")
replace("assert 'stapip.core.setDiagnosticClipRing(true);CoarseStrip::active=ClipTrial::mode==7;' in game",
        "assert 'stapip.core.setDiagnosticClipRing(true);CoarseStrip::active=true;' in game")
replace("assert 'if(CoarseStrip::active!=(ClipTrial::mode==7))stapip.core.setDiagnosticClipRing(false);' in game",
        "assert 'if(!CoarseStrip::active)stapip.core.setDiagnosticClipRing(false);' in game")
replace("assert 'stapip.core.setDiagnosticClipRing(true);CoarseStrip::active=ClipTrial::mode==7;' in physics",
        "assert 'stapip.core.setDiagnosticClipRing(true);CoarseStrip::active=true;WorldRuntime::active=arm==1;' in physics")
replace("assert 'A30 source/delivery contract' in physics",
        "assert 'A31 residual coarse source/delivery contract' in physics")
start=source.index('def cold(log):')
end=source.index('inputs,checks=cold(text)',start)
source=source[:start]+'''def cold(log):
 data=ownerContracts(log)
 assert all(row[2]>0 for row in data['HEADERORACLE'])
 return data['CONSUMERINPUT'],data['HEADERORACLE']
'''+source[end:]
start=source.index("for name,bad in (\n ('missing-source'")
end=source.index("report['residentWords']=resident",start)
source=source[:start]+r'''world=ownerContracts(text)
for name in ('WORLDOWNER','CONSUMERINPUT','HEADERORACLE','COARSESOURCE',
             'COARSEDELIVERY','COARSERESIDUAL','RINGCONTRACT'):
 missing=text.replace(name+' phase=0 arm=1','MISSING'+name+' phase=0 arm=1')
 pattern=name+r' phase=0 arm=1[^\r\n]*=([0-9a-f]+)'
 match=re.search(pattern,text);assert match,name
 value=int(match[1],16 if name=='WORLDOWNER' else 10)^1
 changed=text[:match.start(1)]+(format(value,'08x') if name=='WORLDOWNER' else str(value))+text[match.end(1):]
 for label,bad in (('missing',missing),('changed',changed)):
  try:cold(bad)
  except AssertionError:report['negativeControls'].append(label+'-'+name)
  else:raise AssertionError('A31 owner negative escaped: '+name)
for label,bad in (
 ('inactive-owner',re.sub(r'(WORLDOWNER phase=0 arm=1 sources=\d+ draws=)\d+',r'\g<1>0',text)),
 ('owner-control',re.sub(r'(WORLDOWNER phase=0 arm=0 sources=\d+ draws=)\d+',r'\g<1>1',text)),
 ('zero-owner-triangles',re.sub(r'(WORLDOWNER phase=0 arm=1 sources=\d+ draws=\d+ triangles=)\d+',r'\g<1>0',text))):
 try:cold(bad)
 except AssertionError:report['negativeControls'].append(label)
 else:raise AssertionError('A31 owner activation negative escaped: '+label)
report['worldOwnerContracts']=world
report['nativeFallback']=False
report['worldSourceDeliveryEqual']=True
report['residualInputEqual']=True
report['residualCoarseSourceDeliveryEqual']=True
report['consumerInputEqual']=False
''' +source[end:]
replace('phases=validate(rows,text)',r'''phases=validate(rows,text)
assert 'WorldRuntime::active=ClipTrial::mode==7;prepareWorldOwners();' in game
assert 'if(epoch==worldSeenEpoch && worldSeenActive==WorldRuntime::active)return;' in game
assert 'if(!WorldRuntime::active && !WorldRuntime::cold)return false;' in renderer
assert 'else b->worldOwner.reset();' in game
assert 'TerrainFocus::checkpoint=true;WorldRuntime::cold=true;' in physics
assert 'TerrainFocus::checkpoint=false;WorldRuntime::cold=false;' in physics
assert 'WorldRuntime::ResidualScope worldResidual;' in core
assert core.index('WorldRuntime::ResidualScope worldResidual;')<core.index('qbufferRenderer.flushBuffers();',core.index('WorldRuntime::ResidualScope worldResidual;'))
assert 'worldPool{196608}' in (root/'tyra/engine/inc/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.hpp').read_text()
assert 'WorldPayload::decode(fragment,n+2' in renderer and 'A31 compiled payload differs' in renderer
assert 'worldTriangle(delivered,reinterpret_cast<const Vec4*>(payload[0])' in renderer
assert 'bag->worldOwner->submitted(key,worldLastSequence,worldLastCopied);' in renderer
assert 'WorldRuntime::Spans::waitFor(unsigned n){Tyra::Vif1Queue::waitFor(n);}' in renderer
assert 'WorldRuntime::Spans::collect(const void* p){return Tyra::Vif1Queue::collectRetiredImmutableSpan(p);}' in renderer
assert 'if(ClipTrial::observe){++WorldRuntime::trialDraws;' in renderer
assert 'prepareWorldGeometry(c.bag.get())' in (root/'game/src/gen/game_collision.gen.cpp').read_text()
assert 'prepareWorldGeometry(ch.bag.get())' in physics
def movingOwner(log):
 records=[tuple(map(int,r)) for r in re.findall(r'\bWORLDWITNESS phase=(\d+) mode=(\d+) frames=(\d+) draws=(\d+) qwords=(\d+)',log)]
 assert len(records)==count and [r[0] for r in records]==list(range(count))
 for record,phase in zip(records,phases):
  _,mode,frames,draws,qwords=record
  assert mode==phase['mode'] and frames==phase['witnessFrames']>0
  assert (draws>0 and qwords>=4*draws) if mode==7 else draws==qwords==0
 return records
moving=movingOwner(text)''')
replace("report['residentWords']=resident",r'''for label,bad in (
 ('missing-moving-owner',text.replace('WORLDWITNESS phase=1','MISSINGWORLDWITNESS phase=1')),
 ('inactive-moving-owner',re.sub(r'(WORLDWITNESS phase=1 mode=7 frames=\d+ draws=)\d+',r'\g<1>0',text)),
 ('moving-owner-control',re.sub(r'(WORLDWITNESS phase=0 mode=0 frames=\d+ draws=)\d+',r'\g<1>1',text)),
 ('empty-moving-body',re.sub(r'(WORLDWITNESS phase=1 mode=7 frames=\d+ draws=\d+ qwords=)\d+',r'\g<1>0',text))):
 try:movingOwner(bad)
 except AssertionError:report['negativeControls'].append(label)
 else:raise AssertionError('A31 moving negative escaped: '+label)
report['movingWorldOwners']=moving
report['performanceCandidate']=True
report['residentWords']=resident''')
replace('coarse-strip-{platform}.json','world-runtime-{platform}.json')
replace('Coarse EE partial-strip submission; identical qualified ring shader, quiet transport and stable focus both arms; source/delivery/residual gates; net elapsed, not pure EE cycles',
        'Producer-owned compiled partial-world VIF bodies; live heads and same coarse native shader both arms; full-source/residual/moving/raster gates; net elapsed, not pure EE cycles')
exec(compile(source,str(__file__),'exec'))
