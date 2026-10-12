import hashlib, json, struct, zipfile, zlib
from pathlib import Path
import zstandard

root=Path(__file__).parent/'burnout'
root.mkdir(exist_ok=True)
source=Path('C:/Users/pawel/OneDrive/Documents/PCSX2/sstates')
reports=[]
for path in sorted(source.glob('*75BECC18*')):
    if path.suffix!='.p2s' or path.stem.endswith('resume'):continue
    dest=root/path.stem
    dest.mkdir(exist_ok=True)
    report={'source':str(path),'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'entries':{}}
    raw=path.read_bytes()
    with zipfile.ZipFile(path) as archive:
        for info in archive.infolist():
            off=info.header_offset
            names,extra=struct.unpack_from('<HH',raw,off+26)
            start=off+30+names+extra
            packed=raw[start:start+info.compress_size]
            if info.compress_type==93:data=zstandard.ZstdDecompressor().decompress(packed,max_output_size=info.file_size)
            elif info.compress_type==0:data=packed
            else:data=archive.read(info)
            assert len(data)==info.file_size and zlib.crc32(data)==info.CRC
            report['entries'][info.filename]={'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}
            if info.filename in ('Screenshot.png','eeMemory.bin','eeHwRegs.bin','PCSX2 Internal Structures.dat','PCSX2 Savestate Version.id','vu1Memory.bin','vu1MicroMem.bin'):
                (dest/info.filename).write_bytes(data)
            if info.filename=='eeHwRegs.bin':
                report['dma']={name:{reg:hex(struct.unpack_from('<I',data,offset+delta)[0]) for reg,delta in [('CHCR',0),('MADR',16),('QWC',32),('TADR',48)]} for name,offset in [('VIF0',0x8000),('VIF1',0x9000),('GIF',0xa000)]}
    reports.append(report)
(root/'inventory.json').write_text(json.dumps(reports,indent=2)+'\n')
print(json.dumps([{k:v for k,v in r.items() if k!='entries'} for r in reports],indent=2))
