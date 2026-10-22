"""Restore source only from a verified 497-file base into a new directory."""
from pathlib import Path, PurePosixPath
import argparse
import hashlib
import json
import shutil

ROOT = Path(__file__).resolve().parent


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def relative(name):
    p = PurePosixPath(name)
    if p.is_absolute() or ".." in p.parts or "\\" in name or ":" in name:
        raise ValueError(f"Unsafe manifest path: {name}")
    return Path(*p.parts)


def manifest(name):
    return json.loads((ROOT / "metadata" / name).read_text(encoding="utf-8"))["files"]


def verify(root, files):
    for name, expected in files.items():
        path = root / relative(name)
        if not path.is_file() or path.is_symlink() or digest(path) != expected:
            raise ValueError(f"Missing or mismatched source: {path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--mode", choices=("pricing", "probe"), required=True)
    args = parser.parse_args()
    base = args.base.resolve(strict=True)
    out = args.out.absolute()
    base_files = manifest("base-497-source-manifest.json")
    verify(base, base_files)
    # Exclusive creation prevents overwriting a checkout or previous experiment.
    out.mkdir(parents=True, exist_ok=False)
    for name in base_files:
        target = out / relative(name)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(base / relative(name), target)
    for layer in (["pricing"] if args.mode == "pricing" else ["pricing", "probe"]):
        overlay = ROOT / "postimages" / layer
        for source in sorted(overlay.rglob("*")):
            if source.is_file():
                if source.is_symlink():
                    raise ValueError(f"Symlink in overlay: {source}")
                target = out / relative(source.relative_to(overlay).as_posix())
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source, target)
    target_files = manifest(f"{args.mode}/target-source-manifest.json")
    verify(out, target_files)
    actual = {p.relative_to(out).as_posix() for p in out.rglob("*") if p.is_file()}
    if actual != set(target_files):
        raise ValueError("Unexpected restored source files")
    print(json.dumps({"status": "PASS_SOURCE_ONLY", "mode": args.mode,
                      "files": len(actual), "output": str(out)}, indent=2))


if __name__ == "__main__":
    main()
