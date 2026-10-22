from pathlib import Path
p=Path(__file__).parent
s=(p.parent/'tyrax2-closeout-motion-v1/analyze-quiet-motion.py').read_text()
s=s.replace('(?=LOG: CLOSEOUT)','(?=LOG: (?:CLOSEOUT|DAY))')
s=s.replace('middle=(phase==1)!=(order==1);players=1;', 'middle=(phase==1)!=(order==1);players=0;')
s=s.replace("all(n(r,k)==1 for k in ('night','paused','hourZero'))", "n(r,'night')==0 and all(n(r,k)==1 for k in ('paused','hourZero'))")
s=s.replace("'PASS_COMPLETED_QUIET_ONE_ARM_MOTION_PROTOCOL'", "'PASS_COMPLETED_DAY_DRIVE_REPRESENTATIVE_PARK_PROTOCOL'")
marker='a.out.write_text(json.dumps(result,indent=2)'
pos=s.index(marker)
extra='''import struct
def f(r,k):return struct.unpack('!f',bytes.fromhex(r[k]))[0]
poses=index(rows(stdout,'DAYPOSE'),'chunk',range(5));selections=rows(stdout,'DAYSELECT');parks=rows(stdout,'DAYPARK');assert len(selections)==len(parks)==1
selection=selections[0];park=parks[0];selected=n(selection,'chunk')
assert n(selection,'captured')==31 and n(selection,'valid')==1 and 0<=selected<5
assert n(park,'frame')==1700 and n(park,'selected')==selected and n(park,'parked')==n(park,'valid')==1
driveChunks=index([r for r in chunks if n(r,'phase')==0],'i',range(5))
for i,r in poses.items():
 assert n(r,'sampleFrame')==832+i*64
 assert n(r,'workTicks')==n(driveChunks[i],'elapsed')-n(driveChunks[i],'pacing')
 for k in ('carX','carY','carZ','yaw','pitch','roll','camX','camY','camZ','lookX','lookY','lookZ'):assert math.isfinite(f(r,k))
assert selected==max(range(5),key=lambda i:n(poses[i],'workTicks'))
assert moved[0], 'actual driving did not move car'
parkedPositions=[]
for phase in (1,2):
 for r in contextmap[phase].values():
  for cold,pose in [('positionX','camX'),('positionY','camY'),('positionZ','camZ'),('lookX','lookX'),('lookY','lookY'),('lookZ','lookZ')]:assert r[cold]==poses[selected][pose], 'parked camera differs from selected drive midpoint'
  driftXZ=math.hypot(f(r,'vehicleX')-f(poses[selected],'carX'),f(r,'vehicleZ')-f(poses[selected],'carZ'))
  assert driftXZ<0.3 and abs(f(r,'vehicleSpeed'))<0.1, 'car did not remain parked near selected point'
  parkedPositions.append(dict(phase=phase,offset=n(r,'offset'),x=f(r,'vehicleX'),y=f(r,'vehicleY'),z=f(r,'vehicleZ'),speed=f(r,'vehicleSpeed'),driftXZ=driftXZ))
result.update(selectedChunk=selected,representativeDrivePose=poses[selected],parkedPositions=parkedPositions,perChunkNonPacingMs={p:[(n(c,'elapsed')-n(c,'pacing'))/64/294912 for c in sorted([r for r in chunks if n(r,'phase')==p],key=lambda r:n(r,'i'))]for p in range(3)},matchedAB=False)
result['limitations']+=['Moving window changes its view; parked view is the selected midpoint, not a pixel-identical moving-frame A/B.','One-time relocation and speed clearing occur outside measured windows. Normal physics/logic continue.','New pose storage and fixed-camera selector overhead are unpriced.','hourZero is an inherited field name: in this fixture it verifies noon (12), not midnight.']
'''
s=s[:pos]+extra+s[pos:]
(p/'analyze-day-drive.py').write_bytes(s.encode())
