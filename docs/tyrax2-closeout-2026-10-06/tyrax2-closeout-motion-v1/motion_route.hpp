#pragma once
#include <pad/pad.hpp>

// Private controller only. Caller passes ordinary NightSampler::frame.
// No clock, DMA/presentation wait, host file polling or object teleport.
namespace CloseoutMotion {
struct State { unsigned char r2, l2, lh, lv, rh, rv; };
inline State state(unsigned frame, unsigned route) {
  const unsigned offset = frame % 1800U;
  State s = {0, 0, 127, 127, 127, 127};
  if (route == 1) {
    if (offset >= 800 && offset < 992) s.r2 = 1;
    if (offset >= 864 && offset < 928) s.lh = 167;
    if (offset >= 928 && offset < 992) s.lh = 87;
    if (offset >= 992 && offset < 1056) s.l2 = 1;
    if (offset >= 1056 && offset < 1120) s.rh = 155;
  } else if (route == 2) {
    if (offset >= 800 && offset < 896) s.lv = 47;
    if (offset >= 896 && offset < 1024) s.lv = 207;
    if (offset >= 1056 && offset < 1120) s.rh = 145;
  }
  return s;
}
inline void apply(Tyra::Pad& pad, unsigned frame, unsigned route) {
  const State s = state(frame, route);
  const State previous = state(frame == 0 ? 0 : frame - 1, route);
  Tyra::PadButtons held = {}, clicked = {};
  held.R2 = s.r2; held.L2 = s.l2;
  clicked.R2 = s.r2 && !previous.r2;
  clicked.L2 = s.l2 && !previous.l2;
  pad.setState(held, clicked, s.lh, s.lv, s.rh, s.rv);
}
}  // namespace CloseoutMotion
