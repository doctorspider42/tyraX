"""Host controls for the private plan cache; types are shims, algorithm is actual."""
import importlib.util
import json
import subprocess
import tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
cache=(ROOT/'tools/tyrax2-static-plan-cache.hpp').read_text()
cache='\n'.join(line for line in cache.splitlines() if not line.startswith('#include "'))
prefix=r'''
#include <cstring>
#include <cassert>
#include <cstdio>
#define STATIC_PLAN_VERIFY 1
namespace Tyra {
enum {IN_FRUSTUM,OUTSIDE_FRUSTUM,PARTIALLY_IN_FRUSTUM};
struct Vec{float x=0,y=0,z=0;};struct Plane{Vec normal;float distance=0;};
struct StaPipBagPackage{
const void*bag=nullptr,*vertices=nullptr,*sts=nullptr,*colors=nullptr,*normals=nullptr;
unsigned size=0,isInFrustum=0,clipPlaneMask=0,guardBandOnly=0,indexOf1By3BBox=0,endIndexOf1By3BBox=0;
};
}
namespace MotionProbe {
enum{Packages,Count};unsigned counters[Count]{},slots=0,descHits=0,viewHits=0,checks=0,errors=0,cumulativeChecks=0,cumulativeErrors=0,misses=0,resident=0,packagesIn=0,packagesOut=0,packagesPartial=0,offset=0,mode=0;
bool target=true,valid=true,sample=false,enabled=true;
unsigned variant(){return mode;}void add(unsigned c,unsigned n){counters[c]+=n;}
}
'''
tests=r'''
int main(){using namespace MotionProbe;using namespace Tyra;
  StaPipBagPackage original[2];original[0].size=3;original[1].size=6;
  StaticPlan::Key key{};key.count=9;key.size=6;key.maximum=6;
  Plane view[6]{},clip[8]{};unsigned short count=0;unsigned builds=0,preparedBuilds=0;
  auto build=[&](StaPipBagPackage*ready){++builds;count=2;if(ready){++preparedBuilds;return ready;}return original;};
  auto run=[&](){slots=0;return StaticPlan::run(&count,key,view,clip,true,build);};
  run();assert(resident==2*sizeof(StaPipBagPackage));
  sample=true;mode=1;run();assert(descHits==1&&preparedBuilds==1&&cumulativeErrors==0);
  mode=2;run();assert(viewHits==1&&cumulativeErrors==0);
  clip[7].distance=1;run();assert(viewHits==1&&preparedBuilds==2);
  run();assert(viewHits==2);
  key.bboxVersion++;run();assert(misses==1);
  key.vertices=reinterpret_cast<void*>(16);run();assert(misses==2);
  key.first++;run();assert(misses==3);
  auto bad=original[0];bad.endIndexOf1By3BBox++;assert(!StaticPlan::samePackage(bad,original[0]));
  bad=original[0];bad.clipPlaneMask++;assert(!StaticPlan::samePackage(bad,original[0]));
  bad=original[0];bad.guardBandOnly=1;assert(!StaticPlan::samePackage(bad,original[0]));
  entries_unused: ;
  StaticPlan::entries[0].records[0].size=99;run();assert(cumulativeErrors==1&&!valid);
  printf("PASS actual plan-cache host controls; shim types, no DMA emulation\n");
}
'''
with tempfile.TemporaryDirectory() as folder:
    p=Path(folder);(p/'test.cpp').write_text(prefix+cache+tests)
    subprocess.run(['g++','-std=c++17','-O1','-g','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)

spec=importlib.util.spec_from_file_location('analyzer',ROOT/'tools/tyrax2-static-plan-analyze.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
# Completed target inputs are supplied separately, then mutated without rewriting them.
import sys,csv,copy
if len(sys.argv)>2:
    rows=[{k:int(v) for k,v in r.items()} for r in csv.DictReader(Path(sys.argv[1]).open())]
    log=Path(sys.argv[2]).read_text(errors='replace');oracle='--oracle' in sys.argv
    module.analyze(rows,log,oracle)
    mutations=[('missingRow',None,None),('accounting','whole',0),('inactive','descHits',0),('viewLeak','viewHits',1),('geometry','packages',999),('warmMiss','misses',1),('badResult','errors',1),('clockLeak','coreTicks',1)]
    result={'reference':True}
    for name,key,value in mutations:
        bad=copy.deepcopy(rows)
        if key is None:bad.pop()
        else:bad[(6 if name=='clockLeak' else 0 if name=='viewLeak' else 1)*180][key]=value
        try:module.analyze(bad,log,oracle)
        except AssertionError:result[name]=False
        else:raise AssertionError('Accepted '+name)
    for name,badlog in [('missingDone',log.replace('STATICDONE','REMOVED')),('cameraDrift',log.replace('eye=0.000000','eye=1.000000',1)),('coldFailure',log.replace('errors=0','errors=1',1))]:
        try:module.analyze(rows,badlog,oracle)
        except AssertionError:result[name]=False
        else:raise AssertionError('Accepted '+name)
    print(json.dumps(result))
