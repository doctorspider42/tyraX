from pathlib import Path
import subprocess,re,json,hashlib
lab=Path('/mnt/f/Projects/tyrax2-lab-20261001');o=lab/'corona24-isolated-compiler-v3';assert not o.exists();o.mkdir()
base=Path('/home/spider/.cache/tyrax/native/toolchains/eff2c8918589264d49aa9a0f')
src=lab/'wild-gs-sprite-corona-tc-native-v23-v6/artifacts/stapip_cull_tc_vu1.o.vcl';t=src.read_text()
for a,b in [('sub.z   vf00, outputStq1, i','sub.z   color3, outputStq1, i'),('sub.z   vf00, vertex3, outputStq1','sub.z   color3, vertex3, outputStq1'),('sub.xy  vf00, outputStq3, vertex3','sub.xy  color3, outputStq3, vertex3')]:assert t.count(a)==1;t=t.replace(a,b)
p=o/'candidate.vcl';p.write_text(t);r=subprocess.run([str(base/'bin/vcl'),str(p)],stdout=subprocess.PIPE,stderr=subprocess.PIPE);assert r.returncode==0,r.stderr
lines=r.stdout.decode().splitlines();changes=[]
for i,s in enumerate(lines):
 if not re.search(r'\bfmand\b',s):continue
 assert re.match(r'\s*nop\s+fmand',s)
 m=re.match(r'\s*subi?\.([xyzw]+)\s+(VF[0-9]+),',lines[i-1]);assert m,lines[i-1]
 mask,reg=m.groups();assert reg!='VF00'
 lines[i]=re.sub(r'nop(?=\s+fmand)',f'abs.{mask} VF00, {reg}',s,count=1);changes.append(dict(line=i+1,mask=mask,source=reg))
assert len(changes)==6
vsm=o/'candidate.vsm';vsm.write_text('\n'.join(lines)+'\n')
r=subprocess.run([str(base/'dvp/bin/dvp-as'),str(vsm),'-o',str(o/'candidate.o')],stdout=subprocess.PIPE,stderr=subprocess.PIPE);assert r.returncode==0,r.stderr
nm=subprocess.check_output([str(base/'ee/bin/mips64r5900el-ps2-elf-nm'),'-S',str(o/'candidate.o')],text=True);(o/'symbols.txt').write_text(nm)
end=int(re.search(r'([0-9a-f]+) [A-Z] StaPipVU1Cull_TC_CodeEnd',nm)[1],16)
q=dict(status='PASS_ISOLATED_ASSEMBLY_ONLY',words=end//8,pairedUpperDependencies=changes,actualRuntimeAccepted=False,sourceSha256=hashlib.sha256(src.read_bytes()).hexdigest());(o/'proof.json').write_text(json.dumps(q,indent=2)+'\n');print(q)
r=subprocess.run(['g++','-O2',str(lab/'corona24-domain-proof-v1.cpp'),'-o',str(o/'domain')],capture_output=True);assert r.returncode==0,r.stderr
r=subprocess.run([str(o/'domain')],capture_output=True);assert r.returncode==0;rtext=r.stdout.decode();(o/'domain-proof.json').write_text(rtext);print(rtext)
