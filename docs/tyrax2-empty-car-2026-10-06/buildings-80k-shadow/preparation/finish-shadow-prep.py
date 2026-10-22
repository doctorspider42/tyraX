from pathlib import Path
import hashlib,json,shutil
lab=Path('F:/Projects/tyrax2-lab-20261001');parent=lab/'tyrax2-closeout-road-two-cars-light-buildings-80k-v1';root=lab/'tyrax2-closeout-road-two-cars-light-buildings-80k-shadow-v1';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
s=(parent/'analyze-80k.py').read_text(encoding='utf8').replace('route\')==9','route\')==10').replace('MODELRENDER|BUILDINGRENDER','MODELRENDER|BUILDINGRENDER|BLOBRENDER').replace('80133','80187')
needle='modelWitnesses={}';extra="""blobRows=rows(stdout,'BLOBRENDER');assert len(blobRows)==6
blobWitnesses={}
for phase in range(3):
 cold=index([r for r in blobRows if n(r,'phase')==phase],'offset',[750,1155]);blobWitnesses[phase]=cold
 for r in cold.values():
  assert n(r,'owner')==1 and n(r,'acceptedBags')==1 and n(r,'acceptedVertices')==54 and n(r,'valid')==1
"""
s=s.replace(needle,extra+needle).replace("+n(buildingWitnesses[phase][n(r,'offset')],'acceptedBags')==","+n(buildingWitnesses[phase][n(r,'offset')],'acceptedBags')+n(blobWitnesses[phase][n(r,'offset')],'acceptedBags')==").replace("+n(buildingWitnesses[phase][n(r,'offset')],'acceptedVertices')==","+n(buildingWitnesses[phase][n(r,'offset')],'acceptedVertices')+n(blobWitnesses[phase][n(r,'offset')],'acceptedVertices')==")
(root/'analyze-shadow.py').write_bytes(s.encode())
assets=json.loads((parent/'bin-assets-source-pins.json').read_text());assert all(sha(root/'empty-car/bin'/p)==h for p,h in assets['files'].items());assets['status']='PARENT_80K_RUNTIME_ASSETS_RETAINED_INCLUDING_RAVAGER_NORMAL_BLOB_TEXTURE';assets['parentAssetsProofSha256']=sha(parent/'bin-assets-source-pins.json');assets['activeBlobTexture']='vehicles/veh-ravager000001-shadow.png';assert assets['activeBlobTexture'] in assets['files'];(root/'bin-assets-source-pins.json').write_bytes((json.dumps(assets,indent=2)+'\n').encode())
(root/'README.md').write_bytes(b'''# Normal Ravager blob shadow on the actual 80k scene

Private sibling of the qualified 80k scene. Only the driver Ravager's authored dynamic shadowMode is changed to 2; castShadow remains false and the global blob preference remains false. Pica and all fifty building owners remain unchanged. The normal generated setupBlobShadows/updateAndRenderBlobShadows functions were never disabled by the eight cosmetic FX stubs and are retained. Both cached and rebuilt normal submissions use a separate cold carrier56. Normal blob uses its public Ravager shadow texture,54 input vertices,TestOnly depth,precise/full clipping, and no dynamic light picking. Cold checks require the actual accepted blob, not merely registration.

Ordinary parked/short-drive/postdrive5400-loop sampler and controller are inherited unchanged except route identity10. Observer counts only at750/1155 per phase, with no additional clocks or waits. Parent road/cars/pine/buildings input target80133 plus accepted54 yields80187 if runtime validates; accepted input is not emitted or visible triangle count. This is a one-arm workload qualification; it is not a causal54-vertex cost subtraction or generic60FPS promise. The normal ground-surface samples and shadow cache are timed as actual added workload. Residual common observer footprint remains unpriced. No devices are run by this preparation agent.
''')
print('PARSER_ASSETS_READY')
