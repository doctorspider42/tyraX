from pathlib import Path
import importlib.util,json,struct,hashlib,re
from PIL import Image
lab=Path('/mnt/f/Projects/tyrax2-lab-20261001');primary=lab/'pcsx2-control-routes/source-all/pcsx2/GS';reader=lab/'corona24-probe-runtime-v2/savestate-reader-pinned.py'
s=importlib.util.spec_from_file_location('r',reader);r=importlib.util.module_from_spec(s);s.loader.exec_module(r)
tables=(primary/'GSTables.cpp').read_text()
def table(name,rows,cols):
 body=re.search(r'\b'+name+r'\['+str(rows)+r'\]\['+str(cols)+r'\]\s*=\s*\{(.*?)\};',tables,re.S)[1]
 n=list(map(int,re.findall(r'\d+',body)));assert len(n)==rows*cols;return [n[i*cols:(i+1)*cols]for i in range(rows)]
blocks=table('_blockTable32',4,8);columns=table('columnTable32',8,8)
images=[];rows=[];pins={}
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
for arm in (0,1):
 folder=lab/f'corona-probe-v4-case1-arm{arm}-repeat3-20261005-v4-launch';state=folder/'state.p2s';m=r.zip_members(state.read_bytes());gs=m['GS.bin'];assert len(gs)==4194813 and struct.unpack_from('<I',gs)[0]==9
 # Preserve version9 layout observation; actual FRAME0 and SCISSOR0 are decoded.
 for off in (196,204,212):print('reg',off,hex(struct.unpack_from('<Q',gs,off)[0]))
 frame=struct.unpack_from('<Q',gs,204)[0];scissor=struct.unpack_from('<Q',gs,172)[0]
 print('frame',hex(frame),'scissor',hex(scissor))
 # Full fixed PSMCT32 draw allocation, supported by actual frame/source allocation.
 vram=gs[-84-4194304:-84];width=((scissor>>16)&0x7ff)+1;height=((scissor>>48)&0x7ff)+1;fbp=frame&0x1ff;bw=(frame>>16)&0x3f
 assert (width,height,fbp,bw,(frame>>24)&0x3f)==(448,448,0,7,0)
 raw=bytearray();seen=set()
 for y in range(height):
  for x in range(width):
   page=(x//64)+(y//32)*bw
   block=blocks[(y%32)//8][(x%64)//8]
   word=((fbp*8192+page*8192+block*256)//4)+columns[y%8][x%8]
   assert word not in seen;seen.add(word);raw.extend(vram[word*4:word*4+4])
 pixels=bytes(raw);images.append(pixels);im=Image.frombytes('RGBA',(width,height),pixels).convert('RGB');im.save(folder/'gs-draw-raster.png')
 pts=[(i%width,i//width)for i in range(width*height)if any(pixels[i*4:i*4+3])]
 assert pts,'draw raster must contain authored texture'
 box=(min(x for x,y in pts),min(y for x,y in pts),max(x for x,y in pts)+1,max(y for x,y in pts)+1)
 im.crop(box).resize((240,240),Image.NEAREST).save(folder/'gs-draw-corona-enlarged.png')
 rows.append(dict(arm=arm,nonblackPixels=len(pts),nonblackBounds=box,pixelSha256=hashlib.sha256(pixels).hexdigest()))
 pins[str(state)]=sha(state);pins[str(folder/'completed-probe-proof.json')]=sha(folder/'completed-probe-proof.json')
assert images[0]==images[1],'actual rendered GS framebuffer pixels differ'
for p in (reader,Path(__file__),primary/'GSTables.cpp',primary/'GSState.cpp',primary/'GSRegs.h',primary/'GSLocalMemory.h'):pins[str(p)]=sha(p)
q=dict(status='PASS_OBSERVED_PSMCT32_DRAW_FRAMEBUFFER_PIXELS_EQUAL',decodedGSVersion=9,rawLayoutBytes=425,tailBytes=84,width=448,height=448,base=0,bufferWidthPages=7,arms=rows,exactRGBAEqual=True,inputPins=pins,displayWindowRemainsBlack=True,performanceAccepted=False,limits=['Actual saved FRAME0 and SCISSOR0 bind the PSMCT32 draw allocation; this is one planar positive case, not full ordinary-scene coverage.','This reads completed drawing VRAM, not the Hybrid scanout or physical PS2 framebuffer.'])
(lab/'corona24-draw-raster-pair-v1.json').write_text(json.dumps(q,indent=2)+'\n');print(q['status'],rows)
