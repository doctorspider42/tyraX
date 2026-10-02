// Native frame-chain recording: ordered VIF words and transactional bounds.
#include <cstdio>
#include <vector>
#include "../vendor/tyra/engine/inc/renderer/core/paths/path1/frame_vif_writer.hpp"
namespace {
alignas(16) uint32_t source[4] = {1,2,3,4};
const uint32_t* resolve(uint32_t address, uint32_t qwords, void*) {
  return address == 0x1000 && qwords == 1 ? source : nullptr;
}
// Independent oracle: collect the physical stream's TTE commands/data words.
std::vector<uint32_t> stream(const uint32_t* words, uint32_t count) {
  std::vector<uint32_t> result;
  for (uint32_t at=0;at<count;) {
    const uint32_t* tag=words+at*4;
    const uint32_t id=(tag[0]>>28)&7, qwords=tag[0]&65535;
    result.push_back(tag[2]); result.push_back(tag[3]);
    const uint32_t* payload=id==3 ? resolve(tag[1],qwords,nullptr) : tag+4;
    result.insert(result.end(),payload,payload+qwords*4);
    at+=id==3 ? 1 : 1+qwords;
    if (id==7) break;
  }
  return result;
}
}
int main() {
  unsigned checks=0;
  auto expect=[&](bool value) {++checks;if (!value) {
    std::fprintf(stderr,"Frame writer case %u failed\n",checks);return false;
  }return true;};
#define CHECK(x) do {if (!expect(x)) return 1;} while(0)
  alignas(16) uint32_t memory[128] = {};
  Tyra::FrameVifWriter writer(memory,32);
  CHECK(writer.inlineVif(0x01000101,0x6c010000,source,1));
  CHECK(writer.referencedVif(0x01000101,0x6c010010,0x1000,1,resolve));
  CHECK(writer.inlineVif(0x14000000,0x10000000,nullptr,0));
  const uint32_t count=writer.finish(); CHECK(count==5);
  CHECK(static_cast<bool>(Tyra::Vif1ChainCheck::validate(memory,count,resolve)));
  const std::vector<uint32_t> oracle={0x01000101,0x6c010000,1,2,3,4,
      0x01000101,0x6c010010,1,2,3,4,0x14000000,0x10000000,0,0};
  CHECK(stream(memory,count)==oracle);
  CHECK(writer.finish()==count && !writer.inlineVif(0,0,nullptr,0));
  CHECK(!writer.rewind(0));
  writer.reset(); CHECK(writer.checkpoint()==0);
  const uint32_t mark=writer.checkpoint();
  CHECK(writer.inlineVif(0,0x50000001,source,1)); // one DIRECT quad
  CHECK(writer.rewind(mark)); // atomic rollback of an unsubmitted operation
  CHECK(writer.inlineVif(0,0x4a020000,source,1)); // two MPG instructions
  CHECK(static_cast<bool>(Tyra::Vif1ChainCheck::validate(memory,writer.finish(),resolve)));
  Tyra::FrameVifWriter tiny(memory,2);
  CHECK(!tiny.inlineVif(0,0,source,1) && tiny.checkpoint()==0);
  CHECK(tiny.inlineVif(0,0,nullptr,0) && tiny.finish()==2);
  Tyra::FrameVifWriter missing(nullptr,32);
  CHECK(!missing.inlineVif(0,0,nullptr,0) && !missing.finish());
  Tyra::FrameVifWriter unaligned(memory+1,32);
  CHECK(!unaligned.inlineVif(0,0,nullptr,0));
  writer.reset();
  CHECK(!writer.referencedVif(0,0,0x1001,1,resolve));
  CHECK(!writer.referencedVif(0,0,0x2000,1,resolve));
  CHECK(!writer.inlineVif(0,0,source,65536));
  CHECK(!writer.inlineVif(0,0,source+1,1));
  CHECK(!writer.rewind(1) && writer.checkpoint()==0);
  // No END is synthesized by ordinary records; only finish terminates.
  for (unsigned i=0;i<20;++i) CHECK(writer.inlineVif(0,0,nullptr,0));
  const uint32_t full=writer.finish();
  unsigned ends=0;
  for (unsigned i=0;i<full;++i) ends+=(memory[i*4]>>28)==7;
  CHECK(ends==1 && full==21);
  std::printf("Frame writer checks passed: %u cases\n",checks);
}
