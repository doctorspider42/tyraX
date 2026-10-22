#pragma once
#include "renderer/core/paths/path1/frame_vif_writer.hpp"

namespace NativeSegments {
struct Span {
  const void* data = nullptr;
  uint32_t qwords = 0, offset = 0;
  explicit operator bool() const { return data && qwords; }
};

// Storage partition only. The queue owns publication, ordered sequence fences
// and bank leases; constructing a new Cursor on reused memory requires a fence.
class Cursor {
 public:
  Cursor(void* memory, uint32_t capacity)
      : memory_(static_cast<uint8_t*>(memory)), capacity_(capacity),
        writer_(memory, capacity) {}
  Tyra::FrameVifWriter& writer() { return writer_; }
  uint32_t committedQwords() const { return committed_; }
  bool sealed() const { return sealed_; }
  Span seal() {
    if (sealed_ || !writer_.checkpoint()) return {};
    const uint32_t size = writer_.finish();
    if (!size) return {};
    committed_ = offset_ + size;
    sealed_ = true;
    return {writer_.data(), size, offset_};
  }
  bool advance() {
    if (!sealed_ || capacity_ - committed_ < 2) return false;
    offset_ = committed_;
    writer_ = Tyra::FrameVifWriter(memory_ + size_t(offset_) * 16,
                                  capacity_ - offset_);
    sealed_ = false;
    return true;
  }
 private:
  uint8_t* memory_;
  uint32_t capacity_, offset_ = 0, committed_ = 0;
  Tyra::FrameVifWriter writer_;
  bool sealed_ = false;
};
}  // namespace NativeSegments
