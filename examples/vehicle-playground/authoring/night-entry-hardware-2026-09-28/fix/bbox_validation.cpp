// Scratch-only acceptance probe against fresh canonical package bounds.
#include "renderer/3d/pipeline/static/core/stapip_bag_bboxes_cacher.hpp"
#include "file/file_utils.hpp"
#include <cstdio>
namespace Tyra {
extern unsigned budgetBBoxChecked,budgetBBoxFailures;
bool budgetCheckBBox(StaPipBagPackagesBBox*,const Vec4*,unsigned,unsigned);
void budgetValidateCache() {
  alignas(16) Vec4 vertices[300];
  for(unsigned i=0;i<300;++i)vertices[i].set((i*17%43)-21.0F,(i*7%31)-15.0F,(i*11%71)-35.0F,1);
  unsigned version=1,checks=0,failures=0,maxEntries=0;
  StapipBagBBoxesCacher cache;cache.budgetCountVariants=true;
  auto take=[&](unsigned count,unsigned pkg,unsigned id=123) {
    auto* box=cache.getBBoxes(vertices,count,id,version,pkg);
    ++checks;
    if(!budgetCheckBBox(box,vertices,count,pkg))++failures;
  };
  auto end=[&]() {cache.onFrameEnd(); if(cache.stats.entries>maxEntries)maxEntries=cache.stats.entries;};
  FILE* f=fopen(FileUtils::fromCwd("bbox-check.csv").c_str(),"w");
  if(f)fprintf(f,"case,checks,failures,entries,fresh,recalcs,hits\n");
  auto row=[&](const char* name) {
    if(f)fprintf(f,"%s,%u,%u,%u,%u,%u,%u\n",name,checks,failures,cache.stats.entries,cache.stats.fresh,cache.stats.recalcs,cache.stats.hits);
  };
  for(unsigned i=0;i<1000;++i){take(300,96);take(96,96);end();}
  if(cache.stats.entries!=2 || cache.stats.fresh!=2 || cache.stats.hits!=1998)++failures;
  row("stable_full_prefix_1000_frames");
  for(unsigned i=0;i<30;++i){++version;vertices[i].x+=100;take(300,96);take(96,96);end();}
  if(cache.stats.entries!=2 || cache.stats.recalcs!=60)++failures;
  row("dynamic_versions_same_buffer");
  for(unsigned i=0;i<900;++i){take(120+i%150,96);end();if(cache.stats.entries>2)++failures;}
  row("changing_count_bounded");
  take(300,192);take(96,192);end();
  if(cache.stats.entries>4)++failures;
  row("different_package_capacity");
  ++version;
  for(unsigned i=0;i<300;++i)vertices[i].z-=200;
  take(300,96);take(96,96);take(300,192);take(96,192);end();
  row("recycled_address_new_version");
  for(unsigned i=0;i<250;++i)end();
  if(cache.stats.entries!=0)++failures;
  row("all_entries_expire");
  take(300,96);take(96,96);take(240,96,321);end();
  for(unsigned i=0;i<250;++i){take(300,96);take(96,96);end();}
  if(cache.stats.entries!=2)++failures;
  take(240,96,321);end();
  row("index_rebuilt_after_partial_expiry");
  if(f){fprintf(f,"TOTAL,%u,%u,%u,%u,%u,%u\n",checks,failures,cache.stats.entries,cache.stats.fresh,cache.stats.recalcs,cache.stats.hits);fclose(f);}
}
}
