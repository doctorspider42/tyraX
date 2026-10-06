from pathlib import Path
import hashlib,json,os,re
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
def path(value):
 s=str(value)
 if os.name!='nt'and re.match(r'^[A-Za-z]:[\\/]',s):return Path('/mnt/'+s[0].lower()+'/'+s[3:].replace('\\','/'))
 if os.name=='nt'and re.match(r'^/mnt/[a-z]/',s):return Path(s[5].upper()+':/'+s[7:])
 return Path(s)
def load(p):return json.loads(Path(p).read_text(encoding='utf-8-sig'))
def sources(fixture):
 f=Path(fixture);return {p.relative_to(f).as_posix():sha(p)for d in('src','inc','objects')for p in(f/d).rglob('*')if p.is_file()}|{p.name:sha(p)for p in f.glob('*.tyra')}|{'Makefile':sha(f/'Makefile')}
MUTABLE={'livepad.bin','livedbg.bin','livedbg.cmd','livedbg.trace','livelink.bin','livelogic.bin','livetime.bin','livecheckpoint.bin','ps2link.run','log.txt'}
def assets(fixture):
 f=Path(fixture)/'bin';return {p.relative_to(f).as_posix():sha(p)for p in f.rglob('*')if p.is_file()and p.name not in MUTABLE and p.suffix not in('.elf','.sym','.log')}
def verify_review(reviewfile):
 review=load(reviewfile);assert review['status']=='PASS_ROOT_PUBLIC_RECEIVER_RUNTIME_INPUT_REVIEW'
 recordfile=path(review['buildRecord']);assert sha(recordfile)==review['buildRecordSha256'];r=load(recordfile);fixture=path(r['fixture'])
 assert r['status']=='PASS_NORMAL_NATIVE_EDITOR_CLI_BUILD_ONLY'and r['exitCode']==0
 assert sources(fixture)==r['sources']and assets(fixture)==r['assets']
 for name,digest in r['repoEngineSources'].items():assert sha(path(name))==digest,name
 assert sha(path(r['buildLog']))==r['buildLogSha256'];assert sha(path(r['editor']))==r['editorSha256']
 assert sha(path(r['elf']))==r['elfSha256']==review['actualELFSha256'];assert sha(path(r['symbol']))==r['symbolSha256']==review['actualSymbolSha256']
 assert review['actualCompiledSourceReviewPassed']and review['actualELFSymbolTextReviewPassed']
 return r,fixture
