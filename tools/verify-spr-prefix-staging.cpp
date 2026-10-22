// Scratch-only host bookkeeping experiment. No EE DMA/cache/barrier model.
#include <algorithm>
#include <array>
#include <string>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
#include "../vendor/tyra/engine/inc/renderer/core/paths/path1/frame_vif_writer.hpp"
struct alignas(16) Quad { uint32_t word[4]; };
unsigned checks=0, cases=0, transfers=0, blocked=0;
void check(bool b) { ++checks; if(!b) throw std::runtime_error("check "+std::to_string(checks)); }
alignas(16) uint32_t ref[4]={2,4,8,16};
const uint32_t* resolve(uint32_t a,uint32_t n,void*) { return a==0x1000 && n==1 ? ref : nullptr; }
struct Bank {
  std::vector<Quad> data; size_t expected=0, completed=0, pending=0;
  bool active=false, published=false, reader=false;
  bool begin(size_t n) {
    if(active || reader) {++blocked; return false;}
    data.assign(n+2,Quad{{0xdeadbeef,0xdeadbeef,0xdeadbeef,0xdeadbeef}});
    expected=n; completed=pending=0; active=true; published=false; return true;
  }
  bool publish() {
    if(!active || pending || completed!=expected || published) {++blocked;return false;}
    published=true;reader=true;return true;
  }
  void release() { check(reader);reader=false;active=false; }
};
struct Window {
  std::vector<Quad> data; bool busy=false; Bank* bank=nullptr;size_t offset=0,count=0;
  explicit Window(size_t capacity):data(capacity) {}
  bool stage(Bank& b,const Quad* source,size_t at,size_t n) {
    if(busy || !b.active || b.published || n>data.size() || at>b.expected || n>b.expected-at || !n) {
      ++blocked;return false;
    }
    std::memcpy(data.data(),source,n*sizeof(Quad));busy=true;bank=&b;offset=at;count=n;++b.pending;return true;
  }
  void complete() {
    check(busy && bank && bank->pending);
    std::memcpy(bank->data.data()+offset+1,data.data(),count*sizeof(Quad));
    bank->completed+=count;--bank->pending;
    std::memset(data.data(),0x63,data.size()*sizeof(Quad));
    busy=false;bank=nullptr;++transfers;
  }
};
void stageCase(const std::vector<Quad>& input,size_t count,size_t chunk,bool dual) {
  ++cases;
  Bank b;check(b.begin(count));Window a(chunk),c(chunk);
  const Quad* expected=input.data();
  size_t at=0;unsigned turn=0;
  while(at<count) {
    Window& w=dual && (turn++%2) ? c:a;
    if(w.busy) w.complete();
    size_t n=std::min(chunk,count-at);
    check(w.stage(b,input.data()+at,at,n));
    check(!w.stage(b,input.data()+at,at,n)); // delayed transfer protects source window
    check(!b.publish()); // all data not yet present
    check(!b.begin(count)); // incomplete bank cannot be reused
    if(!dual) w.complete();
    at+=n;
  }
  if(c.busy)c.complete();
  if(a.busy)a.complete();
  check(b.pending==0 && b.completed==count);
  check(b.publish());check(!b.begin(count)); // VIF1-reader hold remains after staging
  check(std::memcmp(expected,b.data.data()+1,count*sizeof(Quad))==0);
  for(unsigned i=0;i<4;++i) {check(b.data.front().word[i]==0xdeadbeef);check(b.data.back().word[i]==0xdeadbeef);}
  check(bool(Tyra::Vif1ChainCheck::validate(b.data.data()+1,uint32_t(count),resolve)));
  check(!a.stage(b,input.data(),0,1)); // no writes to published bank
  b.release();check(b.begin(count)); // completed reader now permits reuse
}
std::vector<Quad> nopPrefix(unsigned count) {
  std::vector<Quad> m(count+10);Tyra::FrameVifWriter w(m.data(),uint32_t(m.size()));
  for(unsigned i=0;i<count-1;++i)check(w.inlineVif(0,0,nullptr,0));
  check(w.finish()==count);check(w.finish()==count);check(!w.rewind(0));
  m.resize(count);return m;
}
int main() {try {
  for(unsigned n:{1u,511u,512u,513u,1023u,1024u,1025u,2049u,8192u}) {
    auto m=nopPrefix(n);
    for(size_t chunk:{512u,1024u})for(bool dual:{false,true})stageCase(m,n,chunk,dual);
  }
  std::vector<Quad> m(8192),payload(2400);
  for(size_t i=0;i<payload.size();++i)for(unsigned j=0;j<4;++j)payload[i].word[j]=uint32_t(i*17+j);
  Tyra::FrameVifWriter w(m.data(),uint32_t(m.size()));
  check(w.inlineVif(0x01000101,0x6c000000,payload.data(),256)); // NUM=0 UNPACK, 256 quads
  unsigned mark=w.checkpoint();
  check(w.inlineVif(0,0x50000960,payload.data(),2400)); // rolled-back multi-window DIRECT
  check(w.rewind(mark));check(w.checkpoint()==mark);
  check(w.inlineVif(0,0x50000900,payload.data(),2304)); // retained multi-window DIRECT
  check(w.inlineVif(0,0x4a000000,payload.data(),128)); // NUM=0 MPG, 256 instructions
  check(w.referencedVif(0x01000101,0x6c010100,0x1000,1,resolve));
  unsigned n=w.finish();check(n==2693);check(bool(Tyra::Vif1ChainCheck::validate(m.data(),n,resolve)));
  // Verify the rolled-back bytes are excluded, the surviving operation is exact.
  check(m[mark].word[3]==0x50000900);
  check(std::memcmp(m.data()+mark+1,payload.data(),2304*sizeof(Quad))==0);
  for(size_t chunk:{512u,1024u})for(bool dual:{false,true})stageCase(m,n,chunk,dual);
  // Two banks: new staging cannot retire another bank's held VIF1 reader.
  auto small=nopPrefix(3); Bank b0,b1;Window stage(1024);
  check(b0.begin(3));check(stage.stage(b0,small.data(),0,3));stage.complete();check(b0.publish());
  check(b1.begin(3));check(stage.stage(b1,small.data(),0,3));stage.complete();check(b1.publish());
  check(!b0.begin(3));check(!b1.begin(3));b0.release();check(b0.begin(3));check(!b1.begin(3));b1.release();
  std::printf("PASS cases=%u checks=%u transfers=%u expected_refusals=%u\n",cases,checks,transfers,blocked);
  std::puts("Scope: actual FrameVifWriter finalized bytes; host staging bookkeeping only. No DMA/cache/barrier/performance proof.");
  return 0;
} catch(const std::exception& e){ std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;} }
