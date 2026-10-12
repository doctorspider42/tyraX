// Numerical oracle for the ACTUAL private VCL prepass, not a rewritten shader.
#include "vuasm.hpp"
#include "vusim.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>

static void put(std::vector<uint32_t>& m, int q, int lane, float f) {
  std::memcpy(&m[q*4+lane], &f, 4);
}
static float get(const std::vector<uint32_t>& m, int q, int lane) {
  float f; std::memcpy(&f, &m[q*4+lane], 4); return f;
}
int main(int argc, char** argv) {
  assert(argc==2);
  std::ifstream in(argv[1]); assert(in);
  std::string source((std::istreambuf_iterator<char>(in)), {});
  auto start=source.find("    ; Private paint prepass.");
  auto end=source.find("paintDone:\n", start);
  assert(start!=std::string::npos && end!=std::string::npos);
  std::string body="#vuprog PaintTest\n    xtop buffer\n"+source.substr(start,end-start)+"paintDone:\n#endvuprog\n";
  vuir::Program p; std::string error;
  assert(vuasm::parseText(body,"actual paint prepass",{},p,error));
  assert(p.code.size()>30);
  std::mt19937 rng(94305);
  std::uniform_real_distribution<float> d(-1,1);
  auto unit=[&]() {
    std::array<float,3> v={d(rng),d(rng),d(rng)};
    float l=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
    for(auto& f:v)f/=l;
    return v;
  };
  vusim::Config cfg;cfg.top=32;
  float maxError=0; unsigned checked=0;
  for(int trial=0;trial<500;++trial) {
    const int n=trial%75+1, normal=34+n, color=normal+n;
    std::vector<uint32_t> mem(4096,0x12345678);
    auto f=unit(),h=unit();
    for(int k=0;k<3;++k){put(mem,12,k,f[k]);put(mem,13,k,h[k]);}
    mem[8*4+1]=2;
    mem[32*4+3]=n|((trial%64)<<10);
    std::vector<std::array<float,4>> want;
    for(int i=0;i<n;++i) {
      auto v=unit();for(int k=0;k<3;++k)put(mem,normal+i,k,v[k]);
      float df=v[0]*f[0]+v[1]*f[1]+v[2]*f[2];
      float rgb=128.0F*(.30F+.70F*(1.0F-std::abs(df)));
      float sp=std::max(v[0]*h[0]+v[1]*h[1]+v[2]*h[2],0.0F);
      sp*=sp;sp*=sp;sp*=sp;
      want.push_back({rgb,rgb,rgb,std::min(1.0F+220.0F*sp,255.0F)});
    }
    auto r=vusim::run(p,mem,cfg);assert(r.ok && !r.hitStepLimit && r.kicks.empty());
    for(int i=0;i<4096;++i) {
      if(i>=color*4 && i<(color+n)*4)continue;
      assert(r.mem[i]==mem[i]);
    }
    for(int i=0;i<n;++i)for(int k=0;k<4;++k) {
      float e=std::abs(get(r.mem,color+i,k)-want[i][k]);
      maxError=std::max(maxError,e);
      // VU's non-IEEE rounding is not the host FPU's rounding. Bound it to
      // 1/1000 of one GS colour step; raster qualification remains separate.
      if(e>.001F) {
        std::cerr<<"trial="<<trial<<" vertex="<<i<<" lane="<<k
                 <<" actual="<<get(r.mem,color+i,k)<<" expected="<<want[i][k]
                 <<" error="<<e<<"\n";
        return 1;
      }
      ++checked;
    }
    // Ordinary env, ordinary texture, and lit bags must retain every byte.
    for(int mode:{-1,0,1}) {
      mem[8*4+1]=uint32_t(mode);
      auto off=vusim::run(p,mem,cfg);assert(off.ok && off.mem==mem);
    }
  }
  // Falsify the actual instruction stream, not the expected result.
  unsigned changed=0;
  for(auto& op:p.code)if(op.op==vuir::Op::Loi && op.fimm==220.0F){op.fimm=0;++changed;}
  assert(changed==1);
  std::vector<uint32_t> m(4096,0);m[8*4+1]=2;m[32*4+3]=1;
  put(m,12,0,1);put(m,13,0,1);put(m,35,0,1);
  auto bad=vusim::run(p,m,cfg);assert(bad.ok);
  assert(std::abs(get(bad.mem,36,3)-221.0F)>1.0F);
  std::cout<<"PASS components="<<checked<<" maxAbs="<<maxError
           <<" disabledModes=3 negativeControl=specular-zero\n";

  // Run the whole shader twice: EE colours versus the new VU prepass. Compare
  // actual simulated GIF words, including clipping/interpolation, not just math.
  std::string path=argv[1];
  vuasm::Options options;options.includeRoot=path.substr(0,path.find("/src/"));
  vuir::Program whole;assert(vuasm::parseFile(path,options,whole,error));
  bool clip=path.find("/clip/")!=std::string::npos;
  unsigned words=0, colorDifferences=0, changedCounts=0, nonempty=0;
  for(int trial=0;trial<300;++trial) {
    const int n=12, pos=34, normal=pos+n, color=normal+n;
    std::vector<uint32_t> a(4096,0);
    const float mvp[4][4]={{1.2F,0,0,0},{0,1.6F,0,0},
                           {0,0,-1.002F,-1},{0,0,-2,60}};
    for(int q=0;q<4;++q)for(int k=0;k<4;++k)put(a,q,k,mvp[q][k]);
    const float env[3][4]={{.4F,-.09F,0,0},{0,-.477F,0,0},{-.3F,-.12F,1,.5F}};
    for(int q=0;q<3;++q)for(int k=0;k<4;++k)put(a,4+q,k,env[q][k]);
    put(a,8,2,-255.0F/900);put(a,8,3,255.0F*1000/900);
    put(a,32,0,2048);put(a,32,1,2048);put(a,32,2,8388607.5F);
    a[32*4+3]=n|(clip?0xfc00:0);a[33*4]=n|0x8000;
    put(a,20,0,1-4096);put(a,20,1,-1000-4096);put(a,20,3,4096);
    const float planes[6][4]={{0,0,1,0},{0,0,-1,0},{-1,0,0,1},
                              {1,0,0,1},{0,-1,0,1},{0,1,0,1}};
    for(int q=0;q<6;++q)for(int k=0;k<4;++k)put(a,944+q*2,k,planes[q][k]);
    put(a,945,0,5);put(a,947,0,1000);
    auto f=unit(),h=unit();
    for(int k=0;k<3;++k){put(a,12,k,f[k]);put(a,13,k,h[k]);}
    for(int i=0;i<n;++i) {
      put(a,pos+i,0,d(rng)*(clip?80:5));put(a,pos+i,1,d(rng)*(clip?80:5));
      put(a,pos+i,2,d(rng)*5);put(a,pos+i,3,1);
      auto v=unit();for(int k=0;k<3;++k)put(a,normal+i,k,v[k]);
      float df=v[0]*f[0]+v[1]*f[1]+v[2]*f[2];
      float rgb=128*(.30F+.70F*(1-std::abs(df)));
      float sp=std::max(v[0]*h[0]+v[1]*h[1]+v[2]*h[2],0.0F);
      sp*=sp;sp*=sp;sp*=sp;
      for(int k=0;k<3;++k)put(a,color+i,k,rgb);
      put(a,color+i,3,std::min(1+220*sp,255.0F));
    }
    a[8*4+1]=1;
    auto b=a;b[8*4+1]=2;
    for(int i=0;i<n;++i)for(int k=0;k<4;++k)put(b,color+i,k,0);
    auto ee=vusim::run(whole,a,cfg),vu=vusim::run(whole,b,cfg);
    assert(ee.ok && vu.ok && ee.kicks.size()==1 && ee.kicks==vu.kicks);
    int kick=ee.kicks[0],count=ee.mem[(kick+8)*4]&0x7fff;
    assert(count==int(vu.mem[(kick+8)*4]&0x7fff));
    nonempty+=count>0;changedCounts+=count!=n;
    assert(kick+9+count*3<944);
    for(int q=0;q<9+count*3;++q)for(int k=0;k<4;++k) {
      uint32_t x=ee.mem[(kick+q)*4+k],y=vu.mem[(kick+q)*4+k];
      if(q>=9 && (q-9)%3==1) {
        assert(x<=255 && y<=255 && std::abs(int(x)-int(y))<=1);
        colorDifferences+=x!=y;
      } else assert(x==y);
      ++words;
    }
  }
  assert(nonempty>250 && (!clip || changedCounts>200));
  std::cout<<"PASS fullShader trials=300 gifWords="<<words
           <<" colorWordsWithinOne="<<colorDifferences
           <<" nonempty="<<nonempty<<" clippedCountChanged="<<changedCounts<<"\n";
}
