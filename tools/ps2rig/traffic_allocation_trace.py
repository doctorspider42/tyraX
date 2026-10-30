"""Install a private global-new failure trace and retain the unstripped ELF."""
from pathlib import Path
import argparse
import re
import subprocess
import zipfile

SOURCE = r'''
#include "debug/traffic_bench.hpp"
#include <cstdlib>
#include <cstdio>
#include <new>
#include <malloc.h>

namespace {
void* traceAllocate(std::size_t bytes,std::size_t alignment,void* caller){
  if(!bytes)bytes=1;
  for(;;){
    void* p=alignment?memalign(alignment,bytes):std::malloc(bytes);
    if(p)return p;
    std::printf("TRAFFICALLOC bytes=%u alignment=%u caller=%08lx phase=%d frame=%d stage=%d object=%d\n",
      unsigned(bytes),unsigned(alignment),reinterpret_cast<unsigned long>(caller),
      TrafficBench::phase,TrafficBench::frame,TrafficBench::allocationStage,TrafficBench::allocationOwner);
    std::fflush(stdout);
    if(const auto handler=std::get_new_handler())handler();
    else throw std::bad_alloc();
  }
}
}
__attribute__((noinline)) void* operator new(std::size_t n){return traceAllocate(n,0,__builtin_return_address(0));}
__attribute__((noinline)) void* operator new[](std::size_t n){return traceAllocate(n,0,__builtin_return_address(0));}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
#endif
#if defined(__cpp_aligned_new)
__attribute__((noinline)) void* operator new(std::size_t n,std::align_val_t a){return traceAllocate(n,std::size_t(a),__builtin_return_address(0));}
__attribute__((noinline)) void* operator new[](std::size_t n,std::align_val_t a){return traceAllocate(n,std::size_t(a),__builtin_return_address(0));}
void operator delete(void* p,std::align_val_t)noexcept{std::free(p);}
void operator delete[](void* p,std::align_val_t)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void* p,std::size_t,std::align_val_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t,std::align_val_t)noexcept{std::free(p);}
#endif
#endif
'''


def install(repo, project):
    common = Path(subprocess.check_output([
        'git', '-C', str(repo), 'rev-parse', '--path-format=absolute',
        '--git-common-dir'], text=True).strip())
    assert project.is_relative_to((common.parent/'.tmp').resolve())
    target = project/'src/traffic_alloc_trace.cpp'
    assert not target.exists()
    make = project/'Makefile'
    text = make.read_text(encoding='utf8')
    text, count = re.subn(r'(?m)^(KEEPSYM\s*:=)\s*0\s*$', r'\1 1', text)
    assert count == 1
    with zipfile.ZipFile(project.parent/'before-traffic-global-allocation.zip', 'x', zipfile.ZIP_DEFLATED) as z:
        z.write(make, 'Makefile')
    make.write_bytes(text.encode())
    target.write_bytes(SOURCE.encode())
    print('Installed diagnostic global-new/new[] trace; native build with KEEPSYM=1 required')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--project', required=True, type=Path)
    p.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[2])
    a = p.parse_args()
    install(a.repo.resolve(), a.project.resolve())
