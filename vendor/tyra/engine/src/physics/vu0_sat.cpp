// Added by TyraX: asynchronous, bounded OBB SAT/linear-CCD micro batches.
#include "physics/vu0_sat.hpp"
#include <cstring>
#ifndef TYRA_VU0_SAT_PROFILE
#define TYRA_VU0_SAT_PROFILE 0
#endif
#ifdef _EE
#include "debug/debug.hpp"
#include "math/vu0_owner.hpp"
extern unsigned Vu0SatKernel_CodeStart __attribute__((section(".vudata")));
extern unsigned Vu0SatKernel_CodeEnd __attribute__((section(".vudata")));
#endif
namespace Tyra {
bool packVu0Sat(Vu0SatJob& job, const ConvexView& a, const ConvexView& b,
               const CollisionObb& boxA, const CollisionObb& boxB,
               CollisionV3 motionA, CollisionV3 motionB) {
  const int count = a.nn + b.nn + a.ne * b.ne;
  if (!a.nv || !b.nv || count <= 0 || count > 21) return false;
  auto put = [&](int index, CollisionV3 v) {
    job.q[index][0] = v.x; job.q[index][1] = v.y;
    job.q[index][2] = v.z; job.q[index][3] = 0;
  };
  put(0, boxA.centre); put(4, boxB.centre);
  for (int i = 0; i < 3; ++i) {
    put(i + 1, boxA.halfAxes[i]); put(i + 5, boxB.halfAxes[i]);
  }
  put(8, motionA); put(9, motionB);
  int index = 10;
  for (int i = 0; i < a.nn; ++i) put(index++, a.normals[i]);
  for (int i = 0; i < b.nn; ++i) put(index++, b.normals[i]);
  int uncertain=0;
  for (int i = 0; i < a.ne; ++i)
    for (int j = 0; j < b.ne; ++j) {
      const auto cross=cvCross(a.edges[i],b.edges[j]);put(index++,cross);
      const float l2=cvDot(cross,cross);
      if(l2>1e-20f && l2<1e-8f)uncertain=1;
    }
  std::memcpy(&job.q[31][0], &count, sizeof(count));
  // Almost parallel axes can exchange rank after VU normalization rounding.
  // Their support vertices need not be close. Preserve the scalar tie policy.
  std::memcpy(&job.q[31][1],&uncertain,sizeof(uncertain));
  return true;
}
bool finishVu0Sat(const Vu0SatResult& r, const Vu0SatJob& job, const ConvexView& a,
                 const ConvexView& b, CollisionV3 motionA, CollisionV3 motionB,
                 ConvexContact& out) {
  // Linear CCD's strict support-vertex tie policy is sensitive even to one
  // VU/FPU rounding bit. Confirm positive sweeps on EE; micro still rejects
  // separated pairs over the complete interval and retains every SAT axis.
  const bool found=(int(r.flags[3])&1)!=0;
  const bool sweepHit=r.flags[1]==0 && r.flags[2]!=0 && found &&
      r.sweepEnter[3]<=1 && r.flags[0]>=0;
  const bool tiedHit=r.flags[3]>=2 && r.flags[1]!=0 && r.normalDepth[3]<1e30f;
  // A tied minimum cannot invalidate a separating-axis proof. Confirm ties
  // only when they would choose a contact, not for already separated pairs.
  if(vu0SatScalarPreferred(job) || tiedHit || sweepHit) {
#if TYRA_VU0_SAT_PROFILE
    vu0SatMetrics.confirmedCcd+=sweepHit;
    vu0SatMetrics.confirmedTie+=tiedHit;
    vu0SatMetrics.confirmedParallel+=vu0SatScalarPreferred(job);
#endif
    CollisionObb boxes[2];
    for(int k=0;k<2;++k) {
      const float* c=job.q[k*4];boxes[k].centre={c[0],c[1],c[2]};
      for(int h=0;h<3;++h){const float* v=job.q[k*4+h+1];boxes[k].halfAxes[h]={v[0],v[1],v[2]};}
    }
    // The EE compiler otherwise inlines a second copy here while the game
    // calls the out-of-line weak definition. Different FPU contraction and
    // register schedules can then pick opposite strict CCD support vertices.
    // Use the same compiled scalar routine as the game for confirmations.
    return scalarMotionContactReference(a,b,motionA,motionB,out,&boxes[0],&boxes[1]);
  }
  auto exactAxis = [&](int index) {
    const float* v=job.q[10+index];CollisionV3 n{v[0],v[1],v[2]};
    return cvMul(n,1/std::sqrt(cvDot(n,n)));
  };
  auto project = [&](int box,CollisionV3 n,float& lo,float& hi) {
    auto vec=[&](int index){const float* v=job.q[index];return CollisionV3{v[0],v[1],v[2]};};
    const auto centre=vec(box*4);const float c=cvDot(n,centre);
    float radius=std::fabs(cvDot(n,vec(box*4+1)))+std::fabs(cvDot(n,vec(box*4+2)))+std::fabs(cvDot(n,vec(box*4+3)));
    radius+=1e-6f*(1+std::fabs(n.x*centre.x)+std::fabs(n.y*centre.y)+std::fabs(n.z*centre.z)+radius);
    lo=c-radius;hi=c+radius;
  };
  CollisionV3 n{};
  if (r.flags[1] != 0 && r.normalDepth[3] < 1e30f) {
    n=exactAxis(int(r.indices[0]));
    float alo,ahi,blo,bhi;project(0,n,alo,ahi);project(1,n,blo,bhi);
    const float plus=bhi-alo,minus=ahi-blo;
    if(!(plus<minus))n=cvMul(n,-1);
    auto support = [&](const ConvexView& h, CollisionV3 axis) {
      float best = -1e30f;
      for(int i=0;i<h.nv;++i) best=std::max(best,cvDot(h.verts[i],axis));
      CollisionV3 sum{}; int count=0;
      for(int i=0;i<h.nv;++i) if(best-cvDot(h.verts[i],axis)<=1e-4f) {
        sum=cvAdd(sum,h.verts[i]); ++count;
      }
      return cvMul(sum,1.f/count);
    };
    out.normal=n; out.depth=std::min(plus,minus);
    out.point=cvMul(cvAdd(support(a,cvMul(n,-1)),support(b,n)),.5f);
    return true;
  }
  float enter=r.sweepEnter[3];
  if(r.flags[2]==0 || !found || enter>1 || r.flags[0]<0) return false;
  n=exactAxis(int(r.indices[1]));
  float alo,ahi,blo,bhi;project(0,n,alo,ahi);project(1,n,blo,bhi);
  const float da=cvDot(n,motionA),db=cvDot(n,motionB),d=da-db;
  alo-=da;ahi-=da;blo-=db;bhi-=db;
  float t0=(blo-ahi)/d,t1=(bhi-alo)/d;
  if(t0>t1){std::swap(t0,t1);}else n=cvMul(n,-1);
  enter=t0;
  float amin=1e30f,bmax=-1e30f;CollisionV3 av{},bv{};
  for(int i=0;i<a.nv;++i) {float v=cvDot(a.verts[i],n);if(v<amin){amin=v;av=a.verts[i];}}
  for(int i=0;i<b.nv;++i) {float v=cvDot(b.verts[i],n);if(v>bmax){bmax=v;bv=b.verts[i];}}
  out.normal=n;out.depth=std::max(0.f,bmax-amin);
  out.point=cvMul(cvAdd(cvSub(av,cvMul(motionA,1-enter)),
                        cvSub(bv,cvMul(motionB,1-enter))),.5f);
  return true;
}
#ifdef _EE
namespace {
void copyQwords(volatile void* destination,const volatile void* source,int count) {
  auto dst=static_cast<volatile unsigned char*>(destination);
  auto src=static_cast<const volatile unsigned char*>(source);
  // EE integer LQ/SQ, not COP2: no VU register or macro instruction is used.
  // Both endpoints must be 16-byte aligned and VU0 must already be idle.
  asm volatile(
      "1:\n lq $8,0(%0)\n sq $8,0(%1)\n"
      "addiu %0,%0,16\n addiu %1,%1,16\n addiu %2,%2,-1\n"
      "bnez %2,1b\n nop\n"
      : "+r"(src),"+r"(dst),"+r"(count) :: "$8","memory");
}
void waitIdle() {
  unsigned status;
  do {asm volatile("cfc2 %0, $29" : "=r"(status) :: "memory");} while(status&1);
}
void storeWords(int qw,const void* source,int count) {
  auto dst=reinterpret_cast<volatile unsigned*>(0x11004000)+qw*4;
  if(count>0 && count%4==0 && !(reinterpret_cast<unsigned long>(source)&15)) {
    copyQwords(dst,source,count/4);return;
  }
  auto src=static_cast<const unsigned char*>(source);
  for(int i=0;i<count;++i){unsigned word;std::memcpy(&word,src+i*4,4);dst[i]=word;}
}
}
bool Vu0SatBatch::begin() {
  TYRA_ASSERT(!active,"VU0 SAT: nested ownership");
  waitIdle();
  if(vu0MicroOwner!=&Vu0SatKernel_CodeStart) {
    const auto* first=&Vu0SatKernel_CodeStart;
    const unsigned words=&Vu0SatKernel_CodeEnd-first;
    if(!words || words>1024)return false;
    auto dst=reinterpret_cast<volatile unsigned*>(0x11000000);
    for(unsigned i=0;i<words;++i)dst[i]=first[i];
    asm volatile("sync.l" ::: "memory");
    vu0MicroOwner=first;
  }
  return true;
}
void Vu0SatBatch::start(const Vu0SatJob* jobs,int n) {
  TYRA_ASSERT(!active && n>0 && n<=Capacity,"VU0 SAT: invalid batch");
  waitIdle();
  alignas(16) const float constants[4]={1e30f,1e-10f,1e-9f,1e-6f};
  storeWords(0,&n,1);storeWords(1,constants,4);
  storeWords(2,jobs,n*128);
  count=n;active=true;
#if TYRA_VU0_SAT_PROFILE
  vu0SatMetrics.jobs+=n;++vu0SatMetrics.batches;
#endif
  asm volatile("sync.l\n vcallms 0" ::: "memory");
}
void Vu0SatBatch::finish(Vu0SatResult* results) {
  if(!active)return;
#if TYRA_VU0_SAT_PROFILE
  unsigned start,stop,status;
  asm volatile("mfc0 %0,$9\n cfc2 %1,$29" : "=r"(start),"=r"(status) :: "memory");
  vu0SatMetrics.busyAtFinish+=(status&1)!=0;
#endif
  waitIdle();
#if TYRA_VU0_SAT_PROFILE
  asm volatile("mfc0 %0,$9" : "=r"(stop) :: "memory");
  vu0SatMetrics.waitTicks+=stop-start;
#endif
  auto src=reinterpret_cast<volatile unsigned*>(0x11004000)+226*4;
  if(!(reinterpret_cast<unsigned long>(results)&15))copyQwords(results,src,count*4);
  else for(int i=0;i<count*16;++i){const unsigned word=src[i];std::memcpy(reinterpret_cast<unsigned char*>(results)+i*4,&word,4);}
  active=false;
}
Vu0SatBatch::~Vu0SatBatch(){if(active)waitIdle();}
void Vu0SatBatch::observeOverlap() const {
#if TYRA_VU0_SAT_PROFILE
  if(!active)return;
  unsigned status;asm volatile("cfc2 %0,$29" : "=r"(status) :: "memory");
  vu0SatMetrics.busyWhileQuerying+=(status&1)!=0;
#endif
}
#else
bool Vu0SatBatch::begin(){return false;}
void Vu0SatBatch::start(const Vu0SatJob*,int){}
void Vu0SatBatch::finish(Vu0SatResult*){}
Vu0SatBatch::~Vu0SatBatch()=default;
void Vu0SatBatch::observeOverlap() const {}
#endif
} // namespace Tyra
