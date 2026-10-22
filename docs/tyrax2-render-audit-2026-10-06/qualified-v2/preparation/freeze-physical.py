from pathlib import Path
import re,json,hashlib
r=Path(__file__).parent.parent/'tyrax2-closeout-ps2-render-audit-v1';out=r/'completed-prefix';assert not out.exists();out.mkdir()
raw=(r/'ps2client.stdout').read_bytes();(out/'ps2client.stdout').write_bytes(raw);(out/'ps2client.stderr').write_bytes((r/'ps2client.stderr').read_bytes())
text=raw.decode('latin1',errors='strict').replace('\r','').replace('\n','')
patterns={'CLOSEOUTCONFIG':(1,r'chunkBytes=\d+'),'CLOSEOUTPHASE':(3,r'countReads=\d+'),'CLOSEOUTCONTEXT':(6,r'valid=\d+'),'DAYPOSE':(5,r'lookZ=[0-9a-fA-F]{8}'),'DAYSELECT':(1,r'valid=\d+'),'DAYPARK':(1,r'valid=\d+'),'CLOSEOUTDONE':(1,r'loops=5400')}
records=[]
for label,(count,end) in patterns.items():
 matches=list(re.finditer(r'LOG: '+label+r' (?:(?!LOG:).)*?'+end,text))
 assert len(matches)==count,(label,len(matches))
 for m in matches:assert len(m.group())<2000 and m.group().isascii();records.append((m.start(),m.group()))
records.sort();(out/'normalized.log').write_bytes(('\n'.join(t for _,t in records)+'\n').encode())
samples=(r/'bin/closeout-samples.txt').read_bytes();(out/'closeout-samples.txt').write_bytes(samples)
sha=lambda b:hashlib.sha256(b).hexdigest()
proof=dict(status='FROZEN_COMPLETED_PREFIX_NEWLINE_REASSEMBLY_ONLY',rawPrefixSha256=sha(raw),normalizedSha256=sha((out/'normalized.log').read_bytes()),samplesSha256=sha(samples),recordCounts={k:v[0]for k,v in patterns.items()},method='Remove transport CR/LF only; extract bounded known record families through their explicit terminal field; no field/value repair. Strict parser independently validates counts and cross-record identities.',terminalClientExitClaim=False)
(out/'freeze-proof.json').write_text(json.dumps(proof,indent=2)+'\n');print(proof['status'])
