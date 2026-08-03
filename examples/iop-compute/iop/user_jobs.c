// examples/iop-compute took ownership of this file (the generated marker
// line is gone), because it carries a third job the template does not: the
// Mandelbrot ROW the pillars in the scene are shaped by. That is also the
// demonstration - your jobs live here and the editor stops touching it.
//
// Your code, running on the IOP. Add a function, add it to the table at the
// bottom, and the EE can call it by index - from a script (txiop::call /
// txiop::submit) or from the "Run IOP Job" flow node.
//
// Three rules, all of them consequences of what the IOP is:
//
//   * INTEGER ONLY. The IOP has no FPU, so a float here is a software-emulated
//     crawl. Pass fixed point if you need fractions (the example below does).
//   * NO libc. This is a bare IRX: no malloc, no printf, no memcpy unless you
//     import it in iop/imports.lst. Static arrays and plain loops.
//   * BE WORTH THE TRIP. One call costs ~110 us round trip, so a job that
//     takes 10 us is slower than doing it on the EE. Jobs should be chunky and
//     their results should be awaited asynchronously.
//
// The IOP is also not yours alone - it runs the pad, the audio and the dev
// filesystem. A job that spins for a whole frame will be felt elsewhere.
#include "txiop.h"

// Job 0: sum of squares of the inputs. The "hello world" - it proves the round
// trip carries data both ways and that the arithmetic happened over there.
static int jobSumSquares(const int* in, int inCount, int* out, int outMax) {
  int i;
  int acc = 0;
  if (outMax < 1) return 0;
  for (i = 0; i < inCount; i++) acc += in[i] * in[i];
  out[0] = acc;
  return 1;
}

// Job 1: an integer Mandelbrot escape count in 16.16 fixed point - a real
// workload rather than a toy. in[0]=cx, in[1]=cy (16.16), in[2]=max
// iterations; out[0] = the escape iteration. Chunky enough to be worth the
// round trip and a fair demonstration of what the IOP costs: it is the same
// arithmetic the EE would do, just on a CPU that is several times slower and
// otherwise idle.
static int jobMandelIter(const int* in, int inCount, int* out, int outMax) {
  int cx, cy, maxIter, i;
  int zx = 0, zy = 0;
  if (inCount < 3 || outMax < 1) return 0;
  cx = in[0];
  cy = in[1];
  maxIter = in[2];
  if (maxIter > 100000) maxIter = 100000;
  for (i = 0; i < maxIter; i++) {
    /* 16.16 multiply: (a*b) >> 16, kept in 32 bits, so the orbit is clamped
     * rather than wrapped when it escapes. */
    int zx2 = (int)(((long long)zx * zx) >> 16);
    int zy2 = (int)(((long long)zy * zy) >> 16);
    if (zx2 + zy2 > (4 << 16)) break;
    {
      int nzx = zx2 - zy2 + cx;
      int nzy = (int)(((long long)zx * zy) >> 15) + cy;
      zx = nzx;
      zy = nzy;
    }
  }
  out[0] = i;
  return 1;
}


// Job 2: one ROW of the Mandelbrot set - the job examples/iop-compute is built
// around, and the shape of work this whole feature is for. Sixteen escape counts
// out of a few thousand fixed-point orbits: expensive to compute, tiny to carry.
//
//   in[0] = imaginary coordinate (16.16)
//   in[1] = real coordinate of column 0 (16.16)
//   in[2] = real step between columns (16.16)
//   in[3] = iteration ceiling
//   out[0..15] = escape count per column
//
// The EE keeps an identical copy of this loop (src/scripts/iop_fractal.cpp,
// solveOnEe) as the fallback. Change the arithmetic here and change it there, or
// the skyline will jump the moment a console cannot use its IOP.
static int jobMandelRow(const int* in, int inCount, int* out, int outMax) {
  int ci, reStart, reStep, maxIter;
  int cols = 16;
  int c;
  if (inCount < 4) return 0;
  ci = in[0];
  reStart = in[1];
  reStep = in[2];
  maxIter = in[3];
  if (maxIter > 512) maxIter = 512;
  if (outMax < cols) cols = outMax;

  for (c = 0; c < cols; c++) {
    const int cr = reStart + reStep * c;
    int zx = 0, zy = 0, i = 0;
    for (; i < maxIter; i++) {
      /* 16.16 multiplies through 64-bit intermediates. This is the expensive
       * part on a 32-bit CPU with no FPU, and it is deliberately the same
       * expression the EE fallback uses. */
      const int zx2 = (int)(((long long)zx * zx) >> 16);
      const int zy2 = (int)(((long long)zy * zy) >> 16);
      if (zx2 + zy2 > (4 << 16)) break;
      {
        const int nzy = (int)(((long long)zx * zy) >> 15) + ci;
        zx = zx2 - zy2 + cr;
        zy = nzy;
      }
    }
    out[c] = i;
  }
  return cols;
}

// The table the module dispatches through. The index IS the job number game
// code and the flow node use, so appending is safe and reordering is not.
TxiopJobFn txiopJobs[] = {
    jobSumSquares,
    jobMandelIter,
    jobMandelRow,
};
int txiopJobCount = (int)(sizeof(txiopJobs) / sizeof(txiopJobs[0]));
