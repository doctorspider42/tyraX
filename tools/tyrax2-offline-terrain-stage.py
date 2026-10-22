"""Stage an offline/material trial with its explicitly pinned PS2 bootstrap asset."""
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("fixture", type=Path)
    p.add_argument("attempt", type=Path)
    a = p.parse_args()
    manifest = json.loads((a.fixture / "world-cell-price-manifest.json").read_text())
    assert manifest["variant"] in ("offline-terrain", "material-compose")
    expected = manifest["offlineBinaryByPlatform"]["ps2"]
    asset = a.fixture / "offline-terrain-ps2.bin"
    assert sha(asset) == expected
    assert not a.attempt.exists(), "Use a fresh attempt directory"
    subprocess.run([sys.executable, str(Path(__file__).with_name("tyrax2-motion-run.py")),
                    "stage", str(a.fixture), "--stage-dir", str(a.attempt)], check=True)
    shutil.copy2(asset, a.attempt / "bin/offline-terrain.bin")
    if manifest.get("finalVifOffline"):
        vif=a.fixture / "offline-vif-ps2.bin"
        assert sha(vif)==manifest["vifBinaryByPlatform"]["ps2"]
        shutil.copy2(vif,a.attempt / "bin/offline-vif.bin")
    pins = {str(f.relative_to(a.attempt / "bin")): sha(f) for f in (a.attempt / "bin").rglob("*") if f.is_file()}
    (a.attempt / "launch-inputs.json").write_text(json.dumps(pins, indent=2) + "\n")
    (a.attempt / "offline-platform-input.json").write_text(json.dumps(dict(
        platform="ps2", offlineBinarySha256=expected,
        emulatorBinarySha256=manifest["offlineBinaryByPlatform"]["emulator"],
        sameCrossPlatformAsset=False), indent=2) + "\n")
    print(a.attempt)


if __name__ == "__main__":
    main()
