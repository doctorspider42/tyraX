#pragma once
#include <vector>
#include <utility>
#include <algorithm>
#include <cstddef>
namespace StripPack {
// Preserve the order within each independent strip, including its provoking
// vertex. Only degenerate joins and two-vertex package overlaps are inserted.
inline std::vector<std::size_t> indices(const std::vector<std::pair<std::size_t,std::size_t>>& spans, unsigned run) {
 std::vector<std::size_t> out;
 if(run<6 || run%3)return out;
 std::size_t start=0;
 auto close=[&](bool last){if(out.size()==start)return;const std::size_t len=out.size()-start,target=last?((len+2)/3)*3:run;while(out.size()-start<target)out.push_back(out.back());start=out.size();};
 for(const auto& span:spans) {
  if(span.second<3 || span.second>run)return {};
  // Do not split an already full run just to append it after a short tail.
  if(span.second==run && out.size()!=start)close(false);
  std::size_t at=0;
  while(span.second-at>=3) {
   const std::size_t len=out.size()-start,join=len?2:0;
   if(len+join+3>run){close(false);continue;}
   const std::size_t take=std::min<std::size_t>(span.second-at,run-len-join);
   if(join){out.push_back(out.back());out.push_back(span.first+at);}
   for(std::size_t k=0;k<take;++k)out.push_back(span.first+at+k);
   if(take==span.second-at)break;
   at+=take-2;close(false);
  }
 }
 close(true);return out;
}
}
