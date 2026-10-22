/* Modified by TyraX: bounded, host-testable validation before VIF1 submission.
 * Licensed under Apache 2.0. See docs/tyrax2.md. */
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifndef TYRA_VIF1_CHAIN_CHECK
#define TYRA_VIF1_CHAIN_CHECK 0
#endif

namespace Tyra {
namespace Vif1ChainCheck {

enum class Error {
  None, Empty, Alignment, TooLarge, InlineBounds, Reference, UnsupportedTag,
  MissingEnd, TrailingData, UnknownCommand, InvalidUnpack, TruncatedPayload,
  PayloadBudget, UnsupportedControl
};
struct Result {
  Error error = Error::None;
  uint32_t tag = 0;
  uint32_t command = 0;
  uint32_t pending = 0;
  uint32_t tags = 0;
  uint32_t refs = 0;
  explicit operator bool() const { return error == Error::None; }
};
// The resolver owns the readable range proof. Never dereference an unchecked
// DMA address; host tests can map synthetic EE addresses to bounded arrays.
using Resolver = const uint32_t* (*)(uint32_t address, uint32_t qwords,
                                    void* context);

struct Stream {
  Result result;
  uint32_t cl = 1, wl = 1;
  void word(uint32_t value) {
    if (!result) return;
    if (result.pending) { --result.pending; return; }
    const uint32_t cmd = (value >> 24) & 0x7f; // ignore VIF IRQ bit
    const uint32_t num = (value >> 16) & 255;
    const uint32_t imm = value & 65535;
    result.command = cmd;
    if ((cmd & 0x60) == 0x60) {
      const uint32_t vn = (cmd >> 2) & 3, vl = cmd & 3;
      if (vl == 3 && vn != 3) {
        result.error = Error::InvalidUnpack; return;
      }
      uint32_t count = num ? num : 256;
      // In fill mode WL>CL, VIF synthesizes the remaining output slots.
      if (wl > cl) count = (count / wl) * cl +
          ((count % wl) < cl ? count % wl : cl);
      const uint32_t bits = vl == 3 ? 16 : (vn + 1) * (32 >> vl);
      result.pending = (count * bits + 31) / 32;
      return;
    }
    switch (cmd) {
      case 0x01:
        cl = (imm & 255) ? imm & 255 : 256;
        wl = (imm >> 8) ? imm >> 8 : 256;
        return;
      case 0x00: case 0x02: case 0x03: case 0x04: case 0x05:
      case 0x06: case 0x07: case 0x10: case 0x11: case 0x13:
      case 0x14: case 0x15: case 0x17: return;
      case 0x20: result.pending = 1; return;
      case 0x30: case 0x31: result.pending = 4; return;
      case 0x4a: result.pending = (num ? num : 256) * 2; return;
      case 0x50: case 0x51:
        result.pending = (imm ? imm : 65536) * 4; return;
      default: result.error = Error::UnknownCommand; return;
    }
  }
};

// Only complete, linear TTE source chains are accepted. NEXT/CALL/RET and
// scratchpad/stall-control REFs require a different explicit ownership model.
// The caller guarantees that base contains qwords readable quadwords.
inline Result validate(const void* base, uint32_t qwords, Resolver resolver,
                       void* context = nullptr) {
  Stream stream;
  auto& out = stream.result;
  if (!base || !qwords) { out.error = Error::Empty; return out; }
  if (reinterpret_cast<uintptr_t>(base) & 15) {
    out.error = Error::Alignment; return out;
  }
  if (qwords > 65536) { out.error = Error::TooLarge; return out; }
  const auto* words = static_cast<const uint32_t*>(base);
  uint32_t at = 0;
  uint32_t payloadQwords = 0;
  while (at < qwords) {
    out.tag = at;
    ++out.tags;
    const auto* tag = words + at * 4;
    const uint32_t size = tag[0] & 65535;
    const uint32_t id = (tag[0] >> 28) & 7;
    // The queue's CHCR enables TIE. An unexpected tag IRQ can end transfer
    // early; PCE controls are not part of our ABI. Ignore reserved bits:
    // packet2 writes named bitfields without clearing the tag's padding.
    if (tag[0] & 0x8c000000u) {
      out.error = Error::UnsupportedControl; return out;
    }
    // Bound total work as well as packet traversal: repeated valid REF tags
    // must not turn corrupt input into billions of payload reads.
    if (size > 1048576 - payloadQwords) {
      out.error = Error::PayloadBudget; return out;
    }
    payloadQwords += size;
    const uint32_t* payload = nullptr;
    bool end = false;
    if (id == 1 || id == 7) {
      if (size > qwords - at - 1) {
        out.error = Error::InlineBounds; return out;
      }
      payload = tag + 4;
      at += 1 + size;
      end = id == 7;
    } else if (id == 0 || id == 3) {
      ++out.refs;
      const uint32_t address = tag[1];
      if (size) {
        if ((address & 0x8000000f) || !resolver ||
            !(payload = resolver(address, size, context))) {
          out.error = Error::Reference; return out;
        }
      }
      ++at;
      end = id == 0;
    } else {
      out.error = Error::UnsupportedTag; return out;
    }
    stream.word(tag[2]);
    stream.word(tag[3]);
    for (uint32_t i = 0; i < size * 4 && out; ++i)
      stream.word(payload[i]);
    if (!out) return out;
    if (end) {
      if (out.pending) out.error = Error::TruncatedPayload;
      else if (at != qwords) out.error = Error::TrailingData;
      return out;
    }
  }
  out.error = Error::MissingEnd;
  return out;
}

} // namespace Vif1ChainCheck
} // namespace Tyra
