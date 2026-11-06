# Imported by make_arm.py for --subsegs: where Vif1Queue::submit spends its time.
# Lean ticks: snapshot (preflight + chain copy + fixup) vs native record emission.
def apply(eng, edit, include):
    p = eng / 'src/renderer/core/paths/path1/vif1_queue.cpp'
    include(p)
    a = '''    auto snapshot = copyForNativeRecording(chain, qwords, sourceCopied,
                                          snapshotRecords, snapshotCounted);
'''
    edit(p, a, '    const unsigned cldQ0 = Cld::ticks();\n' + a +
         '    Cld::scope("Q.snap", Cld::ticks() - cldQ0);\n')
    a = '''      HardwareTrace::Scope traceEmit("NativeEmission", HardwareTrace::Kind::Span, true);
'''
    edit(p, a, a + '      struct CldE { unsigned t0 = Cld::ticks(); ~CldE() { Cld::scope("Q.native", Cld::ticks() - t0); } } cldE;\n')
