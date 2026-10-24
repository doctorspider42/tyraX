/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
*/

// Modified by TyraX: the VU0 operators moved inline into math/vec4.hpp with
// exact memory operands (docs/vu0-macro-mode.md); what is left here is the
// scalar and printing half.

#include "math/vec4.hpp"
#include <iomanip>
#include <cmath>
#include <sstream>

namespace Tyra {

const Vec4 Vec4::Identity = Vec4(0.0F, 0.0F, 0.0F, 1.0F);

Vec4 Vec4::operator-(void) const { return Vec4(-x, -y, -z, w); }

Vec4 Vec4::operator/(const Vec4& v) const {
  return Vec4(x / v.x, y / v.y, z / v.z, w / v.w);
}

void Vec4::operator/=(const Vec4& v) {
  x /= v.x;
  y /= v.y;
  z /= v.z;
  w /= v.w;
}

void Vec4::rotateZ(const int& angle) {
  auto s = Math::sin(angle);
  auto c = Math::cos(angle);
  x = x * c - y * s;
  y = x * s + y * c;
}

int Vec4::getRelativeCosBetween(const Vec4& v) const {
  return dot3(v) / (length() * v.length());
}
int Vec4::getRelativeAngleBetween(const Vec4& v) const {
  return acos(getRelativeCosBetween(v));
}

bool Vec4::collidesBox(const Vec4& min, const Vec4& max) const {
  return ((this->x <= max.x && this->x >= min.x) &&
          (this->y < max.y && this->y >= min.y) &&
          (this->z <= max.z && this->z >= min.z))
             ? 1
             : 0;
}

bool Vec4::isOnBox(const Vec4& min, const Vec4& max) const {
  return ((this->x <= max.x && this->x >= min.x) && (this->y >= max.y) &&
          (this->z <= max.z && this->z >= min.z))
             ? 1
             : 0;
}

u8 Vec4::shouldBeBackfaceCulled(const Vec4* cameraPos, const Vec4* v0,
                                const Vec4* v1, const Vec4* v2) {
  float dot;
  asm volatile(
      "lqc2        $vf4, %1         \n\t"  // $vf4 = cameraPos
      "lqc2        $vf5, %2         \n\t"  // $vf5 = v0
      "lqc2        $vf6, %3         \n\t"  // $vf6 = v1
      "lqc2        $vf7, %4         \n\t"  // $vf7 = v2
      "vsub.xyz    $vf8, $vf7, $vf5 \n\t"  // $vf8 = $vf7(v2) - $vf5(v0)
      "vsub.xyz    $vf9, $vf6, $vf5 \n\t"  // $vf9 = $vf6(v1) - $vf5(v0)
      "vopmula.xyz $ACC, $vf8, $vf9 \n\t"  // $vf6 = cross($vf8, $vf9)
      "vopmsub.xyz $vf6, $vf9, $vf8 \n\t"
      "vsub.w      $vf6, $vf6, $vf6 \n\t"
      "vsub.xyz    $vf7, $vf5, $vf4 \n\t"  // $vf7 = $vf5(v0) - $vf4(cameraPos)
      "vmul.xyz    $vf5, $vf7, $vf6 \n\t"  // $vf5 = dot($vf7, $vf6)
      "vaddy.x     $vf5, $vf5, $vf5 \n\t"
      "vaddz.x     $vf5, $vf5, $vf5 \n\t"
      "qmfc2       %0,   $vf5       \n\t"  // store result on `dot` variable
      : "=r"(dot)
      : "m"(cameraPos->xyzw), "m"(v0->xyzw), "m"(v1->xyzw), "m"(v2->xyzw));
  return dot <= 0.0F;
}

void Vec4::print() const {
  auto text = getPrint(nullptr);
  printf("%s\n", text.c_str());
}

void Vec4::print(const char* name) const {
  auto text = getPrint(name);
  printf("%s\n", text.c_str());
}

std::string Vec4::getPrint(const char* name) const {
  std::stringstream res;
  if (name) {
    res << name << "(";
  } else {
    res << "Vec4(";
  }
  res << std::fixed << std::setprecision(4);
  res << x << ", " << y << ", " << z << ", " << w << ")";
  return res.str();
}

}  // namespace Tyra
