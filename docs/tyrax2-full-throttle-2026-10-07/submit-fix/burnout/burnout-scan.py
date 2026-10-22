from pathlib import Path
import struct,json
root=Path(__file__).parent/'burnout'
ram=(root/'SLES-52584 (75BECC18).06/eeMemory.bin').read_bytes()
hits=[]
for at in range(0x100000,0x400000,4):
    ins=struct.unpack_from('<I',ram,at)[0]
    if ins>>26!=15 or ins&65535 not in (0x1000,0x1001):continue
    reg=ins>>16&31; base=(ins&65535)<<16
    for after in range(at+4,at+44,4):
        op=struct.unpack_from('<I',ram,after)[0];rs=op>>21&31;rt=op>>16&31;imm=op&65535
        if rs!=reg:continue
        code=op>>26
        if code in (9,13) and rt==reg:
            base=(base+(imm-65536 if imm&32768 else imm))&0xffffffff if code==9 else base|imm
        if code in (35,43):
            addr=(base+(imm-65536 if imm&32768 else imm))&0xffffffff
            if 0x10009000<=addr<0x10009080:
                hits.append({'lui':hex(at),'pc':hex(after),'kind':'sw' if code==43 else 'lw','register':rt,'address':hex(addr)})
(root/'vif1-accesses.json').write_text(json.dumps(hits,indent=2)+'\n')
print(json.dumps(hits,indent=2))
