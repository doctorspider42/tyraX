#!/usr/bin/env python3
"""Exercise the actual EE normal-preparation body with colliding stable arrays.

Host mechanism test only: no PS2 timing or full-scene collision claim.
Run under WSL with a C++17 compiler; all generated files stay in a temp folder.
"""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'vendor/tyra/engine/src/renderer/3d/pipeline/static/core/stapip_core.cpp'


def main():
    source = SOURCE.read_text()
    begin = source.index('void StaPipCore::ensureEnvNormalsUnit(StaPipBag* bag) {')
    end = source.index('\nvoid StaPipCore::render(', begin)
    body = source[begin:end]
    assert body.count('sqrtf(l2)') == 1
    # Observe actual loop work without changing its cache key or arithmetic.
    body = body.replace('sqrtf(l2)', 'observedSqrt(l2)')
    harness = r'''
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
using u32 = uint32_t;
static unsigned roots;
float observedSqrt(float x) { ++roots; return std::sqrt(x); }
#define TYRA_LOG(...) ((void)0)
struct Vec4 { float x, y, z, w; };
struct Texture { Vec4* coordinates; const u32* contentVersion; };
struct StaPipBag { Texture* texture; u32 count, bboxVersion; };
struct StaPipCore {
  struct EnvNormalsSeen {
    const void* coordinates = nullptr;
    u32 count = 0, bboxVersion = 0, content = 0;
  };
  EnvNormalsSeen envNormalsSeen[32];
  void ensureEnvNormalsUnit(StaPipBag*);
};
''' + body + r'''
unsigned run(unsigned secondOffset, bool mutate) {
  alignas(512) Vec4 data[96];
  for (auto& n : data) n = {0.3f, 0.4f, 0.5f, 0};
  u32 version = 1;
  Texture ta{data, &version}, tb{data + secondOffset, &version};
  StaPipBag a{&ta, 16, 1}, b{&tb, 16, 1};
  StaPipCore core;
  roots = 0;
  for (unsigned frame = 0; frame < 100; ++frame) {
    if (mutate) ++version;
    core.ensureEnvNormalsUnit(&a);
    core.ensureEnvNormalsUnit(&b);
  }
  return roots;
}
int main() {
  const unsigned separate = run(17, false);
  const unsigned collision = run(32, false);
  const unsigned mutation = run(17, true);
  std::printf("{\"separate_sqrt_calls\":%u,\"collision_sqrt_calls\":%u,"
              "\"mutation_sqrt_calls\":%u,\"frames\":100}\n",
              separate, collision, mutation);
  return separate == 32 && collision == 3200 && mutation == 3200 ? 0 : 1;
}
'''
    with tempfile.TemporaryDirectory(prefix='tyrax-env-normal-') as tmp:
        cpp = Path(tmp) / 'test.cpp'
        exe = Path(tmp) / 'test'
        cpp.write_text(harness)
        subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', '-Wextra',
                        '-fsanitize=address,undefined', str(cpp), '-o', str(exe)], check=True)
        result = subprocess.run([str(exe)], check=True, capture_output=True, text=True)
    report = json.loads(result.stdout)
    report['source_sha256'] = hashlib.sha256(SOURCE.read_bytes()).hexdigest()
    report['scope'] = 'host mechanism only; no physical performance claim'
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
