#pragma once
#include "debug/world_body.hpp"
#include <vector>
namespace OfflineVifEngine {
using Body=std::vector<WorldBody::Quad>;
inline bool (*append)(unsigned,unsigned,unsigned,unsigned,unsigned,bool,unsigned,Body&,unsigned)=nullptr;
inline std::vector<Body>* capture=nullptr;
inline std::vector<unsigned>* metadata=nullptr;
inline bool audit=false;
inline unsigned restores=0,audits=0;
}
