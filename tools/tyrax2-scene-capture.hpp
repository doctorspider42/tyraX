// Private bounded recorder, injected inside MotionProbe. No target file I/O in captured frames.
namespace SceneCapture {
enum Owner { Unknown,Terrain,Roads,Batches,Objects,Vehicles,Effects,Sky,Procedural,Shadows,Lights };
enum Pass { Unspecified,Base,Layer,AO,GroundShadow,Emission,Environment,Shadow,Wheel,Glass,SkyBody,Proxy,Light,Effect };
inline U owner=Unknown, pass=Unspecified, count=0, clipCount=0, errors=0, frameCalls=0;
inline bool active(){return (phase==1 && (offset==1300||offset==1336||offset==1337||offset==1576||offset==1577)) ||
  (phase==4 && (offset==1380||offset==1381||offset==1500||offset==1501));}
struct OwnerScope { U old,oldPass; explicit OwnerScope(U value,U p=Unspecified):old(owner),oldPass(pass){owner=value;pass=p;} ~OwnerScope(){owner=old;pass=oldPass;} };
struct Record { U offset,bag,vertices,sourceCount,owner,id,pass,view,stripped,texture,version,c[Count],callsCull,callsClip; };
struct ClipRecord { U row,n,mask,planes[32],vertices[64*3]; };
inline Record records[4096]{};
inline ClipRecord clips[1024]{};
inline Record* currentRow=nullptr;
inline U planeBits[32]{};
inline bool planesValid=false;
inline FILE *bagsFile=nullptr,*clipsFile=nullptr;
inline U bits(float f){U u;memcpy(&u,&f,4);return u;}
inline void fail(){++errors;valid=false;}
template<class B> B* tag(B* bag,U own,U id,U pass){
  if(bag && active()){bag->auditOwner=own;bag->auditId=id;bag->auditPass=pass;}
  return bag;
}
struct Bag {
  Record* row=nullptr;
  template<class B> Bag(B* bag,U view){
    if(!active())return;
    ++frameCalls;
    if(currentRow||count==4096){fail();return;}
    row=&records[count++];*row={};
    row->offset=offset;row->bag=U(reinterpret_cast<unsigned long>(bag));
    row->vertices=U(reinterpret_cast<unsigned long>(bag->vertices));row->sourceCount=bag->count;
    row->owner=bag->auditOwner?bag->auditOwner:owner;row->id=bag->auditId;
    row->pass=bag->auditPass?bag->auditPass:pass;row->view=view;row->stripped=bag->stripped;
    row->texture=bag->texture?U(reinterpret_cast<unsigned long>(bag->texture->texture)):0;
    row->version=bag->bboxVersion;currentRow=row;planesValid=false;
  }
  ~Bag(){if(row)currentRow=nullptr;}
};
inline void note(Counter c,U n){if(currentRow)currentRow->c[c]+=n;}
template<class P> void planes(const P* p){
  if(!currentRow)return;
  for(U i=0;i<8;++i){planeBits[i*4]=bits(p[i].normal.x);planeBits[i*4+1]=bits(p[i].normal.y);
    planeBits[i*4+2]=bits(p[i].normal.z);planeBits[i*4+3]=bits(p[i].distance);}
  planesValid=true;
}
template<class B> void dispatch(B* buffer,bool clip){
  if(!currentRow)return;
  if(!clip){++currentRow->callsCull;return;}
  ++currentRow->callsClip;
  if(!planesValid||buffer->size>64||buffer->size%3||clipCount==1024){fail();return;}
  auto& r=clips[clipCount++];r.row=U(currentRow-records);r.n=buffer->size;r.mask=buffer->clipPlaneMask;
  memcpy(r.planes,planeBits,sizeof(planeBits));
  for(U i=0;i<r.n;++i){r.vertices[i*3]=bits(buffer->vertices[i].x);
    r.vertices[i*3+1]=bits(buffer->vertices[i].y);r.vertices[i*3+2]=bits(buffer->vertices[i].z);}
}
inline void begin(){frameCalls=0;if(currentRow)fail();}
inline void save(){if(currentRow || (active() && frameCalls!=counters[Bags]))fail();}
inline void open(){
  bagsFile=fopen("host:scene-bags.csv","w");clipsFile=fopen("host:scene-clips.csv","w");
  if(!bagsFile||!clipsFile){fail();return;}
  fprintf(bagsFile,"phase,row,offset,bag,vertices,sourceCount,owner,id,pass,view,stripped,texture,version,callsCull,callsClip");
  for(U i=0;i<Count;++i)fprintf(bagsFile,",c%u",i);fprintf(bagsFile,"\n");
  fprintf(clipsFile,"phase,row,n,mask");for(U i=0;i<32;++i)fprintf(clipsFile,",p%u",i);
  for(U i=0;i<192;++i)fprintf(clipsFile,",v%u",i);fprintf(clipsFile,"\n");
}
inline void dump(){
  if(!bagsFile||!clipsFile){fail();return;}
  for(U i=0;i<count;++i){const auto&r=records[i];
    fprintf(bagsFile,"%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u",phase,i,r.offset,r.bag,r.vertices,r.sourceCount,r.owner,r.id,r.pass,r.view,r.stripped,r.texture,r.version,r.callsCull,r.callsClip);
    for(U n:r.c)fprintf(bagsFile,",%u",n);fprintf(bagsFile,"\n");}
  for(U i=0;i<clipCount;++i){const auto&r=clips[i];fprintf(clipsFile,"%u,%u,%u,%u",phase,r.row,r.n,r.mask);
    for(U n:r.planes)fprintf(clipsFile,",%u",n);for(U j=0;j<192;++j)fprintf(clipsFile,",%u",j<r.n*3?r.vertices[j]:0);fprintf(clipsFile,"\n");}
  if(fflush(bagsFile)||fflush(clipsFile)||ferror(bagsFile)||ferror(clipsFile))fail();
  printf("SCENECAPTURE phase=%u bags=%u clips=%u errors=%u\n",phase,count,clipCount,errors);fflush(stdout);
  count=clipCount=0;
  if(phase+1==phaseCount){if(fclose(bagsFile)||fclose(clipsFile))fail();bagsFile=clipsFile=nullptr;}
}
}
