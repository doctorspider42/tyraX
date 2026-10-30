// Private shared EE pose arithmetic for the wide/pooled storage experiment.
#pragma once
static_assert(TYRA_COLLISION_VU0==0,"shared pose reference must remain COP2-free");
namespace Vehicle_playground { namespace StepPoseReference {
using namespace runtime_detail;
inline void transform(const float* matrix,RbV position,const float* local,int count,RbV* out){
  static decltype(&Tyra::collisionTransformPoints) volatile routine=&Tyra::collisionTransformPoints;
  routine(matrix,position,local,count,out);
}
inline void boxKernel(const float* local,int count,const float* matrix,RbV position,Tyra::CollisionObb& out){
  float lo[3]={1e30F,1e30F,1e30F},hi[3]={-1e30F,-1e30F,-1e30F};
  for(int v=0;v<count;++v)for(int k=0;k<3;++k){
    lo[k]=std::min(lo[k],local[v*3+k]);hi[k]=std::max(hi[k],local[v*3+k]);
  }
  out.centre=rbAdd(position,rbMat(matrix,rbV((lo[0]+hi[0])*.5F,
      (lo[1]+hi[1])*.5F,(lo[2]+hi[2])*.5F)));
  for(int k=0;k<3;++k)out.halfAxes[k]=rbMul(
      rbV(matrix[k],matrix[k+3],matrix[k+6]),(hi[k]-lo[k])*.5F);
}
inline void box(const float* local,int count,const float* matrix,RbV position,Tyra::CollisionObb& out){
  static decltype(&boxKernel) volatile routine=&boxKernel;
  routine(local,count,matrix,position,out);
}
} }
