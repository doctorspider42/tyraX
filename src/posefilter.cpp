#include "posefilter.hpp"

#include <cmath>
#include <cstring>

namespace posefilter {

namespace {

// The one-euro smoothing factor for a given cutoff and timestep.
float alphaFor(float cutoff, float dt) {
    if (cutoff <= 0.0f || dt <= 0.0f) return 1.0f;
    const float tau = 1.0f / (6.283185307f * cutoff);
    return 1.0f / (1.0f + tau / dt);
}

float dot4(const float* a, const float* b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
}

void normalize4(float* q) {
    const float l = std::sqrt(dot4(q, q));
    if (l < 1e-8f) {
        q[0] = q[1] = q[2] = 0.0f;
        q[3] = 1.0f;
        return;
    }
    for (int i = 0; i < 4; ++i) q[i] /= l;
}

// Angle between two unit quaternions, radians, sign-insensitive.
float angleBetween(const float* a, const float* b) {
    float d = std::fabs(dot4(a, b));
    if (d > 1.0f) d = 1.0f;
    return 2.0f * std::acos(d);
}

}  // namespace

void PoseFilter::reset() {
    joints_.clear();
    lastTime_ = -1.0f;
}

void PoseFilter::apply(float* rot, size_t joints, float t) {
    if (!params_.enabled || !rot || !joints) return;
    if (joints_.size() != joints) {
        joints_.assign(joints, Joint());
        lastTime_ = -1.0f;
    }

    float dt = 1.0f / 30.0f;
    if (lastTime_ >= 0.0f) {
        const float raw = t - lastTime_;
        // A clock that stalls, repeats or runs backwards must not divide
        // anything; fall back to a nominal step rather than producing infinities
        // that then live in the filter state forever.
        if (raw > 1e-4f && raw < 0.5f) dt = raw;
    }
    lastTime_ = t;

    for (size_t i = 0; i < joints; ++i) {
        float* q = rot + i * 4;
        normalize4(q);
        Joint& j = joints_[i];
        if (!j.have) {
            std::memcpy(j.value, q, sizeof(j.value));
            std::memcpy(j.raw, q, sizeof(j.raw));
            j.speed = 0.0f;
            j.have = true;
            continue;
        }

        // A quaternion and its negation are the same orientation, and mixing
        // the two averages the LONG way round - the joint swings through the
        // body. Align every input to the state before touching it.
        float in[4];
        std::memcpy(in, q, sizeof(in));
        if (dot4(in, j.value) < 0.0f)
            for (int k = 0; k < 4; ++k) in[k] = -in[k];

        // How fast this joint is turning, from the raw samples, itself
        // smoothed - the speed is measured from noise and would otherwise make
        // the cutoff jitter as badly as the signal.
        const float instantSpeed = angleBetween(in, j.raw) / dt;
        const float alphaD = alphaFor(params_.derivativeCutoff, dt);
        j.speed += alphaD * (instantSpeed - j.speed);
        std::memcpy(j.raw, in, sizeof(j.raw));

        // The whole idea: the cutoff opens as the joint moves faster, so a
        // still hand is filtered hard and a thrown punch is barely touched.
        const float cutoff = params_.minCutoff + params_.beta * j.speed;
        const float alpha = alphaFor(cutoff, dt);
        for (int k = 0; k < 4; ++k) j.value[k] += alpha * (in[k] - j.value[k]);
        normalize4(j.value);
        std::memcpy(q, j.value, sizeof(j.value));
    }
}

}  // namespace posefilter
