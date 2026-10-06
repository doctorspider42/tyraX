from pathlib import Path
lab=Path('F:/Projects/tyrax2-lab-20261001');parent=lab/'tyrax2-closeout-road-two-cars-light-buildings-80k-shadow-v1';r=lab/'tyrax2-closeout-road-two-cars-light-buildings-80k-shadow-batch-v1'
s=(parent/'analyze-shadow.py').read_text(encoding='utf8').replace('assert order==0 and kind==4','assert order in(0,1) and kind==4').replace('route\')==10','route\')==11').replace('BUILDINGRENDER|BLOBRENDER','BUILDINGRENDER|BLOBRENDER|BATCHCONTEXT|BATCHPHASE')
s=s.replace("assert any(moved.values()),'No actual cold endpoint movement established; route activation missing'","assert not any(moved.values()),'Stationary contrast moved vehicle'")
s=s.replace("assert moved[1],'Actual driving phase endpoint motion missing'",'')
s=s.replace("assert .05<fp(contextmap[1][1155],'vehicleZ')<6.0,'Short driving endpoint is missing or escaped near-car qualification domain'","assert abs(fp(contextmap[1][1155],'vehicleZ'))<.01")
needle='portalCounts=None';addition="""batchRows=rows(stdout,'BATCHCONTEXT');batchPhases=index(rows(stdout,'BATCHPHASE'),'phase',range(3));assert len(batchRows)==6
batchWitnesses={}
for phase in range(3):
 on=int((phase!=1)!=(order==1));q=batchPhases[phase]
 assert n(q,'enabled')==on and n(q,'order')==order and n(q,'retainedBatches')>0
 cold=index([r for r in batchRows if n(r,'phase')==phase],'offset',[750,1155]);batchWitnesses[phase]=cold
 for o,q in cold.items():
  assert n(q,'enabled')==on and n(q,'mappedOwners')==(50 if on else 0)and n(q,'retainedBatches')>0 and n(q,'valid')==1
  assert n(buildingWitnesses[phase][o],'acceptedBags')==(24 if on else 300)
  assert n(renderWitnesses[phase][o],'acceptedBags')==(37 if on else 313)
for o in (750,1155):
 for key in ('positionX','positionY','positionZ','lookX','lookY','lookZ','vehicleX','vehicleY','vehicleZ','vehicleSpeed','playerX','playerY','playerZ'):
  assert len({contextmap[p][o][key]for p in range(3)})==1,('Unmatched parked context',o,key)
 for key in ('x','y','z'):
  assert len({lightContexts[p][o][key]for p in range(3)})==1,('Unmatched point path',o,key)
"""
s=s.replace(needle,addition+needle).replace('PASS_COMPLETED_PRIVATE_NORMAL_RAVAGER_BLOB_80K_PROTOCOL','PASS_COMPLETED_PRIVATE_STATIONARY_STATIC_BATCH_CONTRAST_PROTOCOL').replace('blobColdWitnesses=blobWitnesses,','batchColdWitnesses=batchWitnesses,order=order,blobColdWitnesses=blobWitnesses,').replace('matchedAB=False','matchedAB=True').replace('Controller shortened to keep both models visible, changed and included in the price; no causal incremental subtraction. Full S geometry retained but bends not traversed.','All phases parked with unchanged normal physics; point time is phase-local frame offset/60, common to all arms. Comparison is net batching representation cost, not pure fixed bag price. Full S geometry retained but bends not traversed.')
(r/'analyze-batch.py').write_bytes(s.encode());(r/'README.md').write_bytes(b'''# Static batch representation contrast on the corrected shadow scene

Private same-ELF ON/OFF/ON and reverse stationary contrast. Normal startup builds and retains batch arrays once. OFF resets objectBatchOf routing to-1 and skips renderStaticBatches; ON restores the original eligible membership. No buffers are freed at switches, avoiding any new DMA drain or lifetime hazard. The50 facade source objects are immutable; cold membership rejects50/0 mismatch. Both orders use neutral input throughout, and the point controller repeats the identical frame-local offset/60 sine path with the same colour/range/work. All other renderer/light/shadow paths and assets are inherited, including the corrected public fallback glow texture. Camera/vehicle/light words must match exactly at corresponding sparse cold offsets.

Ordinary sampler measures five64-loop chunks plus128 raw loops per phase after800 warmup loops; no new Count reads or waits. Exact accepted input80187, all50 building refs, both car classes/light consumption, road,pine and54 blob remain mandatory. Expected accepted carrier counts are37 ON versus313 OFF, building24 versus300. Parser accepts slow cadence or synthetic flips and reports them; slower OFF is an outcome, not protocol failure. Interpretation is net representation cost including object transforms,state and packets, not a universal fixed packet price. Common private branch/observer footprint is unpriced. Images and root hardware qualification remain pending; material/light output parity is not inferred from equal vertex totals.
''');print('BATCH_PARSER_READY')
