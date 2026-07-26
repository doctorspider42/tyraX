#pragma once

#include <vector>

// Takes the shake out of a captured pose without taking the movement with it.
//
// Monocular body tracking is noisy by nature: the solver re-estimates every
// joint from scratch each frame, so a performer standing perfectly still still
// arrives shimmering. Averaging fixes that and ruins everything else - a fixed
// smoothing strong enough to settle a still hand puts visible lag on a punch.
//
// The one-euro filter is the standard answer and the reason is worth stating:
// its cutoff RISES WITH SPEED. Slow movement is mostly noise, so it filters
// hard; fast movement is mostly signal, so it gets out of the way. One filter,
// no mode switch, and the two failure modes trade against each other with a
// single knob instead of fighting.
namespace posefilter {

struct Params {
    bool enabled = true;
    // These three are not taste. They came out of a sweep scored against a known
    // signal, weighted the way an eye weights the defects: shimmer on a standing
    // figure is the most visible thing there is, three degrees of lag mid-punch
    // is invisible. At this setting a still joint goes from 1.85 to 0.48 degrees
    // of jitter and from 1.22 to 0.58 of error, and a fast gesture pays about
    // three degrees for it.
    //
    // The cutoff at rest, in hertz. Lower is calmer and laggier.
    float minCutoff = 0.5f;
    // How fast the cutoff opens up with speed. Higher follows quick motion more
    // closely at the cost of letting more noise through with it.
    float beta = 2.0f;
    // Cutoff for the speed estimate itself - the speed is measured from noisy
    // samples, so it needs smoothing of its own or the filter chases its tail.
    // The textbook 1 Hz is tuned for a mouse pointer and is far too slow for a
    // body: it could not follow a 2 Hz gesture, the cutoff never opened in time,
    // and the filter sat 18 degrees behind a punch.
    float derivativeCutoff = 3.0f;
};

// One filter per skeleton, holding a state per joint. Feed it frames in order.
class PoseFilter {
  public:
    void configure(const Params& p) { params_ = p; }
    const Params& params() const { return params_; }

    // Filters `rot` (joints * 4, x y z w) in place. `t` is the frame's own
    // timestamp in seconds - the filter is time-based, not frame-based, so a
    // dropped frame or a rate change does not change how much it smooths.
    //
    // The first frame passes through untouched: there is nothing to smooth
    // toward yet, and starting from identity would make every session begin
    // with the character melting into its pose.
    void apply(float* rot, size_t joints, float t);

    // Forget everything. Needed whenever the stream JUMPS rather than moves -
    // a new performer, tracking regained, a recentre - because smoothing across
    // a jump drags the character through the gap instead of cutting to it.
    void reset();

  private:
    struct Joint {
        float value[4] = {0, 0, 0, 1};   // last filtered
        float raw[4] = {0, 0, 0, 1};     // last input, for the speed estimate
        float speed = 0.0f;              // filtered angular speed, rad/s
        bool have = false;
    };
    Params params_;
    std::vector<Joint> joints_;
    float lastTime_ = -1.0f;
};

}  // namespace posefilter
