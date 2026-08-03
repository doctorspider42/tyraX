# iop-compute — computing on the PS2's other CPU

A minimal project that runs its own code on the **IOP**: the R3000A that exists
so a PlayStation 2 can be a PlayStation 1, and that Sony's documentation
reserves for I/O. See [docs/iop-compute.md](../../docs/iop-compute.md) for the
whole story; this project is the smallest thing that demonstrates it working
*and* demonstrates the fallback when it does not.

## What it does

`iop/user_jobs.c` carries two jobs, compiled into this project's own IRX module
and embedded in the game's ELF:

| Job | What it computes |
| --- | --- |
| 0 | Sum of squares of its inputs — the round-trip proof |
| 1 | An integer Mandelbrot escape count in 16.16 fixed point — a real workload |

The `player-1` object's flow graph, on scene start:

```
On Start ──► Branch ──(true)──► Run IOP Job (job 1, c = -0.5 + 0i, 200 iters)
               ▲                   ├─(done)──► Log "escape count = <var>"
               │                   └─(no IOP)► Log "IOP vanished ... EE path"
        IOP Available              
               └────(false)──────► Log "No usable IOP ... EE instead"
```

Two things that layout is making a point about. The **Branch on IOP Available**
is how every use of this feature should start — the IOP may not be usable and
that is an ordinary state, not an error. And **Run IOP Job's own `no IOP`
output** exists so the fallback cannot be forgotten even by someone who skipped
the check.

## What to look for

Build and run (**F5**, or `tyrax-editor --build . --run`), then read
`bin/log.txt`:

```
LOG: IOP compute: ready. 2 job(s), 1468416 bytes free on the IOP,
     round trip 146us, calibration 7988us vs 1255us on the EE.
LOG: IOP job 1 (Mandelbrot, 200 iters) escape count = 200
```

The first line is the availability gate reported as data — and every number in
it is worth reading:

- **2 job(s)** — the size of the table in `iop/user_jobs.c`. Add a function
  there and this becomes 3.
- **1468416 bytes free** — about 1.4 MB of the IOP's 2 MB. Note it is *less*
  than a bare IOP has free (measured 1.68 MB with nothing else loaded): audsrv,
  padman, sio2man and the `host:` filesystem live there too. The IOP is not
  yours alone, and this number is the proof.
- **round trip 146us** — one RPC call with no work in it (it varies run to run, 146-197us measured, because the IOP is busy with other things). A 50 Hz frame is
  20 ms, so a job that takes less than ~150 µs is *slower* here than on the EE.
- **calibration 7988us vs 1255us** — the same integer loop on both CPUs. The
  IOP is about **6.4× slower**. It is not a second fast core; it is a slow core
  that is otherwise idle.

The second line is the job's answer. **200 is the correct answer**: the point
c = −0.5 + 0i is inside the Mandelbrot set, so the orbit never escapes and the
loop runs its full 200 iterations. That the IOP agrees is the whole point — the
gate already proved it computes a known hash correctly, and this proves it runs
*your* code.

## Turn it off and watch it degrade

*Project > Preferences > Build > IOP compute* off, then rebuild. The game still
runs and the graph still works — it takes the `false` branch and logs the EE
path instead. Nothing in the project has to change, because `txiop::available()`
is a compile-time `false` in that build and every entry point folds away. The
ELF also gets about 9.5 KB smaller: the module and its runtime are simply not
there.

That is the property worth taking away: **the same graph, the same scripts and
the same project ship to a console that can help and to one that cannot.**

## Where the code is

| File | |
| --- | --- |
| `iop/user_jobs.c` | **your code on the IOP.** User-ownable — delete the marker line on top before editing, or the editor regenerates it |
| `iop/txiop.c`, `iop/txiop.h`, `iop/imports.lst` | the module around your jobs: RPC server, wire protocol, kernel imports. Generated |
| `inc/txiop.gen.hpp` | the EE-side API: `available()`, `call()`, `submit()`/`poll()`, `info()` |
| `Makefile` | the IRX build rules, after the `include` |

For calling jobs asynchronously — which is what the feature is actually for,
since a blocking call costs the round trip inside your frame — see the
`txiop::submit` / `txiop::poll` example in
[docs/iop-compute.md](../../docs/iop-compute.md).

## The honest limits

Everything above was verified in PCSX2, which emulates a full IOP. Three claims
it cannot settle, and this README will not pretend otherwise:

1. **The real speed ratio.** PCSX2's IOP timing is not cycle-accurate. On
   hardware the clocks are 294.912 MHz against 36.864 MHz.
2. **Audio contention.** On a console the IOP really is streaming audio. Whether
   a long job makes it crackle is a hardware question — the module's worker
   thread runs at a low priority precisely because of it.
3. **Deckard slims.** PCSX2 emulates a pre-Deckard IOP, so "this is a fat" is
   checkable here and "this is a slim's emulated R3000A" is not.
