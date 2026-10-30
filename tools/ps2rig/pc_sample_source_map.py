"""Map a verified function body to assembler source comments without -g."""
from pathlib import Path
import argparse,hashlib,json,re,struct

class Elf:
    def __init__(self,path):
        self.data=path.read_bytes();d=self.data;assert d[:6]==b'\x7fELF\x01\x01'
        offset=struct.unpack_from('<I',d,32)[0];size,count,names=struct.unpack_from('<HHH',d,46)
        self.sections=[struct.unpack_from('<10I',d,offset+i*size) for i in range(count)]
        s=self.sections[names];strings=d[s[4]:s[4]+s[5]]
        self.section_names=[strings[x[0]:].split(b'\0',1)[0].decode() for x in self.sections]
        self.symbols=[]
        for section in self.sections:
            if section[1]!=2:continue
            strings=self.payload(section[6]);self.symbols=[struct.unpack_from('<IIIBBH',d,section[4]+i*16) for i in range(section[5]//16)]
            self.symbol_names=[strings[s[0]:].split(b'\0',1)[0].decode() for s in self.symbols];break
    def payload(self,index):
        s=self.sections[index];return self.data[s[4]:s[4]+s[5]]
    def executable(self):
        return {self.section_names[i]:self.payload(i) for i,s in enumerate(self.sections) if s[2]&4}
    def function(self,name):
        i=self.symbol_names.index(name);s=self.symbols[i];offset,size,index=s[1],s[2],s[5]
        result=bytearray(self.payload(index)[offset:offset+size]);relocations=[]
        for section in self.sections:
            if section[7]!=index or section[1] not in [4,9]:continue
            stride=12 if section[1]==4 else 8
            for j in range(section[5]//stride):
                where,info=struct.unpack_from('<II',self.data,section[4]+j*stride)
                if not offset<=where<offset+size:continue
                at=where-offset;kind=info&255;symbol=self.symbol_names[info>>8]
                assert kind in [2,4,5,6],kind
                word=struct.unpack_from('<I',result,at)[0]
                mask=0 if kind==2 else 0xfc000000 if kind==4 else 0xffff0000
                struct.pack_into('<I',result,at,word&mask);relocations.append((at,kind,symbol))
        return offset,size,bytes(result),relocations

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--original',type=Path,required=True);p.add_argument('--listed-object',type=Path,required=True)
    p.add_argument('--unlisted-object',type=Path,required=True,help='Same verbose assembly assembled without listing/debug options')
    p.add_argument('--assembly',type=Path,required=True);p.add_argument('--listing',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--function',default='_ZN18Vehicle_playground11TerrainGame28updateObjectPhysicsCandidateEv');a=p.parse_args()
    original=Elf(a.original);unlisted=Elf(a.unlisted_object)
    assert original.executable()==unlisted.executable(),'Verbose assembly does not reproduce original executable sections'
    old=original.function(a.function);listed=Elf(a.listed_object).function(a.function)
    assert old[1]==listed[1],'Function size differs; reject relative source map'
    old_reloc={x[0]:x[1:] for x in old[3]};listed_reloc={x[0]:x[1:] for x in listed[3]}
    changed=[i for i in range(0,old[1],4) if old[2][i:i+4]!=listed[2][i:i+4] or old_reloc.get(i)!=listed_reloc.get(i)]
    excluded={i for offset in changed for i in range(max(0,offset-128),min(old[1],offset+132),4)}
    assert len(excluded)<old[1]//40,'Too much listing-induced code movement; reject source map'
    contexts={};section='.text';previous=section;stack=[];file=None;line=None
    for i,text in enumerate(a.assembly.read_text(encoding='utf8').splitlines(),1):
        t=text.strip()
        if t=='.text':previous,section=section,'.text'
        elif t.startswith('.section'):previous,section=section,t.split()[1].split(',')[0]
        elif t.startswith('.pushsection'):stack.append(section);section=t.split()[1].split(',')[0]
        elif t=='.popsection':section=stack.pop()
        elif t=='.previous':section,previous=previous,section
        m=re.match(r'\s*# (.+):(\d+):',text)
        if m:file,line=m[1],int(m[2])
        contexts[i]=(section,file,line)
    mapping={}
    for text in a.listing.read_text(encoding='utf8').splitlines():
        m=re.match(r'^\s*(\d+)\s+([0-9a-fA-F]+)\s+([0-9A-Fa-f]{8})(?:\s|$)',text)
        if not m:continue
        index,address=int(m[1]),int(m[2],16);section,file,line=contexts[index]
        if section=='.text' and listed[0]<=address<listed[0]+listed[1] and address-listed[0] not in excluded and file:
            mapping[str(address-listed[0])]=dict(file=file,line=line)
    assert len(mapping)>1000,'Insufficient mapped instructions'
    result=dict(function=a.function,size=old[1],unchanged_instruction_spans_only=True,original_executable_sections_reproduced=len(original.executable()),
        excluded_instruction_offsets=sorted(excluded),listing_changed_words=len(changed),original_object_sha256=hashlib.sha256(a.original.read_bytes()).hexdigest(),
        listed_object_sha256=hashlib.sha256(a.listed_object.read_bytes()).hexdigest(),mapped_instructions=len(mapping),lines=mapping,
        scope='Only unchanged spans of this function body are mapped. The unlisted verbose assembly must reproduce EVERY original executable section byte-for-byte. Within the listed function, relocation payloads are masked; changed instructions/relocation positions and128bytes on either side are excluded. Neither analysis object is deployed. Source comments identify direct locations, not a full inline-call stack. Unmapped PCs must remain explicit.')
    a.output.write_text(json.dumps(result,indent=2),encoding='utf8');print('Verified mapped instructions:',len(mapping))

if __name__=='__main__':main()
