# Imported by make_arm.py for --mutsplit (with --copycount): which mutable REF
# targets the frame arena copies - qbuffer copy pool vs everything else.
def apply(eng, proj, edit, include):
    p = eng / 'inc/renderer/core/paths/path1/frame_chain_arena.hpp'
    a = ('        } else if (size) {\n'
         '          const auto* ref = resolver(source[1], size, context);\n'
         '          if (!ref) return {}; // resolver contract must be stable\n'
         '          { HardwareTrace::Scope trace("SnapshotMutableCopy", HardwareTrace::Kind::Span, true);\n')
    edit(p, a, a.replace('          if (!ref) return {}; // resolver contract must be stable\n',
                         '          if (!ref) return {}; // resolver contract must be stable\n'
                         '          ++Cld::mutRefs; if (Cld::isPool && Cld::isPool(ref)) Cld::mutPoolB += size * 16;\n'))
    b = ('              if (!ref) return {}; // engine-known resolver must remain stable\n')
    edit(p, b, b + '              ++Cld::mutRefs; if (Cld::isPool && Cld::isPool(ref)) Cld::mutPoolB += size * 16;\n')
    p = proj / 'src/gen/game_physics.gen.cpp'
    edit(p, '  const u32 profScene0 = DEBUG_SHOW_PROFILER ? profTicks() : 0;\n',
         '  const u32 profScene0 = DEBUG_SHOW_PROFILER ? profTicks() : 0;\n  Cld::isPool = &Tyra::StaPipQBuffer::isPoolAddress;\n')
