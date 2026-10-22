#include "meshstrip.hpp"
#include <algorithm>
extern "C" int stripify(const float* v, const unsigned char* ao, unsigned n, unsigned ac, float* out, unsigned char* oa) {
 std::vector<float> verts(v,v+size_t(n)*8),result;
 std::vector<unsigned char> ambient,ra;
 if(ac)ambient.assign(ao,ao+ac);
 if(!meshstrip::build(verts,ambient,75,result,ra))return 0;
 std::copy(result.begin(),result.end(),out);
 std::copy(ra.begin(),ra.end(),oa);
 return int(result.size()/8);
}
