size_t checks=0,flushes=0,colorOnly=0,partialRanges=0;
void demand(bool b,const std::string& msg){++checks;if(!b) throw std::runtime_error(msg);}
template<class T> std::vector<unsigned char> bytes(const T* p,size_t n){auto* b=reinterpret_cast<const unsigned char*>(p);return std::vector<unsigned char>(b,b+n*sizeof(T));}
struct State {unsigned vs,ss,cs,bs,legacyCs;std::vector<unsigned char> v,s,c;std::vector<uint32_t> ready;};
State state(Candidate::TerrainGame& g){auto& p=g.poolBatch_;return {p.verts.stamp(),p.sts.stamp(),p.colorBag?*p.colorBag->contentVersion:0,p.bag?p.bag->bboxVersion:0,p.colors.stamp(),bytes(p.verts.data(),p.verts.size()),bytes(p.sts.data(),p.sts.size()),bytes(p.lazyColors.data(),p.lazyColors.size()),p.lazyReady};}
void pairFlush(Legacy::TerrainGame& a,Candidate::TerrainGame& b,std::vector<LightPool>& pools,const std::vector<int>& order,const std::vector<float>& fix,bool arm,const std::string& mode){
 auto before=state(b);NightAblation::poolTableEnabled=arm;
 for(size_t i=0;i<order.size();++i){a.poolBatchAdd(pools[order[i]],fix[i]);b.poolBatchAdd(pools[order[i]],fix[i]);}
 a.poolBatchFlush();if(mode=="wrap")g_contentStamp=std::numeric_limits<unsigned int>::max();b.poolBatchFlush();++flushes;
 auto& ap=a.poolBatch_;auto& bp=b.poolBatch_;auto after=state(b);
 demand(ap.bag->count==bp.bag->count,"count");
 demand(bytes(ap.bag->vertices,ap.bag->count)==bytes(bp.bag->vertices,bp.bag->count),"positions");
 demand(bytes(ap.texBag->coordinates,ap.bag->count)==bytes(bp.texBag->coordinates,bp.bag->count),"ST");
 demand(bp.bag->contentVersion==bp.verts.stampPtr()&&bp.texBag->contentVersion==bp.sts.stampPtr(),"geometry stamp binding");
 bool compact=arm&&order.size()<=16&&std::all_of(order.begin(),order.end(),[&](int i){return pools[i].verts.size()==96&&pools[i].sts.size()==96;});
 if(mode=="warm"){
  demand(before.vs==after.vs&&before.ss==after.ss&&before.cs==after.cs&&before.bs==after.bs,"warm stamps");
  demand(before.v==after.v&&before.s==after.s&&before.c==after.c&&before.ready==after.ready,"warm bytes");
 }
 if(mode=="color"&&compact){++colorOnly;
  demand(before.vs==after.vs&&before.ss==after.ss&&before.bs==after.bs,"compact color geometry stamps");
  demand(before.v==after.v&&before.s==after.s&&before.c==after.c&&before.ready==after.ready,"compact color wrote geometry/backing");
  demand(before.legacyCs==after.legacyCs,"compact color touched legacy pervertex BagArray");
  demand(before.cs!=after.cs,"color version advancement");
 }
 if(mode=="geometry"||mode=="order") demand(before.bs!=after.bs,"geometry bbox must invalidate");
 demand(bp.bag->info->additiveBlendFix==128&&!bp.bag->info->dateLit&&bp.bag->info->shadingType==TyraShadingGouraud,"render info");
 if(compact){
  demand(bp.colorBag->contentVersion==&bp.lazyGeneration&&bp.tableView.generation==bp.lazyGeneration,"lazy generation binding");
  auto old=bytes(bp.lazyColors.data(),bp.lazyColors.size());auto ready=bp.lazyReady;
  size_t off=bp.bag->count>96?90:3,count=std::min<size_t>(75,bp.bag->count-off);
  demand(ExperimentalPoolTable::materializeRange(&bp.tableView,bp.colorBag->many+off,static_cast<uint32_t>(count)),"partial fill");++partialRanges;
  for(size_t k=0;k<bp.bag->count;++k){
   if(k>=off&&k<off+count){demand(std::memcmp(bp.colorBag->many+k,ap.colorBag->many+k,16)==0,"partial color");demand(bp.lazyReady[k]==bp.lazyGeneration,"partial ready");}
   else {demand(std::memcmp(bp.colorBag->many+k,old.data()+k*16,16)==0&&bp.lazyReady[k]==ready[k],"partial touched outside");}
  }
  auto partial=bytes(bp.lazyColors.data(),bp.lazyColors.size());
  demand(ExperimentalPoolTable::materializeRange(&bp.tableView,bp.colorBag->many+off,static_cast<uint32_t>(count)),"repeat partial");
  demand(partial==bytes(bp.lazyColors.data(),bp.lazyColors.size()),"repeat partial bytes");
  demand(ExperimentalPoolTable::materializeRange(&bp.tableView,bp.colorBag->many,bp.bag->count),"full fill");
 } else demand(bp.colorBag->contentVersion==bp.colors.stampPtr(),"legacy color stamp binding");
 demand(bytes(ap.colorBag->many,ap.bag->count)==bytes(bp.colorBag->many,bp.bag->count),"expanded coefficients");
 demand(bp.members.empty()&&bp.memberFix.empty(),"cleared submission");
}
int main(){try {
 for(int n=1;n<=16;++n){
  std::vector<LightPool> pools(static_cast<size_t>(n)); std::vector<int> order;std::vector<float> fix;
  for(int m=0;m<n;++m){order.push_back(m);fix.push_back(static_cast<float>((m*17)%129));pools[m].color=Color(12.5F+m,64.0F-m,127.0F-m,128);
   for(int k=0;k<96;++k){pools[m].verts.push_back(Vec4{float(m),float(k),float(m+k),1});pools[m].sts.push_back(Vec4{float(k)/96,float(m)/16,1,0});}}
  Legacy::TerrainGame a;Candidate::TerrainGame b;
  for(bool arm:{false,true,false,true}){
   pairFlush(a,b,pools,order,fix,arm,"flip");pairFlush(a,b,pools,order,fix,arm,"warm");
   for(int j=0;j<8;++j){if(j%2)pools[j%n].color.r+=0.25F;else fix[j%n]+=0.5F;pairFlush(a,b,pools,order,fix,arm,"color");pairFlush(a,b,pools,order,fix,arm,"warm");}
   pools[0].verts[7].y+=0.5F;pairFlush(a,b,pools,order,fix,arm,"geometry");
   pools[n-1].sts[11].x+=0.03125F;pairFlush(a,b,pools,order,fix,arm,"geometry");
   if(n>1){std::reverse(order.begin(),order.end());pairFlush(a,b,pools,order,fix,arm,"order");}
  }
  pools[0].color.g+=0.125F;pairFlush(a,b,pools,order,fix,true,"wrap");
  demand(b.poolBatch_.lazyGeneration==1,"wrapped generation is nonzero");
  // Shape refusal retains ordinary per-vertex output, with a short paired stream.
  pools[0].verts.resize(93);pools[0].sts.resize(93);pairFlush(a,b,pools,order,fix,true,"geometry");
 }
 std::cout<<"PASS checks="<<checks<<" flushes="<<flushes<<" compact_color_only="<<colorOnly<<" partial_ranges="<<partialRanges<<"\n";
 return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL after "<<flushes<<" flushes: "<<e.what()<<"\n";return 1;}}
