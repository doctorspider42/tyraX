import datetime,hashlib,json,pathlib,shutil
lab=pathlib.Path('F:/Projects/tyrax2-lab-20261001');src=lab/'player-light-public-ps2-players-root-v2';out=lab/'player-light-public-ps2-players-prefix-v1'
out.mkdir(exist_ok=False)
def sha(b):return hashlib.sha256(b).hexdigest()
started=datetime.datetime.now(datetime.timezone.utc).isoformat()
log=src/'ps2client.stdout';limit=log.stat().st_size
with log.open('rb') as f:data=f.read(limit)
assert len(data)==limit
(out/'stdout-active-prefix.log').write_bytes(data)
snapshot=datetime.datetime.now(datetime.timezone.utc).isoformat()
meta={'schema':1,'status':'BOUNDED_ACTIVE_RUN_PREFIX_NOT_TERMINAL','pid':51220,'source':str(log),'prefixByteLength':len(data),'prefixSha256':sha(data),'captureStartedUTC':started,'snapshotUTC':snapshot,'clientLeftServing':True,'terminalRunProven':False,'hardwarePerformanceQualified':False,'ELFSha256':'0516e0c4ec66f14b101e4c21f87dbf51eead36bb1dfc7aed37c38ec907327b16','rootReview':'fresh loadelf/Loaded startup, normal boot and exit captures; enter0/exitspot0/exitat-1,-74 route; qualitative correctness only','mutableNormalFiles':['log.txt','livedbg.cmd','frame.tga'],'copied':{},'metadataOnly':{}}
for name in ['owned-launch.json','boot.png','after-exit.png','capture-after-exit.stdout','ps2client.stderr']:
    b=(src/name).read_bytes();(out/name).write_bytes(b);meta['copied'][name]={'sha256':sha(b),'bytes':len(b)}
frame=lab/'player-light-public-runtime-v1/fixtures/vehicle-players/bin/frame.tga'
if frame.exists():
    b=frame.read_bytes();meta['metadataOnly'][str(frame)]={'sha256':sha(b),'bytes':len(b),'role':'mutable captured frame; reviewed PNG retained'}
(out/'prefix-proof.json').write_text(json.dumps(meta,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'status':meta['status'],'byteLength':len(data),'sha256':sha(data),'snapshotUTC':snapshot}))
