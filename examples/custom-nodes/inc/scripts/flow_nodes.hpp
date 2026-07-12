// This file is owned by the project: its editor ownership-marker line was
// removed, so the editor no longer regenerates it. That is the workflow for
// writing your own custom-node C++ (see docs/custom-flow-nodes.md).
#pragma once

// C++ bodies for custom flow-graph nodes. A .flownode file with `call = fn`
// runs the function `fn` here when an exec link reaches the node. Signature:
//
//     void fn(ScriptContext& ctx, FlowNodeIO& io);
//
// `io` (see FlowNodeIO in script.hpp) carries the node's inputs - io.object
// (the target object index), io.position/io.boolIn/io.text (wired inputs),
// io.num (num0..3 params), io.str (string param) - and its outputs: write
// io.objectOut / io.positionOut / io.boolOut / io.textOut and the game latches
// them for downstream nodes (custom OR built-in - e.g. an object output can
// feed a built-in Hide Object).
#include "scripts/script.hpp"

namespace Custom_nodes {

// Output the VISIBLE scene object nearest the player - a stand-in for a
// "what am I looking at" raycast. Wired into a built-in Hide Object (via the
// object output) it lets the player hide crates one at a time: each press picks
// the next-nearest still-visible crate. Runs whenever its node fires, so keep
// it cheap.
inline void flowNearestVisible(ScriptContext& ctx, FlowNodeIO& io) {
  int best = -1;
  float bestDist = 0.0F;
  for (int i = 0; i < ctx.objectCount; ++i) {
    if (i == io.self || !ctx.objects[i].active || !ctx.objects[i].visible) continue;
    const float* p = ctx.objects[i].data.position;
    const float dx = p[0] - ctx.playerPosition.x;
    const float dy = p[1] - ctx.playerPosition.y;
    const float dz = p[2] - ctx.playerPosition.z;
    const float d = dx * dx + dy * dy + dz * dz;
    if (best < 0 || d < bestDist) {
      best = i;
      bestDist = d;
    }
  }
  io.objectOut = best;  // -1 = nothing left; downstream object refs handle that
}

}  // namespace Custom_nodes
