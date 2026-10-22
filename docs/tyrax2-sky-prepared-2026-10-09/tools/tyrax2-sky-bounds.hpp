// Private A03 candidate: conservative bounds of the actual small list slice.
#pragma once
#include <math.h>
namespace SkyBounds {
enum Route { Keep, Cull, Outside };
template<class V,class P> Route classify(const V* vertices,unsigned n,const P* planes) {
  if(!n || !vertices || !planes)return Keep;
  float lo[3]={vertices[0].x,vertices[0].y,vertices[0].z};
  float hi[3]={lo[0],lo[1],lo[2]};
  for(unsigned i=0;i<n;++i){
    const float v[3]={vertices[i].x,vertices[i].y,vertices[i].z};
    for(unsigned j=0;j<3;++j){
      if(!isfinite(v[j]))return Keep;
      if(v[j]<lo[j])lo[j]=v[j];if(v[j]>hi[j])hi[j]=v[j];
    }
  }
  bool inside=true,outside=false;
  for(unsigned i=0;i<8;++i){
    const auto& p=planes[i];const float a[3]={p.normal.x,p.normal.y,p.normal.z};
    float mn=p.distance,mx=p.distance,scale=fabsf(p.distance);
    if(!isfinite(p.distance))return Keep;
    for(unsigned j=0;j<3;++j){
      if(!isfinite(a[j]))return Keep;
      mn+=a[j]*(a[j]>=0?lo[j]:hi[j]);mx+=a[j]*(a[j]>=0?hi[j]:lo[j]);
      const float m=fabsf(lo[j])>fabsf(hi[j])?fabsf(lo[j]):fabsf(hi[j]);
      scale+=fabsf(a[j])*m;
    }
    const float margin=1e-5F*(1.0F+scale);
    if(!isfinite(mn)||!isfinite(mx)||!isfinite(margin))return Keep;
    if(!(mn>margin))inside=false;
    if(i<6 && mx < -margin)outside=true;
  }
  return outside?Outside:(inside?Cull:Keep);
}
// Independent cold witness over vertices, not box corners or the classifier's sums.
template<class V,class P> bool verify(const V* vertices,unsigned n,const P* p,Route r){
  if(r==Keep)return true;
  if(r==Cull){
    for(unsigned i=0;i<n;++i)for(unsigned j=0;j<8;++j){
      const double d=double(p[j].normal.x)*vertices[i].x+double(p[j].normal.y)*vertices[i].y+
        double(p[j].normal.z)*vertices[i].z+p[j].distance;
      if(!(d>0))return false;
    }
    return true;
  }
  for(unsigned j=0;j<6;++j){
    bool all=true;
    for(unsigned i=0;i<n;++i){
      const double d=double(p[j].normal.x)*vertices[i].x+double(p[j].normal.y)*vertices[i].y+
        double(p[j].normal.z)*vertices[i].z+p[j].distance;
      if(!(d<0))all=false;
    }
    if(all)return true;
  }
  return false;
}
}
