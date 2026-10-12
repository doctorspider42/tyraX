from pathlib import Path
import importlib.util,struct,json,sys
root=Path('/mnt/f/Projects/tyrax2-submit-fix-20261007-v1/burnout/live')
helper=Path('/mnt/f/Projects/tyrax2-lab-20261001/pcsx2-savestate-controls/capture-state.py')
spec=importlib.util.spec_from_file_location('capture',helper);mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
slot=int(sys.argv[1]);file=root/f'profile/PCSX2/sstates/SLES-52584 (75BECC18).{slot:02d}.p2s'
assert not file.exists()
c=mod.Pine('/mnt/wslg/runtime-dir/pcsx2.sock.29418')
assert c.status()==1
c.save(slot);raw=mod.stable_read(file)
assert c.status()==1;c.close()
members=mod.zip_members(raw)
hw=members['eeHwRegs.bin'];regs={name:struct.unpack_from('<I',hw,off)[0] for name,off in [('CHCR',0x9000),('MADR',0x9010),('QWC',0x9020),('TADR',0x9030)]}
v=members['PCSX2 Savestate Version.id'];assert v[:4]==struct.pack('<I',0x9a590000) and b'2.9.93' in v
internal=members['PCSX2 Internal Structures.dat'];tag=b'cpuRegs'+bytes(25);assert internal.count(tag)==1
cpu=internal[internal.index(tag)+32:];pc=struct.unpack_from('<I',cpu,680)[0]
ram=members['eeMemory.bin'];(root/f'ram-{slot}.bin').write_bytes(ram)
(root/f'view-{slot}.png').write_bytes(members['Screenshot.png'])
report={'pc':hex(pc),'cpuRegisters':{str(i):hex(struct.unpack_from('<Q',cpu,16*i)[0]) for i in range(32)},'dma':{k:hex(v) for k,v in regs.items()},'stateSha256':mod.sha(raw)}
addr=regs['TADR']&0x1fffffff;stack=[];seen=set();tags=[]
for i in range(100000):
    assert addr%16==0 and addr+16<=len(ram) and (addr,tuple(stack)) not in seen,(hex(addr),len(tags))
    seen.add((addr,tuple(stack)))
    w=struct.unpack_from('<4I',ram,addr);n=w[0]&65535;kind=w[0]>>28&7;ref=w[1]&0x1fffffff
    tags.append({'address':hex(addr),'id':kind,'qwc':n,'ref':hex(ref),'tte':[hex(w[2]),hex(w[3])]})
    if kind in (0,7):break
    if kind in (1,2,5,6):
        assert addr+16+n*16<=len(ram)
        if kind==5:stack.append(addr+16+n*16);addr=ref
        elif kind==6:
            if not stack:break
            addr=stack.pop()
        elif kind==2:addr=ref
        else:addr+=16+n*16
    else:addr+=16
else:raise RuntimeError('chain bound')
report['tags']=tags
(root/f'capture-{slot}.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({'pc':report['pc'],'dma':report['dma'],'tags':len(tags),'tagTypes':{str(k):sum(t['id']==k for t in tags) for k in range(8)},'qwords':sum(t['qwc'] for t in tags)}))
