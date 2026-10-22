"""Sanitize the actual private road repacker and reject a corrupted output."""
import subprocess
import sys
import tempfile
from pathlib import Path

source=(Path(sys.argv[1])/'game/src/gen/game_vehicles.gen.cpp').read_text()
start=source.index('  if(scene==0) {\n    MotionProbe::roadPackBefore=')
end=source.index('  if (any) procFinishChunks();',start)
body=source[start:end]
prefix=r'''
#include <algorithm>
#include <cassert>
#include <cstring>
#include <random>
#include <vector>
#define TYRA_ASSERT(c,m) assert(c)
namespace Tyra {
struct Vec4 {float x,y,z,w;};
struct Color {float r,g,b,a;Color(float R,float G,float B,float A):r(R),g(G),b(B),a(A){}};
}
namespace MotionProbe { unsigned mode,roadPackBefore,roadPackAfter,roadPackVerified,roadPackRun;
unsigned variant(){return mode;} }
struct Chunk {int owner=-3,stripRun=75;bool roadBlend=false,roadEdge=false;
std::vector<Tyra::Vec4> vertices,sts;std::vector<Tyra::Color> colors;};
struct {struct {unsigned capacity=90;unsigned getMaxVertCountByParams(bool,bool,bool){return capacity;}}core;}stapip;
void run(std::vector<Chunk>& procChunks) {int scene=0;bool roadIdxDirty=false;
'''
suffix=r'''
assert(roadIdxDirty);
}
int main(){
std::mt19937 random(7801);
for(unsigned trial=0;trial<2000;++trial){
std::vector<Chunk> chunks(1);auto& c=chunks[0];
const unsigned n=3*(1+random()%600);
for(unsigned i=0;i<n;++i){
Tyra::Vec4 v{float(i),float(random()%31),float(random()%17),1};
if(i && random()%7==0)v=c.vertices.back();
Tyra::Vec4 uv{float(i%13),float(i)/4,1,0};
if(trial%2 && i>=75 && i%75<2){v=c.vertices[i-2];uv=c.sts[i-2];}
c.vertices.push_back(v);c.sts.push_back(uv);c.colors.emplace_back(128,128,128,128);
}
MotionProbe::mode=0;run(chunks);assert(c.vertices.size()==n&&c.stripRun==75);
MotionProbe::mode=6;stapip.core.capacity=trial%2?90:150;run(chunks);
assert(c.stripRun==int(stapip.core.capacity)&&c.vertices.size()%3==0);
}
}
'''
with tempfile.TemporaryDirectory(prefix='road-repack-') as name:
    root=Path(name)
    for label,code in [('good',body),('corrupt',body.replace('      size_t a=0,b=0;','      v[0].x+=10000;\n      size_t a=0,b=0;'))]:
        cpp=root/(label+'.cpp');exe=root/label
        cpp.write_text(prefix+code+suffix)
        subprocess.run(['g++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(cpp),'-o',str(exe)],check=True)
        result=subprocess.run([str(exe)],capture_output=True,text=True)
        assert (result.returncode==0)==(label=='good'),result.stderr
print('PASS: 2000 random strip arrays, 90/150 capacities, controls unchanged, tails/joins, ASan/UBSan; corrupted position rejected')
