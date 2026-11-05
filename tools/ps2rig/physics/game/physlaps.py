import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()

def rep(old, new, count=1):
    global s
    n = s.count(old)
    if n != count:
        raise SystemExit(f"anchor x{n} (want {count}): {old[:60]!r}")
    s = s.replace(old, new)

PRE = (
    "namespace physlap { inline u64 t[6]; inline u64 frames, contacts, worldCand, worldNear, pairCand, steps, awake; }\n"
    "#define PHYS_LAP(k) do { const u64 now_ = Tyra::FrameProfile::ticks(); physlap::t[k] += now_ - lap_; lap_ = now_; } while (0)\n"
)
rep("void TerrainGame::updateObjectPhysics() {\n",
    PRE + "void TerrainGame::updateObjectPhysics() {\n")
rep("  if (!anyAwake) return;  // a settled scene pays one pass over its bodies\n",
    "  if (!anyAwake) return;  // a settled scene pays one pass over its bodies\n"
    "  u64 lap_ = Tyra::FrameProfile::ticks();\n")
rep("  const int awakeCount = (int)steps.size();\n",
    "  const int awakeCount = (int)steps.size();\n  PHYS_LAP(0);\n")
rep("      broadPhaseWorldNear(area, bpNearPhys);\n    }\n",
    "      broadPhaseWorldNear(area, bpNearPhys);\n    }\n    physlap::worldNear += bpNearPhys.size();\n")
rep("      if (dx * dx + dy * dy + dz * dz > rr * rr) continue;\n",
    "      if (dx * dx + dy * dy + dz * dz > rr * rr) continue;\n      ++physlap::worldCand;\n")
rep("  // --- 3. contacts between bodies -------------------------------------------\n",
    "  PHYS_LAP(1);\n  // --- 3. contacts between bodies -------------------------------------------\n")
rep("    bodyPairs.queryBox(pairArea, nearBodies);\n",
    "    bodyPairs.queryBox(pairArea, nearBodies);\n    physlap::pairCand += nearBodies.size();\n")
rep("  // --- 4. sequential impulses -------------------------------------------------\n",
    "  PHYS_LAP(2);\n  // --- 4. sequential impulses -------------------------------------------------\n")
rep("  // --- 5. integrate and write back ------------------------------------------\n",
    "  PHYS_LAP(3);\n  // --- 5. integrate and write back ------------------------------------------\n")
rep("      if (!g.matrixMode) o.dirty = true;\n    }\n  }\n}\n",
    "      if (!g.matrixMode) o.dirty = true;\n    }\n  }\n"
    "  PHYS_LAP(4);\n"
    "  physlap::contacts += nc; physlap::steps += steps.size(); physlap::awake += awakeCount;\n"
    "  if (++physlap::frames == 50) {\n"
    "    const double k = 1.0 / (294912.0 * 50.0);\n"
    "    char line[200];\n"
    "    snprintf(line, sizeof(line), \"PHYSLAP prep=%.3f world=%.3f pairs=%.3f solve=%.3f write=%.3f awake=%.1f steps=%.1f contacts=%.1f worldNear=%.1f worldCand=%.1f pairCand=%.1f\",\n"
    "      physlap::t[0]*k, physlap::t[1]*k, physlap::t[2]*k, physlap::t[3]*k, physlap::t[4]*k,\n"
    "      physlap::awake/50.0, physlap::steps/50.0, physlap::contacts/50.0, physlap::worldNear/50.0, physlap::worldCand/50.0, physlap::pairCand/50.0);\n"
    "    TYRA_LOG(line);\n"
    "    for (auto& v : physlap::t) v = 0;\n"
    "    physlap::frames = physlap::contacts = physlap::worldCand = physlap::worldNear = physlap::pairCand = physlap::steps = physlap::awake = 0;\n"
    "  }\n"
    "}\n")
open(p, "w", encoding="utf-8", newline="\n").write(s)
print("patched")
