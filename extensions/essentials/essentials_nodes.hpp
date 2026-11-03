// essentials_nodes.hpp - C++ bodies of the Essentials extension's flow nodes
// (flow-nodes/*.flownode name them with `call =` and `include = essentials_nodes.hpp`;
// the generated flow graph includes this file). docs/script-extensions.md.
#pragma once

#include "scripts/script.hpp"

TYRAX_NAMESPACE_BEGIN

/** Player Near: is the player within num0 units of the target object? */
inline void essentialsPlayerNear(ScriptContext& ctx, FlowNodeIO& io) {
  const tyrax::ObjectRef o = tyrax::object(ctx, io.object);
  io.boolOut = o.valid() && tyrax::distanceToPlayer(ctx, o.position()) <= io.num[0];
}

TYRAX_NAMESPACE_END
