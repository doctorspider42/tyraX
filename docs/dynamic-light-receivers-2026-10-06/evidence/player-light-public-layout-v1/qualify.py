from pathlib import Path
import hashlib,json,struct,subprocess
root=Path(__file__).parent;repo=Path('F:/Projects/tyra-editor');tool='/home/spider/.cache/tyrax/native/toolchains/eff2c8918589264d49aa9a0f'
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
linux=lambda p:'/mnt/'+str(p.resolve())[0].lower()+'/'+str(p.resolve())[3:].replace('\\','/')
def elf(path):
 b=Path(path).read_bytes();assert b[:6]==b'\x7fELF\x01\x01';h=struct.unpack_from('<16sHHIIIIIHHHHHH',b);off,sz,num,names=h[6],h[11],h[12],h[13]
 raw=[struct.unpack_from('<IIIIIIIIII',b,off+i*sz)for i in range(num)];strings=b[raw[names][4]:raw[names][4]+raw[names][5]]
 def name(pool,index):return pool[index:pool.index(b'\0',index)].decode()
 sections=[dict(name=name(strings,s[0]),type=s[1],addr=s[3],offset=s[4],size=s[5],link=s[6],entsize=s[9])for s in raw]
 symbols={}
 for s in sections:
  if s['type']!=2:continue
  st=sections[s['link']];pool=b[st['offset']:st['offset']+st['size']]
  for p in range(s['offset'],s['offset']+s['size'],s['entsize']):
   n,v,z,i,o,index=struct.unpack_from('<IIIBBH',b,p)
   if n:symbols[name(pool,n)]=dict(value=v,size=z,section=index)
 return b,sections,symbols
def data(path,symbol):
 b,sections,symbols=elf(path);s=symbols[symbol];sec=sections[s['section']];offset=sec['offset']+s['value']-sec['addr'];return b[offset:offset+s['size']]
assert not(root/'proof.json').exists()
header='vendor/tyra/engine/inc/renderer/3d/pipeline/shared/bag/pipeline_info_bag.hpp';old=root/'old/renderer/3d/pipeline/shared/bag/pipeline_info_bag.hpp';old.parent.mkdir(parents=True,exist_ok=True);old.write_bytes(subprocess.check_output(['git','show','HEAD:'+header],cwd=repo))
fields=['model','shadingType','textureMappingType','transformationType','blendingEnabled','antiAliasingEnabled','zTestType','frustumCulling','fogDisabled','additiveBlendFix','subtractiveBlendFix','dynLightPick','dynLightSkipSlot','spotLit','dateLit','blssProxy']
records={}
for mode in ('old','current'):
 fs=fields+(['dynamicLightReceive']if mode=='current'else[])
 source=root/(mode+'.cpp');source.write_text('#include "renderer/3d/pipeline/shared/bag/pipeline_info_bag.hpp"\n#include <cstddef>\nextern "C" const unsigned layout[] = {'+', '.join(['sizeof(Tyra::PipelineInfoBag)','alignof(Tyra::PipelineInfoBag)']+['offsetof(Tyra::PipelineInfoBag, '+f+')'for f in fs])+'};\n'+('extern "C" int receiver_default(){Tyra::PipelineInfoBag b;return b.dynamicLightReceive?1:0;}\n'if mode=='current'else''),encoding='utf8')
 obj=root/(mode+'.o');dep=root/(mode+'.d');inc=repo/'vendor/tyra/engine/inc'
 args=['wsl','-d','Ubuntu','--',tool+'/ee/bin/mips64r5900el-ps2-elf-g++','-std=c++17','-D_EE','-G0','-O2','-Wall','-Wextra','-MMD','-MF',linux(dep),'-I'+linux(root/'old')if mode=='old'else'-I'+linux(inc),'-I'+linux(inc),'-I'+linux(inc/'renderer/3d/pipeline/shared/bag'),'-I'+tool+'/ps2sdk/ee/include','-I'+tool+'/ps2sdk/common/include','-c',linux(source),'-o',linux(obj)]
 r=subprocess.run(args,capture_output=True);(root/(mode+'-compile.stdout')).write_bytes(r.stdout);(root/(mode+'-compile.stderr')).write_bytes(r.stderr);assert r.returncode==0,r.stderr.decode()
 values=struct.unpack('<'+'I'*(2+len(fs)),data(obj,'layout'));records[mode]=dict(sizeof=values[0],alignof=values[1],offsets=dict(zip(fs,values[2:])),objectSha256=sha(obj),sourceSha256=sha(source),dependencySha256=sha(dep),compileCommand=args)
 dis=subprocess.check_output(['wsl','-d','Ubuntu','--',tool+'/ee/bin/mips64r5900el-ps2-elf-objdump','-dr',linux(obj)]);(root/(mode+'-disassembly.txt')).write_bytes(dis)
baseline=repo/'docs/tyrax2-main11-batch-2026-10-06/reviews-and-tools/night-main11-batch-native-root-review-v2/proof.json';prior=json.loads(baseline.read_text(encoding='utf8'))['actualVUPrograms'];build=root.parent/'player-light-public-native-players-root-v1/build-record.json';record=json.loads(build.read_text(encoding='utf8'));actual=Path(record['elf']);sym=Path(record['symbol']);assert sha(actual)==record['elfSha256'];assert sha(sym)==record['symbolSha256'];b,sections,symbols=elf(actual);sb,ss,syms=elf(sym)
images={}
for name,p in prior.items():
 endname=name.replace('CodeStart','CodeEnd');start=syms[name]['value'];end=syms[endname]['value'];section=next(s for s in sections if s['addr']<=start and end<=s['addr']+s['size']);offset=section['offset']+start-section['addr'];image=b[offset:offset+end-start];digest=hashlib.sha256(image).hexdigest();images[name]=dict(address=start,end=end,bytes=len(image),roundedMicroInstructions=(len(image)+7)//8,sha256=digest,baselineSha256=p['sha256'],baselineBytes=p['bytes'],identical=len(image)==p['bytes']and digest==p['sha256'],section=section['name'],symbolAddressSource='actual linked .elf.sym symbol table; bytes extracted from actual stripped .elf')
assert len(images)==16
proof=dict(status='PASS_ACTUAL_R5900_LAYOUT_AND_16_LINKED_VU_IDENTITY'if all(x['identical']for x in images.values())else'DIFF_ACTUAL_R5900_LAYOUT_AND_LINKED_VU_COMPARISON',layout=records,oldHeaderSha256=sha(old),actualCurrentHeaderSha256=sha(repo/header),actualELFSha256=sha(actual),actualSymbolSha256=sha(sym),buildRecordSha256=sha(build),baselineProofSha256=sha(baseline),actualVUPrograms=images,sizeofDelta=records['current']['sizeof']-records['old']['sizeof'],alignmentUnchanged=records['current']['alignof']==records['old']['alignof'],hardwarePerformanceQualified=False,publicBinaryAbiCompatibility=False)
(root/'proof.json').write_text(json.dumps(proof,indent=2)+'\n',encoding='utf8');print(proof['status']);print(records['old']['sizeof'],records['current']['sizeof'],records['current']['alignof'])
