"""ROOT ONLY strict exact owned PID stop. Existing Xvfb and artifacts retained."""
from pathlib import Path
import argparse,os,signal,json,time
from owned_emulator_common import load,owned,sha
p=argparse.ArgumentParser();p.add_argument('--launch',type=Path,required=True);a=p.parse_args();assert os.name=='posix';d=load(a.launch);args=owned(d);out=a.launch.parent/'owned-stop.json';assert not out.exists()
os.kill(d['pid'],signal.SIGTERM)
out.write_text(json.dumps({'status':'OWNED_CLOSEOUT_STOP_REQUESTED','pid':d['pid'],'procStartTicks':d['procStartTicks'],'args':args,'launchSha256':sha(a.launch),'requestedUnix':time.time(),'terminalExitConfirmed':False,'xvfbStopped':False,'artifactsDeleted':False},indent=2)+'\n');print('OWNED_CLOSEOUT_STOP_REQUESTED; no terminal-exit claim; Xvfb retained')
