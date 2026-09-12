/*
# Modified by TyraX - persistent qbuffer pools.
# The original allocated and freed up to four Vec4 arrays PER FILL CALL,
# per subpackage, per frame. Arrays are now allocated once per buffer at
# maxVertCount capacity and reused. The fillByCopy* paths copy whole arrays
# with memcpy instead of a per-vertex Vec4::copy call (plus four branch
# checks) per element - these copies run for every clip-classified
# subpackage, every frame. Based on the original by
# Sandro Sobczynski (h4570/tyra), Apache License 2.0.
*/

#include "renderer/3d/pipeline/static/core/stapip_qbuffer.hpp"
#include <string.h>
#include <sstream>
#include <iomanip>

namespace Tyra {

namespace {

struct QBufferPool {
  const void* owner = nullptr;
  Vec4* vertices = nullptr;
  Vec4* sts = nullptr;
  Vec4* colors = nullptr;
  Vec4* normals = nullptr;
  u16 capacity = 0;
};

constexpr int kMaxPools = 32;

// Modified by TyraX: TWO pools per slot, used alternately. A slot's arrays are
// what the packet's REF tags point at, and the renderer hands the slot to the
// next bag as soon as its packet is SENT - the DMA is still reading the arrays
// while the next bag's copy lands in them. The side flips together with the
// renderer's packet double buffer (StaPipQBufferRenderer::sendPacket flips
// both), and that send waits for the previous DMA first, so the side being
// written is always the one whose transfer has finished. Same guarantee the
// packet buffers already had, extended to the data they reference. Measured
// on a real PS2: without it the last small bag of a run (a lamp's corona) came
// out as a sliver to the screen corner in 4-19 of 30 frames, with an EE-side
// wait per bag it cost 4 FPS; this costs nothing (docs/vu1-clipping.md).
QBufferPool pools[2][kMaxPools];
int g_poolSide = 0;

QBufferPool* poolFor(const void* owner, int side) {
  QBufferPool* set = pools[side];
  for (int i = 0; i < kMaxPools; i++)
    if (set[i].owner == owner) return &set[i];
  for (int i = 0; i < kMaxPools; i++)
    if (set[i].owner == nullptr) {
      set[i].owner = owner;
      return &set[i];
    }
  return nullptr;  // more buffers than pools - fall back to plain new[]
}

QBufferPool* poolFor(const void* owner) { return poolFor(owner, g_poolSide); }

}  // namespace

StaPipQBuffer::StaPipQBuffer() {
  size = 0;
  clipPlaneMask = 0;
  _isDynamicallyAllocated = false;
  _stAllocated = false;
  _colorAllocated = false;
  _normalAllocated = false;

  vertices = nullptr;
  colors = nullptr;
  sts = nullptr;
  normals = nullptr;
}

StaPipQBuffer::~StaPipQBuffer() { deallocateDynamicData(); }

void StaPipQBuffer::flipPoolSide() { g_poolSide ^= 1; }

void StaPipQBuffer::setMaxVertCount(const u32& count) { maxVertCount = count; }

void StaPipQBuffer::fillByPointer(const StaPipBagPackage& pkg) {
  TYRA_ASSERT(pkg.size <= maxVertCount, "VU1 buffer supports only ",
              maxVertCount, " verts. Provided: ", pkg.size);

  deallocateDynamicData();

  // Too bad, but in reality const is not violated.
  // This class needs refactor
  vertices = const_cast<Vec4*>(pkg.vertices);
  sts = const_cast<Vec4*>(pkg.sts);
  colors = const_cast<Vec4*>(pkg.colors);
  normals = const_cast<Vec4*>(pkg.normals);
  size = pkg.size;
  clipPlaneMask = pkg.clipPlaneMask;
  bag = pkg.bag;
}

void StaPipQBuffer::fillByPointer(StaPipBag* source, u32 offset, u32 count) {
  TYRA_ASSERT(count <= maxVertCount, "VU1 buffer supports only ",
              maxVertCount, " verts. Provided: ", count);

  deallocateDynamicData();
  vertices = source->vertices + offset;
  sts = source->texture ? source->texture->coordinates + offset : nullptr;
  colors = source->color->many
               ? const_cast<Vec4*>(reinterpret_cast<const Vec4*>(
                     source->color->many + offset))
               : nullptr;
  normals = source->lighting ? source->lighting->normals + offset : nullptr;
  size = count;
  clipPlaneMask = 0;
  bag = source;
}

void StaPipQBuffer::fillByCopyMax(const StaPipBagPackage& pkg1,
                                  const StaPipBagPackage& pkg2,
                                  const StaPipBagPackage& pkg3) {
  TYRA_ASSERT(pkg1.size <= maxVertCount / 3,
              "Wrong package size (1). Provided: ", pkg1.size);
  TYRA_ASSERT(pkg2.size <= maxVertCount / 3,
              "Wrong package size (2). Provided: ", pkg2.size);
  TYRA_ASSERT(pkg3.size <= maxVertCount / 3,
              "Wrong package size (3). Provided: ", pkg3.size);

  deallocateDynamicData();
  size = pkg1.size + pkg2.size + pkg3.size;
  clipPlaneMask =
      pkg1.clipPlaneMask | pkg2.clipPlaneMask | pkg3.clipPlaneMask;
  allocateDynamicData(size, pkg1.bag);

  const StaPipBagPackage* pkgs[3] = {&pkg1, &pkg2, &pkg3};
  u32 offset = 0;
  for (const auto* pkg : pkgs) {
    const u32 bytes = pkg->size * sizeof(Vec4);
    memcpy(vertices + offset, pkg->vertices, bytes);
    if (pkg1.bag->texture) memcpy(sts + offset, pkg->sts, bytes);
    if (pkg1.bag->color->many) memcpy(colors + offset, pkg->colors, bytes);
    if (pkg1.bag->lighting) memcpy(normals + offset, pkg->normals, bytes);
    offset += pkg->size;
  }

  bag = pkg1.bag;
}

void StaPipQBuffer::fillByCopy1By2(const StaPipBagPackage& pkg1,
                                   const StaPipBagPackage& pkg2) {
  TYRA_ASSERT(pkg1.size <= maxVertCount / 3,
              "Wrong package size (1). Provided: ", pkg1.size);
  TYRA_ASSERT(pkg2.size <= maxVertCount / 3,
              "Wrong package size (2). Provided: ", pkg2.size);

  deallocateDynamicData();
  size = pkg1.size + pkg2.size;
  clipPlaneMask = pkg1.clipPlaneMask | pkg2.clipPlaneMask;
  allocateDynamicData(size, pkg1.bag);

  const StaPipBagPackage* pkgs[2] = {&pkg1, &pkg2};
  u32 offset = 0;
  for (const auto* pkg : pkgs) {
    const u32 bytes = pkg->size * sizeof(Vec4);
    memcpy(vertices + offset, pkg->vertices, bytes);
    if (pkg1.bag->texture) memcpy(sts + offset, pkg->sts, bytes);
    if (pkg1.bag->color->many) memcpy(colors + offset, pkg->colors, bytes);
    if (pkg1.bag->lighting) memcpy(normals + offset, pkg->normals, bytes);
    offset += pkg->size;
  }

  bag = pkg1.bag;
}

void StaPipQBuffer::fillByCopy1By3(const StaPipBagPackage& pkg) {
  TYRA_ASSERT(pkg.size <= maxVertCount / 3,
              "Wrong package size (1). Provided: ", pkg.size);

  deallocateDynamicData();
  size = pkg.size;
  clipPlaneMask = pkg.clipPlaneMask;
  allocateDynamicData(size, pkg.bag);

  const u32 bytes = pkg.size * sizeof(Vec4);
  memcpy(vertices, pkg.vertices, bytes);
  if (pkg.bag->texture) memcpy(sts, pkg.sts, bytes);
  if (pkg.bag->color->many) memcpy(colors, pkg.colors, bytes);
  if (pkg.bag->lighting) memcpy(normals, pkg.normals, bytes);

  bag = pkg.bag;
}

void StaPipQBuffer::reallocateManually(const u16& t_size) {
  deallocateDynamicData();
  allocateDynamicData(t_size, bag);
  size = t_size;
  clipPlaneMask = 0;
}

void StaPipQBuffer::deallocateDynamicData() {
  if (!_isDynamicallyAllocated) return;

  // Pooled arrays are kept for reuse - only true heap fallbacks are freed.
  // The arrays may belong to either side: the side has usually flipped since
  // this slot was filled.
  const QBufferPool* poolA = poolFor(this, 0);
  const QBufferPool* poolB = poolFor(this, 1);
  const bool pooled = (poolA && vertices == poolA->vertices) ||
                      (poolB && vertices == poolB->vertices);

  if (!pooled) {
    delete[] vertices;

    if (_stAllocated) delete[] sts;
    if (_colorAllocated) delete[] colors;
    if (_normalAllocated) delete[] normals;
  }

  _stAllocated = false;
  _colorAllocated = false;
  _normalAllocated = false;
  _isDynamicallyAllocated = false;
}

void StaPipQBuffer::allocateDynamicData(u16 size, StaPipBag* bag) {
  TYRA_ASSERT(size <= maxVertCount, "Wrong size. Max buffer size in VU1 is ",
              maxVertCount, ". Provided: ", size);
  TYRA_ASSERT(!_isDynamicallyAllocated, "Buffer is already allocated");

  if (size == 0) return;

  QBufferPool* pool = poolFor(this);
  if (pool) {
    const u16 needed = maxVertCount > size ? (u16)maxVertCount : size;
    if (pool->capacity < needed) {
      delete[] pool->vertices;
      delete[] pool->sts;
      delete[] pool->colors;
      delete[] pool->normals;
      pool->vertices = new Vec4[needed];
      pool->sts = new Vec4[needed];
      pool->colors = new Vec4[needed];
      pool->normals = new Vec4[needed];
      pool->capacity = needed;
    }
    vertices = pool->vertices;
    if (bag->texture != nullptr) {
      sts = pool->sts;
      _stAllocated = true;
    }
    if (bag->color->many != nullptr) {
      colors = pool->colors;
      _colorAllocated = true;
    }
    if (bag->lighting != nullptr) {
      normals = pool->normals;
      _normalAllocated = true;
    }
    _isDynamicallyAllocated = true;
    return;
  }

  // Fallback: original behavior
  vertices = new Vec4[size];

  if (bag->texture != nullptr) {
    sts = new Vec4[size];
    _stAllocated = true;
  }

  if (bag->color->many != nullptr) {
    colors = new Vec4[size];
    _colorAllocated = true;
  }

  if (bag->lighting != nullptr) {
    normals = new Vec4[size];
    _normalAllocated = true;
  }

  _isDynamicallyAllocated = true;
}

bool StaPipQBuffer::any() const { return size > 0; }

void StaPipQBuffer::print() const {
  auto text = getPrint(nullptr);
  printf("%s\n", text.c_str());
}

void StaPipQBuffer::print(const char* name) const {
  auto text = getPrint(name);
  printf("%s\n", text.c_str());
}

std::string StaPipQBuffer::getPrint(const char* name) const {
  std::stringstream res;
  if (name) {
    res << name << "(";
  } else {
    res << "StaPipQBuffer(";
  }
  res << std::fixed << std::setprecision(2);
  res << std::endl;
  res << "Size: " << static_cast<int>(size) << std::endl;

  res << "Vertices: " << std::endl;
  for (u32 i = 0; i < size; i++)
    res << i << ": " << vertices[i].getPrint() << std::endl;

  if (bag->texture != nullptr) {
    res << "STs: " << std::endl;
    for (u32 i = 0; i < size; i++)
      res << i << ": " << sts[i].getPrint() << std::endl;
  }

  if (bag->color->many != nullptr) {
    res << "Colors: " << std::endl;
    for (u32 i = 0; i < size; i++)
      res << i << ": " << colors[i].getPrint() << std::endl;
  }

  if (bag->lighting != nullptr) {
    res << "Normals: " << std::endl;
    for (u32 i = 0; i < size; i++) {
      res << i << ": " << normals[i].getPrint();
      if (i < size - 1) {
        res << std::endl;
      }
    }
  }

  res << ")";

  return res.str();
}

}  // namespace Tyra
