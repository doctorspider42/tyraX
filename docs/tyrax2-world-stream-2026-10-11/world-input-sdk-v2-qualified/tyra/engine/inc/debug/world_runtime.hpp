#pragma once
#include "debug/clip_consumer.hpp"
#include "debug/world_owner.hpp"
#include "debug/world_body.hpp"
#include "debug/world_payload.hpp"
namespace WorldRuntime {
struct Spans {
  static bool add(const void*,unsigned);
  static void retire(const void*);
  static bool collect(const void*);
  static void drain();
  static void waitFor(unsigned);
};
using Pool=WorldOwner::Pool<Spans>;
using Owner=Pool::Owner;
using Attachment=WorldOwner::Attachment<Owner>;
inline bool active=false,cold=false;
inline bool admitted=false;
struct ResidualScope {
  bool previous;
  ResidualScope():previous(ClipConsumer::observe){if(admitted)ClipConsumer::observe=false;}
  ~ResidualScope(){ClipConsumer::observe=previous;}
};
inline unsigned sources=0, draws=0, triangles=0;
inline unsigned sourceHash=2166136261U;
inline unsigned trialDraws=0,trialQwords=0;
inline void reset(){sources=draws=triangles=0;sourceHash=2166136261U;}
inline void word(unsigned& hash,unsigned value){hash=(hash^value)*16777619U;}
}
