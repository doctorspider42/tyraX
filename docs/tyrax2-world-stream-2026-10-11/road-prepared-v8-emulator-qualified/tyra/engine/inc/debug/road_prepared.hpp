#pragma once
#include "debug/road_package_plan.hpp"
#ifndef WORLD_CELL_ENGINE
#define WORLD_CELL_ENGINE
#define ROAD_PREP_RESTORE_ENGINE_MACRO
#endif
#include "debug/road_package_sdk.hpp"
#ifdef ROAD_PREP_RESTORE_ENGINE_MACRO
#undef ROAD_PREP_RESTORE_ENGINE_MACRO
#undef WORLD_CELL_ENGINE
#endif
#include "debug/world_runtime.hpp"
#include "renderer/core/3d/bbox/core_bbox.hpp"
#include <cstdio>

namespace RoadPrepared {
inline bool active=false,audit=false;
inline unsigned compiled=0,failed=0,inside=0,partial=0,outside=0;
inline unsigned emitted=0,checks=0,unready=0;
inline unsigned wholeReplayed=0,wholeFallback=0;
struct Fragment {
  RoadPackageSdk::Body sdk;
  unsigned offset=0,qwords=0,first=0;
};
struct Package {
  Fragment cull;
  std::vector<Fragment> clips;
};
struct Selection {RoadPackagePlan::Route route;unsigned mask;};
struct Owner {
  Tyra::CoreBBox bounds;
  Tyra::M4x4 model;
  RoadPackagePlan::Plan<Tyra::Vec4> plan;
  std::vector<Package> packages;
  std::vector<Selection> selected;
  std::unique_ptr<WorldRuntime::Owner> geometry;
  WorldOwner::Key key;
  const void *vertices=nullptr,*colors=nullptr,*sts=nullptr;
  unsigned capacity=0,bufferSize=0,primitive=0;
};
inline void reset(){compiled=failed=inside=partial=outside=emitted=checks=unready=wholeReplayed=wholeFallback=0;}
inline void witness(unsigned phase,int arm=-1) {
  std::printf("ROADPREP phase=%u arm=%d active=%u compiled=%u failed=%u inside=%u partial=%u outside=%u emitted=%u checks=%u unready=%u\n",
    phase,arm,unsigned(active),compiled,failed,inside,partial,outside,emitted,checks,unready);
  std::fflush(stdout);
  std::printf("ROADWHOLE phase=%u arm=%d active=%u replayed=%u fallback=%u\n",
    phase,arm,unsigned(active),wholeReplayed,wholeFallback);
  std::fflush(stdout);
}
}
