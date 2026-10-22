"""Compile the actual generated paint block with both GeoPart field templates."""
import argparse
import re
import subprocess
import tempfile
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('repo', type=Path)
parser.add_argument('--baseline', type=Path, required=True)
args = parser.parse_args()
repo = args.repo.resolve()
templates = (repo/'src/templates.cpp').read_text()
body = (repo/'src/game_templates.inc').read_text()

def fields(text):
    return re.findall(r'    short envPaintKey\[6\].*?(?=    // The additive twin)', text, re.S)

def block(text):
    start = text.index('    const int paint = {{VEHICLE_PAINT_FOR}};')
    end = text.index('    renderObjectBag(objectIndex, part.envBag.get());', start)
    return text[start:end].replace('{{VEHICLE_PAINT_FOR}}', 'paintEnabled').replace('{{VEHICLE_PAINT_GATE}}', 'gate')

current = fields(templates)
old = fields((args.baseline/'templates.cpp').read_text())
assert len(current) == len(old) == 2
assert current[0] == current[1], 'Orbit/FPP paint declarations diverged'
paint = block(body)
reset = re.search(r'      part.envPaintValid = false;\s+part.paintMapSrc = nullptr;', body)[0]

support = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
using u32 = unsigned;
namespace Tyra {
struct Vec4 { float x=0,y=0,z=0,w=0;
  Vec4()=default; Vec4(float a,float b,float c,float d):x(a),y(b),z(c),w(d){} };
struct Color {float r=128,g=128,b=128,a=128;};
}
template<class T> struct BagArray : std::vector<T> {
  unsigned writes=0;
  T* span(unsigned first,unsigned n) {assert(first+n<=this->size()); ++writes;return this->data()+first;}
};
struct Tex { const Tyra::Vec4* coordinates=nullptr; const u32* contentVersion=nullptr;
  int textureFunction=0; bool paintVu=false; Tyra::Vec4 paintForward,paintHalf; };
struct Bag {unsigned count=0;};
struct Col {Tyra::Color* many=nullptr;};
struct Tier {BagArray<Tyra::Color> envColors;};
struct Part {
  std::unique_ptr<Tex> envTexBag{new Tex};
  std::unique_ptr<Bag> envBag{new Bag};
  std::unique_ptr<Col> envColorBag{new Col};
  std::vector<Tier> lods{1}; BagArray<Tyra::Color> envColors;
  unsigned shownLod=0;
FIELDS
};
#define TYRA_LOG(...) ((void)0)
struct Core {bool capable=true;Tex* tex=nullptr;
  bool supportsPaintVu(Bag*)const {assert(tex->textureFunction==3);return capable;}
};
struct Pipe {Core core;} stapip;
struct {bool matrixMode=false;} og;
void fold(Tyra::Vec4&){}
void draw(Part& part,Tyra::Vec4 envFwd,bool paintEnabled,bool gate) {
BODY
}
void rebuild(Part& part) {
RESET
}
int main() {
  Part part; stapip.core.tex=part.envTexBag.get();
  Tyra::Vec4 normals[2][16]; unsigned stamp=1, serial=0, held=0;
  Tyra::Vec4 acceptedF,acceptedH;
  for(unsigned i=0;i<6000;++i) {
    const unsigned source=(i/113)%2, n=8+(i/79)%9, lod=(i/61)%2;
    const bool repaint=i%173==0;
    if(i%97==0) ++stamp;
    for(unsigned k=0;k<16;++k) {
      const float t=float(k+stamp)*.17F;
      normals[source][k]={std::cos(t),std::sin(t),0,0};
    }
    auto& dst=lod?part.lods[0].envColors:part.envColors;
    dst.resize(n);part.shownLod=lod;
    part.envColorBag->many=dst.data();part.envBag->count=n;
    part.envTexBag->coordinates=normals[source];part.envTexBag->contentVersion=&stamp;
    if(repaint) {std::fill(dst.begin(),dst.end(),Tyra::Color{});rebuild(part);}
    const float t=float(i/7)*.007F;
    Tyra::Vec4 f(std::sin(t),0,std::cos(t),0);
    const bool capable=(i%6==1||i%6==3||i%6==5), gate=i%5!=0;
    stapip.core.capable=capable;
    short previous[6];std::memcpy(previous,part.envPaintKey,sizeof previous);
    const bool wasValid=part.envPaintValid;
    auto oldSource=part.envPaintSource;auto oldCount=part.envPaintCount;
    auto oldStamp=part.envPaintVersion;auto oldLod=part.envPaintLod;
    const unsigned writes=dst.writes;
    draw(part,f,true,gate);
    const bool mandatory=!wasValid||oldSource!=normals[source]||oldCount!=n||oldStamp!=stamp||oldLod!=lod;
    Tyra::Vec4 h(.35F-f.x,.85F-f.y,.35F-f.z,0);
    float len=std::sqrt(h.x*h.x+h.y*h.y+h.z*h.z);h.x/=len;h.y/=len;h.z/=len;
    const float values[]={f.x,f.y,f.z,h.x,h.y,h.z};int delta=0;
    for(unsigned k=0;k<6;++k) {
      float v=values[k]*128;int key=int(v+(v>=0?.5F:-.5F));
      delta=std::max(delta,std::abs(int(previous[k])-key));
    }
    const bool accepted=mandatory||(delta>=4&&gate);
    if(accepted){acceptedF=f;acceptedH=h;++serial;}
    assert(!std::memcmp(&acceptedF,&part.envTexBag->paintForward,sizeof f));
    assert(!std::memcmp(&acceptedH,&part.envTexBag->paintHalf,sizeof h));
    assert(part.envPaintValid&&part.envTexBag->paintVu==capable);
    if(capable) assert(writes==dst.writes);
    else {
      held+=!accepted&&writes!=dst.writes;
      assert(part.envPaintEeReady);
      for(unsigned k=0;k<n;++k) {
        const auto& a=normals[source][k];const auto& c=dst[k];
        float d=a.x*acceptedF.x+a.y*acceptedF.y+a.z*acceptedF.z;
        float rgb=128*(.30F+.70F*(1-std::fabs(d)));
        float s=std::max(0.0F,a.x*acceptedH.x+a.y*acceptedH.y+a.z*acceptedH.z);
        s*=s;s*=s;s*=s;float alpha=std::min(255.0F,1+220*s);
        assert(std::fabs(c.r-rgb)<.001F&&c.r==c.g&&c.r==c.b&&std::fabs(c.a-alpha)<.001F);
      }
    }
    if(i%101==0) {part.envTexBag->paintVu=true;draw(part,f,false,gate);assert(!part.envTexBag->paintVu);}
  }
  assert(held>0&&serial>0);
  std::printf("6000 actual-block transitions passed: accepted=%u heldRefresh=%u\n",serial,held);
}
'''

with tempfile.TemporaryDirectory() as tmp:
    root=Path(tmp)
    def preprocess(text):
        path=root/'tokens.cpp';path.write_text(text)
        result=subprocess.run(['g++','-E','-P','-x','c++','-DTYRA_VU1_EXP_PAINT=0',str(path)],check=True,capture_output=True,text=True).stdout
        return re.findall(r'\w+|[^\s]',result)
    assert preprocess(paint)==preprocess(block((args.baseline/'game_templates.inc').read_text()))
    for i in range(2): assert preprocess(current[i])==preprocess(old[i])
    print('Default-OFF paint tokens and both GeoPart declarations unchanged',flush=True)
    source=support.replace('FIELDS',current[0]).replace('BODY',paint).replace('RESET',reset)
    variants={
        'actual':source,
        'bad-map-stamp':source.replace('|| part.paintMapVersion != normalStamp',''),
        'bad-fallback-basis':source.replace('fwdL = part.envTexBag->paintForward;',''),
        'bad-readiness':source.replace('part.envPaintEeReady = false;','part.envPaintEeReady = true;'),
    }
    for name,data in variants.items():
        path=root/(name+'.cpp');path.write_text(data);exe=root/name
        subprocess.run(['g++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-pie','-no-pie','-DTYRA_VU1_EXP_PAINT=1',str(path),'-o',str(exe)],check=True)
        result=subprocess.run([str(exe)],capture_output=True,text=True)
        if name=='actual':
            assert result.returncode==0,result.stderr
            print(result.stdout,flush=True)
        else:
            assert result.returncode!=0 and 'Assertion' in result.stderr,(name,result)
            print(name+' rejected by assertion',flush=True)
