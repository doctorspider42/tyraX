from pathlib import Path
import subprocess,json,hashlib
lab=Path('F:/Projects/tyrax2-lab-20261001');r=lab/'tyrax2-closeout-road-two-cars-light-buildings-80k-shadow-batch-v1';out=r/'sentinel-controls-v2';assert not out.exists();(out/'stub/debug').mkdir(parents=True);(out/'stub/debug/closeout.hpp').write_bytes(b'#pragma once\nnamespace Closeout{inline unsigned order=0;}\n')
(out/'controls.cpp').write_bytes(b'''#include "batch_trial.hpp"
#include <cassert>
#include <type_traits>
int main(){
 static_assert(sizeof(short)==2);static_assert(std::is_same<decltype(BatchTrial::originalMembership),std::vector<short>>::value);
 assert(BatchTrial::member(-2));assert(!BatchTrial::member(-1));assert(!BatchTrial::member(-3));assert(!BatchTrial::member(-32768));assert(BatchTrial::member(0));assert(BatchTrial::member(23));assert(BatchTrial::member(32767));
 std::vector<short> normal(55,-1);for(unsigned i=5;i<55;++i)normal[i]=-2;
 auto count=[](const std::vector<short>&v){unsigned n=0;for(unsigned i=5;i<55;++i)n+=BatchTrial::member(v[i]);return n;};
 BatchTrial::originalMembership=normal;assert(count(normal)==50);normal.assign(55,-1);assert(count(normal)==0);normal=BatchTrial::originalMembership;assert(count(normal)==50);for(unsigned i=5;i<55;++i)assert(normal[i]==-2);
 for(unsigned order=0;order<2;++order){Closeout::order=order;for(unsigned p=0;p<3;++p)assert(BatchTrial::desired(p*1800)==((p!=1)!=(order==1)));assert(BatchTrial::desired(5400)==BatchTrial::desired(3600));}
}
''')
linux=lambda p:'/mnt/f/'+str(p).replace('\\','/').split('F:/',1)[1]
cmd=['wsl','-d','Ubuntu','--','g++','-std=c++17','-O2','-I'+linux(out/'stub'),'-I'+linux(r/'empty-car/inc'),linux(out/'controls.cpp'),'-o',linux(out/'controls')];a=subprocess.run(cmd,capture_output=True);(out/'compile.stdout').write_bytes(a.stdout);(out/'compile.stderr').write_bytes(a.stderr);assert a.returncode==0,a.stderr
a=subprocess.run(['wsl','-d','Ubuntu','--',linux(out/'controls')],capture_output=True);assert a.returncode==0,a.stderr
h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();(out/'proof.json').write_bytes((json.dumps({'status':'PASS_ACTUAL_HEADER_MULTIBATCH_SENTINEL_AND_ROUTE_CONTROLS','actualHeaderSha256':h(r/'empty-car/inc/batch_trial.hpp'),'controlsSourceSha256':h(out/'controls.cpp'),'stubSha256':h(out/'stub/debug/closeout.hpp'),'binarySha256':h(out/'controls'),'runtimeQualification':False,'scope':'Actual private header membership(-2/index)/rejected negative sentinels, exact short snapshot type,50-owner OFF/restore preservation, both phase orders and postdone last phase. Stub only Closeout order; no game/PS2/device.'},indent=2)+'\n').encode());print('PASS_HEADER_SENTINELS')
