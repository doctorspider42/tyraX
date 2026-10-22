"""Seal final-VIF bootstrap and equivalent-output emulator/physical protocols."""
import argparse
import hashlib
import json
import re
from pathlib import Path


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source(folder):
    binding=json.loads((folder/'native-binding.json').read_text())
    assert sha(folder/'game/bin/vehicle-playground.elf')==binding['elfSha256']
    for relative,digest in binding['files'].items():
        assert sha(folder/relative)==digest,relative
    return dict(path=str(folder),bindingSha256=sha(folder/'native-binding.json'),
                elfSha256=binding['elfSha256'],nativeSourceFiles=len(binding['files']),
                engineArchiveSha256=binding['engineArchiveSha256'])


def physical(folder,elf):
    deploy=json.loads((folder/'editor-deploy.json').read_text())
    assert deploy['elfSha256']==elf
    for relative,digest in json.loads((folder/'launch-inputs.json').read_text()).items():
        assert sha(folder/'bin'/relative)==digest,relative


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('out','fixture','bootstrap','emulator_compiled','ps2_compiled'):
        p.add_argument(name,type=Path)
    a=p.parse_args()
    record=dict(date='2026-10-20',productionPromoted=False,editorExporterComplete=False,
                fixture=source(a.fixture),bootstrap=source(a.bootstrap),platforms={},compiled={},
                limits=['Controlled fixed-step daytime drive in the inherited renderer/residency harness; not full-current-production, night or variable-dt acceptance.',
                        'On restores raw geometry and final VIF; this is not an isolated final-stage A/B.',
                        'Camera/clip/fog state and TEX0/CLUT/material heads remain live.',
                        'Common resident tables/loading footprint is not priced against a pristine executable.',
                        'Inputs are target bootstrap output with distinct pinned emulator/PS2 colour bytes, not a production host exporter.',
                        'Non-pacing elapsed time includes scheduling/waits; it is not exclusive EE time.'])
    manifest=json.loads((a.fixture/'world-cell-price-manifest.json').read_text())
    assert manifest['finalVifOffline']
    record['fixture']['manifestSha256']=sha(a.fixture/'world-cell-price-manifest.json')
    for platform,compiled in [('emulator',a.emulator_compiled),('ps2',a.ps2_compiled)]:
        report=json.loads((compiled/'report.json').read_text())
        assert report['valid'] and report['exactNativeVariants']==1370
        assert sha(compiled/'offline-terrain.bin')==report['outputSha256']==manifest['vifBinaryByPlatform'][platform]
        assert sha(compiled/'metadata.json')==report['metadataSha256']
        bootstrapAttempt=a.bootstrap if platform=='emulator' else a.bootstrap/'physical-v1'
        bootstrapBin=bootstrapAttempt/'game/bin' if platform=='emulator' else bootstrapAttempt/'bin'
        bootstrapLog=a.bootstrap/'emulator/emulator.log' if platform=='emulator' else bootstrapAttempt/'console.log'
        assert sha(bootstrapBin/'offline-vif-source.bin')==report['sourceSha256']
        assert re.findall(r'VIFDUMPDONE records=1370 bytes=47972016 native=1 gpuExecuted=0',bootstrapLog.read_text(errors='replace'))==['VIFDUMPDONE records=1370 bytes=47972016 native=1 gpuExecuted=0']
        if platform=='ps2':physical(bootstrapAttempt,record['bootstrap']['elfSha256'])
        record['compiled'][platform]=dict(report=report,reportSha256=sha(compiled/'report.json'),bootstrapLogSha256=sha(bootstrapLog))
        attempt=a.fixture if platform=='emulator' else a.fixture/'physical-v1'
        path=attempt/('world-cell-price-'+platform+'.json')
        result=json.loads(path.read_text())
        log=a.fixture/'emulator/emulator.log' if platform=='emulator' else attempt/'console.log'
        assert result['valid'] and result['finalVifOffline'] and result['finalVifSdkExact']
        assert result['poseEqual'] and result['deliveryValid'] and result['offlineTerrainEndpointPixelsExact']
        assert result['offlineTerrainCaptureWorkEqual'] and not result.get('qualityTrial')
        assert result['elfSha256']==record['fixture']['elfSha256'] and sha(log)==result['logSha256']
        if platform=='ps2':physical(attempt,record['fixture']['elfSha256'])
        record['platforms'][platform]=dict(report=result,reportSha256=sha(path),logPath=str(log))
    assert not a.out.exists(),'Do not overwrite a sealed report'
    a.out.write_text(json.dumps(record,indent=2)+'\n')
    print(a.out)


if __name__=='__main__':main()
