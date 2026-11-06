#!/usr/bin/env python3
"""Compare two rig TGA shots. usage: imgdiff.py a.tga b.tga [out_diff.png]
Prints differing pixel count, max/mean abs channel delta; optional amplified diff PNG."""
import struct, sys, zlib
def load(p):
    d = open(p, 'rb').read()
    w, h = struct.unpack('<HH', d[12:16])
    return w, h, d[18:]
wa, ha, a = load(sys.argv[1]); wb, hb, b = load(sys.argv[2])
assert (wa, ha) == (wb, hb)
n = 0; mx = 0; tot = 0
out = bytearray()
for y in range(ha):
    out += b'\0'
    row = (ha - 1 - y) * wa * 4
    for x in range(wa):
        i = row + x * 4
        d = max(abs(a[i] - b[i]), abs(a[i+1] - b[i+1]), abs(a[i+2] - b[i+2]))
        if d: n += 1
        mx = max(mx, d); tot += d
        v = min(255, d * 8)
        out += bytes([v, v, v])
print(f'pixels differing {n} of {wa*ha}  max {mx}  mean {tot/(wa*ha):.3f}')
if len(sys.argv) > 3:
    def chunk(t, data):
        c = struct.pack('>I', len(data)) + t + data
        return c + struct.pack('>I', zlib.crc32(t + data) & 0xffffffff)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', wa, ha, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(bytes(out), 9)) + chunk(b'IEND', b'')
    open(sys.argv[3], 'wb').write(png)
