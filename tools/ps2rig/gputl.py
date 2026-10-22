# Imported by make_arm.py for --gputl: a whole-frame GPU timeline sampler.
# A ps2sdk timer alarm fires every ~100 us and classifies what VU1 / GIF /
# VIF1 are doing (the gsample() classes), all frame long, independent of what
# the EE is doing. Printed per window as CLDGT, in per-mille of samples.
def apply(eng, edit):
    p = eng / 'inc/debug/cld_probe.hpp'
    t = p.read_text(encoding='utf-8-sig')
    anchor = 'inline void flush(const char* why) {\n'
    assert t.count(anchor) == 1
    sampler = r'''}  // namespace Cld
#include <timer_alarm.h>
namespace Cld {
inline volatile U gt_n = 0, gt_vu = 0, gt_kick = 0, gt_gif = 0, gt_vif = 0, gt_idle = 0;
inline volatile U gt_on = 0;
inline u64 gtAlarm(s32, u64, u64, void*, void*) {
  U vpu; asm volatile("cfc2 %0, $vi29" : "=r"(vpu));
  const U gif = *reinterpret_cast<volatile U*>(0x10003020);
  const U vif = *reinterpret_cast<volatile U*>(0x10003c00);
  const U d1 = *reinterpret_cast<volatile U*>(0x10009000);
  const U apath = (gif >> 10) & 3;
  ++gt_n;
  if ((vpu & 0x100) && apath == 1) ++gt_kick;
  else if (vpu & 0x100) ++gt_vu;
  else if (apath != 0 || ((gif >> 24) & 31)) ++gt_gif;
  else if ((vif & 3) || (d1 & 0x100)) ++gt_vif;
  else ++gt_idle;
  return 14746;  // ~100 us at the 147.456 MHz bus clock
}
inline void gtStart() {
  if (gt_on) return;
  gt_on = 1;
  const s32 id = SetTimerAlarm(14746, gtAlarm, nullptr);
  printf("CLDGT start id=%d\n", (int)id);
}
inline void gtPrint() {
  const U n = gt_n;
  if (n) printf("CLDGT n=%u vu=%u kick=%u gif=%u vif=%u idle=%u (per mille, whole frame)\n",
                n, gt_vu * 1000 / n, gt_kick * 1000 / n, gt_gif * 1000 / n, gt_vif * 1000 / n, gt_idle * 1000 / n);
  gt_n = gt_vu = gt_kick = gt_gif = gt_vif = gt_idle = 0;
}
'''
    t = t.replace(anchor, sampler + anchor)
    # print with every window, start at the first calibration frame
    a2 = '  printf("CLDK slowN=%u fastN=%u\\n", slowN, fastN);'
    assert t.count(a2) == 1, t.count(a2)
    t = t.replace(a2, '  gtPrint();\n' + a2)
    a3 = '  if (frame == 600 || frame == 1400) calibrate();'
    assert t.count(a3) == 1
    t = t.replace(a3, a3 + '\n  if (frame == 1500) gtStart();')
    p.write_bytes(t.encode())
    print('gpu timeline sampler')
