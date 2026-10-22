// Modified by TyraX: bounded opt-in EE scopes and actual render-job provenance.
#pragma once
#include <tamtypes.h>
#ifndef TYRA_HARDWARE_TRACE
#define TYRA_HARDWARE_TRACE 1
#endif
namespace Tyra { namespace HardwareTrace {
enum class Kind : u32 { Span, Wait, Pacing, Snapshot, Marker, Legacy };
struct Context { u32 recordJob=0, presentJob=0, sequence=0, context=~u32(0), requested=0, effective=0; };
#if TYRA_HARDWARE_TRACE
extern bool active;
extern bool detailed;
u32 ticks();
void configure();
bool reserveCapture(u32 capacity=8192);
bool configureCapture(u32 first, u32 frames, bool states=false, bool detail=false, u32 capacity=8192);
void disable();
void release();
u32 loopIndex();
u32 captureEpoch();
void beginFrame();
void endFrame();
void record(const char* label, u32 start, u32 end, u32 value=0);
void state(const char* label);
u32 beginScope(const char* label, Kind kind, bool detail=false);
void endScope(u32 token);
Context context();
void setContext(const Context& value);
u32 beginJob(u32 context, bool requested, bool effective);
#else
constexpr bool active=false, detailed=false;
inline u32 ticks(){return 0;}
inline void configure(){}
inline bool reserveCapture(u32=8192){return false;}
inline bool configureCapture(u32,u32,bool=false,bool=false,u32=8192){return false;}
inline void disable(){}
inline void release(){}
inline u32 loopIndex(){return 0;}
inline u32 captureEpoch(){return 0;}
inline void beginFrame(){}
inline void endFrame(){}
inline void record(const char*,u32,u32,u32=0){}
inline void state(const char*){}
inline u32 beginScope(const char*,Kind,bool=false){return 0;}
inline void endScope(u32){}
inline Context context(){return {};}
inline void setContext(const Context&){}
inline u32 beginJob(u32,bool,bool){return 0;}
#endif
struct Scope {
  u32 token;
  explicit Scope(const char* name, Kind kind=Kind::Span, bool detail=true)
      : token(active && (!detail || detailed) ? beginScope(name,kind,detail) : 0) {}
  void finish(){if(token){endScope(token);token=0;}}
  ~Scope(){finish();}
  Scope(const Scope&)=delete;
  Scope& operator=(const Scope&)=delete;
};
// Previous-job completion nests under the current EE recording scope. Only
// presentation ownership changes; recordJob continues to describe the caller.
struct PresentContext {
  Context saved;
  PresentContext(u32 job,u32 sequence,u32 target):saved{} {
    if(active){saved=context();Context next=saved;next.presentJob=job;next.sequence=sequence;
      next.context=target;setContext(next);}
  }
  ~PresentContext(){if(active)setContext(saved);}
};
} }
