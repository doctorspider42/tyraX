from pathlib import Path
import json,hashlib
lab=Path('F:/Projects/tyrax2-lab-20261001')
for name,rel in [('tyrax2-closeout-ps2-two-cars-light-buildings-80k-shadow-v1','ps2client.stdout'),('tyrax2-closeout-emulator-two-cars-light-buildings-80k-shadow-v1','runtime/emulator.log')]:
 r=lab/name;raw=(r/rel).read_bytes();assert b'Failed to load texture:'in raw and b'flare-glow.png'in raw and b'CLOSEOUTDONE'in raw
 d=dict(status='REJECTED_MISSING_DEFAULT_BLOB_RESOURCE',missingResource='hud/flare-glow.png',sourceLog=rel,sourceLogSha256=hashlib.sha256(raw).hexdigest(),completed5400Observed=True,hardwareTimingQualified=False,runtimeQualified=False,rendererRegressionClaim=False,activeVehicleMaskLoadedNotSufficientForAssetClosure=True,correction='Repeat unchanged source and ELF with a separate pinned runtime asset closure including public default flare-glow.png.')
 (r/'rejected-runtime.json').write_bytes((json.dumps(d,indent=2)+'\n').encode())
print('Rejected both missing-resource runs; no accepted performance claim')
