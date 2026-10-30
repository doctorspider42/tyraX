# Traffic and physics investigation

This plan records the user's revised performance target and the required
independent physics reviews before the next console experiment.

## Current checkpoint

- Branch: `codex/ps2-collision-pipeline`, existing ignored worktree.
- Current shared vehicle/prop normal-interval run is finished and archived in
  `results/vehicle-contacts-impact-timing-2026-10-30.*`.
- Shared contacts and opposite impulses work with one full object pass, but
  the measured dense scene is more expensive; this is not a speedup.
- The previously planned double-interval timing is deferred. No new console
  run starts before both requested independent model consultations return.
- All scratch stays under the primary checkout `.tmp`; never power off the
  computer or console. Stop only owned processes identified by command line.

## Independent consultations

1. GPT-6 Astra agent: physics challenge is primary, PR285 review additional.
2. Opus via Claude CLI: a traffic review and a separate physics-focused review,
   both restricted to read-only source inspection. No edits, builds, console
   control or external messages are delegated.

Ask for detailed implementable EE/FPU/VU0 designs: exact offload stage,
matrices/Jacobians/support features, record layouts, batching within4KB,
transfer/readback costs, useful EE work during micro execution, first launch,
fences and macro ownership. Account for exact21-axis SAT/linear CCD,
canonical EE confirmations, six velocity/three position passes, scalar
sequential dependencies, finite-mass/yaw constraints and FPU/VU operation-order
hazards. Read existing measured dead ends before proposing another backend.
Do not pretend FPU is a third independent thread or promise magical VU gains.
Review proposals against existing code and measurements before implementation.

## Primary practical target: traffic

Investigate [PR285](https://github.com/doctorspider42/tyraX/pull/285), fetched as
`origin/pr-285` at `8fc9f33a78e4bdabfe443b5354f7ee660c01a321`.
It is open and conflicting against main at inspection time. Do not overwrite
its branch or unrelated primary-checkout work. Preserve the current branch.

Target physical PS2 normal-clock60 FPS with approximately5 HQ racing vehicles
and5–8 lowpoly civilian vehicles. Define actual geometry/material/wheel/light/
reflection/shadow costs and near/far interaction behavior; labels alone are
not a workload. Separate AI/traffic, ground queries, collision, vehicle model,
EE submission, VU1/GS and DMA/cache costs. Check PR timing claims against their
actual hardware/emulator, camera, distance and quality scopes. The six-car
emulator result is not evidence for thirteen nearby interactive vehicles.

Use a controlled1/3/5/10/13-car scaling matrix and repeat both A/B orders.
Measure full frame periods/work, p50/p95/misses, per-car CPU/model/collision
costs and render passes. Preserve normal-clock gameplay and view/quality
coverage. Prefer concrete data/scheduling/cache/batching fixes over unpriced
quality cuts. Implement supported improvements, push with docs in each commit,
and repeat physical-console validation.60 FPS is a target, not a promise.

## Physics and realistic roadside workload

Add100 physical obstacles separated along a road, with no initial overlap or
stacking, and drive through them. Record placement gaps/hulls, live nearby/
awake counts, actual contacts, car travel and all dropped/missed counters.
Distinguish the total placed count from simultaneous active contact workload.
Compare against a dense pile only as a stress case, not the main design target.

The user still observes boxes penetrating one another when stacked. This is
UNRESOLVED: passing finite-state and canonical-hit tests does not establish
adequate separation or solver convergence. Measure penetration over time,
contact manifolds, split-position residuals, wake/sleep and timing/CCD policy.
Separate convergence/contact-generation/numerical causes from simple overload.
Preserve collision coverage; do not silently omit axes, contacts or physics
updates to improve a timing number. Validate any correction on isolated pairs,
stacks, separated roadside obstacles and vehicle interactions.

## Completion evidence

Save the independent recommendations and their model identities. Record each
accepted/rejected hypothesis and implementation/test outcome. Keep raw inputs,
ELF/scenario identity and hardware logs; do not add gains from unrelated source
baselines. Report remaining limits and any target that cannot be demonstrated
on the measured scene. Keep active worktree/evidence for review and clean
finished temporary helpers when no longer needed.
