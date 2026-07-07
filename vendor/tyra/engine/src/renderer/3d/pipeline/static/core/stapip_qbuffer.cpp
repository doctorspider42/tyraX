/*
# Modified by tyra-editor - persistent qbuffer pools.
# The original allocated and freed up to four Vec4 arrays PER FILL CALL,
# per subpackage, per frame. Arrays are now allocated once per buffer at
# maxVertCount capacity and reused. Based on the original by
# Sandro Sobczynski (h4570/tyra), Apache License 2.0.
*/

#include "renderer/3d/pipeline/static/core/stapip_qbuffer.hpp"
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
QBufferPool pools[kMaxPools];

QBufferPool* poolFor(const void* owner) {
  for (int i = 0; i < kMaxPools; i++)
    if (pools[i].owner == owner) return &pools[i];
  for (int i = 0; i < kMaxPools; i++)
    if (pools[i].owner == nullptr) {
      pools[i].owner = owner;
      return &pools[i];
    }
  return nullptr;  // more buffers than pools - fall back to plain new[]
}

}  // namespace

StaPipQBuffer::StaPipQBuffer() {
  size = 0;
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
  bag = pkg.bag;
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
  allocateDynamicData(size, pkg1.bag);

  for (u16 i = 0; i < pkg1.size; i++) {
    vertices[i].set(pkg1.vertices[i]);

    if (pkg1.bag->texture) sts[i].set(pkg1.sts[i]);

    if (pkg1.bag->color->many)
      colors[i].set(reinterpret_cast<const Vec4&>(pkg1.colors[i]));

    if (pkg1.bag->lighting) normals[i].set(pkg1.normals[i]);
  }

  for (u16 i = 0; i < pkg2.size; i++) {
    vertices[i + pkg1.size].set(pkg2.vertices[i]);

    if (pkg1.bag->texture) sts[i + pkg1.size].set(pkg2.sts[i]);

    if (pkg1.bag->color->many)
      colors[i + pkg1.size].set(reinterpret_cast<const Vec4&>(pkg2.colors[i]));

    if (pkg1.bag->lighting) normals[i + pkg1.size].set(pkg2.normals[i]);
  }

  for (u16 i = 0; i < pkg3.size; i++) {
    vertices[i + pkg1.size + pkg2.size].set(pkg3.vertices[i]);

    if (pkg1.bag->texture) sts[i + pkg1.size + pkg2.size].set(pkg3.sts[i]);

    if (pkg1.bag->color->many)
      colors[i + pkg1.size + pkg2.size].set(
          reinterpret_cast<const Vec4&>(pkg3.colors[i]));

    if (pkg1.bag->lighting)
      normals[i + pkg1.size + pkg2.size].set(pkg3.normals[i]);
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
  allocateDynamicData(size, pkg1.bag);

  for (u16 i = 0; i < pkg1.size; i++) {
    vertices[i].set(pkg1.vertices[i]);

    if (pkg1.bag->texture) sts[i].set(pkg1.sts[i]);

    if (pkg1.bag->color->many)
      colors[i].set(reinterpret_cast<const Vec4&>(pkg1.colors[i]));

    if (pkg1.bag->lighting) normals[i].set(pkg1.normals[i]);
  }

  for (u16 i = 0; i < pkg2.size; i++) {
    vertices[i + pkg1.size].set(pkg2.vertices[i]);

    if (pkg1.bag->texture) sts[i + pkg1.size].set(pkg2.sts[i]);

    if (pkg1.bag->color->many)
      colors[i + pkg1.size].set(reinterpret_cast<const Vec4&>(pkg2.colors[i]));

    if (pkg1.bag->lighting) normals[i + pkg1.size].set(pkg2.normals[i]);
  }

  bag = pkg1.bag;
}

void StaPipQBuffer::fillByCopy1By3(const StaPipBagPackage& pkg) {
  TYRA_ASSERT(pkg.size <= maxVertCount / 3,
              "Wrong package size (1). Provided: ", pkg.size);

  deallocateDynamicData();
  size = pkg.size;
  allocateDynamicData(size, pkg.bag);

  for (u16 i = 0; i < pkg.size; i++) {
    vertices[i].set(pkg.vertices[i]);

    if (pkg.bag->texture) sts[i].set(pkg.sts[i]);

    if (pkg.bag->color->many)
      colors[i].set(reinterpret_cast<const Vec4&>(pkg.colors[i]));

    if (pkg.bag->lighting) normals[i].set(pkg.normals[i]);
  }

  bag = pkg.bag;
}

void StaPipQBuffer::reallocateManually(const u16& t_size) {
  deallocateDynamicData();
  allocateDynamicData(t_size, bag);
  size = t_size;
}

void StaPipQBuffer::deallocateDynamicData() {
  if (!_isDynamicallyAllocated) return;

  // Pooled arrays are kept for reuse - only true heap fallbacks are freed.
  QBufferPool* pool = poolFor(this);
  const bool pooled = pool && vertices == pool->vertices;

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
