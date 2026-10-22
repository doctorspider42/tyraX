"""Symbolize qualified A37 callback PCs against exact native debug ELF (.text equal)."""
import argparse,collections,csv,hashlib,json,re,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('fixture',type=Path);p.add_argument('--physical',action='store_true');p.add_argument('--tool-prefix',required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();root=a.fixture;platform='physical' if a.physical else 'emulator'
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
report=json.loads((root/f'pc-moving-{platform}.json').read_text());assert report['valid']
folder=root/('physical/bin' if a.physical else 'game/bin');elf=folder/'vehicle-playground.elf';sym=root/'vehicle-playground.elf.sym'
assert sha(elf)==report['elfSha256'];assert not a.output.exists()
def tool(name,*args):return subprocess.check_output([a.tool_prefix+name,*map(str,args)],text=True)
with tempfile.TemporaryDirectory() as tmp:
 text=[]
 for index,f in enumerate((elf,sym)):
  path=Path(tmp)/f'text{index}.bin';tool('objcopy','--dump-section',f'.text={path}',f);text.append(path.read_bytes())
 assert text[0]==text[1], 'Debug and deployed executable text differs'
 textSha=hashlib.sha256(text[0]).hexdigest()
sections=tool('objdump','-h',sym)
match=re.search(r'^\s*\d+\s+\.text\s+([0-9a-f]+)\s+([0-9a-f]+)',sections,re.M);assert match
size,begin=map(lambda s:int(s,16),match.groups());end=begin+size
sources={}
for rel,digest in report['samplePins'].items():
 path=root/rel;assert sha(path)==digest
 if 'pc-drive-' in path.name:sources[int(re.search(r'-(\d+)\.csv$',path.name)[1])]=list(csv.DictReader(path.open()))
pcs=sorted({int(r['pc']) for rows in sources.values() for r in rows if begin<=int(r['pc'])<end})
addresses={}
for first in range(0,len(pcs),128):
 raw=tool('addr2line','-a','-f','-i','-C','-e',sym,*[hex(pc) for pc in pcs[first:first+128]])
 chunks=re.split(r'^(0x[0-9a-f]+)\s*$',raw,flags=re.M);assert not chunks[0].strip()
 for i in range(1,len(chunks),2):
  lines=chunks[i+1].strip().splitlines();assert len(lines)%2==0
  addresses[int(chunks[i],16)]=[dict(function=lines[j],location=lines[j+1]) for j in range(0,len(lines),2)]
assert set(addresses)==set(pcs)
result=[]
for phase,rows in sorted(sources.items()):
 bins=collections.Counter();regions=collections.Counter();rawPC=collections.Counter()
 for row in rows:
  pc=int(row['pc']);stack=addresses.get(pc)
  if stack:key=stack[-1]['function'] if stack[-1]['function']!='??' else '<text-unresolved>'
  else:key='<kernel/raw-unmapped>' if pc>=0x80000000 else '<outside-text/raw-unmapped>'
  bins[key]+=1;regions[str(int(row['region'])&15)]+=1;rawPC[hex(pc)]+=1
 result.append(dict(phase=phase,periodUs=report['phases'][phase]['periodUs'],samples=len(rows),
  functions=[dict(function=f,samples=n,share=n/len(rows)) for f,n in bins.most_common()],regions=regions,rawPC=rawPC))
output=dict(valid=True,qualificationSha256=sha(root/f'pc-moving-{platform}.json'),elfSha256=sha(elf),
 debugElfSha256=sha(sym),textSha256=textSha,textRange=[begin,end],phases=result,inlineAttribution={hex(k):v for k,v in addresses.items()},
 scope='Sampled raw interrupted instruction residency; no address alias normalization, no exclusive cycle/caller/ceiling inference')
a.output.write_text(json.dumps(output,indent=2)+'\n')
for phase in result:
 print(phase['phase'],phase['samples'],[(r['function'],round(r['share']*100,2)) for r in phase['functions'][:12]])
