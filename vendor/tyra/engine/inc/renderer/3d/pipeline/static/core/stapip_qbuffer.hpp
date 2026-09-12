/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
*/

#pragma once

#include <math3d.h>
#include <string>
#include "debug/debug.hpp"
#include "./bag/packaging/stapip_bag_package.hpp"
#include "./bag/stapip_bag.hpp"

namespace Tyra {

class StaPipQBuffer {
 public:
  StaPipQBuffer();
  ~StaPipQBuffer();

  void setMaxVertCount(const u32& count);
  // Modified by TyraX: switch the copy pools to their other side - call once
  // per packet SEND, right where the packet double buffer flips (see the pool
  // comment in stapip_qbuffer.cpp).
  static void flipPoolSide();

  /**
   * @brief Dont allocate any dynamic data in buffer.
   * Just copy pointers to input data.
   */
  void fillByPointer(const StaPipBagPackage& pkg);

  /**
   * @brief Point directly at one contiguous range of a wholly visible bag.
   * Avoids constructing package descriptors when the bag-level bbox already
   * proved every range is inside the frustum.
   */
  void fillByPointer(StaPipBag* bag, u32 offset, u32 count);

  /**
   * @brief Allocate dynamic data in buffer
   * And copy input data to it.
   */
  void fillByCopyMax(const StaPipBagPackage& pkg1, const StaPipBagPackage& pkg2,
                     const StaPipBagPackage& pkg3);

  /**
   * @brief Allocate dynamic data in buffer
   * And copy input data to it.
   */
  void fillByCopy1By2(const StaPipBagPackage& pkg1,
                      const StaPipBagPackage& pkg2);

  /**
   * @brief Allocate dynamic data in buffer
   * And copy input data to it.
   */
  void fillByCopy1By3(const StaPipBagPackage& pkg);

  /**
   * @brief Deallocate dynamic data if it was allocated and allocate new data
   * specified by size.
   * @param size 48 is max
   */
  void reallocateManually(const u16& size);

  bool any() const;

  StaPipBag* bag;

  Vec4* vertices;
  Vec4* sts;
  Vec4* colors;
  Vec4* normals;
  u32 size;
  /** Conservative OR of the source packages' exact VU clip-plane masks. */
  u8 clipPlaneMask;

  void print() const;
  void print(const char* name) const;
  void print(const std::string& name) const { print(name.c_str()); }
  std::string getPrint(const char* name = nullptr) const;

 private:
  u32 maxVertCount;
  void deallocateDynamicData();
  void allocateDynamicData(u16 size, StaPipBag* bag);
  u8 _isDynamicallyAllocated, _stAllocated, _colorAllocated, _normalAllocated;
};

}  // namespace Tyra
