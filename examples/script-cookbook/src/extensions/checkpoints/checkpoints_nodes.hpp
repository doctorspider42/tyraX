// checkpoints_nodes.hpp - C++ behind the Checkpoints extension's flow node
// (flow-nodes/checkpoints-done.flownode names it with `call =` and
// `include = checkpoints_nodes.hpp`; the generated flow graph includes this
// file). docs/script-extensions.md.
#pragma once

#include "scripts/script.hpp"

TYRAX_NAMESPACE_BEGIN

// Kept by the Checkpoint script (checkpoints.cpp).
int checkpointsReached();
int checkpointsTotal();

/** Checkpoints Done: have at least num0 checkpoints been reached (0 = all)? */
inline void checkpointsDone(ScriptContext&, FlowNodeIO& io) {
  const int need = io.num[0] > 0.0F ? (int)io.num[0] : checkpointsTotal();
  io.boolOut = need > 0 && checkpointsReached() >= need;
}

TYRAX_NAMESPACE_END
