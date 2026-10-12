struct prim_t {unsigned fogging=0,extra=17;};
struct lod_t {unsigned value=21;};
constexpr unsigned DRAW_ENABLE=1,DRAW_DISABLE=0;
namespace ClipConsumer {inline bool observe=true;}
namespace ClipTrial {inline bool observe=true;}
namespace WorldRuntime {inline bool cold=false;}
namespace Tyra {
void M4x4::operator=(const M4x4& in){M4x4::copy(this,in);}
struct ProducerCache {
  Bounds* boxes=nullptr;bool fail=false,missing=false;unsigned calls=0;
  Bounds* getBBoxes(Vec4*,unsigned,unsigned,unsigned,unsigned){
    ++calls;if(fail)throw std::bad_alloc();return missing?nullptr:boxes;
  }
};
struct ProducerRenderer {
  struct Fog {bool enabled=true;} fog;
  struct Blss {bool active=false;bool wantsProxies()const{return active;}} blss;
};
struct ProducerQueue {
  prim_t* prim;lod_t* lod;
  bool clipping=true,overrides=false,ready=true,fail=false;
  unsigned calls=0;
  bool isVU1ClippingEnabled()const{return clipping;}
  bool hasProgramOverrides()const{return overrides;}
  unsigned getBufferSize()const{return 512;}
  void setInfo(TicketInfo*){prim->extra=99;lod->value=42;}
  bool prepareWorldCullBody(StaPipBag*,unsigned){
    ++calls;assert(!ClipConsumer::observe && !ClipTrial::observe);
    if(fail)throw std::bad_alloc();return ready;
  }
};
struct StaPipCore {
  prim_t prim;lod_t lod;ProducerCache cacher;ProducerRenderer renderer;
  ProducerRenderer* rendererCore=&renderer;ProducerQueue qbufferRenderer{&prim,&lod};
  unsigned getMaxVertCountByBag(StaPipBag*)const{return 6;}
  unsigned getWorldEpoch()const{return 7;}
  bool prepareWorldTicket(StaPipBag*);
};
}
