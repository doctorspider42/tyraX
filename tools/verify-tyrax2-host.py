#!/usr/bin/env python3
"""Compile host-only actual-source arena and generated interleave controls.

Requires a host GCC/Clang C++17 compiler; never builds an editor/game or launches
an emulator/device. Temporary extraction is used instead of copied algorithms.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def extract_interleave(directory):
    templates = (ROOT / "src/templates.cpp").read_text(encoding="utf-8")
    source = (ROOT / "src/game_templates.inc").read_text(encoding="utf-8")
    clock = source[source.index("inline u32 ilObservationTicks()"):]
    clock = clock[:clock.index("\n}") + 2]
    assert clock.count("mfc0") == 1 and ': : "memory"' in clock
    assert "heavyActive = interleaveBegin(costSeq != 0);" in source
    start = source.index("// Settled whole-loop observations")
    end = source.index("void TerrainGame::interleaveEnd()", start)
    methods = source[start:end].replace("{{INTERLEAVE_CAMERA_RIG}}", "mockCameraRig")
    if "{{" in methods:
        raise ValueError("Unexpected unexpanded token in actual selector")
    marker = "  // Settled homogeneous blocks:"
    first = templates.index(marker)
    stop = templates.index("  // `grip`", first)
    second = templates.index(marker, stop)
    second_stop = templates.index("  // `grip`", second)
    fields = templates[first:stop]
    if fields != templates[second:second_stop]:
        raise ValueError("FPP/ORBIT selector declarations diverged")
    (directory / "actual-methods.inc").write_text(methods, encoding="utf-8")
    (directory / "actual-fields.inc").write_text(fields, encoding="utf-8")
    core = (ROOT / "vendor/tyra/engine/src/renderer/core/renderer_core.cpp").read_text(encoding="utf-8")
    begin = core[core.index("void RendererCore::beginFrameRecording()"):]
    begin = begin[:begin.index("\n}") + 2]
    warp = core[core.index("bool RendererCore::presentWarpFrame("):]
    warp = warp[:warp.index("  warp.draw(from, to);") + len("  warp.draw(from, to);")]
    assert begin.count("++recordingGeneration;") == 1
    assert warp.count("++recordingGeneration;") == 1
    assert warp.index("if (!hasPresentedFrame) return false;") < warp.index("++recordingGeneration;")
    assert warp.index("if (settings.isHybridOutput()) return false;") < warp.index("++recordingGeneration;")
    # Compile the actual successful-warp prefix, not a reimplemented selector.
    (directory / "actual-warp.inc").write_text(warp + "\n  return true;\n}\n", encoding="utf-8")



def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default=None, help="Host g++/clang++ executable")
    parser.add_argument("--only", choices=("all", "adaptive", "count", "partial-baked"), default="all")
    parser.add_argument("--submission-candidate", action="store_true",
                        help="Test the rejected submission experiment in temporary sources; requires --only count")
    args = parser.parse_args()
    if args.submission_candidate and args.only != "count":
        parser.error("--submission-candidate requires --only count")
    compiler = args.compiler or shutil.which("g++") or shutil.which("clang++")
    if not compiler:
        parser.error("No host compiler found; pass --compiler /path/to/g++")
    compiler = str(Path(compiler).resolve()) if Path(compiler).exists() else compiler
    environment = dict(os.environ)
    environment["PATH"] = str(Path(compiler).parent) + os.pathsep + environment.get("PATH", "")
    with tempfile.TemporaryDirectory(prefix="tyrax2-host-") as scratch:
        directory = Path(scratch)
        source_root = ROOT
        if args.submission_candidate:
            source_root = directory / "candidate"
            shutil.copytree(ROOT / "vendor/tyra/engine/inc",
                            source_root / "vendor/tyra/engine/inc")
            queue = Path("vendor/tyra/engine/src/renderer/core/paths/path1/vif1_queue.cpp")
            (source_root / queue.parent).mkdir(parents=True)
            shutil.copy2(ROOT / queue, source_root / queue)
            (source_root / "tools").mkdir()
            shutil.copy2(ROOT / "tools/verify-frame-arena.cpp",
                         source_root / "tools/verify-frame-arena.cpp")
            test_source = source_root / "tools/verify-frame-arena.cpp"
            test_source.write_bytes(test_source.read_text(encoding="utf-8").encode("utf-8"))
            for source in source_root.rglob("*"):
                if source.is_file():
                    source.chmod(0o644)
            evidence = ROOT / "docs/tyrax2-full-throttle-2026-10-07/submit-fix"
            for name in ("engine-candidate.patch", "host-candidate.patch"):
                command = ["git", "apply", "-p1", str(evidence / name)]
                subprocess.run(command[:2] + ["--check"] + command[2:], cwd=source_root, check=True)
                subprocess.run(command, cwd=source_root, check=True)
        # HardwareTrace is compiled out; its declarations only need EE typedefs.
        # This is a host type shim, not a stubbed arena or selector algorithm.
        (directory / "tamtypes.h").write_text(
            "#pragma once\n#include <stdint.h>\n"
            "typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;typedef uint64_t u64;\n"
            "typedef int8_t s8;typedef int16_t s16;typedef int32_t s32;typedef int64_t s64;\n",
            encoding="utf-8")
        targets = []
        if args.only in ("all", "adaptive"):
            extract_interleave(directory)
            targets.extend(("verify-interleave.cpp", "verify-warp-generation.cpp"))
        if args.only in ("all", "count"):
            targets.append("verify-frame-arena.cpp")
        if args.only in ("all", "partial-baked"):
            targets.append("verify-stapip-baked-body.cpp")
        if args.only == "all":
            targets.append("verify-frame-writer.cpp")
        for target in targets:
            executable = directory / (Path(target).stem + (".exe" if os.name == "nt" else ""))
            command = [compiler, "-std=c++17", "-O0", "-DTYRA_HARDWARE_TRACE=0",
                       "-I" + str(directory), "-I" + str(source_root / "vendor/tyra/engine/inc"),
                       str(source_root / "tools" / target), "-o", str(executable)]
            subprocess.run(command, check=True, env=environment)
            subprocess.run([str(executable)], check=True, env=environment)
    print("PASS host-only actual-source controls; no target performance claim")


if __name__ == "__main__":
    main()
