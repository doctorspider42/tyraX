"""Private TC-only paired upper VF dependency; fail closed on changed schedule."""
from pathlib import Path
import re,sys
p=Path(sys.argv[1]);lines=p.read_text().splitlines();changed=[]
start=next(i for i,s in enumerate(lines)if s.strip()=="coronaQuadCheck:")
end=next(i for i,s in enumerate(lines)if s.strip()=="coronaDone:")
for i in range(start,end):
 if not re.search(r"\bfmand\b",lines[i]):continue
 assert re.match(r"\s*nop\s+fmand",lines[i]),lines[i]
 m=re.match(r"\s*(subi?|sub)\.([xyzw]+)\s+(VF[0-9]+),",lines[i-1]);assert m,lines[i-1]
 mask,reg=m.group(2,3);assert reg!="VF00",lines[i-1]
 lines[i]=re.sub(r"nop(?=\s+fmand)",f"abs.{mask} VF00, {reg}",lines[i],count=1)
 changed.append(dict(line=i+1,mask=mask,source=reg))
assert len(changed)==6,changed
p.write_text("\n".join(lines)+"\n")
print("PRIVATE_CORONA_PAIRED_ABS_INTERLOCK",changed)
