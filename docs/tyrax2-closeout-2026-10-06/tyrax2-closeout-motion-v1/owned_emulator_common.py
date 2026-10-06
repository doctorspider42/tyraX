from pathlib import Path
import hashlib,json,os,re
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def path(p):
 s=str(p)
 if re.match(r'^[A-Za-z]:[\\/]',s):return Path('/mnt/'+s[0].lower()+'/'+s[3:].replace('\\','/'))
 return Path(s)
def load(p):return json.loads(Path(p).read_text(encoding='utf-8-sig'))
def review(p):
 d=load(p);assert d['status']=='ROOT_REVIEWED_CLOSEOUT_INPUTS' and d['rootReviewed'] is True
 assert d['noAddedWaitsReviewed'] is True and d['ordinarySamplerOnlyReviewed'] is True
 roles={x['role'] for x in d['inputs']};assert {'native-proof','source-review','elf','sample-config'}<=roles
 for x in d['inputs']:assert sha(path(x['path']))==x['sha256'],x['role']
 assert Path(d['fixture']).is_absolute();return d
def proc(pid):
 p=Path('/proc')/str(pid);args=[s.decode() for s in (p/'cmdline').read_bytes().split(b'\0') if s]
 return str((p/'exe').resolve()),args,(p/'stat').read_text().rpartition(') ')[2].split()[19]
def owned(d):
 exe,args,start=proc(d['pid']);assert exe==d['exe'] and sha(exe)==d['exeSha256']
 assert start==d['procStartTicks'],'PID identity changed'
 assert args[args.index('-datapath')+1]==d['profile'] and args[args.index('-elf')+1]==d['elf']
 assert sha(path(d['review']))==d['reviewSha256'];review(path(d['review']))
 return args
