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

bool writeFile(const std::string& dir, const std::string& name,
               const std::string& body) {
    const std::string path = dir + "/" + name;
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
                                   vugen::classTitle(cls), instrs});
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
    } else {
        h += "/** Installs every program the project's src/vu/*.cpp built, over\n";
        h += " * the material classes they claim. Call AFTER setRenderer and\n";
        h += " * setVU1Clipping - both rebuild the resident program cache. */\n";
        h += "void install(Tyra::StaPipCore& core);\n";
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
        c += "}  // namespace\n\n";
        c += "void install(Tyra::StaPipCore& core) {\n";
        c += "  static const Tyra::StaPipProgramName kSlots[] = {\n";
        for (const Emitted& e : emitted)
            c += "      Tyra::" + e.programEnum + ",\n";
        c += "  };\n";
        c += "  Tyra::StaPipVU1Program* progs[] = {\n";
        for (const Emitted& e : emitted) c += "      &g_" + e.className + ",\n";
        c += "  };\n";
        // One call for the whole set: per class it would be a pipeline drain
        // and a program-cache upload EACH.
        c += "  core.setProgramOverrides(kSlots, progs, " +
             std::to_string(emitted.size()) + ");\n";
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
        m += e.script + "	" + e.classTitle + "	" +
             std::to_string(e.instructions) + "	" + e.programEnum + "\n";
    if (!writeFile(out, "vu_scripts.manifest", m)) return 1;

    if (failures) {
        std::fprintf(stderr, "[vugen] %d program(s) failed to build\n", failures);
        return 1;
    }
    std::printf("[vugen] %d microprogram(s) from %d script(s)\n",
                (int)emitted.size(), (int)progs.size());
    return 0;
}
