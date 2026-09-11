"""Ask the running game for one VU1 packet capture, without the GUI.

The devkit's "Capture VU1 packet" button writes bin/livedbg.cmd; there is no CLI for
it, so this writes the same bytes. Layout from src/livedbg.cpp::encodeCommand:

    magic "TXDC", version 1, seq, flags, stepFrames,
    nBreakpoints, nFire, nWatch, [ids...], seq ^ 0x5A5A5A5A

flags bit 3 (8) = capture a VU1 packet; bit 4 (16) plus bits 8..23 = capture the
flush with this index, which is what makes two captures comparable - "the next
packet" would be a different draw each time.

Usage: armvucap.py <projectDir> <flushIndex> [seq]
"""
import struct, sys, pathlib

MAGIC = 0x43445854
VERSION = 1
FOOTER_XOR = 0x5A5A5A5A

proj = pathlib.Path(sys.argv[1])
flush = int(sys.argv[2])
seq = int(sys.argv[3]) if len(sys.argv) > 3 else 1

flags = 8                                     # captureVu
flags |= 16 | ((flush & 0xFFFF) << 8)         # named flush index

out = struct.pack("<IIII", MAGIC, VERSION, seq, flags)
out += struct.pack("<i", 0)                   # stepFrames
out += struct.pack("<iii", 0, 0, 0)           # no breakpoints / fire / watch ids
out += struct.pack("<I", (seq ^ FOOTER_XOR) & 0xFFFFFFFF)

target = proj / "bin" / "livedbg.cmd"
target.write_bytes(out)
print(f"uzbrojono: {target}  (flush={flush}, seq={seq}, {len(out)} bajtow)")
