#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

// Cold pure-VIF transformation only. Caller must supply the actual ROW/MASK,
// guarantee STMOD=0, and preserve publication/owner leases independently.
namespace WorldInputCompact {
using Word = std::uint32_t;
struct State { std::array<Word,4> row; Word mask; };
struct Result { std::vector<Word> words; unsigned compacted=0, fallback=0; };
inline void rowMask(std::vector<Word>& out, const State& state) {
  out.push_back(0x30000000);
  out.insert(out.end(),state.row.begin(),state.row.end());
  out.push_back(0x20000000);out.push_back(state.mask);out.push_back(0);
}
inline Result compile(const std::vector<Word>& source, unsigned streams,
                      const State& incoming) {
  if(source.empty() || source.size()%4 || (streams!=2 && streams!=3))
    throw std::invalid_argument("compact invalid body shape");
  Result out;
  for(std::size_t at=0;at<source.size();) {
    if(source.size()-at<12 || source[at]!=0 || source[at+1]!=0 ||
       source[at+2]!=0x01000101 || source[at+3]!=0x6c02c000)
      throw std::invalid_argument("compact non-SDK package head");
    const unsigned count=source[at+7]&1023;
    if(count<3 || count>72)
      throw std::invalid_argument("compact unsupported count");
    out.words.insert(out.words.end(),source.begin()+at,source.begin()+at+12);
    at+=12;
    for(unsigned stream=0;stream<streams;++stream) {
      const Word unpack=0x6c00c000|(count<<16)|(2+stream*count);
      if(source.size()-at<4+count*4 || source[at]!=0 || source[at+1]!=0 ||
         source[at+2]!=0x01000101 || source[at+3]!=unpack)
        throw std::invalid_argument("compact non-SDK geometry unpack");
      const Word w=source[at+7];bool constant=true;
      for(unsigned vertex=1;vertex<count;++vertex)
        constant=constant && source[at+7+4*vertex]==w;
      const unsigned packed=(count*3+3)/4*4;
      if(constant && 20+packed<4+count*4) {
        rowMask(out.words,State{{0,0,0,w},0x40404040});
        out.words.insert(out.words.end(),{0,0,0x01000101,
                                         (unpack&0x00ffffff)|0x78000000});
        for(unsigned vertex=0;vertex<count;++vertex)
          out.words.insert(out.words.end(),source.begin()+at+4+4*vertex,
                                          source.begin()+at+7+4*vertex);
        out.words.insert(out.words.end(),packed-count*3,0);
        rowMask(out.words,incoming);++out.compacted;
      } else {
        out.words.insert(out.words.end(),source.begin()+at,source.begin()+at+4+count*4);
        ++out.fallback;
      }
      at+=4+count*4;
    }
    if(source.size()-at<4 || source[at]!=0 || source[at+1]!=0 ||
       source[at+2]!=0x11000000 ||
       ((source[at+3]>>24)!=0x14 && source[at+3]!=0x17000000))
      throw std::invalid_argument("compact non-SDK program kick");
    out.words.insert(out.words.end(),source.begin()+at,source.begin()+at+4);at+=4;
  }
  if(out.words.size()>source.size() || out.words.size()%4)
    throw std::logic_error("compact size invariant");
  return out;
}
}
