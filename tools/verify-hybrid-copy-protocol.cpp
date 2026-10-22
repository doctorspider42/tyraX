#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include "tyrax2-hybrid-copy-protocol.hpp"
using P = Tyra::HybridCopyProtocol;
using S = P::Stage;
static_assert(sizeof(P)==36 && sizeof(P::Ready)==12,"Fixed protocol ABI");

int main() {
  unsigned guards = 0, schedules = 0;
  for (unsigned job : {0u, 1u, 42u, 0xffffffffu}) {
    for (unsigned mask = 0; mask < 32; ++mask) {
      P p;
      assert(p.arm(job, 17, true, true));
      assert(!p.arm(job + 1, 18, true, true));
      P::Ready r{job, bool(mask & 1), bool(mask & 2), bool(mask & 4),
                 bool(mask & 8), bool(mask & 16)};
      assert(!p.claimISR(r, 17));
      assert(p.blankStart());
      assert(!p.blankStart());
      assert(!p.claimISR(r, 18));
      auto stale = r; stale.job = job + 1;
      assert(!p.claimISR(stale, 17));
      assert(p.claimISR(r, 17) == (mask == 31));
      assert(!p.claimISR(r, 17));
      assert(p.sourceProtected());
      assert(!p.release(job));
      ++guards;
    }
    for (unsigned publication = 0; publication < 4; ++publication) {
      P p;
      assert(p.arm(job, 17, publication & 1, publication & 2) == (publication == 3));
      ++guards;
    }
    // Enumerate both serializations of every main/ISR operation, including
    // early/stale finishes and source release attempts before completion.
    std::array<unsigned, 8> order{0,1,2,3,4,5,6,7};
    do {
      P p; assert(p.arm(job, 17, true, true));
      bool dma = false, finish = false, gif = false, issued = false, completed = false;
      unsigned copies = 0;
      for (unsigned event : order) {
        P::Ready r{job, dma, finish, gif, true, true};
        switch (event) {
          case 0: assert(p.blankStart()); break;
          case 1: dma = true; break;
          case 2: finish = true; break;
          case 3: gif = true; break;
          case 4:
            if (p.claimISR(r, 17)) { issued = true; ++copies; }
            break;
          case 5:
            if (p.withdraw(job) && p.claimFallback(r, 17)) { issued = true; ++copies; }
            break;
          case 6:
            assert(p.finish(job, issued, issued) == issued);
            completed = issued;
            break;
          case 7:
            assert(p.release(job) == completed);
            break;
        }
        assert(copies <= 1);
        if (p.state() == S::Idle) assert(completed);
        assert(!p.finish(job + 1, true, true));
      }
      assert(p.blankEnd());
      assert(!p.blankEnd());
      assert(p.late() == unsigned(issued && !completed));
      if (p.state()==S::Copying) assert(p.finish(job,true,true));
      if (p.state()==S::Done) assert(p.release(job));
      if (p.state()==S::Withdrawn) {
        assert(p.blankStart());
        P::Ready ready{job,true,true,true,true,true};
        assert(p.claimFallback(ready,17));
        assert(p.finish(job,true,true) && p.release(job));
        assert(p.blankEnd());
      }
      assert(p.state()==S::Idle && !p.sourceProtected());
      ++schedules;
    } while (std::next_permutation(order.begin(), order.end()));
  }
  P p; P::Ready r{5, true, true, true, true, true};
  assert(p.blankStart() && p.arm(5,17,true,true) && p.claimISR(r,17));
  assert(!p.finish(5,false,true) && !p.finish(5,true,false));
  assert(p.blankEnd() && p.late()==1);
  P fresh;P::Ready proof{8,true,true,true,true,true};
  assert(fresh.arm(8,20,true,true) && fresh.blankStart());
  assert(fresh.withdraw(8) && !fresh.claimFallback(proof,20));
  assert(fresh.blankEnd() && fresh.blankStart());
  assert(fresh.claimFallback(proof,20) && fresh.finish(8,true,true));
  assert(fresh.release(8) && fresh.blankEnd());
  P wrap(0xfffffffeu);
  for (unsigned job : {0xffffffffu,0u,1u}) {
    P::Ready ready{job,true,true,true,true,true};
    assert(wrap.arm(job,17,true,true) && wrap.blankStart());
    assert(wrap.claimISR(ready,17) && wrap.finish(job,true,true));
    assert(wrap.release(job) && wrap.blankEnd() && wrap.late()==0);
  }
  assert(p.finish(5,true,true) && p.release(5));
  assert(p.blankStart() && p.arm(6,18,true,true));
  r.job=6;assert(p.claimISR(r,18) && p.finish(6,true,true) && p.release(6));
  assert(p.arm(7,19,true,true));r.job=7;
  assert(!p.claimISR(r,19)); // Even a released packet cannot reuse this blank.
  assert(p.withdraw(7) && !p.claimFallback(r,19));
  assert(p.blankEnd() && p.blankStart());
  assert(p.claimFallback(r,19) && p.finish(7,true,true) && p.release(7));
  assert(p.blankEnd() && p.late()==1);
  std::printf("HYBRIDCOPY guards=%u schedules=%u lifecycle=PASS late-copy-detection=PASS\n",guards,schedules);
}
