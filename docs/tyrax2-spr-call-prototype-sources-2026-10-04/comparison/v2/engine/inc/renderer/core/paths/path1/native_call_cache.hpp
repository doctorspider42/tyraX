/* Modified by TyraX: PRIVATE bounded immutable native CALL/RET candidate.
 * Licensed under Apache 2.0. No production/default acceptance. */
#pragma once
#include "frame_vif_writer.hpp"

namespace Tyra {
class NativeCallCache {
 public:
  static constexpr uint32_t kEntries = 32, kRecords = 32;
  using Lease = bool (*)(uint32_t, uint32_t, void*);
  struct Stats {
    uint32_t calls=0, hits=0, builds=0, fallbacks=0;
    uint32_t singleRefCalls=0, multiRecordCalls=0, avoidedNativeRecords=0;
    uint32_t targetBytes=0, replayedRecords=0, nativeCommands=0;
  } stats;
  explicit NativeCallCache(uint32_t syntheticDmaBase=0):dmaBase(syntheticDmaBase) {}
  // The queue must not infer command boundaries from an unknown previous
  // hardware STCYCL. Require a reset before any payload-producing command.
  static bool hasCycleResetPrefix(const uint32_t* source,uint32_t qwords,
      Vif1ChainCheck::Resolver readable,void* context=nullptr) {
    if(!source || !qwords)return false;
    uint32_t inspected=0;
    for(uint32_t at=0;at<qwords && inspected<128;) {
      const auto* t=source+at*4;const uint32_t id=(t[0]>>28)&7,size=t[0]&65535;
      if((t[0]&0x8c000000u) || (id!=1 && id!=3 && id!=7 && id!=0))return false;
      if((id==1||id==7) && size>qwords-at-1)return false;
      const uint32_t* data=(id==1||id==7)?t+4:
        size && readable?readable(t[1],size,context):nullptr;
      if(size && !data)return false;
      for(uint32_t i=0;i<2+size*4 && inspected<128;++i,++inspected) {
        const uint32_t word=i<2?t[i+2]:data[i-2],cmd=(word>>24)&0x7f;
        if(cmd==1)return true;
        switch(cmd){
          case 0:case 2:case 3:case 4:case 5:case 6:case 7:
          case 0x10:case 0x11:case 0x13:case 0x14:case 0x15:case 0x17:break;
          default:return false;
        }
      }
      at+=(id==1||id==7)?1+size:1;
      if(id==7||id==0)break;
    }
    return false;
  }
  // Called only at a known command boundary. Payload bytes are immutable and
  // complete; initial cycle state is part of the key. A hit compares control
  // tags, not immutable payload, and skips native per-record writer/resolver.
  uint32_t append(const uint32_t* source,uint32_t remaining,
                  Vif1ChainCheck::Stream& stream,FrameVifWriter& writer,
                  Vif1ChainCheck::Resolver readable,Lease eligible,
                  Lease acquire,void* context,uint32_t bankMask) {
    if (!source || stream.result.pending || !stream.result ||
        !bankMask || (bankMask & ~3u) || !eligible || !acquire) return 0;
    uint32_t count=0,refs=0;
    while(count<remaining && count<kRecords) {
      const uint32_t* tag=source+count*4;
      uint32_t id=(tag[0]>>28)&7,size=tag[0]&65535;
      if ((tag[0]&0x8c000000u) || !((id==1 && !size) ||
          (id==3 && size && !(tag[1]&0x8000000fu) &&
           eligible(tag[1],size,context)))) break;
      refs+=id==3;
      ++count;
    }
    if (!count || !refs) return 0;
    ++stats.calls; Entry* selected=nullptr; bool hit=false;
    for (auto& e:entries) if(e.valid && e.count==count &&
        e.initialCl==stream.cl && e.initialWl==stream.wl) {
      bool same=true;
      for(uint32_t i=0;i<count && same;++i) {
        const auto* t=source+i*4;const auto* k=e.key+i*4;
        same=k[0]==(t[0]&0x7000ffffu) &&
          k[1]==((t[0]>>28)==3?t[1]:0) && k[2]==t[2] && k[3]==t[3];
      }
      if(same){selected=&e;hit=true;break;}
    }
    Vif1ChainCheck::Stream end=stream;
    if (!selected) {
      // Miss only: validate complete VIF semantics once. A failed candidate
      // leaves the original writer/state unchanged and takes REF fallback.
      for(uint32_t i=0;i<count && end.result;++i) {
        auto* t=source+i*4;const uint32_t size=t[0]&65535;
        end.word(t[2]);end.word(t[3]);
        const uint32_t* payload=size && readable?readable(t[1],size,context):nullptr;
        if(size && !payload){++stats.fallbacks;return 0;}
        for(uint32_t j=0;j<size*4 && end.result;++j)end.word(payload[j]);
      }
      if(!end.result || end.result.pending){++stats.fallbacks;return 0;}
      for(auto& e:entries)if(!e.readers){selected=&e;break;}
      if(!selected){++stats.fallbacks;return 0;}
    }
    // Existing snapshot leases are preserved, and acquire is idempotent.
    // Never reuse or overwrite any CALL target with live reader-bank bits.
    for(uint32_t i=0;i<count;++i)if((source[i*4]>>28)==3 &&
        !acquire(source[i*4+1],source[i*4]&65535,context)) {
      ++stats.fallbacks;return 0;
    }
    if(!hit) {
      selected->valid=false;selected->count=count;
      selected->initialCl=stream.cl;selected->initialWl=stream.wl;
      selected->finalCl=end.cl;selected->finalWl=end.wl;
      for(uint32_t i=0;i<count;++i) {
        const auto* t=source+i*4;auto* k=selected->key+i*4;
        k[0]=t[0]&0x7000ffffu;k[1]=(t[0]>>28)==3?t[1]:0;
        k[2]=t[2];k[3]=t[3];
      }
      memcpy(selected->target,selected->key,count*16);
      uint32_t* ret=selected->target+count*4;
      ret[0]=6u<<28;ret[1]=ret[2]=ret[3]=0;selected->valid=true;
      ++stats.builds;stats.targetBytes+=(count+1)*16;
    }
    if(!writer.calledVif(address(*selected))){++stats.fallbacks;return 0;}
    selected->readers|=bankMask;
    stream.cl=selected->finalCl;stream.wl=selected->finalWl;
    if(hit)++stats.hits;
    stats.replayedRecords+=count;
    if(count==1)++stats.singleRefCalls;
    else {++stats.multiRecordCalls;if(hit)stats.avoidedNativeRecords+=count-1;}
    return count;
  }
  void releaseBanks(uint32_t mask){for(auto& e:entries)e.readers&=~mask;}
  // Retirement stops future reuse immediately. Existing immutable payload and
  // target bytes remain held by the original completion-backed bank leases.
  void invalidateAll(){for(auto& e:entries)e.valid=false;}
  const uint32_t* resolve(uint32_t address,uint32_t& qwords)const {
    for(const auto& e:entries)if(e.readers && this->address(e)==address){
      qwords=e.count+1;return e.target;
    }
    qwords=0;return nullptr;
  }
  static void advance(Vif1ChainCheck::Stream& stream,const uint32_t* tag,
                      Vif1ChainCheck::Resolver readable,void* context=nullptr){
    const uint32_t id=(tag[0]>>28)&7,size=tag[0]&65535;
    stream.word(tag[2]);stream.word(tag[3]);
    const uint32_t* payload=(id==1||id==7)?tag+4:
       size && readable?readable(tag[1],size,context):nullptr;
    if(size && !payload){stream.result.error=Vif1ChainCheck::Error::Reference;return;}
    uint32_t i=0;
    // Skip raw data slots without reading mutable values. Only embedded VIF
    // commands require decoding. This additional On-arm work remains priced.
    if(stream.result.pending){i=stream.result.pending<size*4?stream.result.pending:size*4;stream.result.pending-=i;}
    for(;i<size*4 && stream.result;++i)stream.word(payload[i]);
  }
 private:
  struct alignas(16) Entry {
    alignas(16) uint32_t target[(kRecords+1)*4]={};
    uint32_t key[kRecords*4]={};
    uint32_t count=0,readers=0,initialCl=1,initialWl=1,finalCl=1,finalWl=1;
    bool valid=false;
  } entries[kEntries];
  uint32_t dmaBase;
  uint32_t address(const Entry& e)const {
    return dmaBase?dmaBase+static_cast<uint32_t>(&e-entries)*sizeof(Entry):
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(e.target));
  }
};
} // namespace Tyra
