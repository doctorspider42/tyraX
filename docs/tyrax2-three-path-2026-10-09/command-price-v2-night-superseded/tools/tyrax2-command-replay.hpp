#pragma once
#include "debug/command_tape.hpp"
#include "renderer/core/renderer_core.hpp"
#include "renderer/core/paths/path1/vif1_queue.hpp"
#include <packet2_utils.h>
extern u32 VU1DrawFinish_CodeStart,VU1DrawFinish_CodeEnd;
namespace CommandTape {
inline packet2_t* helper=nullptr;
inline packet2_t* toggle=nullptr;
constexpr U restoreCapacity=1100;
alignas(16) inline U restore[restoreCapacity*4];
inline U restoreQwords=0,helperQwords=0,target=0,replays=0;
inline bool ready=false;
inline bool stateEqual(const State& a,const State& b){
  return ((a.stat^b.stat)&0x80)==0 && a.cycle==b.cycle && a.mode==b.mode &&
    a.base==b.base && a.ofst==b.ofst && a.tops==b.tops && a.top==b.top &&
    a.itop==b.itop && a.itops==b.itops && a.mask==b.mask &&
    !memcmp(a.row,b.row,sizeof(a.row)) && !memcmp(a.col,b.col,sizeof(a.col));
}
inline void prepare(RendererCore& core){
  TYRA_ASSERT(valid && flushes==1 && submissions==1 && !mpg && calls &&
    ((firstCall>>24)&127)==0x14 && !memcmp(programBefore,programAfter,16384),"Unsupported command program closure");
  TYRA_ASSERT(before.cycle==0x101 && before.mode==0 && before.itop==0 && before.itops==0 &&
    before.top==before.base+((before.stat&0x80)?0:before.ofst) &&
    before.tops==before.base+((before.stat&0x80)?before.ofst:0),
    "Unsupported command double-buffer prestate");
  U df=core.getPath1()->getDrawFinishAddr();
  U dfBytes=U(reinterpret_cast<uintptr_t>(&VU1DrawFinish_CodeEnd)-reinterpret_cast<uintptr_t>(&VU1DrawFinish_CodeStart));
  TYRA_ASSERT(dfBytes && df*8+dfBytes<=16384 && !memcmp(reinterpret_cast<char*>(programBefore)+df*8,&VU1DrawFinish_CodeStart,dfBytes),
    "Draw finish image does not match captured residency");
  FrameVifWriter writer(restore,restoreCapacity);
  TYRA_ASSERT(writer.inlineVif(0x01000101,0x05000000,nullptr,0),"Restore prefix failed");
  for(U i=0;i<4;++i)TYRA_ASSERT(writer.inlineVif(0,0x6c000000|i*256,dataBefore+i*1024,256),"Restore data failed");
  alignas(16) U payload[16]={0x20000000,before.mask,0x30000000,before.row[0],before.row[1],before.row[2],before.row[3],
    0x31000000,before.col[0],before.col[1],before.col[2],before.col[3],0x04000000|before.itops,0,0,0};
  TYRA_ASSERT(writer.inlineVif(0x01000000|before.cycle,0x05000000|before.mode,payload,4),"Restore state failed");
  restoreQwords=writer.finish();TYRA_ASSERT(restoreQwords,"Restore finish failed");
  helper=packet2_create(32,P2_TYPE_NORMAL,P2_MODE_CHAIN,true);TYRA_ASSERT(helper,"Helper allocation failed");
  packet2_utils_vu_add_double_buffer(helper,uint16_t(before.base),uint16_t(before.ofst));
  packet2_chain_open_cnt(helper,0,0,0);
  packet2_add_u32(helper,0x01000000|before.cycle);packet2_add_u32(helper,0x05000000|before.mode);
  for(U i=0;i<16;++i)packet2_add_u32(helper,payload[i]);
  packet2_chain_close_tag(helper);
  core.getPath1()->addDrawFinishTag(helper);
  packet2_chain_open_cnt(helper,0,0,0);packet2_vif_flusha(helper,0);packet2_vif_nop(helper,0);packet2_chain_close_tag(helper);
  packet2_utils_vu_add_end_tag(helper);helperQwords=packet2_get_qw_count(helper);
  toggle=packet2_create(32,P2_TYPE_NORMAL,P2_MODE_CHAIN,true);TYRA_ASSERT(toggle,"Toggle allocation failed");
  core.getPath1()->addDrawFinishTag(toggle);
  packet2_chain_open_cnt(toggle,0,0,0);packet2_vif_flusha(toggle,0);packet2_vif_nop(toggle,0);packet2_chain_close_tag(toggle);
  packet2_utils_vu_add_end_tag(toggle);
  TYRA_ASSERT(Vif1ChainCheck::validate(helper->base,helperQwords,ram) && Vif1ChainCheck::validate(restore,restoreQwords,ram),"Invalid replay startup chains");
  TYRA_ASSERT(Vif1ChainCheck::validate(toggle->base,packet2_get_qw_count(toggle),ram),"Invalid toggle chain");
  ready=true;
  printf("COMMANDREADY target=%u helper=%u restore=%u finishAddr=%u first=%08x\n",unsigned(target),unsigned(helperQwords),unsigned(restoreQwords),unsigned(df),unsigned(firstCall));
}
inline void submitOwned(const void* root){
  TYRA_ASSERT(ready && (root==memory || root==restore || root==helper->base || root==toggle->base),"Unknown diagnostic root");
  Vif1Queue::waitFor(Vif1Queue::submitDiagnosticOwned(root));
}
inline void startup(RendererCore& core,bool verify){
  core.sync.clear();submitOwned(helper->base);core.sync.waitAndClear();
  if(!(before.stat&0x80)){core.sync.clear();submitOwned(toggle->base);core.sync.waitAndClear();}
  submitOwned(restore);
  // DMA completion is not VIF FIFO consumption. Wait before reading VU memory.
  while(U(VIF1_STAT)&0x1f000003){}
  if(verify){State state{};alignas(16) static U p[4096],d[4096];
    TYRA_ASSERT(snapshot(state,p,d) && stateEqual(before,state) && !memcmp(p,programBefore,16384) && !memcmp(d,dataBefore,16384),"Replay startup mismatch");}
}
inline void run(RendererCore& core){
  TYRA_ASSERT(core.gs.selectDiagnosticTarget(uint8_t(target)),"Unsupported command target");
  startup(core,true);core.sync.clear();submitOwned(memory);core.sync.waitAndClear();++replays;
  core.gs.flipBuffers(false);
}
}
