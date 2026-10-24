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

extern "C" {
#include <stdio.h>
#include <tamtypes.h>
}

#include <math3d.h>
#include <string>
#include "math/math.hpp"

// Modified by TyraX: the VU0 macro-mode operators are inline, below the
// class (docs/vu0-macro-mode.md). Every block names the memory it reads and
// writes as "m" operands and takes its GPR scratch from the compiler: the
// upstream blocks passed bare pointers ("r") with no "memory" clobber, which
// GCC is free to schedule around - inlined, it skips the stores that feed the
// asm. The out-of-line call per operator was the only thing hiding that.

namespace Tyra {

class Vec4 {
 public:
  union {
    struct {
      float x;
      float y;
      float z;
      float w;
    };
    VECTOR xyzw alignas(sizeof(float) * 4);
  };

  static inline void copy(Vec4* out, const float* in);
  static inline void copy(Vec4* out, const Vec4& in) { *out = in; }

  static const Vec4 Identity;

  /** Initialize Vec4 without setting default values */
  Vec4() {}

  /** Initialize Vec4 with provided values */
  Vec4(const float& x, const float& y = 0.0F, const float& z = 0.0F,
       const float& w = 1.0F)
      : x(x), y(y), z(z), w(w) {}

  /**
   * Initialize Vec4 with other Vec4 values. Modified by TyraX: trivial, so a
   * copy is two 64-bit moves the compiler can keep in registers or drop,
   * instead of a call into an lqc2/sqc2 pair.
   */
  Vec4(const Vec4& v) = default;

  /** Initialize Vec4 with float[4] values */
  explicit Vec4(const float* v) { copy(this, v); }

  /** copy */
  Vec4& operator=(const Vec4& v) = default;

  /** negate xyz */
  Vec4 operator-(void) const;

  /** xyz + xyz */
  Vec4 operator+(const Vec4& v) const;
  void operator+=(const Vec4& v);

  /** xyz - xyz */
  Vec4 operator-(const Vec4& v) const;
  void operator-=(const Vec4& v);

  /** xyzw * xyzw */
  Vec4 operator*(const Vec4& v) const;
  void operator*=(const Vec4& v);

  /** xyzw / xyzw */
  Vec4 operator/(const Vec4& v) const;
  void operator/=(const Vec4& v);

  /** xyz * v */
  Vec4 operator*(const float& v) const;
  void operator*=(const float& v);

  /** xyz / v */
  Vec4 operator/(const float& v) const;
  void operator/=(const float& v);

  /** (0,0,0,1) */
  void unit();

  inline void set(const Vec4& v) { copy(this, v.xyzw); }
  inline void set(const float* v) { copy(this, v); }
  inline void set(const float& t_x, const float& t_y = 0.0F,
                  const float& t_z = 0.0F, const float& t_w = 1.0F) {
    x = t_x;
    y = t_y;
    z = t_z;
    w = t_w;
  }

  Vec4 cross(const Vec4& v) const;

  void rotateZ(const int& angle);

  int getRelativeCosBetween(const Vec4& v) const;
  int getRelativeAngleBetween(const Vec4& v) const;

  float innerProduct(const Vec4& v) const;
  inline float dot3(const Vec4& v) const { return innerProduct(v); }

  /** Get vector length */
  float length() const;

  /** Normalize vector */
  void normalize();

  Vec4 getNormalized() const;

  /** Returns distance between two vectors */
  float distanceTo(const Vec4& v) const;

  /**
   * Checks intersection with given box
   * @param min Opposite min vertex (ex. near, left, down vertex of bounding
   * box)
   * @param max Opposite max vertex (ex. far, right, up vertex of bounding
   * box)
   */
  bool collidesBox(const Vec4& min, const Vec4& max) const;

  /**
   * Checks if this vector is on given box (this->y >= box.y)
   * @param min Opposite min vertex (ex. near, left, down vertex of bounding
   * box)
   * @param max Opposite max vertex (ex. far, right, up vertex of bounding
   * box)
   */
  bool isOnBox(const Vec4& min, const Vec4& max) const;

  /** Check if given triangle should be backface culled */
  static u8 shouldBeBackfaceCulled(const Vec4* cameraPos, const Vec4* v0,
                                   const Vec4* v1, const Vec4* v2);

  /**
   * Update this vector by linear interpolation between two vertices
   * @param v1 Before vertex
   * @param v2 After vertex
   * @param interp State of interpolation
   */
  void lerp(const Vec4& v1, const Vec4& v2, const float& interp);

  /**
   * Get vector by linear interpolation between two vertices
   * @param v1 Before vertex
   * @param v2 After vertex
   * @param interp State of interpolation
   */
  static Vec4 getByLerp(const Vec4& v1, const Vec4& v2, const float& interp);

  void print() const;
  void print(const char* name) const;
  void print(const std::string& name) const { print(name.c_str()); }
  std::string getPrint(const char* name = nullptr) const;

  static void setLerp(Vec4* output, const Vec4& v1, const Vec4& v2,
                      const float& interp);
};

// --- VU0 macro mode, inline (Modified by TyraX) ---------------------------
// Scratch is $vf4-$vf9 and ACC/Q, the same registers the upstream bodies
// used, so code that keeps VU0 state across its own asm blocks (skinning:
// $vf20/$vf21) is unaffected. Every block stays `volatile`: GCC never orders
// a volatile asm past another one, which is what lets those callers rely on
// VU0 state between blocks. The `w` lane: + and - keep this->w, normalize()
// keeps it too (all three used to store whatever $vf6.w last held).

inline void Vec4::copy(Vec4* out, const float* in) {
  asm volatile(
      "lqc2       $vf4, %1            \n\t"
      "sqc2       $vf4, %0            \n\t"
      : "=m"(out->xyzw)
      : "m"(*reinterpret_cast<const float(*)[4]>(in)));
}

inline Vec4 Vec4::operator+(const Vec4& v) const {
  Vec4 res;
  asm volatile(
      "lqc2       $vf4, %1            \n\t"
      "lqc2       $vf5, %2            \n\t"
      "vadd.xyz   $vf4, $vf4, $vf5    \n\t"
      "sqc2       $vf4, %0            \n\t"
      : "=m"(res.xyzw)
      : "m"(xyzw), "m"(v.xyzw));
  return res;
}

inline void Vec4::operator+=(const Vec4& v) {
  asm volatile(
      "lqc2       $vf4, %0            \n\t"
      "lqc2       $vf5, %1            \n\t"
      "vadd.xyz   $vf4, $vf4, $vf5    \n\t"
      "sqc2       $vf4, %0            \n\t"
      : "+m"(xyzw)
      : "m"(v.xyzw));
}

inline Vec4 Vec4::operator-(const Vec4& v) const {
  Vec4 res;
  asm volatile(
      "lqc2       $vf4, %1            \n\t"
      "lqc2       $vf5, %2            \n\t"
      "vsub.xyz   $vf4, $vf4, $vf5    \n\t"
      "sqc2       $vf4, %0            \n\t"
      : "=m"(res.xyzw)
      : "m"(xyzw), "m"(v.xyzw));
  return res;
}

inline void Vec4::operator-=(const Vec4& v) {
  asm volatile(
      "lqc2       $vf4, %0            \n\t"
      "lqc2       $vf5, %1            \n\t"
      "vsub.xyz   $vf4, $vf4, $vf5    \n\t"
      "sqc2       $vf4, %0            \n\t"
      : "+m"(xyzw)
      : "m"(v.xyzw));
}

inline Vec4 Vec4::operator*(const Vec4& v) const {
  Vec4 res;
  asm volatile(
      "lqc2       $vf4, %1            \n\t"
      "lqc2       $vf5, %2            \n\t"
      "vmul.xyzw  $vf4, $vf4, $vf5    \n\t"
      "sqc2       $vf4, %0            \n\t"
      : "=m"(res.xyzw)
      : "m"(xyzw), "m"(v.xyzw));
  return res;
}

inline void Vec4::operator*=(const Vec4& v) {
  asm volatile(
      "lqc2       $vf4, %0            \n\t"
      "lqc2       $vf5, %1            \n\t"
      "vmul.xyzw  $vf4, $vf4, $vf5    \n\t"
      "sqc2       $vf4, %0            \n\t"
      : "+m"(xyzw)
      : "m"(v.xyzw));
}

// The scalar goes in through a GPR ("r"): GCC loads it with `lw` straight
// from memory when it lives there, with no FPU round trip.
inline Vec4 Vec4::operator*(const float& v) const {
  Vec4 res;
  asm volatile(
      "lqc2       $vf4, %1            \n\t"
      "qmtc2      %2,   $vf5          \n\t"
      "vmulx.xyz  $vf4, $vf4, $vf5    \n\t"
      "sqc2       $vf4, %0            \n\t"
      : "=m"(res.xyzw)
      : "m"(xyzw), "r"(v));
  return res;
}

inline void Vec4::operator*=(const float& v) {
  asm volatile(
      "lqc2       $vf4, %0            \n\t"
      "qmtc2      %1,   $vf5          \n\t"
      "vmulx.xyz  $vf4, $vf4, $vf5    \n\t"
      "sqc2       $vf4, %0            \n\t"
      : "+m"(xyzw)
      : "r"(v));
}

inline Vec4 Vec4::operator/(const float& v) const {
  Vec4 res;
  asm volatile(
      "qmtc2      %2,   $vf5          \n\t"
      "vdiv       $Q,   $vf0w, $vf5x  \n\t"
      "lqc2       $vf4, %1            \n\t"
      "vwaitq                         \n\t"
      "vmulq.xyz  $vf4, $vf4, $Q      \n\t"
      "sqc2       $vf4, %0            \n\t"
      : "=m"(res.xyzw)
      : "m"(xyzw), "r"(v));
  return res;
}

inline void Vec4::operator/=(const float& v) {
  asm volatile(
      "qmtc2      %1,   $vf5          \n\t"
      "vdiv       $Q,   $vf0w, $vf5x  \n\t"
      "lqc2       $vf4, %0            \n\t"
      "vwaitq                         \n\t"
      "vmulq.xyz  $vf4, $vf4, $Q      \n\t"
      "sqc2       $vf4, %0            \n\t"
      : "+m"(xyzw)
      : "r"(v));
}

inline void Vec4::unit() {
  asm volatile("sqc2       $vf0, %0            \n\t" : "=m"(xyzw));
}

inline Vec4 Vec4::cross(const Vec4& v) const {
  Vec4 res;
  asm volatile(
      "lqc2         $vf4, %1          \n\t"
      "lqc2         $vf5, %2          \n\t"
      "vopmula.xyz  $ACC, $vf4, $vf5  \n\t"
      "vopmsub.xyz  $vf6, $vf5, $vf4  \n\t"
      "vsub.w       $vf6, $vf0, $vf0  \n\t"
      "sqc2         $vf6, %0          \n\t"
      : "=m"(res.xyzw)
      : "m"(xyzw), "m"(v.xyzw));
  return res;
}

inline float Vec4::innerProduct(const Vec4& v) const {
  float result;
  asm volatile(
      "lqc2       $vf4, %1            \n\t"
      "lqc2       $vf5, %2            \n\t"
      "vmul.xyz   $vf4, $vf4, $vf5    \n\t"
      "vaddy.x    $vf4, $vf4, $vf4    \n\t"
      "vaddz.x    $vf4, $vf4, $vf4    \n\t"
      "qmfc2      %0,   $vf4          \n\t"
      : "=r"(result)
      : "m"(xyzw), "m"(v.xyzw));
  return result;
}

inline float Vec4::length() const {
  float result;
  asm volatile(
      "lqc2       $vf4, %1            \n\t"
      "vmul.xyz   $vf5, $vf4, $vf4    \n\t"
      "vaddy.x    $vf5, $vf5, $vf5    \n\t"
      "vaddz.x    $vf5, $vf5, $vf5    \n\t"
      "vsqrt      $Q,   $vf5x         \n\t"
      "vwaitq                         \n\t"
      "vaddq.x    $vf5, $vf0, $Q      \n\t"
      "qmfc2      %0,   $vf5          \n\t"
      : "=r"(result)
      : "m"(xyzw));
  return result;
}

inline void Vec4::normalize() {
  asm volatile(
      "lqc2       $vf4, %0            \n\t"
      "vmul.xyz   $vf5, $vf4, $vf4    \n\t"
      "vaddy.x    $vf5, $vf5, $vf5    \n\t"
      "vaddz.x    $vf5, $vf5, $vf5    \n\t"
      "vrsqrt     $Q,   $vf0w, $vf5x  \n\t"
      "vwaitq                         \n\t"
      "vmulq.xyz  $vf4, $vf4, $Q      \n\t"
      "sqc2       $vf4, %0            \n\t"
      : "+m"(xyzw));
}

inline Vec4 Vec4::getNormalized() const {
  Vec4 result(*this);
  result.normalize();
  return result;
}

inline float Vec4::distanceTo(const Vec4& v) const {
  float result;
  asm volatile(
      "lqc2       $vf4, %1            \n\t"
      "lqc2       $vf5, %2            \n\t"
      "vsub.xyz   $vf6, $vf4, $vf5    \n\t"
      "vmul.xyz   $vf7, $vf6, $vf6    \n\t"
      "vaddy.x    $vf7, $vf7, $vf7    \n\t"
      "vaddz.x    $vf7, $vf7, $vf7    \n\t"
      "vsqrt      $Q,   $vf7x         \n\t"
      "vwaitq                         \n\t"
      "vaddq.x    $vf8, $vf0, $Q      \n\t"
      "qmfc2      %0,   $vf8          \n\t"
      : "=r"(result)
      : "m"(xyzw), "m"(v.xyzw));
  return result;
}

inline void Vec4::setLerp(Vec4* output, const Vec4& v1, const Vec4& v2,
                          const float& interp) {
  asm volatile(
      "lqc2       $vf4, %1            \n\t"  // $vf4 = v1
      "lqc2       $vf5, %2            \n\t"  // $vf5 = v2
      "qmtc2      %3,   $vf6          \n\t"  // $vf6.x = t
      "vsub.xyzw  $vf7, $vf5, $vf4    \n\t"  // $vf7 = v2 - v1
      "vmulx.xyzw $vf8, $vf7, $vf6    \n\t"  // $vf8 = $vf7 * t
      "vadd.xyzw  $vf9, $vf8, $vf4    \n\t"  // $vf9 = $vf8 + $vf4
      "sqc2       $vf9, %0            \n\t"
      : "=m"(output->xyzw)
      : "m"(v1.xyzw), "m"(v2.xyzw), "r"(interp));
}

inline void Vec4::lerp(const Vec4& v1, const Vec4& v2, const float& interp) {
  setLerp(this, v1, v2, interp);
}

inline Vec4 Vec4::getByLerp(const Vec4& v1, const Vec4& v2,
                            const float& interp) {
  Vec4 result;
  setLerp(&result, v1, v2, interp);
  return result;
}

}  // namespace Tyra
