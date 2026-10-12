"""Exercise the actual fixture's runtime-switch/reservation function with host I/O shims."""
import argparse
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('fixture',type=Path);a=p.parse_args()
header=a.fixture/'game/inc/world_source_capture.hpp'
text=header.read_text()
start=text.index('namespace WorldSourceCapture {')
end=text.index('inline void capture(',start)
actual=text[start:end]+'}\n'
harness=r'''
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <cassert>
static FILE dummy{};
static std::string data;
static bool present=true;
static unsigned readOffset=0,opens=0;
static FILE* testOpen(const char* name,const char* mode){
  assert(std::string(name)=="host:source-control.txt" && std::string(mode)=="rb");
  ++opens;readOffset=0;return present?&dummy:nullptr;
}
static int testGet(FILE*){return readOffset<data.size()?static_cast<unsigned char>(data[readOffset++]):EOF;}
static int testClose(FILE*){return 0;}
#define fopen testOpen
#define fgetc testGet
#define fclose testClose
'''+actual+r'''
#undef fopen
#undef fgetc
#undef fclose
int main(){
  using namespace WorldSourceCapture;
  const char* modes[]={"0\n","1\n","0","1","x\n","1x","","0\r\n","0\n"};
  const bool expected[]={true,true,true,true,false,false,false,false,false};
  for(unsigned i=0;i<9;++i){
    configured=false;enabled=false;active=true;present=i!=8;data=modes[i];opens=0;
    begin(i);assert(valid==expected[i]);assert(stage==i && records==0 && bytes.empty());
    assert(bytes.capacity()>=limit && opens==1);
    if(expected[i]){
      const bool on=i==1 || i==3;assert(enabled==on && active==on);
      data=on?"0\n":"1\n";begin(i+10);
      assert(opens==1 && enabled==on && active==on && valid);
    }else assert(!active && !configured);
  }
}
'''
with tempfile.TemporaryDirectory() as tmp:
    root=Path(tmp);(root/'test.cpp').write_text(harness)
    for mode,flags in (('optimized',['-O3']),('sanitized',['-O1','-fsanitize=address,undefined'])):
        subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror',*flags,
                        str(root/'test.cpp'),'-o',str(root/'test')],check=True)
        subprocess.run([str(root/'test')],check=True)
report={'valid':True,'cases':9,'repeatedReadControls':4,'modes':['optimized','ASan/UBSan'],
        'headerSha256':hashlib.sha256(header.read_bytes()).hexdigest(),
        'limits':['Host I/O shims and extracted begin function, not whole target execution.']}
(a.fixture/'source-switch-host.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report))
