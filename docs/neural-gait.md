# Neural gait: a learned pose corrector on VU0

[Foot IK](foot-ik.md) fixes a foot *after* the clip has already put it in the
wrong place. It is a solver, and a solver only knows about now.

What it cannot do is the half of stair walking that happens before the foot
lands: choosing where to put the next one, shifting weight onto the leading
leg, leaning into the climb — and above all **shortening the stride**. People
take quicker, smaller steps going up a flight. No amount of IK produces that,
because the clip's timing is not something a solver has any handle on.

So the stage in front of it is a small neural network that reads the ground
ahead and rewrites the stride.

## Yes, on a PlayStation 2

VU0 and VU1 are SIMD vector units. For an entire console generation they
computed geometry and nothing else — nobody had a reason to point that much
multiply-accumulate throughput at anything but transforms.

The arithmetic works out with room to spare. VU0 in macro mode issues COP2
instructions next to the EE at 294 MHz; the inner loop here is load, load,
multiply, add per four MACs, so call it ~150–300 MMAC/s after overhead. At
60 fps a 1 ms slice of the frame buys 150–300 thousand MACs:

| Net | MAC/frame | Budget at 60 fps |
|---|---|---|
| residual corrector, 22→64→64→n | ~8 k | under 50 µs — effectively free |
| 3 × 128 hidden units | ~50 k | 0.2–0.4 ms |
| PFNN as published (512 × 4) | ~500 k | 2–4 ms — a 30 fps hero character |

The weights stream straight out of main memory through `lqc2`, so the net's
size is bounded by the 32 MB of RAM rather than by VU0's 4 KB of data memory
— that limit only applies to microprograms, and this is not one. A 128-unit
three-layer net is about 50 KB.

Nothing here needed a new microprogram, a spare DMA channel or a frame of
latency. It is a matrix-vector product, on the unit that already skins the
mesh, in the gap the skinning is not using.

## What the net actually decides

Inputs (`MotionFeatures`, 22 values):

- **phase** as sin/cos through the locomotion clip
- **speed**, **turn rate**, **strafe fraction**
- a **3 × 3 probe grid** of ground heights in the character's own frame —
  behind, under and ahead, times left/centre/right. Sampling in the
  character's frame rather than the world's is what makes a learned gait
  direction-independent: the net sees "a step 0.35 ahead of me", never "a
  step at world +X"
- per leg, **what the ground did to that foot last frame** (the IK's own
  smoothed offset and whether it was planted). The cheapest possible memory
  of the terrain, and the only input that tells the net whether the previous
  step landed

Outputs: an exponential-map **rotation delta per listed joint** — legs,
pelvis, spine, whatever the rig binds — plus one scalar, the **phase rate**.
That last one is the stride. The game multiplies its animation step by it,
and it is the only output that leaves the pose.

## The rules it plays by

**It never moves the character.** Pose and playback rate only. Collision,
triggers and the camera see exactly the world they saw before, so a net that
drifts is ugly and never wrong. This is the line that makes shipping a
learned system on gameplay-critical code sane at all.

**A missed probe reads as "the floor is where I am"**, not as a hole. Feeding
an extreme value to a net on a probe pattern it never saw in training is
precisely how you get something confident and spectacular.

**The feature layout is versioned and a mismatch is refused at load.** A net
silently handed shifted columns does not crash — it produces a plausible,
confident, wrong pose, which is the worst failure mode available. `.tnet`
carries `kFeatureVersion` and `MotionNetLoader` returns `nullptr` rather than
guess.

**A missing net is never fatal.** The character walks without it, IK still
plants the feet, and a warning goes in the log — the same contract every
other asset loader in the engine follows.

**Outputs are low-passed, not poses.** A net evaluated per frame on a stepped
input (the probes cross a tread edge in one frame) is jittery by
construction. Smoothing the outputs costs one multiply-add each; smoothing
poses would cost a second blend.

**VU0 floats are not IEEE.** No infinities, no NaN — overflow saturates and
denormals are zero. Training has to clamp activations accordingly; ReLU is
free (one `max`) and is what the format assumes.

## The file

`.tnet`, written by the editor and read by `MotionNetLoader`, following the
`.tskl` conventions: four-byte magic `TXNN`, a version read as a range,
packed little-endian, counts followed by inline arrays. It carries the
feature version, the weights and biases, the joint list the outputs map onto,
the probe pattern the net was trained with, and the two scales that turn raw
outputs back into radians and into a playback rate.

Weight rows are padded to a multiple of four floats at load and the whole net
lives in one allocation advanced onto a 16-byte boundary — `lqc2` requires
the alignment and `std::vector` only promises four bytes. The pad columns are
zero, so they contribute nothing and there is no tail to special-case in the
inner loop.

## See also

- [docs/foot-ik.md](foot-ik.md) — the stage that runs after this one
- [docs/animated-models.md](animated-models.md) — the skeletal runtime both
  hang off
