#include <cstdio>
#include <cstdlib>
#include <climits>
#include "stapip_pool_color_packet.hpp"
using namespace Tyra::ExperimentalPoolTable;
static unsigned checks=0,cases=0;
static void need(bool v){++checks;if(!v){std::fprintf(stderr,"FAIL check=%u case=%u\n",checks,cases);std::exit(1);}}
int main(){ColorBits members[16];for(unsigned m=0;m<16;++m){members[m]={{0x42000000u+m*0x1000u,0x42100000u+m*0x1000u,0x42200000u+m*0x1000u,0x43000000u}};}
 Admission yes{true,true,true,true,true};
 for(unsigned m=1;m<=16;++m){std::vector<ColorBits> baseline;for(unsigned r=0;r<m;++r)for(unsigned v=0;v<96;++v)baseline.push_back(members[r]);
  for(unsigned offset=0;offset<m*96;offset+=3)for(unsigned n=3;n<=75 && n<=m*96-offset;n+=3){++cases;Slice s{};need(describe(members,m,m*96,offset,n,yes,s));need(expansionMatches(s,baseline.data()+offset,n));need(totalExtent(n)<=460);
   // Independent source-level interpreter for the VU's triangle-granularity color pointer.
   int remaining=(int)s.descriptor[0];unsigned index=0;
   for(unsigned tri=0;tri<n/3;++tri){for(unsigned k=0;k<3;++k)need(std::memcmp(&s.colors[index],&baseline[offset+tri*3+k],16)==0);remaining-=3;if(remaining==0)++index;}
   packet2_t packet;unsigned v=1,st=2;appendInput(&packet,&v,&st,n,s);need(packet.writes.size()==3);need(packet.writes[0].reference&&packet.writes[0].source==&v&&packet.writes[0].address==2&&packet.writes[0].count==n&&packet.writes[0].top);
   need(packet.writes[1].reference&&packet.writes[1].source==&st&&packet.writes[1].address==2+n&&packet.writes[1].count==n&&packet.writes[1].top);
   need(!packet.writes[2].reference&&packet.writes[2].address==2+2*n&&packet.writes[2].count==3&&packet.writes[2].top);
   need(std::memcmp(packet.writes[2].words.data(),&s,48)==0);Slice saved=s;std::memset(&s,0xcc,48);need(std::memcmp(packet.writes[2].words.data(),&saved,48)==0);
  }
 }
 Slice untouched;std::memset(&untouched,0xcc,48);const Slice original=untouched;
 const unsigned badCounts[]={0,1,2,74,76,96,UINT_MAX};for(unsigned n:badCounts){++cases;need(!describe(members,8,768,0,n,yes,untouched));need(std::memcmp(&untouched,&original,48)==0);}
 for(unsigned field=0;field<5;++field){Admission no=yes;if(field==0)no.inside=false;if(field==1)no.list=false;if(field==2)no.unlitTC=false;if(field==3)no.contiguous=false;if(field==4)no.currentGeneration=false;++cases;need(!describe(members,8,768,0,75,no,untouched));need(std::memcmp(&untouched,&original,48)==0);}
 ++cases;need(!describe(members,17,1632,0,75,yes,untouched));need(!describe(nullptr,8,768,0,75,yes,untouched));need(!describe(members,8,769,0,75,yes,untouched));need(!describe(members,8,768,UINT_MAX,75,yes,untouched));need(!describe(members,8,768,3,75,yes,untouched)==false);
 const uint32_t badBits[]={0x7fc00000u,0x7f800000u,0xff800000u,0xbf800000u};for(uint32_t b:badBits){ColorBits bad[1]={members[0]};bad[0].word[0]=b;++cases;need(!describe(bad,1,96,0,75,yes,untouched));}
 ColorBits badAlpha[1]={members[0]};badAlpha[0].word[3]=0x43010000u;++cases;need(!describe(badAlpha,1,96,0,75,yes,untouched));
 // Malformed descriptor and comparator must never admit a changed source color.
 Slice s{};need(describe(members,1,96,0,75,yes,s));ColorBits b[75];for(auto& x:b)x=members[0];need(expansionMatches(s,b,75));b[4].word[1]^=1;need(!expansionMatches(s,b,75));s.descriptor[0]=2;need(!expansionMatches(s,b,75));
 std::printf("PASS cases=%u checks=%u maxN=75 members=1..16 twoRuns=1 extent75=%u descriptorCopiedInline=1 sharedScratch=0 targetVUExecuted=0\n",cases,checks,totalExtent(75));
}
