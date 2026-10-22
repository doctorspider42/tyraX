#pragma once
// Modified by TyraX: private lattice input. Owner is the current packet's
// protected qbuffer copy-pool side, never a stack/local or retained replay REF.
#include <packet2_utils.h>
#include "stapip_pool_lattice.hpp"
namespace Tyra { namespace ExperimentalPoolLattice {
inline void appendInput(packet2_t* packet,const Package& source) {
 const uint32_t unique=source.descriptor.word[0],tris=source.descriptor.word[1];
 packet2_utils_vu_open_unpack(packet,2,true);
 for(unsigned i=0;i<4;++i)packet2_add_u32(packet,source.descriptor.word[i]);
 for(unsigned i=0;i<4;++i)packet2_add_u32(packet,source.color.word[i]);
 packet2_utils_vu_close_unpack(packet);
 packet2_utils_vu_add_unpack_data(packet,4,const_cast<Bits*>(source.positions),unique,true);
 packet2_utils_vu_add_unpack_data(packet,4+unique,const_cast<Bits*>(source.sts),unique,true);
 packet2_utils_vu_add_unpack_data(packet,4+2*unique,const_cast<Bits*>(source.triangles),tris,true);
}
} }
