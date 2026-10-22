# PCCR operation-mode primary-source verification

Earlier USER labeling is confirmed; no source/runtime correction is made. Sony EE Core User's Manual v6.0 printed84 (register reference) and144 (section7.1.1) independently give the exact bit assignment:

| Counter | EXL(Level1 handler) | Kernel outside handler | Supervisor | User | Event |
|---|---|---|---|---|---|
| PCR0 | bit1 | bit2 | bit3 | bit4 | bits9:5 |
| PCR1 | bit11 | bit12 | bit13 | bit14 | bits19:15 |

CTE bit31 enables counting and exception generation. Configuration0x800340D0 has CTE1; both event fields6; U0/U1=1; EXL0/1,K0/1,S0/1=0. Stopped0x000340D0 retains the same selectors/mode bits with CTE0. Installed/pinned ps2stuff SetupPerfCounters sets bits4 and14 plus event fields and master enable, which is precisely USER selection, not an EXL selection. Its historical choice does not prove the application's actual operation mode.

Printed146 section7.2.1 explicitly requires CTE, matching selected processor operating mode, and not executing a Level2 handler before an event increments. Current Scene activation therefore requires actual operating-mode qualification: configured USER-only counters could remain zero while code executes in Kernel/Supervisor/EXL. Access to COP0 Count alone is insufficient evidence of the selected mode, and standalone opcode compilation proves neither mode nor event activation. No actual Status read or runtime was performed here.

A bounded leaf with finite instructions/no waits can in principle support a finite direct-work event-count proof if each event's applicable generation semantics and maximum number per operation are primary-qualified. This review has no new primary guarantee converting static instruction count into selector6 event bound; the manual says increment1 per generated event, not that arbitrary instruction/cycle counts are interchangeable. Async preemption remains distinct: PMU is global, mode filtered but not thread filtered. An enabled interval can include another thread in the same counted operation mode; an unbounded scheduling gap is not bounded by the leaf's own finite code. Different-mode handler work may be excluded by this USER-only mask, but another counted-mode thread still is not. A future bounded leaf needs execution-environment/ownership and preemption qualification in addition to its static work proof. No runtime release or lifetime approval follows.

Captured exact primary-page text and PDF/ps2stuff/current-header hashes are attached. No event totals, current cost, execution mode or hardware safety inferred.
