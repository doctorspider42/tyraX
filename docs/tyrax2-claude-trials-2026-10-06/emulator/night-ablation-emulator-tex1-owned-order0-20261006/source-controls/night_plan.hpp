#pragma once
#include "night_sampler.hpp"
#include <stdio.h>
#include <ctype.h>
namespace NightPlan {
struct Config {unsigned kind,order,joint,restored;};
inline bool valid(const Config& c){return c.order<=1&&((c.kind==0&&c.joint==0&&c.restored==0)||(c.kind==1&&c.joint==31&&c.restored==0)||(c.kind==2&&c.joint==31&&c.restored>0&&(c.restored&~c.joint)==0)||(c.kind==3&&c.joint==0&&c.restored>0&&c.restored<=7)||((c.kind==5||c.kind==6||c.kind==7||c.kind==9||c.kind==28)&&c.joint==0&&c.restored==0));}
inline bool readConfig(const char* path,Config& cfg){FILE* f=fopen(path,"r");if(!f)return false;unsigned values[4]{};bool good=true;int c=fgetc(f);
 for(unsigned i=0;i<4;++i){while(c!=EOF&&isspace(static_cast<unsigned char>(c)))c=fgetc(f);if(c<'0'||c>'9'){good=false;break;}unsigned n=0;do{unsigned digit=unsigned(c-'0');if(n>(0xffffffffu-digit)/10u){good=false;break;}n=n*10u+digit;c=fgetc(f);}while(c>='0'&&c<='9');if(!good)break;values[i]=n;if(c!=EOF&&!isspace(static_cast<unsigned char>(c))){good=false;break;}}
 while(good&&c!=EOF){if(!isspace(static_cast<unsigned char>(c)))good=false;c=fgetc(f);}if(ferror(f))good=false;fclose(f);cfg={values[0],values[1],values[2],values[3]};return good&&valid(cfg);}
inline bool build(const Config& c,NightSampler::PhaseConfig (&out)[3]){if(!valid(c))return false;for(unsigned p=0;p<3;++p){bool middle=(p==1)!=(c.order==1);out[p]=c.kind==0?NightSampler::PhaseConfig{0,middle?1u:0u}:c.kind==1?NightSampler::PhaseConfig{middle?c.joint:0u,1}:(c.kind==3||c.kind==5||c.kind==6||c.kind==7||c.kind==9||c.kind==28)?NightSampler::PhaseConfig{0,1}:NightSampler::PhaseConfig{middle?(c.joint&~c.restored):c.joint,1};}return true;}
inline unsigned extraMask(const Config& c,unsigned p){if(c.kind!=3)return 0;bool middle=(p==1)!=(c.order==1);return middle?c.restored:0;}
inline unsigned wildVariant(const Config& c){return(c.kind==5||c.kind==6)?c.kind:0;}
inline bool wildEnabled(const Config& c,unsigned p){return wildVariant(c)&&((p==1)!=(c.order==1));}
inline bool poolTableEnabled(const Config& c,unsigned p){return c.kind==9 || c.kind==28 || (c.kind==7&&((p==1)!=(c.order==1)));}
inline bool coronaSpriteEnabled(const Config& c,unsigned p){return c.kind==9&&p<3&&((p==1)!=(c.order==1));}
}
