"""Lossless physical wrapper normalization; no device/process or live input mutation."""
from pathlib import Path
import argparse,json,hashlib,re
p=argparse.ArgumentParser();p.add_argument('raw',type=Path);p.add_argument('normalized',type=Path);p.add_argument('--proof',type=Path,required=True);a=p.parse_args();assert not a.normalized.exists()and not a.proof.exists(),'preserve old outputs';raw=a.raw.read_bytes();text=raw.decode('latin1');assert text.encode('latin1')==raw
protocol=[]
for line in raw.splitlines():
 match=re.search(rb'LOG: NIGHT[A-Z]+ ',line)
 if match:
  item=line[match.start():];assert all(c<128 for c in item),'nonASCII authoritative NIGHT protocol';protocol.append(item)
encoded=text.encode('utf8');normalized=[]
for line in encoded.splitlines():
 match=re.search(rb'LOG: NIGHT[A-Z]+ ',line)
 if match:normalized.append(line[match.start():])
assert normalized==protocol,'ASCII protocol bytes changed';assert encoded.decode('utf8').encode('latin1')==raw,'roundtrip failed';a.normalized.write_bytes(encoded);sha=lambda b:hashlib.sha256(b).hexdigest();r=dict(status='PASS_LOSSLESS_LATIN1_ASCII_NIGHT_PROTOCOL_NORMALIZATION',rawPath=str(a.raw),normalizedPath=str(a.normalized),rawSha256=sha(raw),normalizedSha256=sha(encoded),rawBytes=len(raw),normalizedBytes=len(encoded),authoritativeASCIIProtocolRows=len(protocol),authoritativeASCIIProtocolSha256=sha(b'\n'.join(protocol)),losslessLatin1Roundtrip=True,originalRawChanged=False);a.proof.write_text(json.dumps(r,indent=2)+'\n',encoding='utf8');print(r['status'])
