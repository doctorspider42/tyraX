#include "batch_trial.hpp"
#include <cassert>
#include <type_traits>
int main(){
 static_assert(sizeof(short)==2);static_assert(std::is_same<decltype(BatchTrial::originalMembership),std::vector<short>>::value);
 assert(BatchTrial::member(-2));assert(!BatchTrial::member(-1));assert(!BatchTrial::member(-3));assert(!BatchTrial::member(-32768));assert(BatchTrial::member(0));assert(BatchTrial::member(23));assert(BatchTrial::member(32767));
 std::vector<short> normal(55,-1);for(unsigned i=5;i<55;++i)normal[i]=-2;
 auto count=[](const std::vector<short>&v){unsigned n=0;for(unsigned i=5;i<55;++i)n+=BatchTrial::member(v[i]);return n;};
 BatchTrial::originalMembership=normal;assert(count(normal)==50);normal.assign(55,-1);assert(count(normal)==0);normal=BatchTrial::originalMembership;assert(count(normal)==50);for(unsigned i=5;i<55;++i)assert(normal[i]==-2);
 for(unsigned order=0;order<2;++order){Closeout::order=order;for(unsigned p=0;p<3;++p)assert(BatchTrial::desired(p*1800)==((p!=1)!=(order==1)));assert(BatchTrial::desired(5400)==BatchTrial::desired(3600));}
}
