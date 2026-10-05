from pathlib import Path
import subprocess,json,hashlib,datetime
lab=Path('F:/Projects/tyrax2-lab-20261001')
out=lab/'claude-night-review-20261006';out.mkdir(exist_ok=True)
prompt=out/'prompt.txt';assert prompt.exists()
exe=Path('C:/Users/pawel/.local/bin/claude.exe')
args=[str(exe),'-p','--safe-mode','--no-session-persistence','--tools','Read,Glob,Grep','--allowedTools','Read,Glob,Grep','--permission-mode','plan','--output-format','stream-json','--verbose','--add-dir',str(lab)]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
record={'command':args,'cwd':'F:/Projects/tyra-editor','promptSha256':sha(prompt),'cliSha256':sha(exe),'startedUtc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'authorization':'User explicitly requested an independent Claude CLI code review after the three leads. Read-only built-in tools; no hardware, edits, commands or agent tools provided.'}
assert not (out/'stdout.jsonl').exists()
(out/'invocation.json').write_bytes((json.dumps(record,indent=2)+'\n').encode())
with (out/'stdout.jsonl').open('wb') as stdout,(out/'stderr.log').open('wb') as stderr:
 p=subprocess.Popen(args,cwd=record['cwd'],stdin=subprocess.PIPE,stdout=stdout,stderr=stderr)
 record['pid']=p.pid
 try:p.communicate(prompt.read_bytes(),timeout=900);record['exitCode']=p.returncode
 except subprocess.TimeoutExpired:p.kill();p.communicate();record['exitCode']=p.returncode;record['timeout']=True
record['completedUtc']=datetime.datetime.now(datetime.timezone.utc).isoformat()
record['stdoutSha256']=sha(out/'stdout.jsonl');record['stderrSha256']=sha(out/'stderr.log')
results=[];toolcalls=[]
for line in (out/'stdout.jsonl').read_text(encoding='utf-8',errors='replace').splitlines():
 try:d=json.loads(line)
 except json.JSONDecodeError:continue
 if d.get('type')=='result':results.append(d)
 for item in d.get('message',{}).get('content',[]) if isinstance(d.get('message',{}).get('content',[]),list) else []:
  if item.get('type')=='tool_use':toolcalls.append({'name':item.get('name'),'input':item.get('input')})
record['toolCalls']=toolcalls;record['resultCount']=len(results)
(out/'completion.json').write_bytes((json.dumps(record,indent=2)+'\n').encode())
if results:
 (out/'result.json').write_bytes((json.dumps(results[-1],indent=2)+'\n').encode())
 (out/'report.md').write_bytes((results[-1].get('result','')+'\n').encode())
print(json.dumps({'exitCode':record['exitCode'],'resultCount':len(results),'toolCalls':len(toolcalls),'reportPath':str(out/'report.md')},indent=2))
