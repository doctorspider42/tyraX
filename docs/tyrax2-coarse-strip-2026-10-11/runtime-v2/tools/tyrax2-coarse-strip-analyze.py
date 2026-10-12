"""A30 source/delivery/residual gates; identical native shader both arms."""
from pathlib import Path
base=Path(__file__).with_name('tyrax2-clip-ring-analyze.py').read_text()
finish="exec(compile(source, str(Path(__file__)), 'exec'))"
assert base.rstrip().endswith(finish)
exec(compile(base.rstrip()[:-len(finish)],str(__file__),'exec'))
replace("plan['experiment']=='A29'","plan['experiment']=='A30' and plan['coarseStrip']")
replace("assert ('ClipConsumer::noteFallback(n);' in core)==plan.get('nativeFallback',False)",
        "assert 'ClipConsumer::noteFallback(n);' in core and not plan['nativeFallback']")
replace("assert 'stapip.core.setDiagnosticClipRing(ClipTrial::mode==7);' in game",
        """assert 'stapip.core.setDiagnosticClipRing(true);CoarseStrip::active=ClipTrial::mode==7;' in game
assert 'if(CoarseStrip::active!=(ClipTrial::mode==7))stapip.core.setDiagnosticClipRing(false);' in game
assert game.count('setDiagnosticClipRing(false)')==1""")
replace("assert 'stapip.core.setDiagnosticClipRing(ClipTrial::mode==7);' in physics",
        "assert 'stapip.core.setDiagnosticClipRing(true);CoarseStrip::active=ClipTrial::mode==7;' in physics")
replace("assert 'stapip.core.setDiagnosticClipRing(active==7);' in physics",
        "assert 'stapip.core.setDiagnosticClipRing(true);CoarseStrip::active=active==7;' in physics")
replace("assert 'if(native)buffer->fillNativeClipStrip(pkg,first,n);' in core", """assert 'if(native)buffer->fillNativeClipStrip(pkg,first,n);' in core
assert sha(root/'tyra/engine/inc/debug/coarse_strip.hpp')==plan['coarseSha256']
assert 'if(sourceEligible)CoarseStrip::expect(pkg);' in core
assert 'if(sourceEligible && CoarseStrip::active)' in core
assert 'buffer->coarseClipStrip=true;buffer->partialStripOrigin=true;' in core
assert 'CoarseStrip::input(*buffer);' in renderer
assert 'A30 invalid coarse shader/source admission' in renderer
assert 'CoarseStrip::reset();ClipConsumer::observe=frame==31;' in physics
assert 'A30 source/delivery contract' in physics
assert core.count('buffer->partialStripOrigin=sourceEligible;')==3
assert not plan['ringStateOnce']""")
replace('all(r[2:]==group[0][2:] for r in group)',
        'all(group[i][2:]==group[0][2:] for i in (0,2,3,4)) and group[1][2]>0 and group[1][3]>0')
replace('assert 0<oracle[1][2]<oracle[0][2]','assert oracle[1][2]>0')
replace("report['consumerInputEqual']=True;report['headerOracle']=checks;report['inputs']=inputs",
        "report['consumerInputEqual']=False;report['headerOracle']=checks;report['inputs']=inputs")
start=source.index("rings=[tuple(map(int,r))")
end=source.index(' return inputs,checks',start)
source=source[:start]+r'''def records(prefix,fields):
  pattern=prefix+r' phase=(\d+) arm=(\d+)'+''.join(' '+name+r'=(\d+)' for name in fields)
  rows=[tuple(map(int,r)) for r in re.findall(pattern,log)]
  assert len(rows)==count*5,prefix
  return rows
 src=records('COARSESOURCE',('packages','triangles','vertices','hash'))
 dst=records('COARSEDELIVERY',('jobs','triangles','vertices','hash'))
 rest=records('COARSERESIDUAL',('triangles','vertices','hash'))
 rings=records('RINGCONTRACT',('calls','triangles'))
 for phase in range(count):
  s,d,r,n=([row for row in rows if row[0]==phase] for rows in (src,dst,rest,rings))
  assert all([row[1] for row in rows]==list(range(5)) for rows in (s,d,r,n))
  assert all(row[2:]==s[0][2:] for row in s) and s[0][2]>0
  assert s[0][3]>0 and s[0][4]==s[0][3]*3
  assert d[1][2]>0 and d[1][3:]==s[0][3:]
  assert all(d[i][2:]==(0,0,0,2166136261) for i in (0,2,3,4))
  assert all(row[2:]==r[0][2:] for row in r) and r[0][3]==r[0][2]*3
  assert all(row[2]>0 and row[3]>=row[2] for row in n)
  assert all(n[i][2:]==n[0][2:] for i in (0,2,3,4))
  assert n[1][3]>=d[1][3]
''' +source[end:]
# Replace the A29 activation-specific negative controls with source admission
# controls; candidate canonical input is now qualified at two disjoint seams.
start=source.index("for bad in (text.replace('RINGCONTRACT")
end=source.index("report['residentWords']=resident",start)
source=source[:start]+r'''for name,bad in (
 ('missing-source',text.replace('COARSESOURCE phase=0 arm=1','MISSINGSOURCE phase=0 arm=1')),
 ('changed-source',re.sub(r'(COARSESOURCE phase=0 arm=1 packages=\d+ triangles=\d+ vertices=\d+ hash=)\d+',r'\g<1>0',text)),
 ('missing-delivery',text.replace('COARSEDELIVERY phase=0 arm=1','MISSINGDELIVERY phase=0 arm=1')),
 ('inactive-coarse',re.sub(r'(COARSEDELIVERY phase=0 arm=1 jobs=)\d+',r'\g<1>0',text)),
 ('coarse-control',re.sub(r'(COARSEDELIVERY phase=0 arm=0 jobs=)\d+',r'\g<1>1',text)),
 ('changed-delivery',re.sub(r'(COARSEDELIVERY phase=0 arm=1 jobs=\d+ triangles=\d+ vertices=\d+ hash=)\d+',r'\g<1>0',text)),
 ('omitted-delivery',re.sub(r'(COARSEDELIVERY phase=0 arm=1 jobs=\d+ triangles=)\d+',r'\g<1>0',text)),
 ('missing-residual',text.replace('COARSERESIDUAL phase=0 arm=1','MISSINGRESIDUAL phase=0 arm=1')),
 ('changed-residual',re.sub(r'(COARSERESIDUAL phase=0 arm=1 triangles=\d+ vertices=\d+ hash=)\d+',r'\g<1>0',text)),
 ('changed-baseline-input',re.sub(r'(CONSUMERINPUT phase=0 arm=2 calls=\d+ verts=\d+ hash=)\d+',r'\g<1>0',text)),
 ('missing-native',text.replace('RINGCONTRACT phase=0 arm=0','MISSINGRING phase=0 arm=0')),
 ('inactive-native',re.sub(r'(RINGCONTRACT phase=0 arm=0 calls=)\d+',r'\g<1>0',text))):
 try:cold(bad)
 except AssertionError:report['negativeControls'].append(name)
 else:raise AssertionError('A30 negative escaped: '+name)
report['ringContracts']=re.findall(r'RINGCONTRACT[^\r\n]+',text)
report['coarseContracts']={name:re.findall(name+r'[^\r\n]+',text) for name in
 ('COARSESOURCE','COARSEDELIVERY','COARSERESIDUAL')}
report['nativeFallback']=False
report['coarseSourceDeliveryEqual']=True
report['residualInputEqual']=True
report['baselinePostRouteInputEqual']=True
report['consumerInputEqual']=False
''' +source[end:]
replace('clip-ring-{platform}.json','coarse-strip-{platform}.json')
replace('Native clip strips with bounded VU output rings; stable terrain focus and quiet transport in both arms; net elapsed, not pure EE cycles',
        'Coarse EE partial-strip submission; identical qualified ring shader, quiet transport and stable focus both arms; source/delivery/residual gates; net elapsed, not pure EE cycles')
exec(compile(source,str(__file__),'exec'))
