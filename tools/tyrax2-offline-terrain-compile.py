"""Factor native terrain oracle streams into base LODs and sparse edge patches."""
import argparse
import hashlib
import itertools
import json
import struct
from pathlib import Path


class Reader:
    def __init__(self, data):
        self.data, self.at = data, 0

    def take(self, size):
        if size < 0 or self.at + size > len(self.data):
            raise ValueError("Truncated terrain input")
        result = self.data[self.at:self.at + size]
        self.at += size
        return result

    def word(self):
        return struct.unpack("<I", self.take(4))[0]


def parse(data):
    reader = Reader(data)
    if (reader.word(), reader.word()) != (0x3158544F, 1):
        raise ValueError("Unknown terrain oracle format")
    scene, nx, nz = [reader.word() for _ in range(3)]
    if scene != 0 or not 0 < nx <= 64 or not 0 < nz <= 64:
        raise ValueError("Unsupported scene or dimensions")
    records, total = {}, 0
    while True:
        tag = reader.word()
        if tag == 0x454E4431:
            if reader.word() != len(records) or reader.word() != total or reader.at != len(data):
                raise ValueError("Bad terrain footer")
            return scene, nx, nz, records
        if tag != 0x52454331:
            raise ValueError("Bad terrain record")
        cx, cz, own, *edges = [reader.word() for _ in range(7)]
        key = (cx, cz, own, *edges)
        if cx >= nx or cz >= nz or own not in (1, 2, 4) or any(e not in (1, 2, 4) or e < own for e in edges) or key in records:
            raise ValueError("Invalid or duplicate terrain key")
        bounds = reader.take(32)
        arrays = []
        for _ in range(6):
            size = reader.word()
            if size % 16 or size > 1024 * 1024:
                raise ValueError("Invalid terrain stream size")
            arrays.append(reader.take(size))
            total += size
        if len(arrays[0]) != len(arrays[1]) or len(arrays[0]) != len(arrays[2]):
            raise ValueError("Mismatched base streams")
        # The original producer leaves gsCols from the previous slot content
        # when no shadow geometry exists. That inactive array is not submitted.
        # Canonicalise only this explicitly inactive stream; retain raw input.
        if not arrays[3]:
            arrays[4] = b""
        if len(arrays[3]) != len(arrays[4]) or len(arrays[3]) != len(arrays[5]):
            raise ValueError("Mismatched shadow streams")
        records[key] = (bounds, arrays)


def compile_data(data):
    scene, nx, nz, records = parse(data)
    output = bytearray()

    def word(value):
        output.extend(struct.pack("<I", value))

    for value in (0x3242544F, 2, scene, nx, nz, nx * nz * 3):
        word(value)
    patched_words = verified = raw_bytes = base_bytes = 0
    for cz in range(nz):
        for cx in range(nx):
            for own in (1, 2, 4):
                base_key = (cx, cz, own, own, own, own, own)
                if base_key not in records:
                    raise ValueError("Missing base LOD")
                base_bounds, base = records[base_key]
                for value in (cx, cz, own):
                    word(value)
                output.extend(base_bounds)
                for stream in base:
                    word(len(stream))
                    output.extend(stream)
                    base_bytes += len(stream)
                patches = {}
                for edge in range(4):
                    for neighbour in (2, 4):
                        edges = [own] * 4
                        edges[edge] = neighbour
                        key = (cx, cz, own, *edges)
                        patch = []
                        target = records.get(key)
                        for stream_index, original in enumerate(base):
                            changed = []
                            if neighbour > own and target:
                                other = target[1][stream_index]
                                if len(original) != len(other):
                                    raise ValueError("Edge changes stream topology")
                                for offset in range(0, len(original), 4):
                                    if original[offset:offset + 4] != other[offset:offset + 4]:
                                        changed.append((offset, other[offset:offset + 4]))
                            word(len(changed))
                            for offset, value in changed:
                                word(offset)
                                output.extend(value)
                            patched_words += len(changed)
                            patch.append(changed)
                        patches[edge, neighbour] = patch

                variants = {key: value for key, value in records.items() if key[:3] == (cx, cz, own)}
                boundary = (cx == 0, cx == nx - 1, cz == 0, cz == nz - 1)
                choices = [[own] + ([] if boundary[e] else [n for n in (2, 4) if n > own]) for e in range(4)]
                expected = {(cx, cz, own, *edges) for edges in itertools.product(*choices)}
                if set(variants) != expected:
                    raise ValueError("Incomplete neighbour coverage")
                word(len(variants))
                for key, (bounds, arrays) in sorted(variants.items()):
                    reconstructed = [bytearray(stream) for stream in base]
                    touched = {}
                    for edge, neighbour in enumerate(key[3:]):
                        if neighbour <= own:
                            continue
                        for index, changes in enumerate(patches[edge, neighbour]):
                            for offset, value in changes:
                                identity = index, offset
                                if identity in touched and touched[identity] != value:
                                    raise ValueError("Conflicting corner patches")
                                touched[identity] = value
                                reconstructed[index][offset:offset + 4] = value
                    if reconstructed != arrays:
                        raise ValueError("Independent edge patches do not reproduce native bytes")
                    for neighbour in key[3:]:
                        word(neighbour)
                    output.extend(bounds)
                    verified += 1
                    raw_bytes += sum(map(len, arrays))
    word(0x454E4432)
    return bytes(output), dict(scene=scene, chunks=nx * nz, lods=nx * nz * 3,
                              exactNativeVariants=verified, rawStreamBytes=raw_bytes,
                              baseStreamBytes=base_bytes, edgePatchWords=patched_words,
                              encodedBytes=len(output), sourceSha256=hashlib.sha256(data).hexdigest(),
                              limits=["Target-produced oracle input, not a production host exporter.",
                                      "Static daylight streams only; no runtime/image/performance qualification."])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("out", type=Path)
    parser.add_argument("--oracle-platform", choices=("emulator", "ps2"), default="emulator")
    args = parser.parse_args()
    data = args.source.read_bytes()
    compiled, report = compile_data(data)
    args.out.mkdir(parents=True, exist_ok=False)
    (args.out / "offline-terrain.bin").write_bytes(compiled)
    controls = {}
    for name, bad in (("truncated", data[:-4]), ("trailing", data + b"x"), ("magic", b"BAD!" + data[4:]), ("version", data[:4] + struct.pack("<I", 99) + data[8:])):
        try:
            compile_data(bad)
        except (ValueError, struct.error):
            controls[name] = True
        else:
            raise AssertionError("Accepted corrupt input: " + name)
    report.update(valid=True, oraclePlatform=args.oracle_platform, negativeControls=controls, outputSha256=hashlib.sha256(compiled).hexdigest())
    (args.out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
