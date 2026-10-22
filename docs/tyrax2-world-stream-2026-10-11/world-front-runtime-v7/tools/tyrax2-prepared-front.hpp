#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <utility>

namespace PreparedFront {
constexpr std::uint32_t MaxSourceVertices=65535,MaxParts=4096;
enum Shape : std::uint32_t {
  Textured=1, SingleColor=2, Stripped=4, Precise=8, FullClip=16, MVP=32
};
struct Key {
  std::uint64_t generation=0;
  std::array<std::uintptr_t,4> streams{};
  std::array<std::uintptr_t,3> resources{};
  std::array<std::uint32_t,4> content{};
  std::array<std::uint32_t,8> material{};
  std::uint32_t count=0,capacity=0,run=0,bboxVersion=0,shape=0;
  std::uint32_t programEpoch=0,programAddress=0,programClass=0,depthBits=0;
  bool operator==(const Key& b) const {
    return generation==b.generation && streams==b.streams && resources==b.resources &&
      content==b.content && material==b.material && count==b.count && capacity==b.capacity &&
      run==b.run && bboxVersion==b.bboxVersion && shape==b.shape &&
      programEpoch==b.programEpoch && programAddress==b.programAddress &&
      programClass==b.programClass && depthBits==b.depthBits;
  }
};
inline bool valid(const Key& k) {
  if(!k.generation || !k.programEpoch || k.programAddress>=2048 ||
      (k.shape & ~63U) || (k.shape & (Precise|FullClip|MVP))!=(Precise|FullClip|MVP) ||
      !k.streams[0] || (k.streams[0]&15U) || k.streams[3] || !k.resources[2] ||
      k.count<3 || k.count>MaxSourceVertices || k.count%3 ||
      k.capacity<3 || k.capacity>999 || k.capacity%3 ||
      k.run>k.capacity || k.run%3)return false;
  if((k.count+k.capacity/3-1)/(k.capacity/3)>MaxParts)return false;
  if(k.shape&SingleColor) {
    if(k.streams[1] || !k.resources[1])return false;
  } else if(!k.streams[1] || k.resources[1])return false;
  if(k.shape&Textured) {
    if(!k.streams[2] || !k.resources[0])return false;
  } else if(k.streams[2] || k.resources[0])return false;
  return !(k.shape&Stripped) || k.run>=3;
}

// Bounds are EE-only owned values. This contract grants no geometry DMA lease.
template<class Bounds,class Vertex> class Owner {
  Key key_{};
  std::unique_ptr<Bounds> bounds_;
  std::uint64_t lastGeneration_=0;
 public:
  Owner()=default;
  Owner(const Owner&)=delete;
  Owner& operator=(const Owner&)=delete;
  bool compile(const Key& k) {
    if(!valid(k) || k.generation<=lastGeneration_)return false;
    auto next=std::make_unique<Bounds>(reinterpret_cast<const Vertex*>(k.streams[0]),
                                     k.count,k.capacity);
    if(next->getVertexCount()!=k.count || !next->getPartsCount())return false;
    bounds_=std::move(next);key_=k;lastGeneration_=k.generation;return true;
  }
  Bounds* admit(const Key& k) const {
    return bounds_ && key_==k?bounds_.get():nullptr;
  }
  void invalidate(){bounds_.reset();}
  const Key& key() const{return key_;}
};
template<class Bounds,class Vertex> struct Attachment {
  std::unique_ptr<Owner<Bounds,Vertex>> value;
  Attachment()=default;
  Attachment(const Attachment&){}
  Attachment& operator=(const Attachment& b){if(this!=&b)value.reset();return *this;}
  Attachment(Attachment&&)=default;
  Attachment& operator=(Attachment&&)=default;
};
}
