"""Seal completed offline-world protocols without treating quality reduction as equivalence."""
import argparse
import hashlib
import json
from pathlib import Path


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("out", type=Path)
    p.add_argument("terrain", type=Path)
    p.add_argument("material", type=Path)
    p.add_argument("combined", type=Path)
    p.add_argument("emulator_compiled", type=Path)
    p.add_argument("ps2_compiled", type=Path)
    a = p.parse_args()
    report = dict(productionPromoted=False, editorExporterComplete=False,
                  date="2026-10-20", trials={}, compiled={},
                  limits=["Private controlled fixed-step daytime fixture, not full current-production or night qualification.",
                          "Common table/storage/quality-selector footprint is not priced against a pristine build.",
                          "Bootstrap inputs come from the native producer; this is not a completed host-side colour exporter.",
                          "Emulator and PS2 use distinct pinned colour input bytes; cross-platform input identity is not claimed.",
                          "Material and combined timings deliberately change appearance; visual acceptance remains open.",
                          "Do not sum gains measured in different ELFs."])
    for name, folder in (("emulator", a.emulator_compiled), ("ps2", a.ps2_compiled)):
        result = json.loads((folder / "report.json").read_text())
        assert result["valid"] and sha(folder / "offline-terrain.bin") == result["outputSha256"]
        report["compiled"][name] = dict(result=result, reportSha256=sha(folder / "report.json"))
    for name, folder in (("terrain", a.terrain), ("material", a.material), ("combined", a.combined)):
        binding = json.loads((folder / "native-binding.json").read_text())
        manifest = json.loads((folder / "world-cell-price-manifest.json").read_text())
        assert sha(folder / "game/bin/vehicle-playground.elf") == binding["elfSha256"]
        for relative, digest in binding["files"].items():
            assert sha(folder / relative) == digest, relative
        results = {}
        for platform, attempt in (("emulator", folder), ("ps2", folder / "physical-v1")):
            path = attempt / ("world-cell-price-" + platform + ".json")
            result = json.loads(path.read_text())
            assert result["valid"] and result["elfSha256"] == binding["elfSha256"]
            log = folder / "emulator/emulator.log" if platform == "emulator" else attempt / "console.log"
            assert sha(log) == result["logSha256"], "Logs changed after validation: " + str(log)
            assert result["poseEqual"] and result["deliveryValid"]
            assert result["physicalPrice"] == (platform == "ps2")
            assert result.get("qualityTrial", False) == (name != "terrain")
            if name == "terrain":
                assert result["offlineTerrainEndpointPixelsExact"] and result["offlineTerrainCaptureWorkEqual"]
            else:
                assert result["equivalentOutput"] is False and result["qualityAccepted"] is False
                assert manifest["qualityEquivalent"] is False
            if platform == "ps2":
                deploy = json.loads((attempt / "editor-deploy.json").read_text())
                assert deploy["elfSha256"] == binding["elfSha256"]
                for relative, digest in json.loads((attempt / "launch-inputs.json").read_text()).items():
                    assert sha(attempt / "bin" / relative) == digest
            results[platform] = dict(report=result, reportSha256=sha(path), logPath=str(log))
        report["trials"][name] = dict(fixture=str(folder), nativeBindingSha256=sha(folder / "native-binding.json"),
                                     manifestSha256=sha(folder / "world-cell-price-manifest.json"),
                                     elfSha256=binding["elfSha256"], nativeSourceFiles=len(binding["files"]), results=results)
    a.out.parent.mkdir(parents=True, exist_ok=True)
    assert not a.out.exists(), "Do not overwrite a sealed report"
    a.out.write_text(json.dumps(report, indent=2) + "\n")
    print(a.out)


if __name__ == "__main__":
    main()
