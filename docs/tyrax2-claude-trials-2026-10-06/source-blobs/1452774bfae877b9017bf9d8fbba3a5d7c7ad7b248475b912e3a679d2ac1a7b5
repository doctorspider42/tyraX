#pragma once
#include <packet2_utils.h>
#include "stapip_pool_color_table.hpp"
namespace Tyra { namespace ExperimentalPoolTable {
// Validated Slice, original contiguous source streams and N<=75 are mandatory.
// Vertices/ST retain existing REF ownership; the 48-byte table is copied inline.
inline void appendInput(packet2_t* packet,const void* vertices,const void* sts,
                        uint32_t count,const Slice& slice) {
 packet2_utils_vu_add_unpack_data(packet,2,const_cast<void*>(vertices),count,true);
 packet2_utils_vu_add_unpack_data(packet,2+count,const_cast<void*>(sts),count,true);
 packet2_utils_vu_open_unpack(packet,2+2*count,true);
 for(unsigned i=0;i<4;++i) packet2_add_u32(packet,slice.descriptor[i]);
 for(unsigned c=0;c<2;++c) for(unsigned i=0;i<4;++i) packet2_add_u32(packet,slice.colors[c].word[i]);
 packet2_utils_vu_close_unpack(packet);
}
} }
