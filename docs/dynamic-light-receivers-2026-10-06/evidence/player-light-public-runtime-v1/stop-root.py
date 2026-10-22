"""ROOT ONLY exact owned emulator stop; never removes live artifacts."""
from pathlib import Path
import argparse,json,os,signal
from runtime_common import sha,load
p=argparse.ArgumentParser();p.add_argument('--launch',type=Path,required=True);a=p.parse_args();d=load(a.launch);proc=Path('/proc')/str(d['pid']);args=[x.decode()for x in(proc/'cmdline').read_bytes().split(b'\0')if x];assert(proc/'exe').resolve()==Path(d['exe'])and sha(Path(d['exe']))==d['exeSha256'];assert args[args.index('-datapath')+1]==d['profile']and args[args.index('-elf')+1]==d['elf'];out=a.launch.parent/'owned-stop.json';assert not out.exists();out.write_text(json.dumps(dict(pid=d['pid'],args=args,reason='root requested owned public runtime stop',liveArtifactsDeleted=False),indent=2)+'\n',encoding='utf8');os.kill(d['pid'],signal.SIGTERM);print('OWNED_EMULATOR_STOP_REQUESTED; Xvfb retained')
