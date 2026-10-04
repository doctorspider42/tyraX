#pragma once
#include <cstring>
typedef float VECTOR[4];
namespace Tyra {struct alignas(16) Vec4 {float x,y,z,w; Vec4(float a=0,float b=0,float c=0,float d=1):x(a),y(b),z(c),w(d){}; static void copy(Vec4* o,const float* i){std::memcpy(static_cast<void*>(o),i,16);} };}
