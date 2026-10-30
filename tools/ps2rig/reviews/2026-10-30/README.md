# Independent traffic and physics reviews

These are independent source-based recommendations requested by the user;
they are not implementations or hardware-qualified speedup claims.

- [GPT-6 Astra: traffic](astra-traffic-review.md).
- [GPT-6 Astra: general physics](astra-physics-review.md).
- [Claude CLI Opus: traffic](opus-review.md).
- [Claude CLI Opus: general physics](opus-physics-review.md).
- [Model/ref/response identities](models.json).

Astra was explicitly configured as `gpt-6-astra`. Both restricted read-only
Claude CLI sessions requested alias `opus` and reported `claude-opus-5-5` in
their successful JSON result. No reviewer ran a new build or console test.
The parent completed the existing run before requesting the next experiment.

## Parent assessment before implementation

### Accepted for immediate diagnosis

1. The split-position passes pursue one fixed Baumgarte target; they are not
   repeated geometric half-penetration removals. Verify isolated analytical
   decay and actual post-writeback overlap rather than increasing passes blindly.
2. Sleep ignores residual depth/pseudo correction. Reproduce whether this
   freezes overlap; do not globally disable sleep or call it proven root cause.
3. A slightly inset corner can select a lateral minimum-depth plane while
   vertical overlap is larger. Exact boundary equality is rejected by
   `rbInsideHull`, so the test must sweep offsets/yaw rather than claim the
   exact-alignment corner counterexample. Compare generated normals/manifold
   with canonical SAT and post-solve overlap.
4. Add100 separated roadside props with validated initial collider gaps,
   total/nearby/awake/contact counts and a scripted drive. Keep dense piles a
   separate correctness stress case.

### Accepted low-risk performance candidates, subject to A/B gates

- Bound and fairly schedule non-driver paint rebuilds. The current deferred
  flag can let several deferred cars bypass the claimed one-per-frame limit.
- Index procedural static colliders and remove linear object-to-vehicle lookups
  when measured in the intended traffic scene, preserving order and generations.
- In the already-unified solver path, kind2 physical-prop vehicle-gather entries
  only fill the now-unused bumper list. Filter that vehicle-physics candidate
  view and its cache/oracle consistently; DO NOT delete the global entries,
  because camera sweeps still consume them. The common body grid remains the
  dynamic collision authority. This shortcut is invalid on legacy PR traffic
  until its prop contacts actually use the shared solver.

### Promising VU0 experiments, deferred until contact quality/baselines exist

- Resident micro solve of small independent islands while EE solves disjoint
  larger islands. Preserve within-island Gauss-Seidel order and all contacts;
  oversized islands fall back whole to EE. Benchmark identical captured inputs,
  transfers, fences, eligibility and whole-frame cost before selecting it.
- Normal-only contact preparation on micro, then price optional lazy/eager
  effective-mass matrices. The SAT and preparation/solve programs share ONE
  VU0 owner; they cannot run simultaneously. A larger symmetric response matrix
  is not an excuse to invert a general3x3 matrix per contact.
- Certified feature/axis results can reduce canonical EE replay only after a
  defensible error bound. Keep the saved late positive-contact regression.

## Corrections and proposals not accepted as written

- Opus's proposed island ABI reconstructs inertia from local diagonal terms.
  Existing irregular hulls have all six symmetric inertia terms and chassis
  uses a constrained WORLD yaw tensor. Preserve full world inverse inertia;
  a five-qword upload can contain v, w and three world-inertia columns instead.
- Branch-free0/1 masks do not make division/sqrt on invalid zero inputs safe.
  Preserve the early-out domain or substitute demonstrably safe operands.
- A fixed certificate K=64 scaled only by coordinate magnitude is not yet a
  proof for normalized near-parallel cross axes, reciprocal/sqrt behavior,
  actual transformed vertices or first-axis ties. Collect statistics/prove
  domains before allowing results to bypass canonical EE evaluation.
- No awake props does NOT alone prove no chassis/prop contact. Prewake has
  motion thresholds and a scene-index dependency; stationary residual overlap
  and stale/custom collider bounds need an explicit conservative proof. Do
  not implement the suggested car-only early-out from awake flags alone.
- A sleep residual estimate must include BOTH bodies and angular pseudo motion,
  or use actual final-pose geometry. `remaining - dot(vp,n)` alone is incomplete.
- Making a finite-mass car infinite for an extra pass changes the model and
  conflicts with the present opposite-impulse contract; it is not an exact fix.
- Historical rejected inner-scope profiling is not qualified exclusive stage
  attribution. Do not reuse its quoted corner/preparation savings as evidence.
- Macro prediction rejection does not prove every micro prediction design
  impossible; nevertheless early pose arithmetic has already caused drift and
  is lower priority. Likewise alleged first divergence must be localized before
  assigning it to a specific FMA instruction.
- Opus's fourteen-car wording (player plus five racers plus eight civilians)
  exceeds the user's intended five HQ TOTAL plus5–8 civilians. Define the
  population explicitly. Its millisecond budget table is a hypothesis, not
  measured independent stages that can automatically be added.
- Far traffic camera distance is not collision authority. Promotion must occur
  before interaction and preserve post-impact dynamics. Do not silently skip
  nearby contacts for an FPS figure.

The next implementation decision follows the diagnostic results and actual
normal-clock traffic attribution. Preserve full collision coverage, keep
quality tradeoffs explicit, and do not enable an unqualified production backend.
