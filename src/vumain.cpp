// The generator a PROJECT's VU scripts are compiled into (docs/vu-authoring.md).
//
// This file never runs on a PS2 and never runs in the editor. It is compiled -
// inside the build container, by the host compiler, together with the framework
// and with every src/vu/*.cpp the project wrote - into a small program that is
// then EXECUTED. What it leaves behind is ordinary generated source:
//
//   src/gen/vu_script<i>_<class>.vclpp        the microprogram
//   src/gen/vu_script<i>_<class>_program.cpp  its EE-side twin
//   src/gen/vu_scripts.gen.{hpp,cpp}          install() for the game
//
// which the game's Makefile then puts through the same vclpp -> vcl -> dvp-as
// chain as the engine's handwritten programs. A script author never sees this
// file; they see a C++ compiler error with their own line number when they get
// something wrong, which is the entire point of doing it this way.
#include <cstdio>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

#include "vugen.hpp"
#include "vushader.hpp"

namespace {

/** vugen::ScriptFn is a plain function pointer - the emitter calls it while it
 * walks the program, and a capturing lambda cannot be one. So the program under
 * construction is handed over here, set immediately before each build(). */
vu::Program* g_current = nullptr;

void prepareTrampoline(vugen::ScriptCtx& sc) {
    vu::Ctx c(sc);
    g_current->prepare(c);
}

void scriptTrampoline(vugen::ScriptCtx& sc) {
    vu::Ctx c(sc);
    g_current->vertex(c);
}

/** The lowercase tag the generated files carry, matching the engine's own
 * spelling of the material classes. */
const char* classTag(unsigned bit) {
    switch (bit) {
        case 1u << 1: return "d";
        case 1u << 2: return "td";
        case 1u << 3: return "tc";
        case 1u << 4: return "tce";
        default: return "c";
    }
}
const char* classSym(unsigned bit) {
    switch (bit) {
        case 1u << 1: return "D";
        case 1u << 2: return "TD";
        case 1u << 3: return "TC";
        case 1u << 4: return "TCE";
        default: return "C";
    }
}

/** "Cell shading" -> "CellShading", so the header can offer a named constant
 * per script instead of making the game count them. */
std::string symbolName(const std::string& in) {
    std::string out;
    bool up = true;
    for (char ch : in) {
        if (!isalnum((unsigned char)ch)) { up = true; continue; }
        out += up ? (char)toupper((unsigned char)ch) : ch;
        up = false;
    }
    if (out.empty() || isdigit((unsigned char)out[0])) out = "P" + out;
    return out;
}

bool writeFile(const std::string& dir, const std::string& name,
               const std::string& body) {
    const std::string path = dir + "/" + name;
    // DO NOT TOUCH A FILE WHOSE CONTENT IS UNCHANGED. make compares mtimes, so
    // rewriting an identical .vclpp is what makes every single build pay for
    // vcl again - and vcl on these programs is the slowest thing in the whole
    // pipeline. The editor's own project::writeFile has followed this rule for
    // the same reason; this is the generator catching up.
    if (FILE* old = std::fopen(path.c_str(), "rb")) {
        std::fseek(old, 0, SEEK_END);
        const long n = std::ftell(old);
        if (n == (long)body.size()) {
            std::rewind(old);
            std::string prev(body.size(), '\0');
            const size_t got = std::fread(&prev[0], 1, body.size(), old);
            std::fclose(old);
            if (got == body.size() && prev == body) return true;
        } else {
            std::fclose(old);
        }
    }
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        std::fprintf(stderr, "[vugen] cannot write %s\n", path.c_str());
        return false;
    }
    std::fwrite(body.data(), 1, body.size(), f);
    std::fclose(f);
    return true;
}

struct Emitted {
    std::string className;   // the EE-side program class
    std::string header;      // its header file name
    std::string programEnum; // the engine slot it replaces
    std::string script;      // the vu::Program that produced it
    std::string classTitle;  // the material class, spelled the way the UI does
    unsigned classBit = 0;
    size_t scriptIndex = 0;  // which vu::Program, for activate/deactivate
    bool asIs = false;       // the twin that draws what the frustum cut
    bool activeAtBoot = true;
    int instructions = 0;
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: vugen <srcGenDir> <incScriptsDir>\n");
        return 2;
    }
    const std::string out = argv[1];
    // The header goes to inc/scripts/ because that is the only directory on the
    // game's include path, and a user script may include it too.
    const std::string incOut = argv[2];
    const std::vector<vu::Program*>& progs = vu::registeredPrograms();

    std::vector<Emitted> emitted;
    int failures = 0;

    for (size_t pi = 0; pi < progs.size(); ++pi) {
        vu::Program* p = progs[pi];
        for (unsigned cls : vugen::customClasses()) {
            if ((p->classes() & cls) == 0) continue;

            // Both halves of the class's resident PAIR. The cull program draws
            // a package wholly inside the frustum and the as_is one draws what
            // the frustum cut, so replacing only the first leaves the effect
            // stopping at the edge of the screen. The as_is twin has no MVP
            // multiply - its vertices are already transformed - so a script
            // that moves GEOMETRY only gets the cull half, and the docs say so
            // rather than the generator silently tearing the mesh.
            const bool geometry = p->slot() == vugen::Slot::ObjectSpace;
            for (int half = 0; half < (geometry ? 1 : 2); ++half) {
                const bool asIs = half == 1;
                vugen::Desc d = vugen::descForClass(cls, 0, asIs);
                const std::string ix = std::to_string(pi);
                const std::string tag = classTag(cls);
                d.fileStem = "vu_script" + ix + "_" + tag + (asIs ? "_ai" : "");
                d.vclName = "TyraXScript" + ix + classSym(cls) + (asIs ? "AI" : "");
                d.asmName = d.vclName;
                d.className = d.vclName + "VU1Program";
                d.title = std::string(p->name()) + " - " +
                          vugen::classTitle(cls);
                d.script = &scriptTrampoline;
                d.scriptPrepare = &prepareTrampoline;
                d.scriptSlot = p->slot();
                g_current = p;

                const vugen::Built b = vugen::build(d);
                if (!b.errors.empty()) {
                    std::fprintf(stderr, "[vugen] %s on %s: %s\n", p->name(),
                                 vugen::classTitle(cls), b.errors[0].c_str());
                    ++failures;
                    continue;
                }
                if (!writeFile(out, d.fileStem + ".vclpp", b.vclpp) ||
                    !writeFile(out, d.fileStem + "_program.hpp", b.eeHeader) ||
                    !writeFile(out, d.fileStem + "_program.cpp", b.eeSource))
                    return 1;
                // The slot name comes from the DESCRIPTION, never spelled
                // again here: a class renamed upstream cannot drift out of step
                // with the override it installs.
                int instrs = 0;
                for (const vuir::Instr& in : b.program.code)
                    if (in.op != vuir::Op::Label && in.op != vuir::Op::Barrier &&
                        in.op != vuir::Op::Cont && in.op != vuir::Op::Nop)
                        ++instrs;
                emitted.push_back({d.className, d.fileStem + "_program.hpp",
                                   d.programEnum, p->name(),
                                   vugen::classTitle(cls), cls, pi, asIs,
                                   p->activeAtBoot(), instrs});
                std::printf("[vugen] %s -> %s (%d instructions)\n", p->name(),
                            d.fileStem.c_str(), (int)b.program.code.size());
            }
        }
    }

    // The runtime glue. Emitted even with no programs at all: the game includes
    // this header unconditionally, and a project that deletes its last script
    // has to keep compiling.
    std::string h;
    h += "// Generated by TyraX (the project's own VU scripts). Do not edit.\n";
    h += "#pragma once\n\n#include <tyra>\n\nnamespace vuscript {\n\n";
    h += std::string("constexpr bool ENABLED = ") +
         (emitted.empty() ? "false" : "true") + ";\n\n";
    if (emitted.empty()) {
        h += "inline void install(Tyra::StaPipCore&) {}\n";
        h += "inline void activate(int) {}\n";
        h += "inline void deactivate(int) {}\n";
        h += "inline void deactivateAll() {}\n";
        h += "inline bool active(int) { return false; }\n";
        h += "constexpr int COUNT = 0;\n";
    } else {
        // One index per SCRIPT, not per emitted microprogram: a script claiming
        // four classes is one thing the game turns on and off.
        h += "// The scripts, in the order the generator found them.\n";
        for (size_t i = 0; i < progs.size(); ++i)
            h += "constexpr int k" + symbolName(progs[i]->name()) + " = " +
                 std::to_string(i) + ";\n";
        h += std::string("constexpr int COUNT = ") +
             std::to_string(progs.size()) + ";\n\n";
        h += "/** Installs the scripts marked activeAtBoot(), over the material\n";
        h += " * classes they claim. Call AFTER setRenderer and setVU1Clipping -\n";
        h += " * both rebuild the resident program cache, so an override\n";
        h += " * installed first would be rebuilt away. */\n";
        h += "void install(Tyra::StaPipCore& core);\n\n";
        h += "/** Put a script on VU1, or take it off and give the class back to\n";
        h += " * the engine's own program. One pipeline drain and one upload, so\n";
        h += " * this belongs on an EVENT - a trigger, a cutscene beat, entering\n";
        h += " * a room - and not in a per-frame update. Only what is active\n";
        h += " * occupies micro memory, which is what lets a game carry more\n";
        h += " * programs than fit at once. */\n";
        h += "void activate(int script);\n";
        h += "void deactivate(int script);\n";
        h += "void deactivateAll();\n";
        h += "/** Both halves of a LOOK in one drain and one upload. A look\n";
        h += " * built from several programs - a shading pass and the outline\n";
        h += " * that finishes it - is switched as a unit, and toggling them\n";
        h += " * one at a time would pay the pipeline for each. */\n";
        h += "void activateAll();\n";
        h += "bool active(int script);\n";
        h += "const char* name(int script);\n\n";
        h += "/** Whether an ACTIVE script asked the game for a shell pass -\n";
        h += " * a second submission of each object, grown by the program\n";
        h += " * itself (outlines, fur, anything built from a grown copy).\n";
        h += " * The game draws it; the program only moves the vertices.\n";
        h += " * shellWidth() is in screen units at one metre, scaled by\n";
        h += " * distance so the result keeps its thickness across a scene. */\n";
        h += "bool shellActive();\n";
        h += "float shellWidth();\n";
    }
    h += "\n}  // namespace vuscript\n";
    if (!writeFile(incOut, "vu_scripts.gen.hpp", h)) return 1;

    std::string c;
    c += "// Generated by TyraX (the project's own VU scripts). Do not edit.\n\n";
    if (!emitted.empty()) {
        c += "#include \"scripts/vu_scripts.gen.hpp\"\n\n";
        for (const Emitted& e : emitted) c += "#include \"" + e.header + "\"\n";
        c += "\nnamespace vuscript {\nnamespace {\n";
        for (const Emitted& e : emitted)
            c += "Tyra::" + e.className + " g_" + e.className + ";\n";
        c += "Tyra::StaPipCore* g_core = nullptr;\n";
        c += std::string("bool g_on[COUNT] = {") ;
        for (size_t i = 0; i < progs.size(); ++i)
            c += std::string(i ? ", " : "") + "false";
        c += "};\n";
        c += "const char* const kNames[] = {";
        for (size_t i = 0; i < progs.size(); ++i)
            c += std::string(i ? ", " : "") + "\"" + progs[i]->name() + "\"";
        c += "};\n";
        c += "const bool kShell[] = {";
        for (size_t i = 0; i < progs.size(); ++i)
            c += std::string(i ? ", " : "") +
                 (progs[i]->shellPass() ? "true" : "false");
        c += "};\n";
        c += "const float kShellW[] = {";
        for (size_t i = 0; i < progs.size(); ++i) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.9gF", progs[i]->shellWidth());
            c += std::string(i ? ", " : "") + buf;
        }
        c += "};\n";
        c += "}  // namespace\n\n";
        c += "bool shellActive() {\n";
        c += "  for (int i = 0; i < COUNT; ++i)\n";
        c += "    if (g_on[i] && kShell[i]) return true;\n";
        c += "  return false;\n}\n\n";
        c += "float shellWidth() {\n";
        c += "  for (int i = 0; i < COUNT; ++i)\n";
        c += "    if (g_on[i] && kShell[i]) return kShellW[i];\n";
        c += "  return 0.0F;\n}\n\n";
        c += "const char* name(int s) {\n";
        c += "  return (s >= 0 && s < COUNT) ? kNames[s] : \"\";\n}\n\n";
        c += "bool active(int s) {\n";
        c += "  return s >= 0 && s < COUNT && g_on[s];\n}\n\n";
        // The whole set goes in ONE call. Per class it would be a pipeline
        // drain and a program-cache upload each, and this runs while a game is
        // on screen.
        c += "static void apply() {\n";
        c += "  if (!g_core) return;\n";
        c += "  Tyra::StaPipProgramName slots[" +
             std::to_string(emitted.size()) + "];\n";
        c += "  Tyra::StaPipVU1Program* progs[" +
             std::to_string(emitted.size()) + "];\n";
        c += "  unsigned n = 0;\n";
        for (const Emitted& e : emitted) {
            c += "  slots[n] = Tyra::" + e.programEnum + ";\n";
            c += "  progs[n++] = g_on[" + std::to_string(e.scriptIndex) +
                 "] ? &g_" + e.className + " : nullptr;\n";
        }
        c += "  g_core->setProgramOverrides(slots, progs, n);\n";
        c += "}\n\n";
        c += "void activate(int s) {\n";
        c += "  if (s < 0 || s >= COUNT || g_on[s]) return;\n";
        c += "  g_on[s] = true;\n  apply();\n}\n\n";
        c += "void deactivate(int s) {\n";
        c += "  if (s < 0 || s >= COUNT || !g_on[s]) return;\n";
        c += "  g_on[s] = false;\n  apply();\n}\n\n";
        c += "void deactivateAll() {\n";
        c += "  bool any = false;\n";
        c += "  for (int i = 0; i < COUNT; ++i) { any |= g_on[i]; g_on[i] = false; }\n";
        c += "  if (any) apply();\n}\n\n";
        c += "void activateAll() {\n";
        c += "  bool any = false;\n";
        c += "  for (int i = 0; i < COUNT; ++i) { any |= !g_on[i]; g_on[i] = true; }\n";
        c += "  if (any) apply();\n}\n\n";
        c += "void install(Tyra::StaPipCore& core) {\n";
        c += "  g_core = &core;\n";
        for (size_t i = 0; i < progs.size(); ++i)
            if (progs[i]->activeAtBoot())
                c += "  g_on[" + std::to_string(i) + "] = true;\n";
        c += "  apply();\n";
        c += "  core.setVuCustomEnabled(true);\n";
        c += "}\n\n}  // namespace vuscript\n";
    }
    if (!writeFile(out, "vu_scripts.gen.cpp", c)) return 1;

    // What the EDITOR cannot know. The panel's micro-memory budget is computed
    // on the host, and the host has no way to compile a project's C++ - so
    // without this the scripts are simply absent from the one screen that
    // answers "does it fit?". A line per emitted program, written where the
    // panel can read it after a build.
    std::string m;
    for (const Emitted& e : emitted)
        m += e.script + "\t" + e.classTitle + "\t" +
             std::to_string(e.instructions) + "\t" + e.programEnum + "\t" +
             std::to_string(e.classBit) + "\t" + (e.asIs ? "twin" : "main") +
             "\t" + (e.activeAtBoot ? "boot" : "manual") + "\n";
    if (!writeFile(out, "vu_scripts.manifest", m)) return 1;

    if (failures) {
        std::fprintf(stderr, "[vugen] %d program(s) failed to build\n", failures);
        return 1;
    }
    std::printf("[vugen] %d microprogram(s) from %d script(s)\n",
                (int)emitted.size(), (int)progs.size());
    return 0;
}
