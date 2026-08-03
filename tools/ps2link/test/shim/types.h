/* Host-side shim for the IOP headers net_fio.c needs, so the real
 * iop/net_fio.c can be compiled and driven by a fake socket on the PC. */
#ifndef SHIM_TYPES_H
#define SHIM_TYPES_H
#include <stdint.h>
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef signed char s8;
typedef short s16;
typedef int s32;
#endif
