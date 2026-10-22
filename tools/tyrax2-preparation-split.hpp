#pragma once
#include <cstdint>
#ifndef PREPARATION_SPLIT_GEOMETRY
#define PREPARATION_SPLIT_GEOMETRY 0
#endif

namespace PreparationSplit {
using U = uint32_t;
enum Kind : U { Other, StaPip, Queue, Geometry };
#ifdef PREPARATION_SPLIT_HOST
U tick();
#else
inline U tick() { U t; asm volatile("mfc0 %0,$9":"=r"(t)::"memory"); return t; }
#endif
struct Part { U sta, queue, staCalls, queueCalls, reads;
#if PREPARATION_SPLIT_GEOMETRY
  U geometry, geometryCalls;
#endif
};
inline Part parts[2] = {};
inline U policy=0, phase=0, depth=0, current=Other, stamp=0, errors=0;
inline bool active=false;
inline U mode(U stage) {
  constexpr U modes[10]={0,0,1,2,1,0,1,2,1,0};
  return modes[stage];
}
inline void transition(U next) {
  auto& p=parts[phase];
  const U now=tick(); ++p.reads;
  if(current==StaPip)p.sta+=now-stamp;
  else if(current==Queue)p.queue+=now-stamp;
#if PREPARATION_SPLIT_GEOMETRY
  else if(current==Geometry)p.geometry+=now-stamp;
#endif
  stamp=now; current=next;
}
inline void begin(U selected) {
  parts[0]=parts[1]={}; errors=0; phase=0; policy=selected;
  if(depth || current!=Other)++errors;
  active=true;
}
inline void boundary() {
  if(depth || current!=Other)++errors;
  phase=1;
}
inline void end() {
  if(depth || current!=Other)++errors;
  active=false;
}
// A producer callback inside a queue scope switches ownership back to StaPip.
// Same-kind nesting contributes calls but no additional clock reads.
struct Scope {
  U previous=Other, level=0;
  bool entered=false, changed=false;
  explicit Scope(Kind kind) {
    if(!active || !policy)return;
    entered=true; previous=current; level=++depth;
    auto& p=parts[phase];
    if(kind==StaPip)++p.staCalls;else if(kind==Queue)++p.queueCalls;
#if PREPARATION_SPLIT_GEOMETRY
    else if(kind==Geometry)++p.geometryCalls;
#endif
    changed=U(kind)!=current;
    if(policy==2 && changed)transition(kind);else current=kind;
  }
  ~Scope() {
    if(!entered)return;
    if(depth!=level || !active)++errors;
    if(policy==2 && changed)transition(previous);else current=previous;
    --depth;
  }
  Scope(const Scope&)=delete;
  Scope& operator=(const Scope&)=delete;
};
struct Sample { U stage,local,policy,errors; Part prep,finish; };
inline Sample samples[1152];
inline U used=0;
}
