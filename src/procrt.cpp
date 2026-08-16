#include "procrt.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>

#include "prefab.hpp"

namespace procrt {

namespace {

// ---------------------------------------------------------------------------
// What the console can run
// ---------------------------------------------------------------------------
// ONE table, read by capability() and by the emitter's dispatch. A node missing
// here is reported honestly instead of silently producing nothing, and a node
// listed here without an emit branch is a compile error in this file rather
// than in the generated game.
const char* const kRuntimeNodes[] = {
    "ScatterSurface", "ScatterGrid", "ScatterVolume", "Point",  "BlocksFill",
    "NoiseMask",      "TerrainMask", "MaskCombine",   "MaskRemap",
    "FilterRange",    "FilterMask",  "FilterDistance", "Merge", "Limit",
    "Array",          "RadialArray", "PickAsset",     "PickPrefab", "Vary",
    "SetAttribute",   "Output",      "ObjectSettings",
};

// A float LITERAL, which is fussier than a printed number: "2f" is not one
// (the compiler reads it as an identifier), and "1e+06f" is one but reads
// badly, so both get a plain decimal point.
std::string fmt(float v) {
    std::ostringstream ss;
    ss.precision(9);
    ss << v;
    std::string s = ss.str();
    if (s.find('e') != std::string::npos || s.find('E') != std::string::npos) {
        std::ostringstream p;
        p.setf(std::ios::fixed);
        p.precision(6);
        p << v;
        s = p.str();
    }
    if (s.find('.') == std::string::npos) s += ".0";
    return s + "f";
}

std::string ifmt(int v) { return std::to_string(v); }

std::string cstr(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

}  // namespace

bool nodeSupported(const std::string& type) {
    for (const char* k : kRuntimeNodes)
        if (type == k) return true;
    return false;
}

std::vector<Issue> capability(const ProcGraph& g) {
    std::vector<Issue> out;
    int blocks = 0;
    for (const ProcNode& n : g.nodes) {
        if (!nodeSupported(n.type)) {
            const ProcNodeType* t = procNodeType(n.type);
            out.push_back({n.id, std::string(t ? t->title : n.type.c_str()) +
                                     " cannot run on the console - it needs "
                                     "data only the editor has"});
            continue;
        }
        if (n.type == "BlocksFill") ++blocks;
        // Per-node limits: a node type can be runnable and still be asked for
        // something the console has no source for.
        if (n.type == "ScatterSurface" && !procgraph::str(n, "target").empty())
            out.push_back({n.id,
                           "Scatter on Surface can only use the TERRAIN at "
                           "runtime - clear the Surface field (scattering over "
                           "another object's mesh needs the mesh on the host)"});
        if (n.type == "TerrainMask" && procgraph::inum(n, "source") == 3)
            out.push_back({n.id,
                           "Terrain Mask cannot read a painted material at "
                           "runtime - the splat map is a build-time asset. Use "
                           "Height, Slope or Curvature"});
    }
    if (blocks > 1)
        out.push_back({0,
                       "only one Blocks Fill per runtime volume - the block "
                       "collision field is a single grid"});
    if (!procgraph::outputNode(g))
        out.push_back({0, "no Output node - nothing to generate"});
    return out;
}

std::vector<Volume> volumes(const Project& p) {
    std::vector<Volume> out;
    for (int si = 0; si < (int)p.scenes.size(); ++si) {
        const SceneData& s = p.scenes[si];
        for (int oi = 0; oi < (int)s.objects.size(); ++oi) {
            const SceneObject& o = s.objects[oi];
            if (o.type != PrimitiveType::Scatter) continue;
            if (!o.procGraph.runtime || o.procGraph.nodes.empty()) continue;
            Volume v;
            v.scene = si;
            v.objectIndex = oi;
            v.name = o.name;
            v.issues = capability(o.procGraph);
            std::set<std::string> seenA, seenP;
            std::vector<const ProcNode*> nodes;
            for (const ProcNode& n : o.procGraph.nodes) nodes.push_back(&n);
            std::sort(nodes.begin(), nodes.end(),
                      [](const ProcNode* a, const ProcNode* b) {
                          return a->id < b->id;
                      });
            for (const ProcNode* n : nodes) {
                if (n->type == "PickAsset")
                    for (const ProcRow& r : n->rows) {
                        if (r.s.empty() || !seenA.insert(r.s).second) continue;
                        v.assets.push_back(r.s);
                    }
                if (n->type == "PickPrefab")
                    for (const ProcRow& r : n->rows) {
                        if (r.s.empty() || !seenP.insert(r.s).second) continue;
                        v.prefabs.push_back(r.s);
                    }
                if (n->type == "BlocksFill") {
                    v.hasBlocks = true;
                    v.blockLevels = std::clamp(procgraph::inum(*n, "levels"), 1, 32);
                    const float block = std::max(0.25f, procgraph::num(*n, "block"));
                    // The lattice covers the volume's axis-aligned footprint;
                    // the yaw only widens it, so this over-estimates a rotated
                    // volume rather than under-allocating it.
                    const float ex = std::fabs(o.scale[0]), ez = std::fabs(o.scale[2]);
                    const float c = std::fabs(std::cos(o.rotation[1] * 3.14159265f / 180.0f));
                    const float sn = std::fabs(std::sin(o.rotation[1] * 3.14159265f / 180.0f));
                    const int nx = std::max(1, (int)std::floor((ex * c + ez * sn) / block));
                    const int nz = std::max(1, (int)std::floor((ex * sn + ez * c) / block));
                    v.blockColumns = nx * nz;
                }
            }
            out.push_back(std::move(v));
        }
    }
    return out;
}

int volumeIndexOf(const Project& p, int scene, const std::string& objectName) {
    int emitted = 0;
    for (const Volume& v : volumes(p)) {
        if (!v.issues.empty()) continue;  // skipped by emit(), so not in VOLUMES
        if (v.scene == scene && v.name == objectName) return emitted;
        ++emitted;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// The emitter
// ---------------------------------------------------------------------------

namespace {

// Every attribute name the project's runtime graphs mention, in a stable order.
// A point carries one float slot per name; a graph that never mentions an
// attribute pays nothing for it.
std::vector<std::string> attrNames(const Project& p) {
    std::vector<std::string> out;
    auto add = [&](const std::string& n) {
        if (n.empty()) return;
        if (std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n);
    };
    // The ones the generators always write - listed first so their slots stay
    // put as a project grows nodes.
    for (const char* n : {procattr::kSlope, procattr::kHeight,
                          procattr::kNormalX, procattr::kNormalY,
                          procattr::kNormalZ, procattr::kRandom,
                          procattr::kSize, procattr::kMask, procattr::kDepth,
                          procattr::kFaces})
        add(n);
    for (const SceneData& s : p.scenes)
        for (const SceneObject& o : s.objects) {
            if (o.type != PrimitiveType::Scatter || !o.procGraph.runtime) continue;
            for (const ProcNode& n : o.procGraph.nodes) {
                if (n.type == "SetAttribute" || n.type == "FilterRange")
                    add(procgraph::str(n, "attr"));
            }
        }
    return out;
}

struct Emitter {
    const Project& p;
    const SceneData& scene;
    const SceneObject& volObj;
    const ProcGraph& g;
    const Volume& vol;
    const std::vector<std::string>& attrs;
    std::ostringstream masks;  // mask functions emitted ahead of the generator
    std::ostringstream body;
    std::set<int> maskDone;
    int emitSeq = 0;
    int indent = 2;

    int attrSlot(const std::string& name) const {
        for (size_t i = 0; i < attrs.size(); ++i)
            if (attrs[i] == name) return (int)i;
        return -1;
    }
    std::string pad() const { return std::string(indent, ' '); }
    void line(const std::string& s) { body << pad() << s << "\n"; }

    const ProcNode* inputNode(const ProcNode& n, int pin) const {
        const ProcLink* l = procgraph::linkTo(g, n.id, pin);
        return l ? procgraph::node(g, l->fromNode) : nullptr;
    }

    // ---- masks: compiled to pointwise functions ---------------------------
    // The host builds a mask as a GRID and samples it bilinearly; the console
    // evaluates the formula per point instead. That is cheaper (no allocation,
    // no resolution decision) and strictly more accurate - the only thing lost
    // is the grid's implicit smoothing, which nothing depended on.
    std::string maskFn(const ProcNode* n) {
        if (!n) return "";
        if (maskDone.insert(n->id).second) emitMaskFn(*n);
        return "pm_" + ifmt(vol.scene) + "_" + ifmt(vol.objectIndex) + "_" +
               ifmt(n->id);
    }

    void emitMaskFn(const ProcNode& n) {
        // Dependencies first (a combine reads two masks).
        std::string a, b;
        if (n.type == "MaskCombine") {
            a = maskFn(inputNode(n, 0));
            b = maskFn(inputNode(n, 1));
        } else if (n.type == "MaskRemap") {
            a = maskFn(inputNode(n, 0));
        }
        const std::string fn = "pm_" + ifmt(vol.scene) + "_" +
                               ifmt(vol.objectIndex) + "_" + ifmt(n.id);
        masks << "static float " << fn << "(const procrt::Ctx& c, float x, float z) {\n";
        if (n.type == "NoiseMask") {
            const int kind = std::clamp(procgraph::inum(n, "kind"), 0, 3);
            const float scale = std::max(1.0f, procgraph::num(n, "scale"));
            const int oct = std::clamp(procgraph::inum(n, "octaves"), 1, 6);
            masks << "  unsigned int sd = (unsigned int)(procrt::mix64(((unsigned long long)c.seed << 32) ^ "
                  << ifmt(n.id) << "ULL) & 0xffffffffu);\n";
            masks << "  float v = procrt::noiseAt(" << ifmt(kind) << ", x / "
                  << fmt(scale) << ", z / " << fmt(scale) << ", " << ifmt(oct)
                  << ", sd);\n";
            masks << "  v = procrt::remap01(v, " << fmt(procgraph::num(n, "low"))
                  << ", " << fmt(procgraph::num(n, "high")) << ");\n";
            masks << "  return " << (procgraph::flag(n, "invert") ? "1.0F - v" : "v")
                  << ";\n";
        } else if (n.type == "TerrainMask") {
            const int src = std::clamp(procgraph::inum(n, "source"), 0, 3);
            if (src == 1)
                masks << "  float v = procrt::terrainSlope(c, x, z);\n";
            else if (src == 2)
                masks << "  float v = procrt::terrainCurvature(c, x, z);\n";
            else
                masks << "  float v = c.terrainY(x, z);\n";
            masks << "  float b = procrt::band(v, " << fmt(procgraph::num(n, "min"))
                  << ", " << fmt(procgraph::num(n, "max")) << ", "
                  << fmt(procgraph::num(n, "falloff")) << ");\n";
            masks << "  return " << (procgraph::flag(n, "invert") ? "1.0F - b" : "b")
                  << ";\n";
        } else if (n.type == "MaskCombine") {
            const int op = std::clamp(procgraph::inum(n, "op"), 0, 5);
            const float blend = std::clamp(procgraph::num(n, "blend"), 0.0f, 1.0f);
            masks << "  float va = " << (a.empty() ? "1.0F" : a + "(c, x, z)") << ";\n";
            masks << "  float vb = " << (b.empty() ? "1.0F" : b + "(c, x, z)") << ";\n";
            switch (op) {
                case 1: masks << "  float r = va + vb;\n"; break;
                case 2: masks << "  float r = va - vb;\n"; break;
                case 3: masks << "  float r = va < vb ? va : vb;\n"; break;
                case 4: masks << "  float r = va > vb ? va : vb;\n"; break;
                case 5:
                    masks << "  float r = va * (1.0F - " << fmt(blend) << ") + vb * "
                          << fmt(blend) << ";\n";
                    break;
                default: masks << "  float r = va * vb;\n"; break;
            }
            masks << "  return procrt::clamp01(r);\n";
        } else if (n.type == "MaskRemap") {
            masks << "  float v = " << (a.empty() ? "1.0F" : a + "(c, x, z)") << ";\n";
            masks << "  float r = procrt::powf01(procrt::remap01(v, "
                  << fmt(procgraph::num(n, "low")) << ", "
                  << fmt(procgraph::num(n, "high")) << "), "
                  << fmt(std::max(0.05f, procgraph::num(n, "gamma"))) << ");\n";
            masks << "  return procrt::clamp01("
                  << (procgraph::flag(n, "invert") ? "1.0F - r" : "r") << ");\n";
        } else {
            masks << "  (void)x; (void)z; (void)c;\n  return 1.0F;\n";
        }
        masks << "}\n\n";
    }

    // A mask expression for the node feeding `pin`, or "" when unconnected.
    std::string maskExpr(const ProcNode& n, int pin, const char* xv,
                         const char* zv) {
        const ProcNode* src = inputNode(n, pin);
        if (!src) return "";
        const std::string fn = maskFn(src);
        return fn + "(c, " + xv + ", " + zv + ")";
    }

    // ---- points -----------------------------------------------------------
    // Every node writes into ONE growing buffer and returns the [begin, end)
    // range it produced; a range always ends at c.count, which is what makes a
    // filter's in-place compaction and a Merge's plain concatenation correct.
    // Fan-out re-evaluates its source, which is exactly the dataflow semantics
    // (two consumers get two independent copies of the same cloud) and the
    // reason the compiler needs no cache.
    struct Range {
        std::string begin, end;
    };

    Range emitPoints(const ProcNode& n) {
        // A node feeding TWO consumers is emitted twice - that IS the dataflow
        // semantics (each branch gets its own copy of the cloud, which is what
        // makes "blocks -> three filters -> merge" mean what it looks like), so
        // the variable names have to be unique per EMISSION, not per node.
        const std::string v = "r" + ifmt(n.id) + "_" + ifmt(++emitSeq);
        if (n.bypass) {
            if (const ProcNode* in0 = inputNode(n, 0)) return emitPoints(*in0);
        }
        if (n.type == "ScatterSurface") return emitScatterSurface(n, v);
        if (n.type == "ScatterGrid") return emitScatterGrid(n, v);
        if (n.type == "ScatterVolume") return emitScatterVolume(n, v);
        if (n.type == "Point") return emitSinglePoint(n, v);
        if (n.type == "BlocksFill") return emitBlocks(n, v);
        if (n.type == "Merge") {
            Range first;
            bool have = false;
            std::string last;
            const ProcNodeType* t = procNodeType(n.type);
            for (size_t i = 0; t && i < t->ins.size(); ++i) {
                const ProcNode* src = inputNode(n, (int)i);
                if (!src) continue;
                Range r = emitPoints(*src);
                if (!have) {
                    first = r;
                    have = true;
                }
                last = r.end;
            }
            if (!have) return emitEmpty(v);
            // A merge is pure bookkeeping: the inputs were appended one after
            // another, so the result is already the contiguous span from the
            // first one's start to the last one's end. Naming it in a variable
            // would only be an unused one.
            return {first.begin, last};
        }
        const ProcNode* in0 = inputNode(n, 0);
        if (!in0) return emitEmpty(v);
        Range r = emitPoints(*in0);
        if (n.type == "FilterRange") return emitFilterRange(n, v, r);
        if (n.type == "FilterMask") return emitFilterMask(n, v, r);
        if (n.type == "FilterDistance") return emitFilterDistance(n, v, r);
        if (n.type == "Limit") return emitLimit(n, v, r);
        if (n.type == "Array") return emitArray(n, v, r);
        if (n.type == "RadialArray") return emitRadial(n, v, r);
        if (n.type == "PickAsset") return emitPick(n, v, r, false);
        if (n.type == "PickPrefab") return emitPick(n, v, r, true);
        if (n.type == "Vary") return emitVary(n, v, r);
        if (n.type == "SetAttribute") return emitSetAttr(n, v, r);
        if (n.type == "Output") return r;
        return r;
    }

    Range emitEmpty(const std::string& v) {
        line("int " + v + "b = c.count; int " + v + "e = c.count;");
        return {v + "b", v + "e"};
    }

    // Shared tail of every generator: the base attributes, in the same order
    // and with the same channels procgen writes, so the console and the
    // preview draw the same cloud from the same seed.
    void baseAttrs(const std::string& nx, const std::string& ny,
                   const std::string& nz, int nodeId) {
        auto set = [&](const char* name, const std::string& expr) {
            const int s = attrSlot(name);
            if (s >= 0) line("  P.a[" + ifmt(s) + "] = " + expr + ";");
        };
        set(procattr::kNormalX, nx);
        set(procattr::kNormalY, ny);
        set(procattr::kNormalZ, nz);
        set(procattr::kSlope, "procrt::slopeDeg(" + ny + ")");
        set(procattr::kHeight, "P.y");
        set(procattr::kRandom,
            "procrt::rand01(c.seed, " + ifmt(nodeId) + ", P.key, 1)");
        set(procattr::kSize, "1.0F");
    }

    Range emitScatterSurface(const ProcNode& n, const std::string& v) {
        const float density = procgraph::num(n, "density");
        const int cap = std::max(1, procgraph::inum(n, "max"));
        const float lift = procgraph::num(n, "lift");
        const std::string mask = maskExpr(n, 0, "px", "pz");
        line("int " + v + "b = c.count;");
        line("{");
        line("  float area = procrt::footprintArea(c);");
        line("  int cnt = (int)(" + fmt(density) + " * area / 100.0F + 0.5F);");
        line("  if (cnt > " + ifmt(cap) + ") cnt = " + ifmt(cap) + ";");
        line("  for (int i = 0; i < cnt; ++i) {");
        line("    unsigned long long key = procrt::mix64(((unsigned long long)" +
             ifmt(n.id) + " << 40) ^ (unsigned long long)i);");
        line("    float lx = (procrt::halton(i, 2) - 0.5F) * c.volScale[0];");
        line("    float lz = (procrt::halton(i, 3) - 0.5F) * c.volScale[2];");
        line("    float px, pz; procrt::toWorld(c, lx, lz, px, pz);");
        line("    if (px < -c.mapW * 0.5F || px > c.mapW * 0.5F || pz < -c.mapD * "
             "0.5F || pz > c.mapD * 0.5F) continue;");
        if (!mask.empty()) {
            line("    float mv = procrt::clamp01(" + mask + ");");
            line("    if (procrt::rand01(c.seed, " + ifmt(n.id) +
                 ", key, 0) >= mv) continue;");
        }
        line("    if (c.count >= c.cap) break;");
        line("    procrt::Pt& P = c.buf[c.count++];");
        line("    procrt::clearPt(P);");
        line("    P.key = key; P.x = px; P.z = pz; P.y = c.terrainY(px, pz) + " +
             fmt(lift) + ";");
        line("    float nx, ny, nz; procrt::terrainNormal(c, px, pz, nx, ny, nz);");
        baseAttrs("nx", "ny", "nz", n.id);
        if (!mask.empty() && attrSlot(procattr::kMask) >= 0)
            line("    P.a[" + ifmt(attrSlot(procattr::kMask)) + "] = mv;");
        line("  }");
        line("}");
        line("int " + v + "e = c.count;");
        return {v + "b", v + "e"};
    }

    Range emitScatterGrid(const ProcNode& n, const std::string& v) {
        const float spacing = std::max(0.1f, procgraph::num(n, "spacing"));
        const float jitter = std::clamp(procgraph::num(n, "jitter"), 0.0f, 1.0f);
        const bool snap = procgraph::flag(n, "snap");
        const float lift = procgraph::num(n, "lift");
        const int levels = std::max(1, procgraph::inum(n, "levels"));
        const float step = procgraph::num(n, "levelstep");
        const std::string mask = maskExpr(n, 0, "px", "pz");
        line("int " + v + "b = c.count;");
        line("{");
        line("  int gx = (int)(c.volScale[0] / " + fmt(spacing) +
             "); if (gx < 1) gx = 1;");
        line("  int gz = (int)(c.volScale[2] / " + fmt(spacing) +
             "); if (gz < 1) gz = 1;");
        line("  for (int iy = 0; iy < " + ifmt(levels) + "; ++iy)");
        line("  for (int iz = 0; iz < gz; ++iz)");
        line("  for (int ix = 0; ix < gx; ++ix) {");
        line("    unsigned long long key = procrt::mix64(((unsigned long long)" +
             ifmt(n.id) +
             " << 40) ^ ((unsigned long long)(unsigned)iy << 32) ^ "
             "((unsigned long long)(unsigned)iz << 20) ^ (unsigned long long)(unsigned)ix);");
        line("    float jx = (procrt::rand01(c.seed, " + ifmt(n.id) +
             ", key, 2) - 0.5F) * " + fmt(jitter * spacing) + ";");
        line("    float jz = (procrt::rand01(c.seed, " + ifmt(n.id) +
             ", key, 3) - 0.5F) * " + fmt(jitter * spacing) + ";");
        line("    float lx = -c.volScale[0] * 0.5F + ((float)ix + 0.5F) * " +
             fmt(spacing) + " + jx;");
        line("    float lz = -c.volScale[2] * 0.5F + ((float)iz + 0.5F) * " +
             fmt(spacing) + " + jz;");
        line("    float px, pz; procrt::toWorld(c, lx, lz, px, pz);");
        line("    if (px < -c.mapW * 0.5F || px > c.mapW * 0.5F || pz < -c.mapD * "
             "0.5F || pz > c.mapD * 0.5F) continue;");
        if (!mask.empty()) {
            line("    float mv = procrt::clamp01(" + mask + ");");
            line("    if (procrt::rand01(c.seed, " + ifmt(n.id) +
                 ", key, 0) >= mv) continue;");
        }
        line("    if (c.count >= c.cap) break;");
        line("    procrt::Pt& P = c.buf[c.count++];");
        line("    procrt::clearPt(P);");
        line("    P.key = key; P.x = px; P.z = pz;");
        line(std::string("    P.y = ") +
             (snap ? "c.terrainY(px, pz)" : "c.volPos[1]") + " + " + fmt(lift) +
             " + (float)iy * " + fmt(step) + ";");
        if (snap)
            line("    float nx, ny, nz; procrt::terrainNormal(c, px, pz, nx, ny, nz);");
        else
            line("    float nx = 0.0F, ny = 1.0F, nz = 0.0F;");
        baseAttrs("nx", "ny", "nz", n.id);
        if (!mask.empty() && attrSlot(procattr::kMask) >= 0)
            line("    P.a[" + ifmt(attrSlot(procattr::kMask)) + "] = mv;");
        line("  }");
        line("}");
        line("int " + v + "e = c.count;");
        return {v + "b", v + "e"};
    }

    Range emitScatterVolume(const ProcNode& n, const std::string& v) {
        const int count = std::max(0, procgraph::inum(n, "count"));
        line("int " + v + "b = c.count;");
        line("for (int i = 0; i < " + ifmt(count) + " && c.count < c.cap; ++i) {");
        line("  unsigned long long key = procrt::mix64(((unsigned long long)" +
             ifmt(n.id) + " << 40) ^ (unsigned long long)i);");
        line("  float lx = (procrt::halton(i, 2) - 0.5F) * c.volScale[0];");
        line("  float lz = (procrt::halton(i, 3) - 0.5F) * c.volScale[2];");
        line("  float ly = (procrt::halton(i, 5) - 0.5F) * c.volScale[1];");
        line("  float px, pz; procrt::toWorld(c, lx, lz, px, pz);");
        line("  procrt::Pt& P = c.buf[c.count++];");
        line("  procrt::clearPt(P);");
        line("  P.key = key; P.x = px; P.z = pz; P.y = c.volPos[1] + ly;");
        line("  float nx = 0.0F, ny = 1.0F, nz = 0.0F;");
        baseAttrs("nx", "ny", "nz", n.id);
        line("}");
        line("int " + v + "e = c.count;");
        return {v + "b", v + "e"};
    }

    Range emitSinglePoint(const ProcNode& n, const std::string& v) {
        // A named target is resolved HERE, at build time: the console has the
        // object, but nothing about a Single Point is meant to move, and
        // baking it keeps the generated code free of a name lookup.
        float ax = volObj.position[0], ay = volObj.position[1],
              az = volObj.position[2];
        const std::string& target = procgraph::str(n, "target");
        if (!target.empty())
            for (const SceneObject& o : scene.objects)
                if (o.name == target) {
                    ax = o.position[0];
                    ay = o.position[1];
                    az = o.position[2];
                    break;
                }
        const bool snap = procgraph::flag(n, "snap");
        line("int " + v + "b = c.count;");
        line("if (c.count < c.cap) {");
        line("  procrt::Pt& P = c.buf[c.count++];");
        line("  procrt::clearPt(P);");
        line("  P.key = procrt::mix64(((unsigned long long)" + ifmt(n.id) +
             " << 40) ^ 1ULL);");
        line("  P.x = " + fmt(ax + procgraph::num(n, "x")) + ";");
        line("  P.z = " + fmt(az + procgraph::num(n, "z")) + ";");
        line(std::string("  P.y = ") +
             (snap ? "c.terrainY(P.x, P.z)" : fmt(ay)) + " + " +
             fmt(procgraph::num(n, "y")) + ";");
        if (snap)
            line("  float nx, ny, nz; procrt::terrainNormal(c, P.x, P.z, nx, ny, nz);");
        else
            line("  float nx = 0.0F, ny = 1.0F, nz = 0.0F;");
        baseAttrs("nx", "ny", "nz", n.id);
        line("}");
        line("int " + v + "e = c.count;");
        return {v + "b", v + "e"};
    }

    Range emitBlocks(const ProcNode& n, const std::string& v) {
        const float block = std::max(0.25f, procgraph::num(n, "block"));
        const int levels = std::clamp(procgraph::inum(n, "levels"), 1, 32);
        const int floorL = std::clamp(procgraph::inum(n, "floor"), 0, levels);
        const float scale = std::max(2.0f, procgraph::num(n, "scale"));
        const int oct = std::clamp(procgraph::inum(n, "octaves"), 1, 6);
        const float relief = std::clamp(procgraph::num(n, "relief"), 0.0f, 1.0f);
        const int emitDepth = std::clamp(procgraph::inum(n, "depth"), 1, 32);
        const float baseY = procgraph::num(n, "base");
        const std::string mask = maskExpr(n, 0, "hx", "hz");
        // The column field is built once into the caller's collision buffer and
        // then read for both the visibility test and the walker - one grid, one
        // source of truth about where the ground is.
        line("int " + v + "b = c.count;");
        line("{");
        line("  const float bs = " + fmt(block) + ";");
        line("  int nx = (int)(c.footX / bs); if (nx < 1) nx = 1;");
        line("  int nz = (int)(c.footZ / bs); if (nz < 1) nz = 1;");
        line("  if (nx * nz > c.blockCap) { nx = nz = 0; }");
        line("  c.blockNx = nx; c.blockNz = nz; c.blockCell = bs;");
        line("  c.blockOx = c.footX0; c.blockOz = c.footZ0; c.blockBaseY = " +
             fmt(baseY) + ";");
        line("  c.blockLevels = " + ifmt(levels) + ";");
        line("  for (int iz = 0; iz < nz; ++iz) for (int ix = 0; ix < nx; ++ix) {");
        line("    float hx = c.footX0 + ((float)ix + 0.5F) * bs;");
        line("    float hz = c.footZ0 + ((float)iz + 0.5F) * bs;");
        if (!mask.empty()) {
            line("    float nv = procrt::clamp01(" + mask + ");");
        } else {
            line("    unsigned int sd = c.seed ^ (unsigned int)(" + ifmt(n.id) +
                 " * 2654435761u);");
            line("    float nv = procrt::clamp01(procrt::noiseAt(0, hx / " +
                 fmt(scale) + ", hz / " + fmt(scale) + ", " + ifmt(oct) +
                 ", sd));");
        }
        line("    int h = " + ifmt(floorL) + " + (int)(nv * " +
             fmt((float)levels * relief) + " + 0.5F);");
        line("    if (h < 0) h = 0;");
        line("    if (h > " + ifmt(levels) + ") h = " + ifmt(levels) + ";");
        line("    c.blockCol[iz * nx + ix] = h > 0 ? ((h >= 32) ? 0xffffffffu : "
             "((1u << h) - 1u)) : 0u;");
        line("  }");
        line("  for (int iz = 0; iz < nz; ++iz) for (int ix = 0; ix < nx; ++ix) {");
        line("    int top = procrt::colHeight(c, ix, iz);");
        line("    if (top <= 0) continue;");
        line("    int hxp = procrt::colHeight(c, ix + 1, iz);");
        line("    int hxm = procrt::colHeight(c, ix - 1, iz);");
        line("    int hzp = procrt::colHeight(c, ix, iz + 1);");
        line("    int hzm = procrt::colHeight(c, ix, iz - 1);");
        line("    int lowest = top - " + ifmt(emitDepth) +
             "; if (lowest < 0) lowest = 0;");
        line("    for (int iy = top - 1; iy >= lowest; --iy) {");
        line("      unsigned char faces = 0;");
        line("      if (iy >= hxp) faces |= 1;");
        line("      if (iy >= hxm) faces |= 2;");
        line("      if (iy == top - 1) faces |= 4;");
        line("      if (iy == 0) faces |= 8;");
        line("      if (iy >= hzp) faces |= 16;");
        line("      if (iy >= hzm) faces |= 32;");
        line("      if (faces == 0) continue;");
        line("      if (c.count >= c.cap) break;");
        line("      unsigned long long key = procrt::mix64(((unsigned long long)" +
             ifmt(n.id) +
             " << 44) ^ ((unsigned long long)(unsigned)iy << 34) ^ "
             "((unsigned long long)(unsigned)iz << 17) ^ (unsigned long long)(unsigned)ix);");
        line("      procrt::Pt& P = c.buf[c.count++];");
        line("      procrt::clearPt(P);");
        line("      P.key = key; P.faces = faces; P.sc = bs; P.block = 1;");
        line("      P.x = c.blockOx + ((float)ix + 0.5F) * bs;");
        line("      P.y = " + fmt(baseY) + " + ((float)iy + 0.5F) * bs;");
        line("      P.z = c.blockOz + ((float)iz + 0.5F) * bs;");
        line("      float nx2 = 0.0F, ny2 = 1.0F, nz2 = 0.0F;");
        {
            const int saved = indent;
            indent += 4;
            baseAttrs("nx2", "ny2", "nz2", n.id);
            indent = saved;
        }
        if (attrSlot(procattr::kDepth) >= 0)
            line("      P.a[" + ifmt(attrSlot(procattr::kDepth)) +
                 "] = (float)(top - 1 - iy);");
        if (attrSlot(procattr::kFaces) >= 0)
            line("      P.a[" + ifmt(attrSlot(procattr::kFaces)) +
                 "] = (float)faces;");
        line("    }");
        line("  }");
        line("}");
        line("int " + v + "e = c.count;");
        return {v + "b", v + "e"};
    }

    Range emitFilterRange(const ProcNode& n, const std::string& v, const Range& r) {
        std::string attr = procgraph::str(n, "attr");
        if (attr.empty()) attr = procattr::kSlope;
        const int slot = attrSlot(attr);
        if (slot < 0) return r;  // an attribute nothing writes: pass through
        const bool inv = procgraph::flag(n, "invert");
        line("int " + v + "b = " + r.begin + "; int " + v + "e = " + r.end + ";");
        line("{ int w = " + v + "b;");
        line("  for (int i = " + v + "b; i < " + v + "e; ++i) {");
        line("    float pr = procrt::band(c.buf[i].a[" + ifmt(slot) + "], " +
             fmt(procgraph::num(n, "min")) + ", " + fmt(procgraph::num(n, "max")) +
             ", " + fmt(procgraph::num(n, "falloff")) + ");");
        if (inv) line("    pr = 1.0F - pr;");
        line("    if (procrt::rand01(c.seed, " + ifmt(n.id) +
             ", c.buf[i].key, 10) < pr) c.buf[w++] = c.buf[i];");
        line("  }");
        line("  " + v + "e = w; c.count = w; }");
        return {v + "b", v + "e"};
    }

    Range emitFilterMask(const ProcNode& n, const std::string& v, const Range& r) {
        const std::string mask = maskExpr(n, 1, "c.buf[i].x", "c.buf[i].z");
        if (mask.empty()) return r;
        const bool inv = procgraph::flag(n, "invert");
        const float strength = std::clamp(procgraph::num(n, "strength"), 0.0f, 1.0f);
        line("int " + v + "b = " + r.begin + "; int " + v + "e = " + r.end + ";");
        line("{ int w = " + v + "b;");
        line("  for (int i = " + v + "b; i < " + v + "e; ++i) {");
        line("    float mv = procrt::remap01(" + mask + ", " +
             fmt(procgraph::num(n, "low")) + ", " +
             fmt(procgraph::num(n, "high")) + ");");
        if (inv) line("    mv = 1.0F - mv;");
        if (attrSlot(procattr::kMask) >= 0)
            line("    c.buf[i].a[" + ifmt(attrSlot(procattr::kMask)) + "] = mv;");
        line("    float pr = 1.0F - " + fmt(strength) + " * (1.0F - mv);");
        line("    if (procrt::rand01(c.seed, " + ifmt(n.id) +
             ", c.buf[i].key, 11) < pr) c.buf[w++] = c.buf[i];");
        line("  }");
        line("  " + v + "e = w; c.count = w; }");
        return {v + "b", v + "e"};
    }

    Range emitFilterDistance(const ProcNode& n, const std::string& v,
                             const Range& r) {
        // O(n^2) inside a cell-free window would be hopeless, so this is the
        // greedy sweep with the same "first point wins" rule the host uses,
        // over a fixed grid the caller owns. Prefix stability makes the greedy
        // pass deterministic - the order is the generator's order.
        const float radius = std::max(0.01f, procgraph::num(n, "radius"));
        const bool bySize = procgraph::flag(n, "bysize");
        const int sizeSlot = attrSlot(procattr::kSize);
        line("int " + v + "b = " + r.begin + "; int " + v + "e = " + r.end + ";");
        line("{ int w = " + v + "b;");
        line("  for (int i = " + v + "b; i < " + v + "e; ++i) {");
        line("    float ri = " + fmt(radius) + ";");
        if (bySize && sizeSlot >= 0)
            line("    ri *= c.buf[i].a[" + ifmt(sizeSlot) + "] * c.buf[i].sc;");
        line("    bool blocked = false;");
        line("    for (int j = " + v + "b; j < w && !blocked; ++j) {");
        line("      float rj = " + fmt(radius) + ";");
        if (bySize && sizeSlot >= 0)
            line("      rj *= c.buf[j].a[" + ifmt(sizeSlot) + "] * c.buf[j].sc;");
        line("      float ddx = c.buf[j].x - c.buf[i].x;");
        line("      float ddz = c.buf[j].z - c.buf[i].z;");
        line("      float need = ri > rj ? ri : rj;");
        line("      if (ddx * ddx + ddz * ddz < need * need) blocked = true;");
        line("    }");
        line("    if (!blocked) c.buf[w++] = c.buf[i];");
        line("  }");
        line("  " + v + "e = w; c.count = w; }");
        return {v + "b", v + "e"};
    }

    Range emitLimit(const ProcNode& n, const std::string& v, const Range& r) {
        const int max = std::max(1, procgraph::inum(n, "max"));
        line("int " + v + "b = " + r.begin + "; int " + v + "e = " + r.end + ";");
        line("if (" + v + "e - " + v + "b > " + ifmt(max) + ") { " + v + "e = " +
             v + "b + " + ifmt(max) + "; c.count = " + v + "e; }");
        return {v + "b", v + "e"};
    }

    Range emitArray(const ProcNode& n, const std::string& v, const Range& r) {
        const int count = std::clamp(procgraph::inum(n, "count"), 1, 2000);
        const float dx = procgraph::num(n, "dx"), dy = procgraph::num(n, "dy"),
                    dz = procgraph::num(n, "dz");
        const float yaw = procgraph::num(n, "yaw");
        const float sc = std::max(0.1f, procgraph::num(n, "scale"));
        const bool local = procgraph::flag(n, "local");
        const bool snap = procgraph::flag(n, "snap");
        line("int " + v + "b = " + r.begin + ";");
        line("{ int src = c.count; int ie = " + r.end + ";");
        line("  for (int s = " + v + "b; s < ie; ++s) {");
        line("    procrt::Pt base = c.buf[s];");
        line(std::string("    float a = ") +
             (local ? "base.ry * 0.01745329F" : "0.0F") + ";");
        line("    float ca = procrt::cosf_(a), sa = procrt::sinf_(a);");
        line("    for (int i = 0; i < " + ifmt(count) + "; ++i) {");
        line("      if (c.count >= c.cap) break;");
        line("      procrt::Pt& P = c.buf[c.count++]; P = base;");
        line("      float ox = " + fmt(dx) + " * (float)i, oy = " + fmt(dy) +
             " * (float)i, oz = " + fmt(dz) + " * (float)i;");
        line(std::string("      P.x = base.x + ") +
             (local ? "(ox * ca + oz * sa)" : "ox") + ";");
        line(std::string("      P.z = base.z + ") +
             (local ? "(-ox * sa + oz * ca)" : "oz") + ";");
        line(std::string("      P.y = ") +
             (snap ? "c.terrainY(P.x, P.z)" : "base.y") + " + oy;");
        line("      P.ry = base.ry + " + fmt(yaw) + " * (float)i;");
        line("      P.sc = base.sc * procrt::powf_(" + fmt(sc) + ", (float)i);");
        line("      P.key = i == 0 ? base.key : procrt::copyKey(" + ifmt(n.id) +
             ", base.key, i);");
        line("    }");
        line("  }");
        line("  int total = c.count - src;");
        line("  for (int t = 0; t < total; ++t) c.buf[" + v + "b + t] = c.buf[src + t];");
        line("  c.count = " + v + "b + total; }");
        line("int " + v + "e = c.count;");
        return {v + "b", v + "e"};
    }

    Range emitRadial(const ProcNode& n, const std::string& v, const Range& r) {
        const int count = std::clamp(procgraph::inum(n, "count"), 1, 2000);
        const float radius = std::max(0.0f, procgraph::num(n, "radius"));
        const int axis = std::clamp(procgraph::inum(n, "axis"), 0, 2);
        const float start = procgraph::num(n, "start");
        const float sweep = std::clamp(procgraph::num(n, "sweep"), 1.0f, 360.0f);
        const bool face = procgraph::flag(n, "face");
        const bool snap = procgraph::flag(n, "snap");
        const bool full = sweep >= 359.9f;
        const float stepDeg =
            count > 1 ? sweep / (float)(full ? count : count - 1) : 0.0f;
        line("int " + v + "b = " + r.begin + ";");
        line("{ int src = c.count; int ie = " + r.end + ";");
        line("  for (int s = " + v + "b; s < ie; ++s) {");
        line("    procrt::Pt base = c.buf[s];");
        line("    for (int i = 0; i < " + ifmt(count) + "; ++i) {");
        line("      if (c.count >= c.cap) break;");
        line("      float deg = " + fmt(start) + " + " + fmt(stepDeg) + " * (float)i;");
        line("      float rad = deg * 0.01745329F;");
        line("      float cc = procrt::cosf_(rad), ss = procrt::sinf_(rad);");
        line("      procrt::Pt& P = c.buf[c.count++]; P = base;");
        if (axis == 1) {
            line("      P.y = base.y + " + fmt(radius) + " * cc;");
            line("      P.z = base.z + " + fmt(radius) + " * ss;");
            if (face) line("      P.rx = base.rx + deg;");
        } else if (axis == 2) {
            line("      P.x = base.x + " + fmt(radius) + " * cc;");
            line("      P.y = base.y + " + fmt(radius) + " * ss;");
            if (face) line("      P.rz = base.rz + deg;");
        } else {
            line("      P.x = base.x + " + fmt(radius) + " * ss;");
            line("      P.z = base.z + " + fmt(radius) + " * cc;");
            if (face) line("      P.ry = base.ry + deg;");
        }
        if (snap) line("      P.y = c.terrainY(P.x, P.z);");
        line("      P.key = procrt::copyKey(" + ifmt(n.id) + ", base.key, i);");
        line("    }");
        line("  }");
        line("  int total = c.count - src;");
        line("  for (int t = 0; t < total; ++t) c.buf[" + v + "b + t] = c.buf[src + t];");
        line("  c.count = " + v + "b + total; }");
        line("int " + v + "e = c.count;");
        return {v + "b", v + "e"};
    }

    Range emitPick(const ProcNode& n, const std::string& v, const Range& r,
                   bool prefabs) {
        struct Row {
            int idx;
            float w, smin, smax;
        };
        std::vector<Row> rows;
        float total = 0.0f;
        const std::vector<std::string>& table = prefabs ? vol.prefabs : vol.assets;
        for (const ProcRow& row : n.rows) {
            if (row.s.empty()) continue;
            const float w = std::max(0.0f, row.v[0]);
            if (w <= 0.0f) continue;
            int idx = -1;
            for (size_t i = 0; i < table.size(); ++i)
                if (table[i] == row.s) idx = (int)i;
            if (idx < 0) continue;
            const float smin = row.v[1] > 0.0f ? row.v[1] : 1.0f;
            const float smax = row.v[2] > 0.0f ? row.v[2] : smin;
            rows.push_back({idx, w, smin, smax});
            total += w;
        }
        if (rows.empty()) return r;
        const int chan = prefabs ? 22 : 20;
        const int chan2 = prefabs ? 23 : 21;
        const int sizeSlot = attrSlot(procattr::kSize);
        line("int " + v + "b = " + r.begin + "; int " + v + "e = " + r.end + ";");
        line("for (int i = " + v + "b; i < " + v + "e; ++i) {");
        // Cumulative thresholds - the flat form of the host's subtract-and-walk
        // loop, so a weight change moves the boundary between species without
        // reshuffling anything. A one-entry pool needs no draw at all.
        if (rows.size() > 1)
            line("  float rr = procrt::rand01(c.seed, " + ifmt(n.id) +
                 ", c.buf[i].key, " + ifmt(chan) + ") * " + fmt(total) + ";");
        line("  int pick; float smin, smax;");
        {
            float acc = 0.0f;
            for (size_t k = 0; k < rows.size(); ++k) {
                acc += rows[k].w;
                const std::string head =
                    (k + 1 == rows.size())
                        ? (k ? std::string("  else {") : std::string("  {"))
                        : ("  " + std::string(k ? "else " : "") + "if (rr < " +
                           fmt(acc) + ") {");
                line(head + " pick = " + ifmt(rows[k].idx) + "; smin = " +
                     fmt(rows[k].smin) + "; smax = " + fmt(rows[k].smax) + "; }");
            }
        }
        line("  float t = procrt::rand01(c.seed, " + ifmt(n.id) + ", c.buf[i].key, " +
             ifmt(chan2) + ");");
        line("  float sz = smin + (smax - smin) * t;");
        if (prefabs) {
            line("  c.buf[i].prefab = (short)pick; c.buf[i].asset = -1;");
        } else {
            line("  c.buf[i].asset = (short)pick;");
        }
        line("  c.buf[i].sc *= sz;");
        if (sizeSlot >= 0) line("  c.buf[i].a[" + ifmt(sizeSlot) + "] = sz;");
        line("}");
        return {v + "b", v + "e"};
    }

    Range emitVary(const ProcNode& n, const std::string& v, const Range& r) {
        const float yaw = procgraph::num(n, "yaw");
        const float tilt = procgraph::num(n, "tilt");
        const float smin = procgraph::num(n, "scalemin");
        const float smax = procgraph::num(n, "scalemax");
        const float jitter = procgraph::num(n, "jitter");
        const float align = std::clamp(procgraph::num(n, "align"), 0.0f, 1.0f);
        const int nxs = attrSlot(procattr::kNormalX);
        const int nys = attrSlot(procattr::kNormalY);
        const int nzs = attrSlot(procattr::kNormalZ);
        line("int " + v + "b = " + r.begin + "; int " + v + "e = " + r.end + ";");
        line("for (int i = " + v + "b; i < " + v + "e; ++i) {");
        line("  procrt::Pt& P = c.buf[i];");
        if (yaw != 0.0f)
            line("  P.ry += (procrt::rand01(c.seed, " + ifmt(n.id) +
                 ", P.key, 30) - 0.5F) * " + fmt(yaw) + ";");
        if (align > 0.0f && nxs >= 0 && nys >= 0 && nzs >= 0) {
            line("  { float lean = procrt::acosf_(P.a[" + ifmt(nys) +
                 "]) * 57.29578F * " + fmt(align) + ";");
            line("    float dy = procrt::atan2f_(P.a[" + ifmt(nxs) + "], P.a[" +
                 ifmt(nzs) + "]) * 57.29578F;");
            line("    P.rx += lean * procrt::cosf_((dy - P.ry) * 0.01745329F);");
            line("    P.rz += -lean * procrt::sinf_((dy - P.ry) * 0.01745329F); }");
        }
        if (tilt > 0.0f) {
            line("  P.rx += (procrt::rand01(c.seed, " + ifmt(n.id) +
                 ", P.key, 31) - 0.5F) * " + fmt(2.0f * tilt) + ";");
            line("  P.rz += (procrt::rand01(c.seed, " + ifmt(n.id) +
                 ", P.key, 32) - 0.5F) * " + fmt(2.0f * tilt) + ";");
        }
        if (smax > 0.0f && (smin != 1.0f || smax != 1.0f))
            line("  P.sc *= " + fmt(smin) + " + " + fmt(smax - smin) +
                 " * procrt::rand01(c.seed, " + ifmt(n.id) + ", P.key, 33);");
        if (jitter > 0.0f) {
            line("  P.x += (procrt::rand01(c.seed, " + ifmt(n.id) +
                 ", P.key, 34) - 0.5F) * " + fmt(2.0f * jitter) + ";");
            line("  P.z += (procrt::rand01(c.seed, " + ifmt(n.id) +
                 ", P.key, 35) - 0.5F) * " + fmt(2.0f * jitter) + ";");
        }
        line("}");
        return {v + "b", v + "e"};
    }

    Range emitSetAttr(const ProcNode& n, const std::string& v, const Range& r) {
        std::string name = procgraph::str(n, "attr");
        if (name.empty()) name = procattr::kMask;
        const int slot = attrSlot(name);
        const std::string mask = maskExpr(n, 1, "c.buf[i].x", "c.buf[i].z");
        if (slot < 0 || mask.empty()) return r;
        const float lo = procgraph::num(n, "min"), hi = procgraph::num(n, "max");
        line("int " + v + "b = " + r.begin + "; int " + v + "e = " + r.end + ";");
        line("for (int i = " + v + "b; i < " + v + "e; ++i)");
        line("  c.buf[i].a[" + ifmt(slot) + "] = " + fmt(lo) + " + " +
             fmt(hi - lo) + " * procrt::clamp01(" + mask + ");");
        return {v + "b", v + "e"};
    }
};

// The runtime helpers the generated code calls - the console-side twins of
// procgen.cpp's anonymous namespace. Every one of them must stay numerically
// identical to its host original or the editor preview stops predicting what
// the console builds (which is the whole reason a runtime volume can be
// previewed at all).
const char* kRuntimePrelude = R"CPP(
namespace procrt {

unsigned long long mix64(unsigned long long x) {
  x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
  x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL;
  x ^= x >> 33; return x;
}
unsigned long long hashCombine(unsigned long long h, unsigned long long v) {
  return mix64(h ^ mix64(v));
}
float unitFromHash(unsigned long long h) {
  return (float)(h >> 40) * (1.0F / 16777216.0F);
}
float rand01(unsigned int seed, int nodeId, unsigned long long key, int channel) {
  unsigned long long h = mix64(0x51ed270b7f3ac1ULL ^ (unsigned long long)seed);
  h = hashCombine(h, (unsigned long long)(unsigned)nodeId);
  h = hashCombine(h, key);
  h = hashCombine(h, (unsigned long long)(unsigned)channel);
  return unitFromHash(h);
}
unsigned long long copyKey(int nodeId, unsigned long long srcKey, int i) {
  return mix64(srcKey ^ ((unsigned long long)(unsigned)nodeId << 40) ^
               ((unsigned long long)(unsigned)i * 0x9E3779B97F4A7C15ULL));
}
float halton(int index, int base) {
  float f = 1.0F, r = 0.0F;
  int i = index + 1;
  while (i > 0) { f /= (float)base; r += f * (float)(i % base); i /= base; }
  return r;
}
float clamp01(float v) { return v < 0.0F ? 0.0F : (v > 1.0F ? 1.0F : v); }
float cosf_(float a) { return cosf(a); }
float sinf_(float a) { return sinf(a); }
float acosf_(float v) { return acosf(v < -1.0F ? -1.0F : (v > 1.0F ? 1.0F : v)); }
float atan2f_(float y, float x) { return atan2f(y, x); }
float powf_(float b, float e) { return powf(b, e); }
float powf01(float b, float e) { return b <= 0.0F ? 0.0F : powf(b, e); }
float slopeDeg(float ny) { return acosf_(ny) * 57.29578F; }

float gradDot(int ix, int iz, float dx, float dz, unsigned int seed) {
  unsigned long long h = mix64(((unsigned long long)(unsigned)ix << 32) ^
                               (unsigned)iz ^ ((unsigned long long)seed << 16));
  float a = unitFromHash(h) * 6.2831853F;
  return cosf_(a) * dx + sinf_(a) * dz;
}
float fade(float t) { return t * t * t * (t * (t * 6.0F - 15.0F) + 10.0F); }
float perlin(float x, float z, unsigned int seed) {
  int x0 = (int)floorf(x), z0 = (int)floorf(z);
  float fx = x - (float)x0, fz = z - (float)z0;
  float u = fade(fx), v = fade(fz);
  float n00 = gradDot(x0, z0, fx, fz, seed);
  float n10 = gradDot(x0 + 1, z0, fx - 1.0F, fz, seed);
  float n01 = gradDot(x0, z0 + 1, fx, fz - 1.0F, seed);
  float n11 = gradDot(x0 + 1, z0 + 1, fx - 1.0F, fz - 1.0F, seed);
  float a = n00 + u * (n10 - n00);
  float b = n01 + u * (n11 - n01);
  return clamp01((a + v * (b - a)) * 0.7071F + 0.5F);
}
float worley(float x, float z, unsigned int seed) {
  int cx = (int)floorf(x), cz = (int)floorf(z);
  float best = 4.0F;
  for (int dz = -1; dz <= 1; ++dz)
    for (int dx = -1; dx <= 1; ++dx) {
      int gx = cx + dx, gz = cz + dz;
      unsigned long long h = mix64(((unsigned long long)(unsigned)gx << 32) ^
                                   (unsigned)gz ^ ((unsigned long long)seed << 20));
      float px = (float)gx + unitFromHash(h);
      float pz = (float)gz + unitFromHash(mix64(h));
      float d = (px - x) * (px - x) + (pz - z) * (pz - z);
      if (d < best) best = d;
    }
  float r = sqrtf(best);
  return r > 1.0F ? 1.0F : r;
}
float noiseAt(int kind, float x, float z, int octaves, unsigned int seed) {
  float sum = 0.0F, amp = 1.0F, norm = 0.0F, fx = x, fz = z;
  for (int o = 0; o < octaves; ++o) {
    float n;
    if (kind == 1) {
      n = 1.0F - fabsf(perlin(fx, fz, seed + o * 977) * 2.0F - 1.0F);
    } else if (kind == 2) {
      n = 1.0F - worley(fx, fz, seed + o * 977);
    } else if (kind == 3) {
      float wx = perlin(fx * 0.5F, fz * 0.5F, seed + 31) - 0.5F;
      float wz = perlin(fx * 0.5F + 5.2F, fz * 0.5F + 1.3F, seed + 71) - 0.5F;
      n = perlin(fx + wx * 2.5F, fz + wz * 2.5F, seed + o * 977);
    } else {
      n = perlin(fx, fz, seed + o * 977);
    }
    sum += n * amp; norm += amp; amp *= 0.5F; fx *= 2.0F; fz *= 2.0F;
  }
  return norm > 0.0F ? sum / norm : 0.0F;
}
float band(float v, float lo, float hi, float falloff) {
  if (lo > hi) { float t = lo; lo = hi; hi = t; }
  if (v >= lo && v <= hi) return 1.0F;
  if (falloff <= 0.0F) return 0.0F;
  float d = v < lo ? lo - v : v - hi;
  return clamp01(1.0F - d / falloff);
}
float remap01(float v, float lo, float hi) {
  if (hi - lo < 1e-6F) return v >= hi ? 1.0F : 0.0F;
  return clamp01((v - lo) / (hi - lo));
}

void clearPt(Pt& p) {
  p.x = p.y = p.z = 0.0F; p.rx = p.ry = p.rz = 0.0F; p.sc = 1.0F;
  p.key = 0ULL; p.asset = -1; p.prefab = -1; p.faces = 63; p.block = 0;
  for (int i = 0; i < PROC_ATTR_SLOTS; ++i) p.a[i] = 0.0F;
}
// Volume local -> world, the twin of procgen's Volume::localToWorld.
void toWorld(const Ctx& c, float lx, float lz, float& wx, float& wz) {
  float a = c.volYaw * 0.01745329F;
  float cc = cosf_(a), ss = sinf_(a);
  wx = c.volPos[0] + lx * cc + lz * ss;
  wz = c.volPos[2] - lx * ss + lz * cc;
}
float footprintArea(const Ctx& c) { return c.footX * c.footZ; }
void terrainNormal(const Ctx& c, float x, float z, float& nx, float& ny, float& nz) {
  const float e = 0.5F;
  float hx = c.terrainY(x + e, z) - c.terrainY(x - e, z);
  float hz = c.terrainY(x, z + e) - c.terrainY(x, z - e);
  float vx = -hx, vy = 2.0F * e, vz = -hz;
  float len = sqrtf(vx * vx + vy * vy + vz * vz);
  float inv = len > 1e-6F ? 1.0F / len : 0.0F;
  nx = vx * inv; ny = vy * inv; nz = vz * inv;
}
float terrainSlope(const Ctx& c, float x, float z) {
  float nx, ny, nz; terrainNormal(c, x, z, nx, ny, nz);
  return slopeDeg(ny);
}
float terrainCurvature(const Ctx& c, float x, float z) {
  const float e = 2.0F;
  float cv = c.terrainY(x, z);
  float sum = c.terrainY(x + e, z) + c.terrainY(x - e, z) +
              c.terrainY(x, z + e) + c.terrainY(x, z - e);
  float r = (4.0F * cv - sum) * 0.5F;
  return r < -1.0F ? -1.0F : (r > 1.0F ? 1.0F : r);
}
// Column height in blocks; off the lattice reads as open so the world's rim
// shows its sides instead of ending in a seam.
int colHeight(const Ctx& c, int ix, int iz) {
  if (ix < 0 || iz < 0 || ix >= c.blockNx || iz >= c.blockNz) return 0;
  unsigned int m = c.blockCol[iz * c.blockNx + ix];
  int h = 0;
  while (m) { ++h; m >>= 1; }
  return h;
}

}  // namespace procrt
)CPP";

}  // namespace

Emitted emit(const Project& p) {
    Emitted out;
    const std::vector<Volume> vols = volumes(p);
    const std::vector<std::string> attrs = attrNames(p);

    std::vector<const Volume*> good;
    for (const Volume& v : vols) {
        if (!v.issues.empty()) {
            std::string w = "procedural: runtime volume \"" + v.name +
                            "\" skipped - " + v.issues.front().text;
            out.warnings.push_back(w);
            continue;
        }
        good.push_back(&v);
    }

    int maxInstances = 0, blockCap = 0;
    for (const Volume* v : good) {
        const SceneObject& o = p.scenes[v->scene].objects[v->objectIndex];
        const ProcNode* on = procgraph::outputNode(o.procGraph);
        int cap = on ? std::max(64, procgraph::inum(*on, "maxinst")) : 4096;
        maxInstances = std::max(maxInstances, cap);
        blockCap = std::max(blockCap, v->blockColumns);
    }
    if (blockCap > 0) blockCap = std::min(blockCap + 64, 32768);

    // ---- header -----------------------------------------------------------
    {
        std::ostringstream h;
        h << "// Generated by TyraX. Do not edit - regenerated on every build.\n"
             "#pragma once\n\n"
             "// Runtime procedural generation (docs/procedural-runtime.md).\n"
             "// ENABLED is the on/off seam: with no runtime volume in the\n"
             "// project it is a compile-time false and every call site folds\n"
             "// away, so a game that does not use the feature carries none of\n"
             "// it.\n\n"
             "namespace procrt {\n\n";
        h << "constexpr bool ENABLED = " << (good.empty() ? "false" : "true") << ";\n";
        h << "constexpr int VOLUME_COUNT = " << good.size() << ";\n";
        h << "constexpr int MAX_INSTANCES = " << (good.empty() ? 1 : maxInstances)
          << ";\n";
        h << "constexpr int BLOCK_COLUMNS = " << blockCap << ";\n";
        h << "#define PROC_ATTR_SLOTS " << (attrs.empty() ? 1 : (int)attrs.size())
          << "\n\n";
        h << "// One generated instance. The buffer is BOTH the working set the\n"
             "// graph transforms and the result the merge reads - a second\n"
             "// array would be a copy of 4000 structs for no reason.\n"
             "struct Pt {\n"
             "  float x, y, z;\n"
             "  float rx, ry, rz;   // degrees\n"
             "  float sc;           // uniform scale\n"
             "  unsigned long long key;  // stable identity (see ProcOverride)\n"
             "  short asset;        // index into VolumeDef::assets, -1 = none\n"
             "  short prefab;       // index into VolumeDef::prefabs, -1 = none\n"
             "  unsigned char faces;  // visible-face mask, 63 = all six\n"
             "  // 1 = this point is a cell of the block lattice, so the merge\n"
             "  // may read the collision field around it for ambient occlusion.\n"
             "  // Stated rather than inferred: `faces` is 63 for a fully\n"
             "  // exposed block AND for every point that never met the node,\n"
             "  // so a graph merging blocks with a scatter has no other way to\n"
             "  // tell the two apart.\n"
             "  unsigned char block;\n"
             "  float a[PROC_ATTR_SLOTS];\n"
             "};\n\n";
        h << "struct Ctx {\n"
             "  float (*terrainY)(float, float);\n"
             "  float volPos[3];\n"
             "  float volScale[3];\n"
             "  float volYaw;            // degrees\n"
             "  float footX0, footZ0;    // axis-aligned footprint corner\n"
             "  float footX, footZ;      // ...and its size\n"
             "  float mapW, mapD;        // terrain extent (off-map rejection)\n"
             "  unsigned int seed;\n"
             "  Pt* buf;\n"
             "  int cap;\n"
             "  int count;\n"
             "  // Block collision field, filled by a Blocks Fill node. One\n"
             "  // 32-bit word per column (bit i = level i solid), which is why\n"
             "  // a block world is capped at 32 levels.\n"
             "  unsigned int* blockCol;\n"
             "  int blockCap;\n"
             "  int blockNx, blockNz, blockLevels;\n"
             "  float blockOx, blockOz, blockCell, blockBaseY;\n"
             "};\n\n";
        h << "struct VolumeDef {\n"
             "  int scene;\n"
             "  int objectIndex;      // the Procedural volume in that scene\n"
             "  const char* name;\n"
             "  float cell;           // chunk size (merge + cull granularity)\n"
             "  float drawDist;\n"
             "  int collide;\n"
             "  int runAtStart;\n"
             "  int seedMode;         // 0 = the authored seed, 1 = fresh per run\n"
             "  unsigned int seed;\n"
             "  int maxInstances;\n"
             "  int hasBlocks;\n"
             "  const short* assets;  // model index per asset slot\n"
             "  int assetCount;\n"
             "  const short* prefabs; // prefab index per prefab slot\n"
             "  int prefabCount;\n"
             "};\n\n";
        h << "extern const VolumeDef VOLUMES[];\n\n"
             "// Fills c.buf with the volume's instances and returns the count.\n"
             "// Never throws, never allocates: everything it touches is in Ctx.\n"
             "int generate(int volume, Ctx& c);\n\n"
             "// Asset and prefab slots are resolved BY NAME once per\n"
             "// generation: the model table is per project and the game owns\n"
             "// it, so baking indices in here would mean two files agreeing\n"
             "// about a table neither of them fully sees.\n"
             "const char* const* volumeAssetNames(int v, int* count);\n"
             "const char* const* volumePrefabNames(int v, int* count);\n"
             "short* volumeAssetSlots(int v);\n"
             "short* volumePrefabSlots(int v);\n\n"
             "}  // namespace procrt\n";
        out.header = h.str();
    }

    // ---- source -----------------------------------------------------------
    std::ostringstream s;
    s << "// Generated by TyraX. Do not edit - regenerated on every build.\n";
    if (good.empty()) {
        // No runtime volume: still define the symbols, because the game's call
        // sites are guarded by the compile-time ENABLED rather than by #if -
        // a runtime `if` keeps the generated game source free of conditional
        // compilation, and the linker still needs something to point at.
        s << "// No runtime procedural volume in this project.\n"
             "#include \"procedural.gen.hpp\"\n\n"
             "namespace procrt {\n"
             "const VolumeDef VOLUMES[1] = {{0, -1, \"\", 48.0F, 0.0F, 0, 0, 0, "
             "1u, 1, 0, 0, 0, 0, 0}};\n"
             "int generate(int, Ctx& c) { c.count = 0; return 0; }\n"
             "const char* const* volumeAssetNames(int, int* n) { *n = 0; return 0; }\n"
             "const char* const* volumePrefabNames(int, int* n) { *n = 0; return 0; }\n"
             "short* volumeAssetSlots(int) { return 0; }\n"
             "short* volumePrefabSlots(int) { return 0; }\n"
             "}  // namespace procrt\n";
        out.source = s.str();
        return out;
    }
    s << "#include \"procedural.gen.hpp\"\n\n"
         "#include <math.h>\n"
         "#include <tyra>\n\n";
    s << kRuntimePrelude << "\n";

    std::ostringstream maskDefs, gens;
    for (size_t vi = 0; vi < good.size(); ++vi) {
        const Volume& v = *good[vi];
        const SceneData& sc = p.scenes[v.scene];
        const SceneObject& o = sc.objects[v.objectIndex];
        Emitter em{p, sc, o, o.procGraph, v, attrs};
        const ProcNode* on = procgraph::outputNode(o.procGraph);
        if (on) em.emitPoints(*on);
        maskDefs << em.masks.str();
        gens << "// --- " << v.name << " (scene " << v.scene << ") ---\n";
        gens << "static int pgen_" << vi << "(procrt::Ctx& c) {\n";
        gens << em.body.str();
        gens << "  return c.count;\n}\n\n";
    }
    s << maskDefs.str() << gens.str();

    // Asset and prefab slots are resolved BY NAME at scene load, not baked as
    // indices here: the model table is per scene and templates.cpp owns it, so
    // baking indices would mean this file and that one agreeing about a table
    // neither of them fully sees. The game resolves once (procResolveTables)
    // and the generator then reads plain shorts.
    s << "namespace {\n";
    for (size_t vi = 0; vi < good.size(); ++vi) {
        const Volume& v = *good[vi];
        s << "const char* const pAssetNames_" << vi << "[] = {";
        for (size_t i = 0; i < v.assets.size(); ++i)
            s << (i ? ", " : "") << cstr(v.assets[i]);
        if (v.assets.empty()) s << "0";
        s << "};\n";
        s << "const char* const pPrefabNames_" << vi << "[] = {";
        for (size_t i = 0; i < v.prefabs.size(); ++i)
            s << (i ? ", " : "") << cstr(v.prefabs[i]);
        if (v.prefabs.empty()) s << "0";
        s << "};\n";
        s << "short pAssets_" << vi << "[" << std::max<size_t>(1, v.assets.size())
          << "] = {0};\n";
        s << "short pPrefabs_" << vi << "["
          << std::max<size_t>(1, v.prefabs.size()) << "] = {0};\n";
    }
    s << "}  // namespace\n\n";

    s << "namespace procrt {\n\n";
    s << "const char* const* volumeAssetNames(int v, int* count) {\n";
    for (size_t vi = 0; vi < good.size(); ++vi)
        s << "  if (v == " << vi << ") { *count = " << good[vi]->assets.size()
          << "; return pAssetNames_" << vi << "; }\n";
    s << "  *count = 0; return 0;\n}\n\n";
    s << "const char* const* volumePrefabNames(int v, int* count) {\n";
    for (size_t vi = 0; vi < good.size(); ++vi)
        s << "  if (v == " << vi << ") { *count = " << good[vi]->prefabs.size()
          << "; return pPrefabNames_" << vi << "; }\n";
    s << "  *count = 0; return 0;\n}\n\n";
    s << "short* volumeAssetSlots(int v) {\n";
    for (size_t vi = 0; vi < good.size(); ++vi)
        s << "  if (v == " << vi << ") return pAssets_" << vi << ";\n";
    s << "  return 0;\n}\n\n";
    s << "short* volumePrefabSlots(int v) {\n";
    for (size_t vi = 0; vi < good.size(); ++vi)
        s << "  if (v == " << vi << ") return pPrefabs_" << vi << ";\n";
    s << "  return 0;\n}\n\n";

    s << "const VolumeDef VOLUMES[] = {\n";
    for (size_t vi = 0; vi < good.size(); ++vi) {
        const Volume& v = *good[vi];
        const SceneObject& o = p.scenes[v.scene].objects[v.objectIndex];
        const ProcNode* on = procgraph::outputNode(o.procGraph);
        const float cell = on ? std::max(4.0f, procgraph::num(*on, "cell")) : 48.0f;
        const float draw = on ? procgraph::num(*on, "draw") : 0.0f;
        const int collide = on ? procgraph::inum(*on, "collide") : 0;
        const int maxinst = on ? std::max(64, procgraph::inum(*on, "maxinst")) : 4096;
        s << "  { " << v.scene << ", " << v.objectIndex << ", " << cstr(v.name)
          << ", " << fmt(cell) << ", " << fmt(draw) << ", " << collide << ", "
          << (o.procGraph.runAtStart ? 1 : 0) << ", " << o.procGraph.seedMode
          << ", " << (unsigned)o.procGraph.seed << "u, " << maxinst << ", "
          << (v.hasBlocks ? 1 : 0) << ", pAssets_" << vi << ", "
          << v.assets.size() << ", pPrefabs_" << vi << ", " << v.prefabs.size()
          << " },\n";
    }
    s << "};\n\n";

    s << "int generate(int volume, Ctx& c) {\n";
    s << "  c.count = 0;\n";
    s << "  c.blockNx = c.blockNz = 0;\n";
    s << "  switch (volume) {\n";
    for (size_t vi = 0; vi < good.size(); ++vi)
        s << "    case " << vi << ": return pgen_" << vi << "(c);\n";
    s << "    default: return 0;\n  }\n}\n\n";
    s << "}  // namespace procrt\n";

    out.source = s.str();
    return out;
}

}  // namespace procrt
