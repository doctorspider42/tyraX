#include "renderer/3d/pipeline/shared/pipeline_transformation_type.hpp"
#include "renderer/3d/pipeline/shared/bag/pipeline_info_bag_frustum_culling.hpp"
namespace Tyra {
struct TicketInfo {M4x4* model;bool fullClipChecks;PipelineTransformationType transformationType;PipelineInfoBagFrustumCulling frustumCulling;bool fogDisabled=false;};
struct TicketColor {void* many;void* single;unsigned* contentVersion;};
struct TicketTexture {void* texture;Vec4* coordinates;unsigned* contentVersion;bool coordinatesAreNormals;};
struct StaPipBag {bool stripped;Vec4* vertices;unsigned* contentVersion;unsigned count,packageSize;TicketInfo* info;TicketColor* color;void* lighting;void* billboard;TicketTexture* texture;unsigned bboxVersion;WorldFront::Attachment worldTicket;};
static bool ticketShape(const StaPipBag* b){
  return b && b->stripped && b->vertices && b->contentVersion && b->count>=3 &&
    b->packageSize>=3 && b->packageSize%3==0 &&
    (b->count%b->packageSize==0 || b->count%b->packageSize>=3) &&
    b->info && b->info->model && b->info->fullClipChecks &&
    b->info->transformationType==TyraMVP &&
    b->info->frustumCulling==PipelineInfoBagFrustumCulling_Precise &&
    b->color && b->color->many && !b->color->single && b->color->contentVersion &&
    !b->lighting && !b->billboard &&
    (!b->texture || (b->texture->texture && b->texture->coordinates &&
      b->texture->contentVersion && !b->texture->coordinatesAreNormals));
}
static bool actualTicketMatches(const WorldFront::Owner& t,const StaPipBag* b,unsigned epoch,unsigned bufferSize){
  if(t.vertices!=b->vertices || t.colors!=b->color->many || t.count!=b->count ||
      t.run!=b->packageSize || t.bboxVersion!=b->bboxVersion || t.epoch!=epoch ||
      t.bufferSize!=bufferSize || t.textured!=(b->texture!=nullptr) ||
      t.coordinates!=(b->texture?b->texture->coordinates:nullptr) ||
      t.stamps[0]!=*b->contentVersion || t.stamps[1]!=*b->color->contentVersion ||
      t.stamps[2]!=(b->texture?*b->texture->contentVersion:0) ||
      memcmp(t.model.data,b->info->model->data,sizeof(t.model.data)))return false;
  return true;
}
}
