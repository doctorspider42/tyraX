#include "tyrax2-native-segments.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
struct alignas(16) Quad { uint32_t w[4]; };
alignas(16) Quad payload[2] = {{{1,2,3,4}}, {{5,6,7,8}}};
// Packed AD GIF tag and its sole GS FINISH register write, in the final window.
alignas(16) Quad finish[2] = {{{0x8001,0,0xe,0}}, {{0,0,0x61,0}}};
const uint32_t* resolve(uint32_t address, uint32_t qwords, void*) {
  return address == 0x1000 && qwords <= 2 ? payload[0].w : nullptr;
}
bool operation(Tyra::FrameVifWriter& writer, unsigned i) {
  switch (i % 5) {
    case 0: return writer.inlineVif(0x01000101, 0x13000000, nullptr, 0);
    case 1: return writer.inlineVif(0, 0x6c020020, payload, 2);
    case 2: return writer.referencedVif(0, 0x6c020030, 0x1000, 2, resolve);
    case 3: return writer.inlineVif(0, 0x14000120, nullptr, 0);
    default: return writer.inlineVif(0, 0x13000000, nullptr, 0);
  }
}
std::vector<uint32_t> flatten(const NativeSegments::Span& span) {
  assert(Tyra::Vif1ChainCheck::validate(span.data, span.qwords, resolve));
  const auto* words = static_cast<const uint32_t*>(span.data);
  std::vector<uint32_t> result;
  for (unsigned i = 0; i < span.qwords;) {
    const auto* tag = words + i * 4;
    const auto id = tag[0] >> 28, count = tag[0] & 65535;
    if (id == 7) {
      // Only the added terminal tag's two clean NOPs are normalized away.
      assert(!count && !tag[1] && !tag[2] && !tag[3]);
      assert(i + 1 == span.qwords);
      break;
    }
    assert(id == 1 || id == 3);
    result.push_back(tag[2]); result.push_back(tag[3]);
    const auto* data = id == 1 ? tag + 4 : resolve(tag[1], count, nullptr);
    assert(data || !count);
    if (count) result.insert(result.end(), data, data + count * 4);
    i += id == 1 ? 1 + count : 1;
  }
  return result;
}
struct Saved {
  NativeSegments::Span span;
  std::vector<uint32_t> bytes;
};
void intact(const std::vector<Saved>& saved) {
  for (const auto& s : saved)
    assert(!std::memcmp(s.span.data, s.bytes.data(), s.bytes.size()*4));
}
}

int main() {
  unsigned cases = 0;
  for (unsigned operations = 1; operations <= 80; ++operations)
  for (unsigned cadence = 1; cadence <= operations; ++cadence) {
    std::array<Quad, 1024> whole{}, split{};
    Tyra::FrameVifWriter oracle(whole.data(), whole.size());
    NativeSegments::Cursor cursor(split.data(), split.size());
    std::vector<Saved> saved;
    std::vector<uint32_t> observed;
    auto seal = [&]() {
      const auto span = cursor.seal(); assert(span);
      const auto* first = static_cast<const uint32_t*>(span.data);
      saved.push_back({span, {first, first + span.qwords*4}});
      const auto stream = flatten(span);
      observed.insert(observed.end(), stream.begin(), stream.end());
      assert(!cursor.seal());
    };
    assert(!cursor.seal() && !cursor.advance());
    for (unsigned i = 0; i < operations; ++i) {
      assert(operation(oracle, i)); assert(operation(cursor.writer(), i));
      intact(saved);
      if ((i + 1) % cadence == 0 && i + 1 < operations) {
        seal(); assert(cursor.advance()); assert(!cursor.advance());
        // Rewind never reaches an earlier submitted window.
        assert(cursor.writer().rewind(0)); intact(saved);
      }
    }
    assert(oracle.inlineVif(0, 0x50000002, finish, 2));
    assert(cursor.writer().inlineVif(0, 0x50000002, finish, 2));
    seal(); intact(saved);
    const auto count = oracle.finish(); assert(count);
    assert(observed == flatten({whole.data(), count, 0}));
    unsigned finishWrites = 0;
    for (const auto& s : saved) {
      const auto stream = flatten(s.span);
      for (unsigned i = 0; i + 3 < stream.size(); ++i)
        if (!stream[i] && !stream[i+1] && stream[i+2] == 0x61 && !stream[i+3])
          ++finishWrites;
    }
    assert(finishWrites == 1);
    for (unsigned i = 1; i < saved.size(); ++i)
      assert(saved[i].span.offset == saved[i-1].span.offset + saved[i-1].span.qwords);
    ++cases;
  }
  for (unsigned capacity = 1; capacity <= 128; ++capacity) {
    std::array<Quad, 130> memory{};
    for (auto& q : memory) for (auto& word : q.w) word = 0xfeedface;
    NativeSegments::Cursor cursor(memory.data(), capacity);
    std::vector<Saved> saved;
    for (;;) {
      if (!operation(cursor.writer(), 0)) break;
      const auto span = cursor.seal(); assert(span);
      const auto* first = static_cast<const uint32_t*>(span.data);
      saved.push_back({span, {first, first + span.qwords*4}});
      intact(saved);
      if (!cursor.advance()) break;
    }
    const auto before = memory;
    assert(!operation(cursor.writer(), 1));
    assert(!std::memcmp(before.data(), memory.data(), sizeof(memory)));
    intact(saved);
    for (unsigned i = capacity; i < memory.size(); ++i)
      for (auto word : memory[i].w) assert(word == 0xfeedface);
    ++cases;
  }
  alignas(16) unsigned char bytes[64]{};
  for (unsigned bad = 0; bad < 3; ++bad) {
    NativeSegments::Cursor cursor(bad == 0 ? nullptr : bytes + (bad == 1 ? 1 : 0),
                                  bad == 2 ? 0 : 4);
    assert(!operation(cursor.writer(), 0));
    assert(!cursor.seal() && !cursor.advance()); ++cases;
  }
  std::printf("PASS native segments: %u actual-writer cases; no queue/PS2/FPS claim\n", cases);
}
