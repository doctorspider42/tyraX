// iop_fractal.cpp - this file is yours.
//
// The point of examples/iop-compute: the IOP is not printing a number, it is
// COMPUTING THE WORLD. Sixteen pillars stand in a row and their heights are the
// escape counts of a row of the Mandelbrot set. The imaginary coordinate sweeps,
// so the row the pillars show slides through the fractal and the skyline
// reshapes itself continuously - and every one of those numbers came off the
// R3000A that is in a PS2 so it can be a PS1.
//
// Three things this script is arranged to demonstrate, and they are the reason
// it is a script rather than a flow graph:
//
//  1. ASYNCHRONOUS. txiop::submit() hands the row over and returns; the frame
//     carries on drawing. Some frames later poll() picks the answer up. The
//     blocking txiop::call() (and the Run IOP Job flow node) would put the whole
//     job inside one frame, and this job is worth several milliseconds.
//  2. A REAL FALLBACK. solveOnEe() below is the same arithmetic in C++. With no
//     usable IOP the pillars move exactly the same way - the EE just pays for
//     it. Turn the IOP compute preference off and watch nothing break.
//  3. COLOUR SAYS WHO DID IT. Green pillars = the IOP solved this row, amber =
//     the EE did. You can see which CPU is working from across the room.
//
// The solver is one file-scope static shared by all sixteen instances (the
// pillars all need the same row); each instance only knows which COLUMN it is,
// and it works that out from where it was placed. So there is no name lookup,
// no controller object, and adding a seventeenth pillar in the editor needs no
// code change.
#include "scripts/script.hpp"
#include "txiop.gen.hpp"

namespace Iop_compute {

namespace {

// Columns = pillars. The job returns one escape count per column, so this is
// also the width of the row the IOP computes.
constexpr int COLS = 16;

// Iterations before a point counts as "inside". The whole cost of the job:
// COLS * MAX_ITER fixed-point orbits. 128 keeps one row at a few milliseconds
// on the IOP, which is exactly the shape this feature is for - too big to want
// inside a frame, small enough to land within a few of them.
constexpr int MAX_ITER = 64;

// The window of the complex plane the pillars show. Chosen deliberately: it
// straddles the set's western boundary, where the escape-time bands are, so the
// sixteen pillars show a GRADED staircase rolling through rather than a wall
// that is either fully up or fully down. A wider window spends most of the
// sweep flat - the profile was checked against a reference implementation of
// the same fixed-point loop before this window was picked.
constexpr float RE_MIN = -1.6F;
constexpr float RE_MAX = -0.6F;
constexpr float IM_RANGE = 0.6F;    // sweeps -IM_RANGE .. +IM_RANGE
constexpr float SWEEP_PER_SEC = 0.13F;

// How tall a pillar gets per escape count. MAX_ITER * this is the tallest.
constexpr float HEIGHT_PER_ITER = 0.06F;

// Where the row of pillars stands. A pillar works out WHICH column it is from
// its own X, so these two constants are the only thing the script and the scene
// have to agree on - move the row in the editor and only COL_X0 changes.
constexpr float COL_X0 = -6.375F;
constexpr float COL_STEP = 0.85F;

// 16.16 fixed point - the IOP has no FPU, so everything crossing the bus is an
// integer. Converting here keeps the job free of floats entirely.
inline int toFx(float v) { return (int)(v * 65536.0F); }

int g_escape[COLS];        // the row the pillars are currently showing
bool g_haveRow = false;    // false until the first row lands
bool g_rowFromIop = false; // who computed the row on show
bool g_inFlight = false;   // a job is out with the IOP
int g_rowsSolved = 0;
int g_solverFrame = -1;    // the frame the solver has already been driven on
float g_im = -IM_RANGE;    // imaginary coordinate of the NEXT row to ask for
int g_frame = 0;

// The EE twin of job 2 in iop/user_jobs.c. Deliberately the same arithmetic in
// the same fixed point rather than a float version: a fallback that computes
// something subtly different is a fallback nobody can trust, and keeping it
// identical is also what lets the two be compared.
void solveOnEe(int ci, int reStart, int reStep, int maxIter, int* out) {
  for (int c = 0; c < COLS; c++) {
    const int cr = reStart + reStep * c;
    int zx = 0, zy = 0, i = 0;
    for (; i < maxIter; i++) {
      const int zx2 = (int)(((long long)zx * zx) >> 16);
      const int zy2 = (int)(((long long)zy * zy) >> 16);
      if (zx2 + zy2 > (4 << 16)) break;
      const int nzy = (int)(((long long)zx * zy) >> 15) + ci;
      zx = zx2 - zy2 + cr;
      zy = nzy;
    }
    out[c] = i;
  }
}

// Every so often, print the row that just landed. This is not decoration: a
// skyline is hard to assert on from a screenshot, and the log makes the sweep
// measurable - consecutive lines MUST differ, and they must differ in the shape
// a Mandelbrot row does. Debug builds only (TYRA_LOG compiles out under
// NDEBUG), and rate-limited so it does not flood the file.
void logRow(const char* who) {
  if ((g_rowsSolved % 24) != 1) return;
  TYRA_LOG("IOP fractal row ", g_rowsSolved, " (", who, ") im=",
           (int)(g_im * 100.0F), "/100: ", g_escape[0], " ", g_escape[2], " ",
           g_escape[4], " ", g_escape[6], " ", g_escape[8], " ", g_escape[10],
           " ", g_escape[12], " ", g_escape[14]);
}

// Advance the sweep and wrap it, so the skyline loops rather than stopping.
void advanceSweep() {
  g_im += SWEEP_PER_SEC * g_frameDt * (float)(g_rowsSolved > 0 ? 1 : 0);
  if (g_im > IM_RANGE) g_im = -IM_RANGE;
}

// Called by whichever instance gets there first each frame - the pillars share
// one row, so the solver must run once per frame and not sixteen times.
void driveSolver() {
  if (g_solverFrame == g_frame) return;
  g_solverFrame = g_frame;
  advanceSweep();

  const int reStart = toFx(RE_MIN);
  const int reStep = toFx((RE_MAX - RE_MIN) / (float)(COLS - 1));

  // 1. Collect a finished row.
  if (g_inFlight) {
    int out[COLS], n = 0;
    if (txiop::poll(out, COLS, &n)) {
      g_inFlight = false;
      if (n >= COLS) {
        for (int c = 0; c < COLS; c++) g_escape[c] = out[c];
        g_haveRow = true;
        g_rowFromIop = true;
        g_rowsSolved++;
        if (g_rowsSolved == 1)
          TYRA_LOG("IOP fractal: first row solved on the IOP (", COLS,
                   " columns, ", MAX_ITER, " iterations each).");
        logRow("iop");
      }
    }
    return;  // one job in flight at a time - nothing else to do this frame
  }

  // 2. Ask for the next one. This is the whole gate: if the IOP cannot help,
  //    the same row is computed here instead and the pillars never notice.
  const int args[4] = {toFx(g_im), reStart, reStep, MAX_ITER};
  if (txiop::available() && txiop::submit(2, args, 4)) {
    g_inFlight = true;
    return;
  }

  solveOnEe(args[0], reStart, reStep, MAX_ITER, g_escape);
  g_haveRow = true;
  g_rowFromIop = false;
  g_rowsSolved++;
  if (g_rowsSolved == 1)
    TYRA_LOG("IOP fractal: no usable IOP - solving on the EE instead.");
  logRow("ee");
}

}  // namespace

class IopFractalColumn : public ObjectScript {
 public:
  void onStart(ScriptContext& ctx) override {
    (void)ctx;
    baseY_ = self->data.position[1];
    // Which column this pillar is: derived from where it was PLACED, so the
    // scene is the configuration and there is nothing to keep in sync.
    col_ = (int)((self->data.position[0] - COL_X0) / COL_STEP + 0.5F);
    if (col_ < 0) col_ = 0;
    if (col_ >= COLS) col_ = COLS - 1;
    // The pillars move every frame, so ask for the matrix fast path: a
    // world-space geometry rebuild per pillar per frame (self->dirty) is the
    // expensive path this engine warns about, and position-only motion does
    // not need it once the object has been promoted.
    self->wantsMatrixPath = true;
    lastColor_ = -1;
  }

  void onUpdate(ScriptContext& ctx) override {
    (void)ctx;
    g_frame++;
    driveSolver();
    if (!g_haveRow) return;

    self->data.position[1] = baseY_ + (float)g_escape[col_] * HEIGHT_PER_ITER;
    // Until renderScene promotes us, a move still needs the rebuild flag; after
    // that the matrix path picks the position up on its own.
    if (!self->onMatrixPath) self->dirty = true;

    // Green = the IOP solved this row, amber = the EE did. Only written when it
    // CHANGES: a colour change is a real geometry rebuild, and doing it every
    // frame would undo the point of the matrix path above.
    const int want = g_rowFromIop ? 1 : 0;
    if (want != lastColor_) {
      lastColor_ = want;
      self->data.color[0] = want ? 0.25F : 1.0F;
      self->data.color[1] = want ? 1.0F : 0.65F;
      self->data.color[2] = want ? 0.45F : 0.15F;
      self->dirty = true;
    }
  }

 private:
  float baseY_ = 0.0F;
  int col_ = 0;
  int lastColor_ = -1;
};

TYRA_OBJECT_SCRIPT(IopFractalColumn);

}  // namespace Iop_compute
