# Imported by make_arm.py for --snapsegs: FrameChainArena::copyEngineKnownImpl split
# into preflight / chain copy / fixup (+ mutable REF copies), with PMU misses when
# not --lean. Also counts bytes: chain qwords and mutable qwords per frame.
def apply(eng, edit, include):
    p = eng / 'inc/renderer/core/paths/path1/frame_chain_arena.hpp'
    include(p, 'debug/cld_probe.hpp')
    edit(p, '''    { HardwareTrace::Scope trace("SnapshotPreflight", HardwareTrace::Kind::Span, true);
    while (at < qwords) {
      const auto* tag = words + at * 4;
      const uint32_t size = tag[0] & 65535, id = (tag[0] >> 28) & 7;
      if (tag[0] & 0x8c000000u) return {};
      if (size > 1048576 - payloadQwords) return {};
      payloadQwords += size;
      ++tagCount;''', '''    const Cld::M cldS0 = Cld::mark();
    { HardwareTrace::Scope trace("SnapshotPreflight", HardwareTrace::Kind::Span, true);
    while (at < qwords) {
      const auto* tag = words + at * 4;
      const uint32_t size = tag[0] & 65535, id = (tag[0] >> 28) & 7;
      if (tag[0] & 0x8c000000u) return {};
      if (size > 1048576 - payloadQwords) return {};
      payloadQwords += size;
      ++tagCount;''')
    edit(p, '''    uint8_t* target = memory + cursor;
    { HardwareTrace::Scope trace("SnapshotChainCopy", HardwareTrace::Kind::Span, true);
      if (!copyQwords(target, base, qwords)) return {}; }
    uint32_t payload = qwords * 16;
    uint32_t borrowed = 0;
    at = 0;
    { HardwareTrace::Scope trace("SnapshotFixup", HardwareTrace::Kind::Span, true);
    while (at < qwords) {
      const auto* source = words + at * 4;
      auto* tag = reinterpret_cast<uint32_t*>(target) + at * 4;
      const uint32_t size = source[0] & 65535, id = (source[0] >> 28) & 7;
      if (id == 1 || id == 7) at += size + 1;
      else {
        {
          if (size) {
            const uint32_t slot = fixRefOrdinal++;''', '''    uint8_t* target = memory + cursor;
    const Cld::M cldS1 = Cld::mark();
    { HardwareTrace::Scope trace("SnapshotChainCopy", HardwareTrace::Kind::Span, true);
      if (!copyQwords(target, base, qwords)) return {}; }
    uint32_t payload = qwords * 16;
    uint32_t borrowed = 0;
    at = 0;
    const Cld::M cldS2 = Cld::mark();
    { HardwareTrace::Scope trace("SnapshotFixup", HardwareTrace::Kind::Span, true);
    while (at < qwords) {
      const auto* source = words + at * 4;
      auto* tag = reinterpret_cast<uint32_t*>(target) + at * 4;
      const uint32_t size = source[0] & 65535, id = (source[0] >> 28) & 7;
      if (id == 1 || id == 7) at += size + 1;
      else {
        {
          if (size) {
            const uint32_t slot = fixRefOrdinal++;''')
    edit(p, '''    cursor += needed;
    return {target, needed, borrowed, tagCount};''', '''    cursor += needed;
    { const Cld::M cldS3 = Cld::mark();
      Cld::span("Z.pre", cldS0, cldS1); Cld::span("Z.copy", cldS1, cldS2); Cld::span("Z.fix", cldS2, cldS3);
      Cld::scope("Z.chainKB", qwords * 16u * 295u / 1024u); Cld::scope("Z.mutKB", (payload - qwords * 16u) * 295u / 1024u);
      Cld::scope("Z.tags", tagCount * 295u); }
    return {target, needed, borrowed, tagCount};''')
    p = eng / 'src/renderer/core/paths/path1/vif1_queue.cpp'
    include(p)
    a = '''      HardwareTrace::Scope traceEmit("NativeEmission", HardwareTrace::Kind::Span, true);
'''
    edit(p, a, a + '      struct CldE { Cld::M m0 = Cld::mark(); ~CldE() { Cld::span("Z.native", m0, Cld::mark()); } } cldE;\n')
    a = '''  u32 sequence = submitted + 1;
  if (!sequence) sequence = 1;
  submitted = sequence;
#if TYRA_NATIVE_VIF_RECORD || TYRA_FRAME_PIPELINE_SUPPORT
  static_assert'''
    edit(p, 'u32 Vif1Queue::submit(const void* chain, u32 qwords, bool* sourceCopied) {\n',
         'u32 Vif1Queue::submit(const void* chain, u32 qwords, bool* sourceCopied) {\n'
         '  struct CldQ { Cld::M m0 = Cld::mark(); ~CldQ() { Cld::span("Z.submit", m0, Cld::mark()); } } cldQ;\n')
