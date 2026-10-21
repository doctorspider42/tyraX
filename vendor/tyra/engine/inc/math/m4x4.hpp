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

#include "math/vec4.hpp"
#include "math/math.hpp"
#include <string>

namespace Tyra {

class M4x4 {
 public:
  MATRIX data alignas(sizeof(float) * 16);

  static const M4x4 Identity;

  /** Create matrix with random values */
  M4x4() {}

  // Modified by TyraX: copy, operator=, operator*(Vec4) and identity are
  // inline VU0 blocks with exact "m" operands (below the class; see the note
  // in math/vec4.hpp and docs/vu0-macro-mode.md).
  M4x4(const M4x4& v) { copy(this, v); }

  explicit M4x4(const float* v) { copy(this, v); }

  M4x4(const float& m11, const float& m12, const float& m13, const float& m14,
       const float& m21, const float& m22, const float& m23, const float& m24,
       const float& m31, const float& m32, const float& m33, const float& m34,
       const float& m41, const float& m42, const float& m43, const float& m44);

  static inline void copy(M4x4* out, const float* in);
  static inline void copy(M4x4* out, const M4x4& in) { copy(out, in.data); }

  inline void operator=(const M4x4& v) { copy(this, v); }
  inline Vec4 operator*(const Vec4& v) const;
  M4x4 operator*(const M4x4& v) const;
  void operator*=(const M4x4& v);
  float& operator[](const u8& index);

  /**
   * 1 0 0 0
   * 0 1 0 0
   * 0 0 1 0
   * 0 0 0 1
   */
  inline void identity();

  inline void unit() { identity(); }

  void set(const float& m11, const float& m12, const float& m13,
           const float& m14, const float& m21, const float& m22,
           const float& m23, const float& m24, const float& m31,
           const float& m32, const float& m33, const float& m34,
           const float& m41, const float& m42, const float& m43,
           const float& m44);

  void translate(const Vec4& v);
  void translateX(const float& v);
  void translateY(const float& v);
  void translateZ(const float& v);

  void rotateByAngle(const float& angle, const Vec4& axis);
  void rotate(const Vec4& v);
  void rotateX(const float& radians);
  void rotateY(const float& radians);
  void rotateZ(const float& radians);

  void scale(const float& v);
  void scale(const Vec4& v);
  void scaleX(const float& v);
  void scaleY(const float& v);
  void scaleZ(const float& v);

  /** Create perspective projection M4x4 (gluPerspective) */
  static M4x4 perspective(const float& fov, const float& width,
                          const float& height, const float& projectionScale,
                          const float& aspectRatio, const float& near,
                          const float& far);

  /**
   * Create a view M4x4 that transforms coordinates in
   * such a way that the user looks at a target vector
   * direction from a position vector (glLookAt)
   */
  static M4x4 lookAt(const Vec4& position, const Vec4& target);
  static void lookAt(M4x4* res, const Vec4& position, const Vec4& target);

  /**
   * Modified by TyraX: same, with an explicit up vector, so a camera can roll
   * about its view axis (cutscene Dutch angles - the two-argument form derives
   * the up vector from world +Y and cannot express one). CameraInfo3D already
   * carries an `up` that defaults to (0, 1, 0), and Renderer3DFrustumPlanes
   * already culls against it; only the view matrix was ignoring it.
   */
  static void lookAt(M4x4* res, const Vec4& position, const Vec4& target,
                     const Vec4& up);

  void print() const;
  void print(const char* name) const;
  void print(const std::string& name) const { print(name.c_str()); }
  std::string getPrint(const char* name = nullptr) const;

 private:
  static float upVec[4] alignas(sizeof(float) * 4);
  static float viewVec[4] alignas(sizeof(float) * 4);

  static void cross(float res[16], const float a[16], const float b[16]);
  void rotationX(const float& v);
  void rotationY(const float& v);
  void rotationZ(const float& v);
  void rotationByAngle(const float& angle, const Vec4& axis);
  void translationX(const float& v);
  void translationY(const float& v);
  void translationZ(const float& v);
  void setScale(const Vec4& val);
  static M4x4 setCamera(const float pos[4], const float vz[4],
                        const float vy[4]);
};

// --- VU0 macro mode, inline (Modified by TyraX) ---------------------------
// Scratch $vf1-$vf7 and ACC, as upstream. A matrix is four quadwords, so the
// rows are addressed off a base register ("r") and the 64 bytes are named
// separately as an "m" operand the asm never prints - "0x10+%0" cannot be
// written, because GAS rejects `0x10+%lo(sym)($2)` for a global such as
// M4x4::Identity under -G0.
typedef float M4x4Floats[16];

inline void M4x4::copy(M4x4* out, const float* in) {
  asm volatile(
      "lqc2       $vf1, 0x00(%2)      \n\t"
      "lqc2       $vf2, 0x10(%2)      \n\t"
      "lqc2       $vf3, 0x20(%2)      \n\t"
      "lqc2       $vf4, 0x30(%2)      \n\t"
      "sqc2       $vf1, 0x00(%1)      \n\t"
      "sqc2       $vf2, 0x10(%1)      \n\t"
      "sqc2       $vf3, 0x20(%1)      \n\t"
      "sqc2       $vf4, 0x30(%1)      \n\t"
      : "=m"(out->data)
      : "r"(out->data), "r"(in),
        "m"(*reinterpret_cast<const M4x4Floats*>(in)));
}

inline Vec4 M4x4::operator*(const Vec4& v) const {
  Vec4 result;
  asm volatile(
      "lqc2       $vf1, 0x00(%2)      \n\t"
      "lqc2       $vf2, 0x10(%2)      \n\t"
      "lqc2       $vf3, 0x20(%2)      \n\t"
      "lqc2       $vf4, 0x30(%2)      \n\t"
      "lqc2       $vf5, %1            \n\t"
      "vmulaw     $ACC, $vf4, $vf0    \n\t"
      "vmaddax    $ACC, $vf1, $vf5    \n\t"
      "vmadday    $ACC, $vf2, $vf5    \n\t"
      "vmaddz     $vf6, $vf3, $vf5    \n\t"
      "sqc2       $vf6, %0            \n\t"
      : "=m"(result.xyzw)
      : "m"(v.xyzw), "r"(data), "m"(data));
  return result;
}

inline void M4x4::identity() {
  asm volatile(
      "vsub.xyzw  $vf4, $vf0, $vf0    \n\t"
      "vadd.w     $vf4, $vf4, $vf0    \n\t"
      "vmr32.xyzw $vf5, $vf4          \n\t"
      "vmr32.xyzw $vf6, $vf5          \n\t"
      "vmr32.xyzw $vf7, $vf6          \n\t"
      "sqc2       $vf4, 0x30(%1)      \n\t"
      "sqc2       $vf5, 0x20(%1)      \n\t"
      "sqc2       $vf6, 0x10(%1)      \n\t"
      "sqc2       $vf7, 0x00(%1)      \n\t"
      : "=m"(data)
      : "r"(data));
}

}  // namespace Tyra
