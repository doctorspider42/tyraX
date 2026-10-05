"""Offline strict single-capture qualification. Never opens a PINE socket."""
from pathlib import Path
import argparse,struct,json,hashlib,importlib.util,configparser
P=Path(__file__).parent;sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def need(x,s):
 if not x:raise ValueError(s)
def module(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
def symbols(path):
 b=path.read_bytes();need(b[:6]==b'\x7fELF\x01\x01','ELF32LE symbols');h=struct.unpack_from('<16sHHIIIIIHHHHHH',b);need(h[1:3]==(2,8)and h[11]==40,'MIPS executable sym')
 ss=[struct.unpack_from('<10I',b,h[6]+i*40)for i in range(h[12])];result={}
 for s in ss:
  if s[1]!=2:continue
  need(s[9]==16 and s[5]%16==0 and s[6]<len(ss),'symtab shape');st=ss[s[6]];names=b[st[4]:st[4]+st[5]]
  for i in range(s[5]//16):
   n,v,size,info,other,index=struct.unpack_from('<IIIBBH',b,s[4]+i*16)
   if not n:continue
   end=names.find(b'\0',n);need(end>=n,'symbol string bounds');name=names[n:end].decode('utf8')
   if name in('pool2ProbeReady','pool2ProbeHalt'):
    need(index!=0 and name not in result,'unique defined probe symbol');result[name]=dict(address=v,bytes=size,type=info&15)
 need(set(result)=={'pool2ProbeReady','pool2ProbeHalt'},'probe ready/halt symbols')
 need(result['pool2ProbeReady']['bytes']==4 and result['pool2ProbeReady']['type']==1,'ready object ABI')
 need(result['pool2ProbeHalt']['type']==2,'halt function ABI');return result
def main():
 a=argparse.ArgumentParser()
 for k in('state','elf','sym','capture-report','cfg','profile','source-manifest','native-proof','out'):a.add_argument('--'+k,type=Path,required=True)
 q=a.parse_args();need(not q.out.exists(),'NEW report required')
 rawcfg=q.cfg.read_text(encoding='ascii').split();need(len(rawcfg)==3 and all(x.isdecimal()for x in rawcfg),'three unsigned config words')
 arm,n,repeats=map(int,rawcfg);need(arm in(0,1)and n in(1,2,3,4)and repeats in(1,2,3),'probe config ranges')
 ini=configparser.ConfigParser(strict=True);ini.read(q.profile,encoding='utf-8-sig')
 mtvu=[v.strip().lower() for sec in ini for k,v in ini[sec].items()if k.lower()=='vuthread'];need(mtvu==['false']or mtvu==['0'],'exact pinned MTVU disabled')
 r=module('reader',P/'savestate-reader-pinned.py');d=module('decoder',P/'decode-vu1-output.py');sy=symbols(q.sym)
 elf,text=r.text_from_elf(q.elf);se,st=r.text_from_elf(q.sym);need(text==st and elf['textAddress']==se['textAddress'],'ELF/sym same text')
 c=json.loads(q.capture_report.read_text(encoding='utf-8-sig'))
 need(c['status']=='PASS_exact_tag_paused_state_decode_text_identity','source-bound completed capture report')
 need(c['pineStatusBefore']==c['pineStatusAfter']==1,'paused PINE bracket')
 need(c['helperSha256']==sha(P/'savestate-reader-pinned.py'),'exact capture helper bytes')
 raw=q.state.read_bytes();need(c['stateSha256']==hashlib.sha256(raw).hexdigest(),'capture state hash')
 need(c['elf']['sha256']==elf['sha256'],'same boot ELF hash')
 decoded=r.parse_bytes(raw,r.OFFSETS,q.elf,sy['pool2ProbeHalt']['address']);need(decoded['registers']['isDelaySlot']==0,'not a delay slot')
 members=r.zip_members(raw);ram=members['eeMemory.bin'];addr=sy['pool2ProbeReady']['address']&0x1fffffff
 need(addr+4<=len(ram)and struct.unpack_from('<I',ram,addr)[0]==0x504f4f32,'accepted completed ready marker in actual EE RAM')
 vu,micro=d.extract(q.state);bank=d.decode(vu,n,arm,repeats)
 native=json.loads(q.native_proof.read_text(encoding='utf-8-sig'))
 need(native['status'].startswith('PASS_')and not native.get('blockers'),'accepted independent native review')
 need(native['actualELFSha256']==sha(q.elf)and native['actualSymbolSha256']==sha(q.sym),'native reviewed exact ELF/symbol')
 need(native['sourceManifestSha256']==sha(q.source_manifest)and native['diagnosticOnly'],'native reviewed diagnostic source')
 manifest=json.loads(q.source_manifest.read_text(encoding='utf-8-sig'));need(manifest['frozen'] is True and len(manifest['files'])==501,'frozen501 corona correctness source authority')
 result=dict(status='PASS_OFFLINE_SINGLE_PAUSED_STATIC_PROBE_CAPTURE_ONLY',arm=arm,case=n,repeats=repeats,
  sourceManifestSha256=sha(q.source_manifest),nativeProofSha256=sha(q.native_proof),symbols=sy,registers=decoded['registers'],
  readyMarker='504f4f32',actualVuCountAndArmLayoutValidated=True,inputTopologyValidated=True,sourceFinalEpochValidated=True,actualFogLightUniformsValidated=True,actualCompletedOutputRouteValidated=True,finalEpoch=repeats,actualMicrocodeSha256=hashlib.sha256(micro).hexdigest(),
  vuMemorySha256=hashlib.sha256(vu).hexdigest(),finalPackageCounts=sorted(bank),actualOutputWitnesses={str(k):dict(marker=v['marker'],sprite=v['sprite'],fogUnique=sorted(set(v['fogValues'])),rgbaUnique=sorted(set(v['rgbaValues'])))for k,v in bank.items()},wholeArchiveMembersCRCValidated=True,
  configAndMTVUOffProfilePinned=True,guestTextUnchanged=True,elfSymTextEqual=True,
  files={str(p):sha(p)for p in(q.state,q.elf,q.sym,q.capture_report,q.cfg,q.profile,q.source_manifest,q.native_proof,Path(__file__))},
  limitations=['Latest source colors identify the final epoch bank; an old other bank is excluded for single-package cases.', 'Native proof is pinned external authority; native acceptance requires its independent review.','Caller must bind source/native/profile/config to the exact owned launch and protocol ready/frame records.','Final requested source epoch is validated from saved actual input colors alongside completed output routing; intermediate epochs require their own captures.','One capture is not paired packed-output equality, GS consumption, raster or performance acceptance.'])
 q.out.write_text(json.dumps(result,indent=2)+'\n',encoding='utf8');print(result['status'])
if __name__=='__main__':main()
