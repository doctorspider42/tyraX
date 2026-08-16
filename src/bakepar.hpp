#pragma once

#include <algorithm>
#include <atomic>
#include <functional>
#include <thread>
#include <vector>

// Range-partitioned parallel-for, shared by the host bakers - the bvh.hpp
// arrangement, and moved here for the same reason: it lived in gibake.cpp's
// anonymous namespace, aobake needed the identical split, and a second copy
// would have been two subtly different answers to one question.
//
// The contract every caller relies on: an element's result depends ONLY on its
// own inputs, so the partition changes the wall clock and never the answer. The
// bakers are built for that - a sample's random spiral is rotated by a hash of
// its own seed (its texel coordinate, its triangle index, its probe index),
// never by shared RNG state - which is what makes a bake bit-identical at any
// core count and an A/B comparison possible at all.
//
// So a body may write only to element i's own slot. An accumulator shared
// across elements (a "did anything land here" flag) belongs in a per-element
// array folded after the call, never in a captured variable.
namespace bakepar {

// Runs bodyRange(lo, hi) over successive chunks of [0, count) until it is
// consumed. `cancel` is polled once per chunk; a body doing long work per
// element should poll it itself as well.
//
// The schedule is DYNAMIC - threads pull the next chunk from a shared counter
// rather than each taking a fixed slice up front. That is not a refinement, it
// is what makes the split work at all on the bakers' real inputs: a lightmap
// region can be 128x128 or 4x4, so an even slice of the region LIST is a wildly
// uneven slice of the work, and one thread finishes the small ones while
// another still holds the biggest wall in the scene. Measured on
// examples/gi-showcase: static slices reached 270% CPU of a possible 800%.
//
// A body must therefore not assume it sees a contiguous prefix or any
// particular chunk - only that every index is handed out exactly once.
inline void parallelFor(int count, const std::atomic<bool>* cancel,
                        const std::function<void(int, int)>& bodyRange) {
    if (count <= 0) return;
    int threads = (int)std::thread::hardware_concurrency();
    if (threads < 1) threads = 1;
    if (threads > 16) threads = 16;
    if (threads > count) threads = count;
    if (threads == 1) {
        bodyRange(0, count);
        return;
    }
    // ~8 chunks per thread: enough to even out a skewed cost distribution,
    // few enough that the counter is not the bottleneck and that a body with
    // per-chunk setup (the bakers re-prune their object state at a boundary)
    // does not pay it per element.
    int chunk = count / (threads * 8);
    if (chunk < 1) chunk = 1;
    std::atomic<int> next{0};
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t) {
        pool.emplace_back([&] {
            for (;;) {
                const int lo = next.fetch_add(chunk);
                if (lo >= count) return;
                if (cancel && cancel->load()) return;
                bodyRange(lo, std::min(count, lo + chunk));
            }
        });
    }
    for (std::thread& th : pool) th.join();
}

}  // namespace bakepar
