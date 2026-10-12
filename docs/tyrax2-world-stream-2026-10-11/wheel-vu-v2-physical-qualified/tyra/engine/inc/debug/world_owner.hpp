#pragma once
#ifdef WORLD_BODY_HOST
#include "tyrax2-world-strip-plan.hpp"
#else
#include "debug/world_strip_plan.hpp"
#endif
#include <cassert>

namespace WorldOwner {
using Quad = WorldStripPlan::Quad;
// Ordinary bag copies retain their old API but never inherit a prepared owner.
template<class Owner> class Attachment {
 public:
  Attachment() = default;
  Attachment(const Attachment&) {}
  Attachment& operator=(const Attachment& other) {
    if(this!=&other)value_.reset();
    return *this;
  }
  explicit operator bool()const{return bool(value_);}
  Owner* operator->()const{return value_.get();}
  void reset(Owner* p=nullptr){value_.reset(p);}
 private:
  std::unique_ptr<Owner> value_;
};
struct Key {
  std::uint64_t generation = 0;
  std::uint32_t programEpoch = 0, programAddress = 0, primitive = 0;
  std::uint32_t depthScale = 0, vertices = 0, run = 0, bboxVersion = 0;
  std::array<std::uint32_t,4> content{};
  bool operator==(const Key& b) const {
    return generation == b.generation && programEpoch == b.programEpoch &&
      programAddress == b.programAddress && primitive == b.primitive &&
      depthScale == b.depthScale && vertices == b.vertices && run == b.run &&
      bboxVersion == b.bboxVersion &&
      content == b.content;
  }
};

// Spans delegates to the existing Vif1Queue immutable-span lifecycle.
template<class Spans> class Pool {
  struct Entry {
    Key key;
    std::vector<Quad> body;
    std::uint32_t rawSequence = 0;
    std::unique_ptr<Entry> next;
  };
 public:
  explicit Pool(std::uint32_t maxQwords) : budget_(maxQwords) {}
  Pool(const Pool&) = delete;
  Pool& operator=(const Pool&) = delete;
  ~Pool() {
    Spans::drain(); collect();
    assert(active_ == 0 && !retired_ && used_ == 0);
  }
  void collect() {
    auto* link = &retired_;
    while (*link) {
      if (Spans::collect((*link)->body.data())) {
        used_ -= static_cast<std::uint32_t>((*link)->body.capacity());
        auto old = std::move(*link);
        *link = std::move(old->next);
      } else link = &(*link)->next;
    }
  }
  std::uint32_t usedQwords() const { return used_; }
  std::uint32_t remainingQwords() const { return budget_ - used_; }
  class Owner {
   public:
    explicit Owner(Pool& pool) : pool_(pool) {}
    Owner(const Owner&) = delete;
    Owner& operator=(const Owner&) = delete;
    ~Owner() { invalidate(); }
    bool publish(const Key& key, std::vector<Quad> body) {
      if (key.generation <= publishedGeneration_) return false;
      auto next = pool_.create(key,std::move(body));
      if (!next) return false;
      pool_.retire(current_); current_ = std::move(next);
      publishedGeneration_ = key.generation;
      return true;
    }
    const std::vector<Quad>* admit(const Key& key) const {
      return current_ && current_->key == key ? &current_->body : nullptr;
    }
    // Caller submits immediately, before allowing owner replacement/collection.
    void submitted(const Key& key, std::uint32_t sequence, bool sourceCopied) {
      assert(current_ && current_->key == key);
      if (!sourceCopied) {
        assert(sequence != 0);
        current_->rawSequence = sequence;
      }
    }
    void invalidate() { pool_.retire(current_); }
   private:
    Pool& pool_;
    std::uint64_t publishedGeneration_ = 0;
    std::unique_ptr<Entry> current_;
  };
 private:
  std::unique_ptr<Entry> create(const Key& key, std::vector<Quad> body) {
    if (!key.generation || !key.programEpoch || key.programAddress > 2047 ||
        key.vertices < 3 || key.run < 3 || key.run % 3 || body.empty() ||
        body.size() > 65535 || body.capacity() > budget_ - used_)
      return nullptr;
    auto entry = std::make_unique<Entry>();
    entry->key = key; entry->body = std::move(body);
    if (!Spans::add(entry->body.data(),static_cast<std::uint32_t>(entry->body.size())))
      return nullptr;
    used_ += static_cast<std::uint32_t>(entry->body.capacity()); ++active_;
    return entry;
  }
  void retire(std::unique_ptr<Entry>& entry) {
    if (!entry) return;
    if (entry->rawSequence) Spans::waitFor(entry->rawSequence);
    Spans::retire(entry->body.data());
    entry->next = std::move(retired_); retired_ = std::move(entry); --active_;
  }
  std::uint32_t budget_ = 0, used_ = 0, active_ = 0;
  std::unique_ptr<Entry> retired_;
};
}
