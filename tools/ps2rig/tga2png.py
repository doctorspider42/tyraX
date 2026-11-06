import struct, sys, zlib
src, dst = sys.argv[1], sys.argv[2]
d = open(src, 'rb').read()
w, h = struct.unpack('<HH', d[12:16])
px = d[18:]
rows = []
for y in range(h):
    line = px[(h - 1 - y) * w * 4:(h - y) * w * 4]
    r = bytearray([0])
    for x in range(w):
        b, g, rr, a = line[x * 4:x * 4 + 4]
        r += bytes([rr, g, b])
    rows.append(bytes(r))
def chunk(t, data):
    c = struct.pack('>I', len(data)) + t + data
    return c + struct.pack('>I', zlib.crc32(t + data) & 0xffffffff)
open(dst, 'wb').write(b'\x89PNG\r\n\x1a\n' +
    chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
    chunk(b'IDAT', zlib.compress(b''.join(rows))) + chunk(b'IEND', b''))
