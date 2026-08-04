#include "procgraph.hpp"

#include <algorithm>
#include <cmath>
#include <set>

// The node library. Every entry states only what the node HAS; `desc` is
// mandatory by convention (it is the node's documentation everywhere in the
// editor). Parameter ranges are the UI slider bounds AND the clamp the
// evaluator trusts, so a value read back from an old project is always sane.
const std::vector<ProcNodeType>& procNodeTypes() {
    using PK = ProcParamKind;
    static const std::vector<ProcNodeType> types = {
        // --- Sources ------------------------------------------------------
        {.key = "ScatterSurface",
         .title = "Scatter on Surface",
         .category = "Sources",
         .ins = {{.label = "density", .type = ProcType::Mask, .optional = true}},
         .outs = {{.label = "points", .type = ProcType::Points}},
         .params =
             {{.key = "density", .label = "Density", .kind = PK::Float,
               .def = 6.0f, .lo = 0.01f, .hi = 400.0f,
               .tip = "Points per 100 square units of footprint."},
              {.key = "target", .label = "Surface", .kind = PK::ObjectName,
               .tip = "Empty = the scene terrain. An object name scatters over "
                      "that object's surface instead (models sample their "
                      "triangles proportionally to area, so a dense mesh "
                      "region does not get more points)."},
              {.key = "lift", .label = "Height offset", .kind = PK::Float,
               .def = 0.0f, .lo = -512.0f, .hi = 512.0f,
               .tip = "Raises (or lowers) every point off the surface it "
                      "landed on, following the ground rather than flattening "
                      "it. 0 = resting on the surface."},
              {.key = "max", .label = "Max points", .kind = PK::Int,
               .def = 20000.0f, .lo = 1.0f, .hi = 200000.0f,
               .tip = "Hard cap on generated candidates - the safety net "
                      "against a mistyped density."}},
         .desc = "Scatters points over a surface inside the Scatter volume: "
                 "the terrain by default, or one object's mesh. Points come "
                 "out of a fixed low-discrepancy sequence, so Density picks a "
                 "PREFIX of it - raising density adds points between the "
                 "existing ones instead of reshuffling them. Each point gets "
                 "the surface normal, slope (degrees) and height attributes. "
                 "An optional mask input multiplies the local density."},

        {.key = "ScatterGrid",
         .title = "Scatter on Grid",
         .category = "Sources",
         .ins = {{.label = "density", .type = ProcType::Mask, .optional = true}},
         .outs = {{.label = "points", .type = ProcType::Points}},
         .params =
             {{.key = "spacing", .label = "Spacing", .kind = PK::Float,
               .def = 4.0f, .lo = 0.1f, .hi = 128.0f,
               .tip = "World units between grid cells."},
              {.key = "jitter", .label = "Jitter", .kind = PK::Float,
               .def = 0.35f, .lo = 0.0f, .hi = 1.0f,
               .tip = "Random offset inside the cell (1 = the full cell)."},
              {.key = "snap", .label = "Snap to surface", .kind = PK::Bool,
               .def = 1.0f,
               .tip = "Drop each point onto the terrain and read its normal. "
                      "Off = the lattice sits flat at the volume's own centre "
                      "height instead."},
              {.key = "lift", .label = "Height offset", .kind = PK::Float,
               .def = 0.0f, .lo = -512.0f, .hi = 512.0f,
               .tip = "Where the FIRST level sits, measured from whatever Snap "
                      "chose as the base - the terrain under each point, or the "
                      "volume's centre. This is how a stack starts above the "
                      "ground instead of on it."},
              {.key = "levels", .label = "Levels", .kind = PK::Int, .def = 1.0f,
               .lo = 1.0f, .hi = 64.0f,
               .tip = "Stacked copies of the whole lattice, so the source is a "
                      "3D grid instead of a floor plan: a tower of rooms, a "
                      "shelf wall, a voxel frame. Works with Snap either way - "
                      "on, each column starts at its own ground height and the "
                      "stack follows the terrain."},
              {.key = "levelstep", .label = "Level height", .kind = PK::Float,
               .def = 4.0f, .lo = 0.1f, .hi = 512.0f,
               .tip = "World units between levels."}},
         .desc = "A regular lattice covering the volume footprint, with "
                 "optional per-cell jitter. Orchards, fence posts, city "
                 "blocks - anything that should read as planted rather than "
                 "grown. Levels > 1 stacks the lattice upward into a full 3D "
                 "grid. The mask input drops cells below its value."},

        // The block-world source. It is one node rather than a chain because
        // the interesting part is what it does NOT emit: a solid field's
        // interior is invisible, so only blocks with an exposed face come out
        // at all, and each of those carries a `faces` mask that costs the
        // merger the hidden faces too. Left as a scatter chain that would be
        // thousands of points filtered down to hundreds - here it is hundreds
        // from the start, which is the difference between a block world that
        // fits on this machine and one that does not.
        {.key = "BlocksFill",
         .title = "Blocks Fill",
         .category = "Sources",
         .ins = {{.label = "height", .type = ProcType::Mask, .optional = true}},
         .outs = {{.label = "points", .type = ProcType::Points}},
         .params =
             {{.key = "block", .label = "Block size", .kind = PK::Float,
               .def = 2.0f, .lo = 0.25f, .hi = 32.0f,
               .tip = "World size of one cube. The lattice is anchored on the "
                      "volume's own corner, so blocks always line up."},
              {.key = "levels", .label = "Max height", .kind = PK::Int,
               .def = 8.0f, .lo = 1.0f, .hi = 32.0f,
               .tip = "Tallest column, in blocks. 32 is the ceiling because the "
                      "runtime collision field packs a column into one 32-bit "
                      "word."},
              {.key = "floor", .label = "Floor layers", .kind = PK::Int,
               .def = 1.0f, .lo = 0.0f, .hi = 32.0f,
               .tip = "Columns are never shorter than this, so the world has a "
                      "solid ground plane instead of holes."},
              {.key = "scale", .label = "Feature size", .kind = PK::Float,
               .def = 30.0f, .lo = 2.0f, .hi = 1024.0f,
               .tip = "World units per hill. Bigger = smoother landscape."},
              {.key = "octaves", .label = "Octaves", .kind = PK::Int, .def = 3.0f,
               .lo = 1.0f, .hi = 6.0f,
               .tip = "Layers of noise detail; each one halves the feature "
                      "size."},
              {.key = "relief", .label = "Relief", .kind = PK::Float, .def = 1.0f,
               .lo = 0.0f, .hi = 1.0f,
               .tip = "How much of Max height the noise actually uses. 0 = a "
                      "flat slab."},
              {.key = "depth", .label = "Emit depth", .kind = PK::Int,
               .def = 2.0f, .lo = 1.0f, .hi = 32.0f,
               .tip = "How many blocks below a column's top are emitted. The "
                      "rest of the column exists for COLLISION but is never "
                      "drawn - it cannot be seen. 1 = a shell one block thick, "
                      "which is all you need on flat ground and shows seams on "
                      "cliffs; 2-3 covers ordinary terrain."},
              {.key = "base", .label = "Base Y", .kind = PK::Float, .def = 0.0f,
               .lo = -2000.0f, .hi = 2000.0f,
               .tip = "World Y of the bottom of the lowest block layer."}},
         .desc = "Fills the volume's footprint with a landscape of stacked "
                 "cubes and emits one point per VISIBLE block - interior blocks "
                 "never leave this node, and every emitted one carries a "
                 "'faces' mask so the merge can drop the faces a neighbour "
                 "covers. Column height comes from layered noise (or the mask "
                 "input, which replaces it). Also writes 'depth' (0 = the top "
                 "block of its column, 1 = the one under it, ...) and 'height' "
                 "(world Y), which is what a Filter by Attribute chain uses to "
                 "give the surface grass, the layer below dirt and the deep "
                 "stone. In a RUNTIME volume the solid field is also published "
                 "as the world's collision, so the player walks on the blocks "
                 "rather than through them."},

        {.key = "ScatterVolume",
         .title = "Scatter in Volume",
         .category = "Sources",
         .outs = {{.label = "points", .type = ProcType::Points}},
         .params = {{.key = "count", .label = "Count", .kind = PK::Int,
                     .def = 120.0f, .lo = 1.0f, .hi = 50000.0f,
                     .tip = "Points placed inside the volume box."}},
         .desc = "Fills the Scatter object's box with points in 3D - no "
                 "surface snapping, so Y comes from the box. For hanging "
                 "props, floating debris, cave clutter."},

        {.key = "ScatterCurve",
         .title = "Scatter along Curve",
         .category = "Sources",
         .ins = {{.label = "curve", .type = ProcType::Curve}},
         .outs = {{.label = "points", .type = ProcType::Points}},
         .params =
             {{.key = "spacing", .label = "Spacing", .kind = PK::Float,
               .def = 3.0f, .lo = 0.05f, .hi = 128.0f,
               .tip = "World units between instances along the curve."},
              {.key = "count", .label = "Count", .kind = PK::Int, .def = 0.0f,
               .lo = 0.0f, .hi = 20000.0f,
               .tip = "0 = use Spacing. Otherwise this many instances spread "
                      "evenly over the whole curve."},
              {.key = "offset", .label = "Side offset", .kind = PK::Float,
               .def = 0.0f, .lo = -64.0f, .hi = 64.0f,
               .tip = "Shift sideways from the curve - lamp posts along one "
                      "side of a road."},
              {.key = "jitter", .label = "Spacing jitter", .kind = PK::Float,
               .def = 0.0f, .lo = 0.0f, .hi = 1.0f,
               .tip = "Randomizes the gap between instances."},
              {.key = "align", .label = "Face along curve", .kind = PK::Bool,
               .def = 1.0f,
               .tip = "Yaw each instance to the curve direction (fence "
                      "segments, guard rails)."},
              {.key = "snap", .label = "Snap to surface", .kind = PK::Bool,
               .def = 1.0f, .tip = "Drop each point onto the terrain."}},
         .desc = "Places instances along a curve at a fixed spacing (or a "
                 "fixed count), optionally offset to one side and yawed to "
                 "follow the curve. Writes the 't' attribute (0..1 along the "
                 "curve) so downstream nodes can vary anything with distance."},

        {.key = "Point",
         .title = "Single Point",
         .category = "Sources",
         .outs = {{.label = "points", .type = ProcType::Points}},
         .params =
             {{.key = "target", .label = "At object", .kind = PK::ObjectName,
               .emptyLabel = "(volume centre)",
               .tip = "Empty = the volume's own centre. An object name puts "
                      "the point at that object's position instead."},
              {.key = "x", .label = "Offset X", .kind = PK::Float, .def = 0.0f,
               .lo = -2000.0f, .hi = 2000.0f},
              {.key = "y", .label = "Offset Y", .kind = PK::Float, .def = 0.0f,
               .lo = -2000.0f, .hi = 2000.0f},
              {.key = "z", .label = "Offset Z", .kind = PK::Float, .def = 0.0f,
               .lo = -2000.0f, .hi = 2000.0f},
              {.key = "snap", .label = "Snap to surface", .kind = PK::Bool,
               .def = 1.0f,
               .tip = "Drop the point onto the terrain and read its normal "
                      "(the offset Y is then measured from the ground)."}},
         .desc = "ONE point, placed exactly. Nothing random happens here - it "
                 "is the start of the analytic side of this graph: place an "
                 "asset, then repeat it with Array or Radial Array. Also the "
                 "way to give those nodes a centre that is not the volume's."},

        {.key = "Curve",
         .title = "Curve",
         .category = "Sources",
         .outs = {{.label = "curve", .type = ProcType::Curve}},
         .params = {{.key = "closed", .label = "Closed loop", .kind = PK::Bool,
                     .def = 0.0f,
                     .tip = "Join the last control point back to the first."}},
         .rows = ProcRowKind::Points,
         .desc = "A Catmull-Rom curve through its control points, edited "
                 "directly in the viewport (select the node, then click the "
                 "terrain to append points and drag the handles). Feeds Scatter "
                 "along Curve and Keep Away From - a road, a river bank, a "
                 "patrol route."},

        // --- Masks --------------------------------------------------------
        {.key = "NoiseMask",
         .title = "Noise Mask",
         .category = "Masks",
         .outs = {{.label = "mask", .type = ProcType::Mask}},
         .params =
             {{.key = "kind", .label = "Kind", .kind = PK::Enum, .def = 0.0f,
               .lo = 0.0f, .hi = 3.0f,
               .choices = "Perlin|Ridged|Cells (Worley)|Warped Perlin",
               .tip = "Perlin = soft blobs; Ridged = veins and ridges; Cells = "
                      "clumps with hard borders; Warped = Perlin pushed "
                      "through itself (organic swirls)."},
              {.key = "scale", .label = "Feature size", .kind = PK::Float,
               .def = 40.0f, .lo = 1.0f, .hi = 1024.0f,
               .tip = "World units per noise feature - the size of the "
                      "clearings."},
              {.key = "octaves", .label = "Octaves", .kind = PK::Int,
               .def = 3.0f, .lo = 1.0f, .hi = 6.0f,
               .tip = "Layers of detail; each one halves the feature size."},
              {.key = "low", .label = "Range low", .kind = PK::Float,
               .def = 0.35f, .lo = 0.0f, .hi = 1.0f,
               .tip = "Noise value that maps to 0 (nothing grows)."},
              {.key = "high", .label = "Range high", .kind = PK::Float,
               .def = 0.75f, .lo = 0.0f, .hi = 1.0f,
               .tip = "Noise value that maps to 1 (full density). Bring the "
                      "two together for hard-edged patches."},
              {.key = "invert", .label = "Invert", .kind = PK::Bool}},
         .desc = "Procedural density: soft organic patches of more and less. "
                 "The one node that turns an even carpet into a forest with "
                 "clearings. Deterministic in the graph seed."},

        {.key = "TerrainMask",
         .title = "Terrain Mask",
         .category = "Masks",
         .outs = {{.label = "mask", .type = ProcType::Mask}},
         .params =
             {{.key = "source", .label = "Source", .kind = PK::Enum,
               .def = 1.0f, .lo = 0.0f, .hi = 3.0f,
               .choices = "Height|Slope|Curvature|Terrain material",
               .tip = "What the mask reads off the terrain. Curvature is "
                      "positive on ridges, negative in valleys; Terrain "
                      "material reads how much of the chosen material the "
                      "ground actually shows."},
              {.key = "layer", .label = "Material", .kind = PK::TerrainLayer,
               .def = 0.0f, .lo = -1.0f, .hi = 7.0f,
               .tip = "Which painted terrain material to read (Source = "
                      "Terrain material). Base material = the ground under "
                      "everything you painted."},
              {.key = "min", .label = "Range min", .kind = PK::Float,
               .def = 0.0f, .lo = -2000.0f, .hi = 2000.0f,
               .tip = "Source value that starts the band (world units for "
                      "height, degrees for slope, -1..1 for curvature, 0..1 "
                      "for a material's coverage)."},
              {.key = "max", .label = "Range max", .kind = PK::Float,
               .def = 25.0f, .lo = -2000.0f, .hi = 2000.0f},
              {.key = "falloff", .label = "Falloff", .kind = PK::Float,
               .def = 6.0f, .lo = 0.0f, .hi = 500.0f,
               .tip = "Soft edge in source units - the mask ramps instead of "
                      "cutting on a line."},
              {.key = "invert", .label = "Invert", .kind = PK::Bool}},
         .desc = "Turns the terrain itself into a mask: height bands, slope "
                 "bands, ridges vs valleys, or how much of one painted "
                 "material the ground shows. Trees only on the grass and "
                 "never on the rock you painted over it, boulders only on "
                 "the ridges - no manual work. Feed it to a Filter by Mask "
                 "(or straight into a scatter's density input) and set the "
                 "band to 0.5..1 to mean \"where this material is what you "
                 "see\"."},

        {.key = "MaskCombine",
         .title = "Combine Masks",
         .category = "Masks",
         .ins = {{.label = "a", .type = ProcType::Mask},
                 {.label = "b", .type = ProcType::Mask}},
         .outs = {{.label = "mask", .type = ProcType::Mask}},
         .params = {{.key = "op", .label = "Operation", .kind = PK::Enum,
                     .def = 0.0f, .lo = 0.0f, .hi = 5.0f,
                     .choices = "Multiply|Add|Subtract|Min|Max|Blend"},
                    {.key = "blend", .label = "Blend", .kind = PK::Float,
                     .def = 0.5f, .lo = 0.0f, .hi = 1.0f,
                     .tip = "Blend only: 0 = all A, 1 = all B."}},
         .desc = "Mixes two masks. Multiply is the AND of two conditions "
                 "(inside the noise patch AND on a gentle slope); Subtract "
                 "carves one out of the other."},

        {.key = "MaskRemap",
         .title = "Remap Mask",
         .category = "Masks",
         .ins = {{.label = "mask", .type = ProcType::Mask}},
         .outs = {{.label = "mask", .type = ProcType::Mask}},
         .params = {{.key = "low", .label = "Input low", .kind = PK::Float,
                     .def = 0.0f, .lo = 0.0f, .hi = 1.0f},
                    {.key = "high", .label = "Input high", .kind = PK::Float,
                     .def = 1.0f, .lo = 0.0f, .hi = 1.0f},
                    {.key = "gamma", .label = "Gamma", .kind = PK::Float,
                     .def = 1.0f, .lo = 0.1f, .hi = 6.0f,
                     .tip = "Bends the response: > 1 pushes values down "
                            "(sparser), < 1 up (denser)."},
                    {.key = "invert", .label = "Invert", .kind = PK::Bool}},
         .desc = "The density curve: rescales a mask's range and bends its "
                 "response. Use it to turn any mask into the exact amount of "
                 "coverage you want without touching its source."},

        // --- Filters ------------------------------------------------------
        {.key = "FilterRange",
         .title = "Filter by Attribute",
         .category = "Filters",
         .ins = {{.label = "points", .type = ProcType::Points}},
         .outs = {{.label = "points", .type = ProcType::Points}},
         .params =
             {{.key = "attr", .label = "Attribute", .kind = PK::Attr,
               .tip = "Per-point attribute to test (slope, height, mask, "
                      "size, random, t, dist, or any name Set Attribute "
                      "wrote)."},
              {.key = "min", .label = "Min", .kind = PK::Float, .def = 0.0f,
               .lo = -2000.0f, .hi = 2000.0f},
              {.key = "max", .label = "Max", .kind = PK::Float, .def = 30.0f,
               .lo = -2000.0f, .hi = 2000.0f},
              {.key = "falloff", .label = "Falloff", .kind = PK::Float,
               .def = 6.0f, .lo = 0.0f, .hi = 500.0f,
               .tip = "Soft band in attribute units: inside it points thin "
                      "out gradually instead of ending on a line."},
              {.key = "invert", .label = "Invert", .kind = PK::Bool}},
         .desc = "Keeps points whose attribute falls in a range, with a soft "
                 "edge. 'Trees only below 40 degrees of slope and above the "
                 "water line' is this node twice - and with Falloff the "
                 "forest thins out at its border instead of ending in a "
                 "straight cut."},

        {.key = "FilterMask",
         .title = "Filter by Mask",
         .category = "Filters",
         .ins = {{.label = "points", .type = ProcType::Points},
                 {.label = "mask", .type = ProcType::Mask}},
         .outs = {{.label = "points", .type = ProcType::Points}},
         .params = {{.key = "low", .label = "Mask low", .kind = PK::Float,
                     .def = 0.15f, .lo = 0.0f, .hi = 1.0f,
                     .tip = "Mask value below which nothing survives."},
                    {.key = "high", .label = "Mask high", .kind = PK::Float,
                     .def = 0.6f, .lo = 0.0f, .hi = 1.0f,
                     .tip = "Mask value above which everything survives."},
                    {.key = "strength", .label = "Strength", .kind = PK::Float,
                     .def = 1.0f, .lo = 0.0f, .hi = 1.0f,
                     .tip = "How much of the thinning to apply (0 = off)."},
                    {.key = "invert", .label = "Invert", .kind = PK::Bool}},
         .desc = "Thins a point cloud by a mask, per point, with the point's "
                 "own random stream - so the result is stable under unrelated "
                 "edits. Also writes the sampled value as the 'mask' "
                 "attribute for anything downstream (scale by density, "
                 "recolor, filter again)."},

        {.key = "FilterDistance",
         .title = "Minimum Distance",
         .category = "Filters",
         .ins = {{.label = "points", .type = ProcType::Points}},
         .outs = {{.label = "points", .type = ProcType::Points}},
         .params =
             {{.key = "radius", .label = "Radius", .kind = PK::Float,
               .def = 2.5f, .lo = 0.05f, .hi = 256.0f,
               .tip = "Minimum world distance between two kept instances."},
              {.key = "bysize", .label = "Scale with size", .kind = PK::Bool,
               .def = 1.0f,
               .tip = "Multiply the radius by each point's size attribute, so "
                      "big trees hold more space than saplings."}},
         .desc = "Enforces breathing room: walks the points in their "
                 "generation order and drops any that lands inside an already "
                 "kept neighbour's radius (a spatial-hash Poisson pass). "
                 "Because the order is the fixed sample sequence, the survivor "
                 "set is deterministic and stable."},

        {.key = "FilterAvoid",
         .title = "Keep Away From",
         .category = "Filters",
         .ins = {{.label = "points", .type = ProcType::Points},
                 {.label = "curve", .type = ProcType::Curve, .optional = true}},
         .outs = {{.label = "points", .type = ProcType::Points}},
         .params =
             {{.key = "target", .label = "Object", .kind = PK::ObjectName,
               .emptyLabel = "(every solid object)",
               .tip = "Name of the object to keep clear of. Empty and with no "
                      "curve connected = every solid object in the scene "
                      "(nothing grows inside the buildings)."},
              {.key = "radius", .label = "Radius", .kind = PK::Float,
               .def = 4.0f, .lo = 0.0f, .hi = 512.0f,
               .tip = "Clearance from the object's box / from the curve."},
              {.key = "mode", .label = "Keep", .kind = PK::Enum, .def = 0.0f,
               .lo = 0.0f, .hi = 1.0f, .choices = "Outside|Inside",
               .tip = "Outside = clear the area; Inside = grow ONLY there (a "
                      "hedge along a path)."},
              {.key = "falloff", .label = "Falloff", .kind = PK::Float,
               .def = 2.0f, .lo = 0.0f, .hi = 256.0f,
               .tip = "Soft border, in world units."}},
         .desc = "Clears (or restricts) points around an object or a curve, "
                 "and writes the measured distance as the 'dist' attribute. "
                 "This is how a path stays walkable and how props stop growing "
                 "through the house."},

        {.key = "Merge",
         .title = "Merge Points",
         .category = "Filters",
         .ins = {{.label = "a", .type = ProcType::Points},
                 {.label = "b", .type = ProcType::Points, .optional = true},
                 {.label = "c", .type = ProcType::Points, .optional = true},
                 {.label = "d", .type = ProcType::Points, .optional = true}},
         .outs = {{.label = "points", .type = ProcType::Points}},
         .desc = "Concatenates up to four point clouds, keeping every "
                 "attribute. Build each species with its own rules, then merge "
                 "them into one output (and one Minimum Distance pass to stop "
                 "them growing through each other)."},

        {.key = "Limit",
         .title = "Limit Count",
         .category = "Filters",
         .ins = {{.label = "points", .type = ProcType::Points}},
         .outs = {{.label = "points", .type = ProcType::Points}},
         .params = {{.key = "max", .label = "Max instances", .kind = PK::Int,
                     .def = 1500.0f, .lo = 1.0f, .hi = 200000.0f}},
         .desc = "Truncates to a budget. Because points arrive in "
                 "low-discrepancy order, keeping the first N thins the cloud "
                 "EVENLY over the whole area instead of cutting off one "
                 "corner."},

        // --- Attributes ---------------------------------------------------
        {.key = "PickAsset",
         .title = "Pick Asset",
         .category = "Attributes",
         .ins = {{.label = "points", .type = ProcType::Points}},
         .outs = {{.label = "points", .type = ProcType::Points}},
         .rows = ProcRowKind::Assets,
         .desc = "Assigns each point one model from a weighted pool (pine 70, "
                 "birch 25, dead 5) and a size inside the row's scale range. "
                 "The draw uses the point's own random stream, so changing a "
                 "weight shifts the proportions without reshuffling the "
                 "layout. The pool holds .obj files from res/models - the Tree "
                 "Generator is a good source. To scatter a PRIMITIVE, or "
                 "anything else already standing in the scene, use Pick Prefab: "
                 "its pool can capture a scene object in one click."},

        {.key = "PickPrefab",
         .title = "Pick Prefab",
         .category = "Attributes",
         .ins = {{.label = "points", .type = ProcType::Points}},
         .outs = {{.label = "points", .type = ProcType::Points}},
         .rows = ProcRowKind::Prefabs,
         .desc = "Assigns each point one prefab from the pool below - the way to "
                 "scatter something BUILT rather than modelled: a room, a shack, "
                 "a lamp post with its light and its script. The pool can also "
                 "CAPTURE a scene object (a box, a coloured primitive, a placed "
                 "model) as a one-member prefab in one click, which is how you "
                 "scatter something that is not a .obj file. A point carries a "
                 "prefab or a model, never both; whichever of these nodes runs "
                 "last wins. Costs one draw call per instance plus one spawn "
                 "slot per member that keeps an identity - Tools > Prefabs "
                 "shows that split for each prefab."},

        {.key = "Vary",
         .title = "Vary Transform",
         .category = "Attributes",
         .ins = {{.label = "points", .type = ProcType::Points}},
         .outs = {{.label = "points", .type = ProcType::Points}},
         .params =
             {{.key = "yaw", .label = "Yaw range", .kind = PK::Float,
               .def = 360.0f, .lo = 0.0f, .hi = 360.0f,
               .tip = "Random rotation around the vertical axis."},
              {.key = "tilt", .label = "Tilt jitter", .kind = PK::Float,
               .def = 0.0f, .lo = 0.0f, .hi = 45.0f,
               .tip = "Random lean off vertical, in degrees."},
              {.key = "scalemin", .label = "Scale min", .kind = PK::Float,
               .def = 0.85f, .lo = 0.05f, .hi = 20.0f},
              {.key = "scalemax", .label = "Scale max", .kind = PK::Float,
               .def = 1.15f, .lo = 0.05f, .hi = 20.0f},
              {.key = "jitter", .label = "Position jitter", .kind = PK::Float,
               .def = 0.0f, .lo = 0.0f, .hi = 32.0f,
               .tip = "Random horizontal nudge in world units."},
              {.key = "align", .label = "Align to normal", .kind = PK::Float,
               .def = 0.0f, .lo = 0.0f, .hi = 1.0f,
               .tip = "0 = always upright, 1 = fully laid over on the slope. "
                      "Trees want a little, rocks want all of it."}},
         .desc = "The node that stops 500 copies from looking like 500 copies: "
                 "random yaw, a scale range, optional tilt/offset and "
                 "alignment to the surface normal. Multiplies the size "
                 "attribute Pick Asset wrote, so per-species scale ranges "
                 "survive."},

        {.key = "SetAttribute",
         .title = "Set Attribute",
         .category = "Attributes",
         .ins = {{.label = "points", .type = ProcType::Points},
                 {.label = "mask", .type = ProcType::Mask}},
         .outs = {{.label = "points", .type = ProcType::Points}},
         .params = {{.key = "attr", .label = "Attribute", .kind = PK::Attr,
                     .tip = "Name to write (any name; downstream nodes read "
                            "it by the same name)."},
                    {.key = "min", .label = "Value at 0", .kind = PK::Float,
                     .def = 0.0f, .lo = -2000.0f, .hi = 2000.0f},
                    {.key = "max", .label = "Value at 1", .kind = PK::Float,
                     .def = 1.0f, .lo = -2000.0f, .hi = 2000.0f}},
         .desc = "Samples a mask per point and stores it as a named attribute "
                 "remapped into a value range. The bridge between the mask "
                 "world and the point world: sample a noise field, then filter "
                 "or scale by it."},

        // --- Repeat -------------------------------------------------------
        // The analytic counterpart to the Scatter sources: exact copies at
        // exact places. Both take a point cloud and emit `count` copies of
        // EVERY point in it, so they work on one placed Single Point ("this
        // pillar, twelve times around a circle") and on a whole scattered
        // field alike ("every bush, three times up the cliff face").
        {.key = "Array",
         .title = "Array",
         .category = "Repeat",
         .ins = {{.label = "points", .type = ProcType::Points}},
         .outs = {{.label = "points", .type = ProcType::Points}},
         .params =
             {{.key = "count", .label = "Count", .kind = PK::Int, .def = 5.0f,
               .lo = 1.0f, .hi = 2000.0f,
               .tip = "Total copies INCLUDING the original, so 1 changes "
                      "nothing."},
              {.key = "dx", .label = "Step X", .kind = PK::Float, .def = 4.0f,
               .lo = -500.0f, .hi = 500.0f},
              {.key = "dy", .label = "Step Y", .kind = PK::Float, .def = 0.0f,
               .lo = -500.0f, .hi = 500.0f,
               .tip = "Stack upwards: Step Y = the asset's height, X and Z "
                      "zero, Snap off."},
              {.key = "dz", .label = "Step Z", .kind = PK::Float, .def = 0.0f,
               .lo = -500.0f, .hi = 500.0f},
              {.key = "yaw", .label = "Yaw per copy", .kind = PK::Float,
               .def = 0.0f, .lo = -360.0f, .hi = 360.0f,
               .tip = "Degrees added per step - a twisted stack, a spiral "
                      "staircase."},
              {.key = "scale", .label = "Scale per copy", .kind = PK::Float,
               .def = 1.0f, .lo = 0.1f, .hi = 3.0f,
               .tip = "Multiplied per step: 0.9 tapers a stack, 1 keeps it "
                      "uniform."},
              {.key = "local", .label = "Step in point space", .kind = PK::Bool,
               .def = 0.0f,
               .tip = "Off = the step is world XYZ. On = it is rotated by the "
                      "point's own yaw, so fence posts march along the fence "
                      "rather than along X."},
              {.key = "snap", .label = "Snap to surface", .kind = PK::Bool,
               .def = 0.0f,
               .tip = "Drop every copy onto the terrain. For a row along the "
                      "ground; leave OFF for a vertical stack, which it would "
                      "flatten."}},
         .desc = "Repeats every incoming point along a straight line: count "
                 "copies, each one step further. Copies keep the source "
                 "point's attributes and its picked asset, and each gets its "
                 "own stable identity, so a manual edit sticks to copy 7 and "
                 "not to 'the seventh point that happens to exist'."},

        {.key = "RadialArray",
         .title = "Radial Array",
         .category = "Repeat",
         .ins = {{.label = "points", .type = ProcType::Points}},
         .outs = {{.label = "points", .type = ProcType::Points}},
         .params =
             {{.key = "count", .label = "Count", .kind = PK::Int, .def = 8.0f,
               .lo = 1.0f, .hi = 2000.0f,
               .tip = "Copies around the ring (the source point becomes the "
                      "first one, at the start angle)."},
              {.key = "radius", .label = "Radius", .kind = PK::Float,
               .def = 8.0f, .lo = 0.0f, .hi = 2000.0f,
               .tip = "Distance from the incoming point, which is the CENTRE "
                      "of the ring."},
              {.key = "axis", .label = "Axis", .kind = PK::Enum, .def = 0.0f,
               .lo = 0.0f, .hi = 2.0f, .choices = "Y (flat ring)|X|Z",
               .tip = "Which axis the ring turns around. Y is the usual one: "
                      "columns around a plaza."},
              {.key = "start", .label = "Start angle", .kind = PK::Float,
               .def = 0.0f, .lo = -360.0f, .hi = 360.0f},
              {.key = "sweep", .label = "Sweep", .kind = PK::Float,
               .def = 360.0f, .lo = 1.0f, .hi = 360.0f,
               .tip = "360 = a full circle (the last copy does not land on the "
                      "first). Less = an arc, spread evenly end to end."},
              {.key = "face", .label = "Turn with the ring", .kind = PK::Bool,
               .def = 1.0f,
               .tip = "Yaw each copy by its own angle, so an asset with a "
                      "front faces outward all the way round. Only meaningful "
                      "for the Y axis."},
              {.key = "snap", .label = "Snap to surface", .kind = PK::Bool,
               .def = 0.0f,
               .tip = "Drop every copy onto the terrain - a ring of stones on "
                      "uneven ground."}},
         .desc = "Repeats every incoming point around a circle centred ON that "
                 "point: count copies, evenly spaced over the sweep. Stone "
                 "circles, colonnades, lamps around a fountain - the placement "
                 "is exact, so it reads as built rather than grown."},

        // --- Output -------------------------------------------------------
        {.key = "ObjectSettings",
         .title = "Object Settings",
         .category = "Output",
         .rows = ProcRowKind::Settings,
         .desc = "Applies a list of properties to EVERY scene object this "
                 "volume bakes - the 'and all of them are like this' of the "
                 "graph. It carries no pins because it is not a step in the "
                 "chain: it states a fact about the whole output. Without it "
                 "the generated chunks take the editor's defaults, and setting "
                 "one by hand in the Properties panel does not survive - the "
                 "next bake makes new chunks. Draw distance, cast shadow, "
                 "collision and the streaming layer are NOT here: those live "
                 "on Output (and the layer on the volume itself), so there is "
                 "one place per field."},

        {.key = "Output",
         .title = "Output",
         .category = "Output",
         .ins = {{.label = "points", .type = ProcType::Points}},
         .params =
             {{.key = "cell", .label = "Chunk size", .kind = PK::Float,
               .def = 48.0f, .lo = 8.0f, .hi = 512.0f,
               .tip = "World size of one baked chunk. Instances inside a chunk "
                      "merge into ONE mesh, so this trades draw calls "
                      "(bigger = fewer) against culling granularity "
                      "(smaller = less drawn off-screen)."},
              {.key = "draw", .label = "Draw distance", .kind = PK::Float,
               .def = 0.0f, .lo = 0.0f, .hi = 4000.0f,
               .tip = "Per-chunk cut-off in world units; 0 = always drawn. The "
                      "cheapest LOD there is."},
              {.key = "shadow", .label = "Cast shadow", .kind = PK::Bool,
               .def = 0.0f,
               .tip = "Let the baked chunks darken the terrain around them. "
                      "Off by default: thousands of instances would make the "
                      "occlusion bake crawl."},
              {.key = "collide", .label = "Collision", .kind = PK::Enum,
               .def = 0.0f, .lo = 0.0f, .hi = 1.0f, .choices = "None|Box",
               .tip = "None (era-correct for vegetation) or one box per "
                      "chunk - which is almost never what you want, so leave "
                      "it off and place real blockers by hand."},
              {.key = "detail", .label = "Instance detail", .kind = PK::Enum,
               .def = 0.0f, .lo = 0.0f, .hi = 2.0f,
               .choices = "Full|Half|Quarter",
               .tip = "Decimates the SOURCE mesh once before merging, so a "
                      "600-triangle tree does not become 60 000 in the chunk. "
                      "Scattered vegetation is read at a distance - Half is "
                      "usually free to the eye and halves the whole cost."},
              {.key = "budget", .label = "Triangle budget", .kind = PK::Int,
               .def = 20000.0f, .lo = 100.0f, .hi = 2000000.0f,
               .tip = "The bake warns above this. A PS2 scene has room for "
                      "some tens of thousands of triangles TOTAL."},
              {.key = "maxinst", .label = "Runtime instance cap",
               .kind = PK::Int, .def = 4096.0f, .lo = 64.0f, .hi = 60000.0f,
               .tip = "RUNTIME volumes only: how many points the console's "
                      "working buffer holds. It is real RAM (about 80 bytes per "
                      "point) reserved for the whole generation, and the "
                      "generator stops rather than overruns it - so set it just "
                      "above what the preview reports, not at the maximum."}},
         .desc = "The graph's result. Everything reaching this node is baked "
                 "into per-chunk static meshes when you build: the console "
                 "loads finished geometry and knows nothing about the graph. "
                 "One Output per graph."},
    };
    return types;
}

const ProcNodeType* procNodeType(const std::string& key) {
    for (const ProcNodeType& t : procNodeTypes())
        if (key == t.key) return &t;
    return nullptr;
}

// What the Object Settings node can set on a generated chunk. Every entry maps
// to one SceneObject field in procbake::applySettings - keep the two together,
// and prefer appending to renaming (a project stores the key).
const std::vector<ProcObjProp>& procObjectProps() {
    using PK = ProcObjPropKind;
    static const std::vector<ProcObjProp> props = {
        {.key = "meshLod", .label = "Mesh LOD distance", .kind = PK::Float,
         .def = 20.0f, .lo = -1.0f, .hi = 2000.0f,
         .tip = "Past this distance the chunk draws its decimated variant "
                "(the build bakes ~50% and ~25% tiers for it). -1 = follow the "
                "project preference, 0 = never decimate. Setting it here is "
                "what makes the build bake the tiers at all, so a scattered "
                "forest gets mesh LOD without touching the whole project."},
        {.key = "bakedLighting", .label = "Baked lighting", .kind = PK::Bool,
         .def = 1.0f,
         .tip = "On = the chunk may take a per-texel lightmap from the GI bake "
                "(best looking, and correct: baked geometry never moves). Off "
                "puts it on the light-probe path instead."},
        {.key = "reflected", .label = "Show in reflections", .kind = PK::Bool,
         .def = 0.0f,
         .tip = "Render the chunk into the dynamic environment map too. Each "
                "marked object costs a second render per frame - on a scatter "
                "volume that is many objects, so mean it."},
    };
    return props;
}

const ProcObjProp* procObjectProp(const std::string& key) {
    for (const ProcObjProp& p : procObjectProps())
        if (key == p.key) return &p;
    return nullptr;
}

namespace procgraph {

void applyDefaults(ProcNode& n) {
    const ProcNodeType* t = procNodeType(n.type);
    if (!t) return;
    for (const ProcParamDef& p : t->params) {
        switch (p.kind) {
            case ProcParamKind::ObjectName:
            case ProcParamKind::Attr:
            case ProcParamKind::Text:
                break;  // strings default to empty
            default:
                if (n.nums.find(p.key) == n.nums.end()) n.nums[p.key] = p.def;
                break;
        }
    }
}

float num(const ProcNode& n, const char* key) {
    auto it = n.nums.find(key);
    if (it != n.nums.end()) return it->second;
    const ProcNodeType* t = procNodeType(n.type);
    if (t)
        for (const ProcParamDef& p : t->params)
            if (std::string(p.key) == key) return p.def;
    return 0.0f;
}

int inum(const ProcNode& n, const char* key) {
    return (int)std::lround(num(n, key));
}

bool flag(const ProcNode& n, const char* key) { return num(n, key) >= 0.5f; }

const std::string& str(const ProcNode& n, const char* key) {
    static const std::string empty;
    auto it = n.strs.find(key);
    return it == n.strs.end() ? empty : it->second;
}

const ProcLink* linkTo(const ProcGraph& g, int nodeId, int pin) {
    for (const ProcLink& l : g.links)
        if (l.toNode == nodeId && l.toPin == pin) return &l;
    return nullptr;
}

const ProcNode* node(const ProcGraph& g, int id) {
    for (const ProcNode& n : g.nodes)
        if (n.id == id) return &n;
    return nullptr;
}

ProcNode* node(ProcGraph& g, int id) {
    for (ProcNode& n : g.nodes)
        if (n.id == id) return &n;
    return nullptr;
}

namespace {

// Does `from` already depend on `to`? Walks upstream from `from` following the
// existing links; a hit means connecting to -> from would close a cycle.
bool dependsOn(const ProcGraph& g, int from, int to, std::set<int>& seen) {
    if (from == to) return true;
    if (!seen.insert(from).second) return false;
    for (const ProcLink& l : g.links)
        if (l.toNode == from && dependsOn(g, l.fromNode, to, seen)) return true;
    return false;
}

const char* typeName(ProcType t) {
    switch (t) {
        case ProcType::Points: return "points";
        case ProcType::Mask: return "mask";
        case ProcType::Curve: return "curve";
    }
    return "?";
}

}  // namespace

std::string linkError(const ProcGraph& g, int fromNode, int fromPin, int toNode,
                      int toPin) {
    if (fromNode == toNode) return "a node cannot feed itself";
    const ProcNode* a = node(g, fromNode);
    const ProcNode* b = node(g, toNode);
    if (!a || !b) return "unknown node";
    const ProcNodeType* ta = procNodeType(a->type);
    const ProcNodeType* tb = procNodeType(b->type);
    if (!ta || !tb) return "unknown node type";
    if (fromPin < 0 || fromPin >= (int)ta->outs.size()) return "unknown output pin";
    if (toPin < 0 || toPin >= (int)tb->ins.size()) return "unknown input pin";
    const ProcType out = ta->outs[fromPin].type;
    const ProcType in = tb->ins[toPin].type;
    if (out != in) {
        std::string msg = "cannot connect a ";
        msg += typeName(out);
        msg += " output to a ";
        msg += typeName(in);
        msg += " input";
        return msg;
    }
    std::set<int> seen;
    if (dependsOn(g, fromNode, toNode, seen)) return "that would create a cycle";
    return std::string();
}

std::vector<ProcIssue> validate(const ProcGraph& g) {
    std::vector<ProcIssue> out;
    int outputs = 0, settings = 0;
    for (const ProcNode& n : g.nodes) {
        const ProcNodeType* t = procNodeType(n.type);
        if (!t) {
            out.push_back({n.id, "unknown node type '" + n.type + "'"});
            continue;
        }
        if (n.type == "Output") ++outputs;
        if (n.type == "ObjectSettings") ++settings;
        if (n.type == "ObjectSettings")
            for (const ProcRow& r : n.rows)
                if (!procObjectProp(r.s))
                    out.push_back({n.id, "Object Settings: unknown property '" +
                                             r.s + "' (ignored)"});
        for (size_t i = 0; i < t->ins.size(); ++i) {
            if (t->ins[i].optional) continue;
            if (!linkTo(g, n.id, (int)i))
                out.push_back({n.id, std::string(t->title) + ": input '" +
                                         t->ins[i].label + "' is not connected"});
        }
        if (n.type == "PickAsset") {
            bool any = false;
            for (const ProcRow& r : n.rows)
                if (!r.s.empty() && r.v[0] > 0.0f) any = true;
            if (!any)
                out.push_back({n.id, "Pick Asset: the asset pool is empty"});
        }
        if (n.type == "Curve" && n.rows.size() < 2)
            out.push_back({n.id, "Curve: needs at least two control points"});
    }
    if (outputs == 0)
        out.push_back({0, "the graph has no Output node - nothing is baked"});
    else if (outputs > 1)
        out.push_back({0, "more than one Output node; the first one is used"});
    if (settings > 1)
        out.push_back({0, "more than one Object Settings node; the first one is "
                          "used"});
    return out;
}

const ProcNode* outputNode(const ProcGraph& g) {
    for (const ProcNode& n : g.nodes)
        if (n.type == "Output") return &n;
    return nullptr;
}

const ProcNode* settingsNode(const ProcGraph& g) {
    for (const ProcNode& n : g.nodes)
        if (n.type == "ObjectSettings") return &n;
    return nullptr;
}

int addNode(ProcGraph& g, const std::string& type, float x, float y) {
    if (!procNodeType(type)) return 0;
    ProcNode n;
    n.id = g.nextId++;
    n.type = type;
    n.pos[0] = x;
    n.pos[1] = y;
    applyDefaults(n);
    g.nodes.push_back(n);
    return n.id;
}

void removeNode(ProcGraph& g, int nodeId) {
    g.nodes.erase(std::remove_if(g.nodes.begin(), g.nodes.end(),
                                 [&](const ProcNode& n) { return n.id == nodeId; }),
                  g.nodes.end());
    g.links.erase(std::remove_if(g.links.begin(), g.links.end(),
                                 [&](const ProcLink& l) {
                                     return l.fromNode == nodeId ||
                                            l.toNode == nodeId;
                                 }),
                  g.links.end());
}

bool addLink(ProcGraph& g, int fromNode, int fromPin, int toNode, int toPin) {
    if (!linkError(g, fromNode, fromPin, toNode, toPin).empty()) return false;
    // One link per input pin: a second connection replaces the first, the way
    // every node editor behaves.
    g.links.erase(std::remove_if(g.links.begin(), g.links.end(),
                                 [&](const ProcLink& l) {
                                     return l.toNode == toNode && l.toPin == toPin;
                                 }),
                  g.links.end());
    ProcLink l;
    l.id = g.nextId++;
    l.fromNode = fromNode;
    l.fromPin = fromPin;
    l.toNode = toNode;
    l.toPin = toPin;
    g.links.push_back(l);
    return true;
}

ProcGraph starterGraph() {
    ProcGraph g;
    const int surface = addNode(g, "ScatterSurface", -640, 0);
    const int noise = addNode(g, "NoiseMask", -640, 260);
    const int slope = addNode(g, "FilterRange", -360, 0);
    const int pick = addNode(g, "PickAsset", -80, 0);
    const int vary = addNode(g, "Vary", 220, 0);
    const int dist = addNode(g, "FilterDistance", 500, 0);
    const int out = addNode(g, "Output", 760, 0);
    addLink(g, noise, 0, surface, 0);
    addLink(g, surface, 0, slope, 0);
    addLink(g, slope, 0, pick, 0);
    addLink(g, pick, 0, vary, 0);
    addLink(g, vary, 0, dist, 0);
    addLink(g, dist, 0, out, 0);
    // The starter chain says "gentle ground only" out of the box - that is the
    // rule everyone writes first, and it makes the empty-pool state the only
    // thing left to fill in.
    if (ProcNode* n = node(g, slope)) {
        n->strs["attr"] = procattr::kSlope;
        n->nums["min"] = 0.0f;
        n->nums["max"] = 28.0f;
        n->nums["falloff"] = 8.0f;
    }
    return g;
}

}  // namespace procgraph
