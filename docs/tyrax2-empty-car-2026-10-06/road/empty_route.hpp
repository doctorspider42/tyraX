#pragma once
#include <pad/pad.hpp>
namespace EmptyRoute {
inline float center(float z){const float zs[7]={-15,0,16,30,44,60,80},xs[7]={0,0,0,3,-3,0,0};for(unsigned i=1;i<7;++i){if(z<=zs[i]){const float t=(z-zs[i-1])/(zs[i]-zs[i-1]);return xs[i-1]+(xs[i]-xs[i-1])*t;}}return 0;}
inline void apply(Tyra::Pad& pad,unsigned frame,float x,float z,float yaw,float speed){
 const unsigned p=frame/1800U,o=frame%1800U;Tyra::PadButtons held={},clicked={};unsigned char h=127;
 if(p==1&&o>=800){held.R2=o<1056&&z<50;held.L2=!held.R2&&speed>0.1F;
  const float ahead=8,desired=(center(z+ahead)-x)/ahead*57.29578F;float error=(desired-yaw)/35.0F;if(error>0.65F)error=0.65F;if(error<-.65F)error=-.65F;
  const float magnitude=error<0?-error:error;if(magnitude>.04F){const int axis=int((.2F+.8F*magnitude)*127);h=(unsigned char)(128+(error>0?-axis:axis));}
 }
 clicked.R2=p==1&&o==800;pad.setState(held,clicked,h,127,127,127);
}
}
