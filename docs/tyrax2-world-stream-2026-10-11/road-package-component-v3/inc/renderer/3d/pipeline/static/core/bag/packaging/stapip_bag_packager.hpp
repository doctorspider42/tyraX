#pragma once
#include "renderer/3d/pipeline/static/core/bag/packaging/stapip_bag_packages_bbox.hpp"
#include "math/plane.hpp"
#include <vector>
namespace Tyra {
struct Renderer3DFrustumPlanes;
struct StaPipTextureBag {const Vec4* coordinates=nullptr;};
struct StaPipColorBag {const Vec4* many=nullptr;};
struct StaPipLightingBag {const Vec4* normals=nullptr;};
struct StaPipBag {
 u32 count=0;const Vec4* vertices=nullptr;
 StaPipTextureBag* texture=nullptr;StaPipColorBag* color=nullptr;
 StaPipLightingBag* lighting=nullptr;
};
struct StaPipBagPackage {
 StaPipBag* bag=nullptr;const Vec4 *vertices=nullptr,*sts=nullptr,*colors=nullptr,*normals=nullptr;
 u16 size=0;u32 indexOf1By3BBox=0,endIndexOf1By3BBox=0;
 u8 clipPlaneMask=0;bool guardBandOnly=false;CoreBBoxFrustum isInFrustum=OUTSIDE_FRUSTUM;
};
class StaPipBagPackager {
 public:
 Renderer3DFrustumPlanes* frustumPlanes=nullptr;u32 maxVertCount=0;
 StaPipBagPackagesBBox* renderBBox=nullptr;
 const Plane *objectSpacePlanes=nullptr,*clipObjectSpacePlanes=nullptr;
 bool capturePlaneMasks=false;
 std::vector<StaPipBagPackage> bagPackagesPool,splitPackagesPool;
 StaPipBagPackager();~StaPipBagPackager();void init(Renderer3DFrustumPlanes*);
 StaPipBagPackage* create(u16*,StaPipBag*,u16);
 StaPipBagPackage* create(u16*,const StaPipBagPackage&,u16);
 CoreBBoxFrustum checkFrustum(const StaPipBagPackage&,u8*,bool*);
 void setMaxVertCount(const u32&);
};
}
