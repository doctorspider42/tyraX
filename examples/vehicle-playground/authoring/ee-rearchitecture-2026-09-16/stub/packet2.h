#pragma once
#include <tamtypes.h>
typedef union {
  unsigned int sw[4];
  unsigned long long dw[2];
} qword_t __attribute__((aligned(16)));
