"""Generate the Big City example: a procedural 1 km city for TyraX (Python 3, stdlib only).

    python authoring/generate-city.py [--seed N] [--editor PATH] [--bootstrap]

Run from any directory. It rewrites, deterministically from the seed:

  * objects/*.json for every object of the `main` scene (the old ones are deleted),
  * the `main` scene's object list, layers and terrain size in big-city.tyra,
  * the handful of project settings listed in apply_settings() (fog, view
    distance, batching, ...). Every other manifest key - vehicles, input, menus,
    fonts, the editor layout - is left exactly as it was.
  * a few small generated assets: res/textures/city-*.png, res/materials/city-*.mtl
    and the stacked-tower models res/models/urban/city-highrise-*.obj.

--bootstrap additionally copies the shared models, textures, sounds and the three
vehicle definitions from ../vehicle-playground (run once when the project is
scaffolded; the result is checked in, so you normally never need it).

--editor PATH (the tyrax-editor executable) additionally regenerates the extra road
textures with `--road-texture` and normalises every written file with `--resave`,
so the files on disk are byte-for-byte what the editor itself would save.

Same seed + same knobs = the same city, file for file. Change a knob, re-run,
rebuild (`tyrax-editor --build examples/big-city`).

KNOBS - everything below the "Knobs" banner is meant to be edited:

  SEED              the city. Different seed = different jitter, lots, parks, cars.
  GRID_LINES        grid lines per axis (avenues + streets). 14 -> 13x13 blocks, a
                    1.4 km city; it fits in the EE's 32 MB only with ROAD_STREAM and
                    STREAM_LAYERS on (10 + RING_RADIUS 500 + TERRAIN_SIZE 1340 is the
                    1 km city that fits without them) - see the README.
  BLOCK             grid spacing in world units (1 unit = 1 m in this project).
  RING_RADIUS       the ring road, a superellipse |x|^4+|z|^4=R^4 round the grid.
  TERRAIN_SIZE      the square terrain (world bounds); ring, railway and spurs fit in it.
  LOCAL_STREET_P    chance a block gets a local street (a T at each end).
  DEAD_END_P        share of those local streets that stop half way (a single T).
  PARK_COUNT        blocks turned into parks (trees, benches, a closed cobble path).
  DOWNTOWN_RADIUS   inside it buildings are tall towers; outside they taper down.
  CORE_RADIUS       the 'core' roads (grid lines this close to the centre, the tram
                    diagonal, downtown lanes) get kerbs, details and furniture.
  KERBS, PAVEMENT_*, TRAM_TRACKS, RAILWAY, RAIL_*, ROAD_DETAILS, BRIDGES
                    the road features (docs/roads.md). road_object() is the ONE
                    place a road's fields are written.
  *_DRAW            per-object draw distances: the main "LOD" of this project.
  FOG_START/END     GS distance fog; keep FOG_END a little under the longest draw
                    distance so things fade out instead of popping.
  VIEW_DISTANCE     terrain chunk streaming ring.
  ROAD_STREAM       road streaming radius (docs/roads.md "Road streaming"): only the
                    road geometry this close to the camera is built. None = every road
                    resident (the whole network costs ~6 MB of EE RAM at 1 km).
  LOWPOLY_PROPS     14-triangle generated trees/lamps (True) or the Kenney ones.
  FURNITURE_CORE_ONLY  street trees, lamps and parked cars only on the core roads.
  STREAM_LAYERS     True = buildings/trees/props go into auto-streamed district
                    layers (layered objects are not batched). DISTRICT_SIZE is the
                    square a layer covers, STREAM_MARGIN how far beyond its centre's
                    half-diagonal the zone reaches (keep it near the draw distances).
  STATIC_BATCHING, OCCLUSION, TREE_MESH_LOD   project rendering switches.
  BUILDINGS, TREES, LAMPS, PARKED_CARS, WRITE_ROADS   content switches for bisecting.
  POSE              (x, z, heading, pitch, eye): a frozen walker instead of the car,
                    for repeatable --profile-frame captures.

Any knob can be overridden for one run: --set GRID_LINES=12 --set KERBS=False.
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import random
import shutil
import struct
import subprocess
import zlib

# ----------------------------------------------------------------------------
# Knobs
# ----------------------------------------------------------------------------
SEED = 2026
GRID_LINES = 14
BLOCK = 92.0
LINE_JITTER = 6.0          # +- offset of each grid line
LINE_WOBBLE = 2.5          # sine wobble amplitude of the minor streets
RING_RADIUS = 700.0
TERRAIN_SIZE = 1740
TERRAIN_DETAIL = 67        # cells per axis: the city is flat, 20-unit cells are plenty
LOCAL_STREET_P = 0.55
DEAD_END_P = 0.3
PARK_COUNT = 7
DOWNTOWN_RADIUS = 230.0
MIDTOWN_RADIUS = 420.0

AVENUE_WIDTH = 14.0        # every second grid line, road-4lane
STREET_WIDTH = 10.0        # the others, road-2lane
LOCAL_WIDTH = 8.0          # mid-block lanes
RING_WIDTH = 16.0          # road-6lane
DIAGONAL_WIDTH = 14.0
BOULEVARD_WIDTH = 12.0
SPUR_WIDTH = 10.0
SAMPLE_STEP = 2.0          # flat city: 2-unit stations everywhere

KERBS = True
KERB_HEIGHT = 0.15
KERB_WIDTH = 0.25
ZEBRAS = True              # zebra crossings on downtown roads
KERB_KINDS = ('avenue', 'street', 'local', 'ring', 'diagonal', 'boulevard', 'spur')
KERBS_DOWNTOWN_ONLY = False  # True = kerbs, pavements and details only on the 'core' roads:
CORE_RADIUS = 100.0        # grid lines this close to the centre, the tram diagonal and
                           # the downtown lanes. Every vertex is EE RAM - see the README.
                           # False since the road tables moved to a file (format 104):
                           # kerbs everywhere cost ~2.5 MB, see the README.
MARKINGS_OUTSIDE_DOWNTOWN = False  # node paint (stop/edge lines) outside downtown
LOWPOLY_PROPS = True       # 20-30-triangle generated trees and lamps instead of
                           # the 152/192-triangle Kenney ones (RAM, see README)

# Road features (docs/roads.md). Each is written only when its knob is set.
PAVEMENT_WIDTH = 2.5       # raised pavement behind the kerb (needs the kerb), None = off
PAVEMENT_MATERIAL = 'res/materials/roads/pavement-slabs.mtl'
PAVEMENT_KINDS = ('diagonal', 'local')  # of the kerbed roads; all kinds: +6-7 MB, 32 FPS downtown
PAVEMENT_BOXES = True      # grey plinth under each block's buildings (inside the pavement)
TRAM_TRACKS = 2            # tram tracks down the Diagonal boulevard (roadKind 2), 0 = none
RAILWAY = True             # a double-track railway loop outside the ring (roadKind 1)
RAIL_GAP = 50.0            # its distance outside the ring road's centre line
RAIL_ARC = (-40.0, 130.0)  # degrees round the city (0 = east, 90 = south); (0, 360) = a loop
ROAD_DETAILS = 0.6         # manholes/gullies/patches density on asphalt, None = off
BRIDGES = True             # Spur 1 flies over the railway instead of a level crossing
BRIDGE_HEIGHT = 6.0        # deck height over the rails

BUILDING_DRAW = 230.0
TOWER_DRAW = 300.0         # downtown towers stay up longer (skyline)
TREE_DRAW = 110.0
LAMP_DRAW = 70.0
PROP_DRAW = 55.0
CAR_DRAW = 80.0
PLINTH_DRAW = 140.0
FOG_START = 90.0
FOG_END = 220.0
VIEW_DISTANCE = 240.0
ROAD_STREAM = 260.0         # None = every road resident
TERRAIN_LOD = 110.0

TREE_SPACING = 20.0
LAMP_SPACING = 40.0
SIDEWALK = 3.5            # building setback from the kerb (trees, lamps)
PARKED_CAR_P = 0.18        # chance per 9-unit parking slot on streets/locals
STREAM_LAYERS = True        # districts stream by distance (docs/streaming-layers.md)
DISTRICT_SIZE = 200.0
STREAM_MARGIN = 150.0       # zone radius = 0.71 x DISTRICT_SIZE + this
TREE_MESH_LOD = 0.0
OCCLUSION = False
FURNITURE_CORE_ONLY = True # trees/lamps/parked cars only along the core roads (RAM)
STATIC_BATCHING = True     # merged bags: fewer submits, but a second copy of the geometry
START_IN_CAR = True
POSE = None                # (x, z, heading, pitch, eye): frozen walker for measuring
BUILDINGS = True          # content switches, for bisecting a limit
TREES = True
LAMPS = True
PARKED_CARS = True
WRITE_ROADS = True

# ----------------------------------------------------------------------------
ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / 'big-city.tyra'
OBJECTS = ROOT / 'objects'
SOURCE = ROOT.parent / 'vehicle-playground'

ROAD_TEX = {
    'avenue': 'res/materials/roads/road-4lane.mtl',
    'street': 'res/materials/roads/road-2lane.mtl',
    'local': 'res/materials/roads/road-2lane.mtl',
    'ring': 'res/materials/roads/road-6lane.mtl',
    'diagonal': 'res/materials/roads/road-4lane.mtl',
    'boulevard': 'res/materials/roads/road-4lane.mtl',
    'spur': 'res/materials/roads/road-2lane.mtl',
    'park': 'res/materials/roads/road-cobble.mtl',
    'rail': 'res/materials/roads/rail-ballast-double.mtl',
}
RAIL_JUNCTION_MAT = 'res/materials/roads/rail-junction.mtl'
JUNCTION_MAT = 'res/materials/roads/road-junction.mtl'


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding='utf-8', newline='\n')


# --- tiny PNG writer (no Pillow) -------------------------------------------
def png(path, w, h, pixel):
    rows = b''.join(b'\0' + bytes(c for x in range(w) for c in pixel(x, y))
                    for y in range(h))

    def chunk(tag, data):
        return (struct.pack('>I', len(data)) + tag + data +
                struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff))
    data = (b'\x89PNG\r\n\x1a\n' +
            chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b''))
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_bytes() != data:
        path.write_bytes(data)


def hash01(*k):
    h = hashlib.sha1(repr(k).encode()).digest()
    return int.from_bytes(h[:4], 'little') / 2**32


def make_assets():
    # Grass ground, 64 px, tiled by the terrain material.
    png(ROOT / 'res/textures/city-ground.png', 64, 64, lambda x, y: (
        int(70 + 18 * hash01('g', x, y)), int(92 + 20 * hash01('g2', x, y)),
        int(48 + 10 * hash01('g3', x, y))))
    write(ROOT / 'res/materials/city-ground.mtl',
          'newmtl city-ground\nKd 1 1 1\nmap_Kd -s 0.25 0.25 1 ../textures/city-ground.png\n')
    # Stacked towers: N copies of the lean tower's 16-unit facade band (so the
    # windows keep their proportions) under a flat roof - same atlas, same
    # material as district-tower, so they batch with every other building.
    wall_uv = [(0.007812, 0.210938), (0.382812, 0.210938),
               (0.382812, 0.992188), (0.007812, 0.992188)]
    roof_uv = [(0.398438, 0.039062), (0.523438, 0.039062),
               (0.523438, 0.164062), (0.398438, 0.164062)]
    for bands in (2, 3, 4, 5):
        out = ['# Stacked lean tower, %d facade bands - generated by authoring/generate-city.py' % bands,
               'mtllib district-buildings.mtl', 'usemtl district_facade']
        n = 0

        def quad(corners, uvs):
            nonlocal n
            for (x, y, z), (u, v) in zip(corners, uvs):
                out.append('v %.6f %.6f %.6f' % (x, y, z))
                out.append('vt %.6f %.6f' % (u, v))
            out.append('f %d/%d %d/%d %d/%d' % (n+1, n+1, n+2, n+2, n+3, n+3))
            out.append('f %d/%d %d/%d %d/%d' % (n+1, n+1, n+3, n+3, n+4, n+4))
            n += 4
        sides = [((-4, -4), (-4, 4)), ((-4, 4), (4, 4)), ((4, 4), (4, -4)), ((4, -4), (-4, -4))]
        for b in range(bands):
            y0, y1 = 16.0 * b, 16.0 * (b + 1)
            for (ax, az), (bx, bz) in sides:
                quad([(ax, y0, az), (bx, y0, bz), (bx, y1, bz), (ax, y1, az)], wall_uv)
        top = 16.0 * bands
        quad([(-4, top, -4), (4, top, -4), (4, top, 4), (-4, top, 4)], roof_uv)
        write(ROOT / ('res/models/urban/city-highrise-%d.obj' % bands), '\n'.join(out) + '\n')
    # A parked car: three untextured boxes (body, cabin, tyres) - 36 triangles.
    # A .glb model object loads as an animated DynamicMesh PER INSTANCE, and
    # 143 parked far-model .glb cars hung the boot (see the README's limits).
    write(ROOT / 'res/models/urban/city-car.mtl',
          'newmtl car_paint\nKd 1 1 1\n\nnewmtl car_glass\nKd 0.16 0.2 0.24\n\n'
          'newmtl car_tyre\nKd 0.06 0.06 0.06\n')
    out = ['# Parked car - generated by authoring/generate-city.py', 'mtllib city-car.mtl']
    n = 0

    def box(mat, x0, y0, z0, x1, y1, z1):
        nonlocal n
        out.append('usemtl ' + mat)
        for x, y, z in ((x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
                        (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)):
            out.append('v %.3f %.3f %.3f' % (x, y, z))
        for a, b, c, d in ((1, 2, 3, 4), (6, 5, 8, 7), (5, 1, 4, 8), (2, 6, 7, 3),
                           (4, 3, 7, 8), (5, 6, 2, 1)):
            out.append('f %d %d %d' % (n + a, n + b, n + c))
            out.append('f %d %d %d' % (n + a, n + c, n + d))
        n += 8
    box('car_tyre', -0.85, 0.0, -1.45, 0.85, 0.42, 1.45)
    box('car_paint', -0.9, 0.3, -2.1, 0.9, 0.95, 2.1)
    box('car_glass', -0.75, 0.95, -0.9, 0.75, 1.42, 1.1)
    write(ROOT / 'res/models/urban/city-car.obj', '\n'.join(out) + '\n')
    # Low-poly street trees and lamp (untextured, so no VRAM and one batch key).
    write(ROOT / 'res/models/urban/city-props.mtl',
          'newmtl prop_bark\nKd 0.36 0.25 0.16\n\nnewmtl prop_leaf\nKd 0.24 0.45 0.2\n\n'
          'newmtl prop_pine\nKd 0.16 0.34 0.2\n\nnewmtl prop_metal\nKd 0.32 0.33 0.35\n\n'
          'newmtl prop_lamp\nKd 1 0.93 0.7\n')

    def solid(name, parts):
        """parts: (material, ring list [(y, radius)], sides, apex_top, apex_bottom)."""
        lines = ['# %s - generated by authoring/generate-city.py' % name, 'mtllib city-props.mtl']
        n = 0
        for mat, rings, sides, offset in parts:
            lines.append('usemtl ' + mat)
            ox, oz = offset
            first = n
            for y, r in rings:
                for k in range(sides):
                    a = 2 * math.pi * (k + 0.5) / sides
                    lines.append('v %.3f %.3f %.3f' % (ox + r * math.cos(a), y, oz + r * math.sin(a)))
                    n += 1
            for ring in range(len(rings) - 1):
                for k in range(sides):
                    a = first + ring * sides + k + 1
                    b = first + ring * sides + (k + 1) % sides + 1
                    c, d = a + sides, b + sides
                    ra, rb = rings[ring][1], rings[ring + 1][1]
                    if ra > 1e-4:
                        lines.append('f %d %d %d' % (a, b, d))
                    if rb > 1e-4:
                        lines.append('f %d %d %d' % (a, d, c))
        write(ROOT / ('res/models/urban/%s.obj' % name), '\n'.join(lines) + '\n')
    # Every instanced vertex costs EE RAM (~70-110 bytes each, see the README),
    # so these are as cheap as a silhouette allows: 14 triangles apiece.
    # Broadleaf: a 3-sided trunk under a 4-sided bipyramid canopy, 7 units tall.
    solid('city-tree', [('prop_bark', [(0, 0.22), (2.6, 0.16)], 3, (0, 0)),
                        ('prop_leaf', [(2.0, 0.0), (4.0, 2.3), (7.2, 0.0)], 4, (0, 0))])
    # Pine: a 3-sided trunk under a tall 4-sided cone.
    solid('city-pine', [('prop_bark', [(0, 0.2), (1.8, 0.15)], 3, (0, 0)),
                        ('prop_pine', [(1.4, 0.0), (1.8, 2.0), (7.6, 0.0)], 4, (0, 0))])
    # Street lamp: a 3-sided pole and a lamp head leaning out over +x.
    solid('city-lamp', [('prop_metal', [(0, 0.09), (6.0, 0.06)], 3, (0, 0)),
                        ('prop_lamp', [(5.55, 0.0), (5.85, 0.42), (6.1, 0.0)], 4, (0.45, 0))])


def bootstrap():
    """Copy the shared assets and vehicle definitions from vehicle-playground."""
    files = ['res/models/urban/' + f for f in (
        'LICENSE.txt', 'district-buildings.mtl', 'district-loft.obj', 'district-tower.obj',
        'district-workshop.obj', 'tree-park-large.obj', 'tree-park-large.mtl',
        'tree-park-pine-large.obj', 'tree-park-pine-large.mtl', 'detail-light-double.obj',
        'detail-light-double.mtl', 'detail-bench.obj', 'detail-bench.mtl')]
    files += ['res/models/urban/Textures/' + f for f in (
        'district-facades.png', 'treeA.png', 'treeB.png', 'tiles.png', 'dirt.png',
        'metal.png', 'planks.png')]
    files += ['res/models/' + f for f in ('ravager.glb', 'ravager-far.glb')]
    files += ['res/textures/ravager-paint-mask.png']
    files += ['res/sfx/' + f for f in ('engine_idle.wav', 'engine_high.wav',
                                       'tires_screech.wav', 'gear-shift.wav')]
    files += ['res/materials/particles/' + f for f in (
        'rally-dust.mtl', 'rally-dust.png', 'rally-dust-f1.mtl', 'rally-dust-f1.png')]
    for f in files:
        (ROOT / f).parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(SOURCE / f, ROOT / f)
    src = json.loads((SOURCE / 'vehicle-playground.tyra').read_text(encoding='utf-8'))
    p = json.loads(MANIFEST.read_text(encoding='utf-8'))
    p['vehicles'] = [v for v in src['vehicles'] if v['name'] == 'Ravager']
    p['vehicleDefaults'] = src['vehicleDefaults']
    p['particleEffects'] = src['particleEffects']
    p['sounds'] = src['sounds']
    p['textureQuality'] = {k: v for k, v in src['textureQuality'].items()
                           if 'pica.glb' not in k and 'strix.glb' not in k}
    write(MANIFEST, json.dumps(p, indent=2) + '\n')
    shutil.copyfile(SOURCE / 'THIRD-PARTY-NOTICES.txt', ROOT / 'THIRD-PARTY-NOTICES.txt')


# ----------------------------------------------------------------------------
# Geometry helpers
# ----------------------------------------------------------------------------
rng = None


def ring_extent(c):
    """The superellipse ring: the other coordinate where |x|^4+|z|^4=R^4."""
    t = 1.0 - (abs(c) / RING_RADIUS) ** 4
    return RING_RADIUS * t ** 0.25 if t > 0 else 0.0


def rail_extent(c):
    rr = RING_RADIUS + RAIL_GAP
    t = 1.0 - (abs(c) / rr) ** 4
    return rr * t ** 0.25 if t > 0 else 0.0


def inside_ring(x, z, margin=0.0):
    r = RING_RADIUS - margin
    return (abs(x) / r) ** 4 + (abs(z) / r) ** 4 < 1.0


class Line:
    """A grid line: axis 'x' means x = f(z) (runs north-south)."""

    def __init__(self, axis, index, base, amp, freq, phase, major):
        self.axis, self.index, self.base = axis, index, base
        self.amp, self.freq, self.phase, self.major = amp, freq, phase, major
        self.width = AVENUE_WIDTH if major else STREET_WIDTH
        self.kind = 'avenue' if major else 'street'

    def at(self, t):
        return self.base + self.amp * math.sin(t * self.freq + self.phase)

    def point(self, t):
        c = self.at(t)
        return (c, t) if self.axis == 'x' else (t, c)


def cross(a, b):
    """Intersection of an x-line and a z-line (fixed point, converges fast)."""
    x, z = a.base, b.base
    for _ in range(30):
        x = a.at(z)
        z = b.at(x)
    return x, z


def ring_end(line, sign):
    t = sign * RING_RADIUS
    for _ in range(40):
        t = sign * ring_extent(line.at(t))
    return t


def dist_seg(px, pz, ax, az, bx, bz):
    dx, dz = bx - ax, bz - az
    l2 = dx * dx + dz * dz
    t = 0.0 if l2 < 1e-9 else max(0.0, min(1.0, ((px-ax)*dx + (pz-az)*dz) / l2))
    qx, qz = ax + t * dx, az + t * dz
    return math.hypot(px - qx, pz - qz)


def catmull(pts, closed, step=2.0):
    """Sample the Catmull-Rom the game builds through the control points."""
    out = []
    n = len(pts)
    segs = n if closed else n - 1
    for i in range(segs):
        p0 = pts[(i - 1) % n] if closed else pts[max(i - 1, 0)]
        p1 = pts[i % n]
        p2 = pts[(i + 1) % n]
        p3 = pts[(i + 2) % n] if closed else pts[min(i + 2, n - 1)]
        length = math.hypot(p2[0]-p1[0], p2[1]-p1[1])
        k = max(1, int(length / step))
        for s in range(k):
            t = s / k
            t2, t3 = t*t, t*t*t
            out.append(tuple(0.5 * ((2*p1[j]) + (-p0[j]+p2[j])*t +
                                    (2*p0[j]-5*p1[j]+4*p2[j]-p3[j])*t2 +
                                    (-p0[j]+3*p1[j]-3*p2[j]+p3[j])*t3) for j in range(2)))
    if not closed:
        out.append(pts[-1])
    return out


class RoadIndex:
    """Spatial hash of road centre samples: is a point clear of every road?"""

    def __init__(self, cell=16.0):
        self.cell, self.grid = cell, {}

    def add(self, road):
        samples = catmull(road['points'], road.get('closed', False))
        hw = road['width'] / 2
        road['samples'] = samples
        for (ax, az), (bx, bz) in zip(samples, samples[1:]):
            mx, mz = (ax + bx) / 2, (az + bz) / 2
            k = (int(math.floor(mx / self.cell)), int(math.floor(mz / self.cell)))
            self.grid.setdefault(k, []).append((ax, az, bx, bz, hw, road['name']))

    def clearance(self, x, z, reach=24.0, skip=None):
        """Distance from (x, z) to the nearest road EDGE (negative = on a road)."""
        best = 1e9
        r = int(math.ceil(reach / self.cell)) + 1
        cx, cz = int(math.floor(x / self.cell)), int(math.floor(z / self.cell))
        for i in range(cx - r, cx + r + 1):
            for j in range(cz - r, cz + r + 1):
                for ax, az, bx, bz, hw, name in self.grid.get((i, j), ()):
                    if name == skip:
                        continue
                    d = dist_seg(x, z, ax, az, bx, bz) - hw
                    if d < best:
                        best = d
        return best


# ----------------------------------------------------------------------------
# Objects
# ----------------------------------------------------------------------------
objects = []
used_ids = set()


def make_id(name):
    h = hashlib.sha1(('big-city/' + name).encode()).hexdigest()
    oid = h[:16]
    k = 16
    while oid in used_ids:
        k += 1
        oid = hashlib.sha1(('big-city/' + name + str(k)).encode()).hexdigest()[:16]
    used_ids.add(oid)
    return oid


def add(name, kind, pos, scale=(1, 1, 1), rot=(0, 0, 0), color=(1, 1, 1), **kw):
    obj = {'id': make_id(name), 'name': name, 'type': kind,
           'position': [round(v, 4) for v in pos],
           'rotation': [round(v, 3) for v in rot],
           'scale': [round(v, 4) for v in scale],
           'color': list(color), 'physics': False}
    obj.update(kw)
    objects.append(obj)
    return obj


def road_object(road):
    """THE road writer. Every road field the city uses is set here, in one place.

    New road features from other branches plug in at the TODO markers: each is
    written only when its knob is set, so a run with the knobs at None produces
    exactly the fields the current editor knows.
    """
    kind = road['kind']
    downtown = road.get('downtown', False)
    fields = {
        'roadPoints': [round(v, 3) for p in road['points'] for v in p],
        'roadWidth': road['width'],
        'roadSampleStep': SAMPLE_STEP,
        'roadTexture': ROAD_TEX[kind],
    }
    if road.get('closed'):
        fields['roadPoints'] += fields['roadPoints'][:2]  # loop seam sentinel
    if kind == 'rail':
        # A railway: never kerbed, Track rank so every street crossing it is the
        # higher rank and gets a level crossing.
        fields.update({'roadKind': 1, 'roadTracks': 2, 'roadRank': 0, 'roadSpill': 0,
                       'roadIntersectionTexture': RAIL_JUNCTION_MAT, 'roadMarkings': 0})
        return add(road['name'], 'road', (0, 0, 0), **fields)
    if kind == 'park':
        fields['roadRank'] = 0            # Track: no patch, never kerbed
        fields['roadMarkings'] = 0
        fields['roadGrip'] = 0.85
    else:
        fields['roadIntersectionTexture'] = JUNCTION_MAT
        kerbed = KERBS and kind in KERB_KINDS and (road.get('core') or not KERBS_DOWNTOWN_ONLY)
        if kerbed:
            fields['roadKerb'] = True
            if KERB_HEIGHT != 0.15:
                fields['roadKerbHeight'] = KERB_HEIGHT
            if KERB_WIDTH != 0.25:
                fields['roadKerbWidth'] = KERB_WIDTH
        if ZEBRAS and downtown and kind in ('avenue', 'diagonal', 'boulevard'):
            fields['roadMarkings'] = 2
        elif not downtown and not MARKINGS_OUTSIDE_DOWNTOWN:
            fields['roadMarkings'] = 0    # no node paint: the paint is RAM (see README)
        # Pavement behind the kerb (a pavement needs its kerb).
        if PAVEMENT_WIDTH and kerbed and kind in PAVEMENT_KINDS:
            fields['roadPavement'] = PAVEMENT_WIDTH
            fields['roadPavementMaterial'] = PAVEMENT_MATERIAL
            road['paved_width'] = PAVEMENT_WIDTH
        # Tram tracks laid in a kerbed asphalt avenue.
        if TRAM_TRACKS and road.get('tram') and kerbed:
            fields['roadKind'] = 2
            fields['roadTracks'] = TRAM_TRACKS
        # Manholes, gullies and patches.
        if ROAD_DETAILS and kerbed:
            fields['roadDetails'] = ROAD_DETAILS
            fields['roadDetailSeed'] = int(hash01('detail', road['name'], SEED) * 100000)
    # A raised road: one height per control point (an overpass makes no junction).
    if BRIDGES and road.get('heights'):
        fields['roadBridge'] = True
        fields['roadHeights'] = [round(h, 3) for h in road['heights']]
    return add(road['name'], 'road', (0, 0, 0), **fields)


def layer_of(x, z):
    if not STREAM_LAYERS:
        return {}
    i = int(math.floor((x + TERRAIN_SIZE / 2) / DISTRICT_SIZE))
    j = int(math.floor((z + TERRAIN_SIZE / 2) / DISTRICT_SIZE))
    return {'layer': 'district-%d-%d' % (i, j)}


def model(name, path, x, z, y=0.0, scale=(1, 1, 1), yaw=0.0, draw=BUILDING_DRAW, **kw):
    kw.setdefault('castShadow', False)
    kw.update(layer_of(x, z))
    return add(name, 'model', (x, y, z), scale, (0, yaw, 0), drawDistance=draw,
               model=path, **kw)


# ----------------------------------------------------------------------------
# The city
# ----------------------------------------------------------------------------
def build():
    global rng, TREE_OBJ, PINE_OBJ, LAMP_OBJ, TREE_SCALE, LAMP_SCALE
    if LOWPOLY_PROPS:   # sized in metres at scale 1; the Kenney ones are 1/6 scale
        TREE_OBJ, PINE_OBJ = 'res/models/urban/city-tree.obj', 'res/models/urban/city-pine.obj'
        LAMP_OBJ, TREE_SCALE, LAMP_SCALE = 'res/models/urban/city-lamp.obj', 1 / 6.0, 1.0
    else:
        TREE_OBJ = 'res/models/urban/tree-park-large.obj'
        PINE_OBJ = 'res/models/urban/tree-park-pine-large.obj'
        LAMP_OBJ, TREE_SCALE, LAMP_SCALE = 'res/models/urban/detail-light-double.obj', 1.0, 6.0
    rng = random.Random(SEED)
    roads = []
    stats = {}

    # Grid lines. Even lines are 4-lane avenues (straight), odd ones 2-lane
    # streets with a gentle wobble.
    xs, zs = [], []
    for axis, store in (('x', xs), ('z', zs)):
        for i in range(GRID_LINES):
            base = (i - (GRID_LINES - 1) / 2) * BLOCK + rng.uniform(-LINE_JITTER, LINE_JITTER)
            major = i % 2 == 0
            amp = 0.0 if major else rng.uniform(0.5, 1.0) * LINE_WOBBLE
            store.append(Line(axis, i, base, amp, rng.uniform(1/160, 1/90),
                              rng.uniform(0, 6.283), major))
    nodes = {(i, j): cross(xs[i], zs[j]) for i in range(GRID_LINES) for j in range(GRID_LINES)}

    def downtown(x, z):
        return math.hypot(x, z) < DOWNTOWN_RADIUS

    names = {'x': 'Avenue', 'z': 'Street'}
    for line in xs + zs:
        t0, t1 = ring_end(line, -1), ring_end(line, 1)
        n = max(2, int((t1 - t0) / 46))
        pts = [line.point(t0 + (t1 - t0) * k / n) for k in range(n + 1)]
        label = '%s %s %d' % ('Grand' if line.major else 'Minor', names[line.axis],
                              line.index + 1)
        roads.append({'name': label, 'kind': line.kind, 'width': line.width,
                      'points': pts, 'downtown': abs(line.base) < DOWNTOWN_RADIUS,
                      'core': abs(line.base) < CORE_RADIUS})

    # The ring road: a closed superellipse loop.
    ring = []
    for k in range(64):
        a = 2 * math.pi * k / 64
        c, s = math.cos(a), math.sin(a)
        r = RING_RADIUS / (abs(c) ** 4 + abs(s) ** 4) ** 0.25
        ring.append((r * c, r * s))
    roads.append({'name': 'Ring road', 'kind': 'ring', 'width': RING_WIDTH,
                  'points': ring, 'closed': True})
    if RAILWAY:
        rail = []
        rr = RING_RADIUS + RAIL_GAP
        rr = RING_RADIUS + RAIL_GAP
        # An arc round the east and south sides, from the map edge to the map
        # edge: Spur 1 flies over it, Spur 2 meets it at a level crossing.
        a0, a1 = math.radians(RAIL_ARC[0]), math.radians(RAIL_ARC[1])
        steps = max(4, int(abs(a1 - a0) / math.radians(5)))
        for k in range(steps + 1):
            a = a0 + (a1 - a0) * k / steps
            c, s_ = math.cos(a), math.sin(a)
            r = rr / (abs(c) ** 4 + abs(s_) ** 4) ** 0.25
            rail.append((r * c, r * s_))
        roads.append({'name': 'Railway', 'kind': 'rail', 'width': 7.6, 'points': rail})

    # Diagonal boulevard SW -> NE through the grid nodes (i, i): every node on
    # it becomes a six-armed crossing. A second, shorter one from the NW stops
    # two nodes short of the centre (a five-armed end node).
    lo, hi = 1, GRID_LINES - 2
    diag = [nodes[(i, i)] for i in range(lo, hi + 1)]
    roads.append({'name': 'Diagonal boulevard', 'kind': 'diagonal', 'width': DIAGONAL_WIDTH,
                  'points': diag, 'downtown': True, 'tram': True, 'core': True})
    diag2 = [nodes[(i, GRID_LINES - 1 - i)] for i in range(lo, GRID_LINES // 2 - 1)]
    roads.append({'name': 'Northwest diagonal', 'kind': 'diagonal', 'width': DIAGONAL_WIDTH,
                  'points': diag2})
    diag_blocks = {(i, i) for i in range(lo, hi)}
    diag_blocks |= {(i, GRID_LINES - 2 - i) for i in range(lo, GRID_LINES // 2 - 2)}

    # A sinuous boulevard in the band between two streets, ring to ring.
    j0 = GRID_LINES // 2 + 1
    zmid = (zs[j0].base + zs[j0 + 1].base) / 2
    band = (zs[j0 + 1].base - zs[j0].base) / 2 - BOULEVARD_WIDTH / 2 - STREET_WIDTH / 2 - 6
    amp = max(4.0, min(24.0, band))
    bz = lambda x: zmid + amp * math.sin(x / 75.0 + 0.7)
    x0 = -RING_RADIUS
    for _ in range(40):
        x0 = -ring_extent(bz(x0))
    x1 = RING_RADIUS
    for _ in range(40):
        x1 = ring_extent(bz(x1))
    nb = int((x1 - x0) / 24)
    blvd = [(x0 + (x1 - x0) * k / nb, bz(x0 + (x1 - x0) * k / nb)) for k in range(nb + 1)]
    roads.append({'name': 'River boulevard', 'kind': 'boulevard', 'width': BOULEVARD_WIDTH,
                  'points': blvd, 'downtown': True})
    blvd_row = j0

    # Four spurs leave the ring for the map edge and fork into a Y there.
    edge = TERRAIN_SIZE / 2 - 14
    for k, (dx, dz) in enumerate(((1, 0), (0, 1), (-1, 0), (0, -1))):
        # Leave the ring between two grid lines so the spur meets it alone.
        lines = zs if dx else xs
        off = (lines[GRID_LINES // 2].base + lines[GRID_LINES // 2 - 1].base) / 2
        r0 = ring_extent(off)

        def at(r):
            return (dx * r, off) if dx else (off, dz * r)
        start, fork = at(r0), at(edge - 40)
        spur = {'name': 'Spur %d' % (k + 1), 'kind': 'spur', 'width': SPUR_WIDTH}
        rail_r = rail_extent(off) if RAILWAY else None
        if BRIDGES and RAILWAY and k == 0:
            # Up from the ring, over the rails, down before the fork.
            ramp = (rail_r - r0 - 6) * 0.9
            rs = [r0, r0 + 6, rail_r - 8, rail_r, rail_r + 8, rail_r + 6 + ramp, edge - 40]
            hs = [0, 0, BRIDGE_HEIGHT, BRIDGE_HEIGHT, BRIDGE_HEIGHT, 0, 0]
            spur.update(points=[at(r) for r in rs], heights=hs)
        else:
            spur['points'] = [start, at((r0 + edge - 40) / 2), fork]
        roads.append(spur)
        side = (dz, dx)
        for s, label in ((1, 'a'), (-1, 'b')):
            tip = (fork[0] + dx * 34 + side[0] * s * 22, fork[1] + dz * 34 + side[1] * s * 22)
            roads.append({'name': 'Spur %d fork %s' % (k + 1, label), 'kind': 'spur',
                          'width': SPUR_WIDTH - 2,
                          'points': [fork, ((fork[0] + tip[0]) / 2 + dx * 6,
                                            (fork[1] + tip[1]) / 2 + dz * 6), tip]})

    # Blocks: (i, j) lies between x-lines i, i+1 and z-lines j, j+1.
    blocks, full = [], []
    for i in range(GRID_LINES - 1):
        for j in range(GRID_LINES - 1):
            corners = [nodes[(i, j)], nodes[(i+1, j)], nodes[(i, j+1)], nodes[(i+1, j+1)]]
            inside = [inside_ring(x, z, RING_WIDTH / 2 + 8) for x, z in corners]
            if any(inside):
                blocks.append((i, j))     # outer blocks are cut by the ring
            if all(inside):
                full.append((i, j))
    centre_blocks = sorted(full, key=lambda b: math.hypot(*block_centre(nodes, b)))
    candidates = [b for b in full if b not in diag_blocks and b[1] != blvd_row]
    parks = set()
    plaza = centre_blocks[0] if centre_blocks[0] not in diag_blocks else centre_blocks[1]
    pool = [b for b in candidates if b != plaza and
            math.hypot(*block_centre(nodes, b)) > DOWNTOWN_RADIUS * 0.6]
    rng.shuffle(pool)
    for b in pool:
        if len(parks) >= PARK_COUNT:
            break
        if all(abs(b[0]-p[0]) + abs(b[1]-p[1]) > 2 for p in parks):
            parks.add(b)

    # Local streets.
    locals_ = {}
    for b in candidates:
        if b in parks or b == plaza or rng.random() > LOCAL_STREET_P:
            continue
        i, j = b
        ns = rng.random() < 0.5
        dead = rng.random() < DEAD_END_P
        f = rng.uniform(0.38, 0.62)
        if ns:   # runs along z, from z-line j to z-line j+1
            xa = xs[i].at(zs[j].base) * (1-f) + xs[i+1].at(zs[j].base) * f
            za, zb = zs[j].at(xa), zs[j+1].at(xa)
            if dead:
                zb = za + (zb - za) * 0.6
            pts = [(xa, za), (xa, (za + zb) / 2), (xa, zb)]
        else:
            za = zs[j].at(xs[i].base) * (1-f) + zs[j+1].at(xs[i].base) * f
            xa, xb = xs[i].at(za), xs[i+1].at(za)
            if dead:
                xb = xa + (xb - xa) * 0.6
            pts = [(xa, za), ((xa + xb) / 2, za), (xb, za)]
        if rng.random() < 0.5 and dead:
            pts = pts[::-1]  # dead end may leave from either side
        locals_[b] = (ns, dead)
        roads.append({'name': 'Lane %d-%d' % (i + 1, j + 1), 'kind': 'local',
                      'width': LOCAL_WIDTH, 'points': pts,
                      'downtown': downtown(*pts[1]),
                      'core': math.hypot(*pts[1]) < CORE_RADIUS * 1.6})

    # Park paths: closed cobble loops well inside the park block (Track rank,
    # they touch no other road).
    for b in sorted(parks):
        cx, cz = block_centre(nodes, b)
        r = BLOCK / 2 - 22
        loop = [(cx + r * math.cos(a) * (1 + 0.15 * math.sin(3 * a)),
                 cz + r * math.sin(a) * (1 + 0.15 * math.cos(2 * a)))
                for a in (2 * math.pi * k / 10 for k in range(10))]
        roads.append({'name': 'Park path %d-%d' % (b[0] + 1, b[1] + 1), 'kind': 'park',
                      'width': 3.0, 'points': loop, 'closed': True})

    index = RoadIndex()
    for r in roads:
        index.add(r)
    for r in roads:
        if WRITE_ROADS:
            road_object(r)
    stats['roads'] = len(roads)
    stats['road_length'] = sum(sum(math.hypot(b[0]-a[0], b[1]-a[1])
                                   for a, b in zip(r['samples'], r['samples'][1:]))
                               for r in roads)

    # ---- buildings -------------------------------------------------------
    nb = 0
    plinths = 0
    for b in blocks:
        if b in parks or b == plaza:
            continue
        i, j = b
        x0 = max(xs[i].at(zs[j].base), xs[i].at(zs[j+1].base)) + xs[i].width / 2 + xs[i].amp
        x1 = min(xs[i+1].at(zs[j].base), xs[i+1].at(zs[j+1].base)) - xs[i+1].width / 2 - xs[i+1].amp
        z0 = max(zs[j].at(xs[i].base), zs[j].at(xs[i+1].base)) + zs[j].width / 2 + zs[j].amp
        z1 = min(zs[j+1].at(xs[i].base), zs[j+1].at(xs[i+1].base)) - zs[j+1].width / 2 - zs[j+1].amp
        rects = [(x0, z0, x1, z1)]
        ls = locals_.get(b)
        if ls and not ls[1]:   # a through lane splits the block in two
            lane = [r for r in roads if r['name'] == 'Lane %d-%d' % (i + 1, j + 1)][0]
            if ls[0]:
                c = lane['points'][0][0]
                rects = [(x0, z0, c - LOCAL_WIDTH / 2, z1), (c + LOCAL_WIDTH / 2, z0, x1, z1)]
            else:
                c = lane['points'][0][1]
                rects = [(x0, z0, x1, c - LOCAL_WIDTH / 2), (x0, c + LOCAL_WIDTH / 2, x1, z1)]
        for (rx0, rz0, rx1, rz1) in rects:
            if rx1 - rx0 < 12 or rz1 - rz0 < 12:
                continue
            cx, cz = (rx0 + rx1) / 2, (rz0 + rz1) / 2
            if PAVEMENT_BOXES and b in full and all(
                    index.clearance(rx0 + 1 + (rx1 - rx0 - 2) * u / 4, rz0 + 1 + (rz1 - rz0 - 2) * v / 4, 60) > 0.3
                    for u in range(5) for v in range(5)):
                inset = 0.6 + 2 * ((PAVEMENT_WIDTH or 0) + KERB_WIDTH)
                w, d = rx1 - rx0 - inset, rz1 - rz0 - inset
                add('Plinth %d-%d %d' % (i + 1, j + 1, plinths), 'box', (cx, 0.135, cz),
                    (w, 0.27, d), color=(0.47, 0.47, 0.45), castShadow=False,
                    collision='none', drawDistance=PLINTH_DRAW, **layer_of(cx, cz))
                plinths += 1
            if BUILDINGS:
                nb += fill_lots(index, rx0 + SIDEWALK, rz0 + SIDEWALK, rx1 - SIDEWALK, rz1 - SIDEWALK, nb)
    stats['buildings'] = nb
    stats['plinths'] = plinths

    # ---- the plaza ---------------------------------------------------------
    pcx, pcz = block_centre(nodes, plaza)
    add('Central plaza', 'box', (pcx, 0.1, pcz), (BLOCK - 24, 0.2, BLOCK - 24),
        color=(0.66, 0.62, 0.55), castShadow=False, collision='none', drawDistance=PLINTH_DRAW)
    for k in range(8):
        a = 2 * math.pi * k / 8
        model('Plaza tree %d' % k, TREE_OBJ, pcx + 26 * math.cos(a), pcz + 26 * math.sin(a),
              scale=(TREE_SCALE * 6,) * 3, draw=TREE_DRAW,
              **tree_lod())
        model('Plaza lamp %d' % k, LAMP_OBJ,
              pcx + 18 * math.cos(a + 0.39), pcz + 18 * math.sin(a + 0.39), scale=(LAMP_SCALE,) * 3,
              yaw=math.degrees(a), draw=LAMP_DRAW)

    # ---- parks -------------------------------------------------------------
    trees = 0
    benches = 0
    for b in sorted(parks):
        cx, cz = block_centre(nodes, b)
        for k in range(26):
            x = cx + rng.uniform(-BLOCK / 2 + 14, BLOCK / 2 - 14)
            z = cz + rng.uniform(-BLOCK / 2 + 14, BLOCK / 2 - 14)
            if index.clearance(x, z) < 2.5:
                continue
            pine = rng.random() < 0.4
            s = rng.uniform(5.5, 8.0)
            model('Park tree %d' % trees, PINE_OBJ if pine else TREE_OBJ, x, z,
                  scale=(s * TREE_SCALE,) * 3, yaw=rng.uniform(0, 360),
                  draw=TREE_DRAW, **tree_lod())
            trees += 1
        for k in range(4):
            a = 2 * math.pi * k / 4 + 0.4
            r = BLOCK / 2 - 22
            x, z = cx + (r + 3.2) * math.cos(a), cz + (r + 3.2) * math.sin(a)
            if index.clearance(x, z) > 0.8:
                model('Bench %d' % benches, 'res/models/urban/detail-bench.obj', x, z,
                      scale=(2, 2, 2), yaw=math.degrees(-a) + 90, draw=PROP_DRAW)
                benches += 1

    # ---- street furniture along the roads --------------------------------
    lamps = cars = 0
    paints = [(0.75, 0.12, 0.1), (0.15, 0.3, 0.7), (0.85, 0.82, 0.78), (0.2, 0.2, 0.22),
              (0.9, 0.7, 0.15), (0.25, 0.5, 0.3)]
    for r in roads:
        if r['kind'] in ('park', 'rail') or r.get('heights'):
            continue
        if FURNITURE_CORE_ONLY and not r.get('core'):
            continue
        s = r['samples']
        hw = r['width'] / 2
        acc = 0.0
        next_tree = TREE_SPACING / 2
        next_lamp = LAMP_SPACING / 2
        next_car = 6.0
        side = 1
        for (ax, az), (bx, bz) in zip(s, s[1:]):
            seg = math.hypot(bx - ax, bz - az)
            if seg < 1e-6:
                continue
            ux, uz = (bx - ax) / seg, (bz - az) / seg
            nx, nz = uz, -ux
            heading = math.degrees(math.atan2(ux, uz))
            acc += seg
            if TREES and r['kind'] in ('avenue', 'diagonal', 'boulevard') and acc >= next_tree:
                next_tree += TREE_SPACING
                for sd in (1, -1):
                    off = hw + 2.2
                    x, z = bx + nx * off * sd, bz + nz * off * sd
                    if index.clearance(x, z) > 1.6 and building_free(x, z, 1.2):
                        sc = rng.uniform(4.5, 6.0)
                        model('Avenue tree %d' % trees, TREE_OBJ,
                              x, z, scale=(sc * TREE_SCALE,) * 3, yaw=rng.uniform(0, 360), draw=TREE_DRAW,
                              **tree_lod())
                        trees += 1
            if LAMPS and r['kind'] != 'spur' and acc >= next_lamp:
                next_lamp += LAMP_SPACING
                side = -side
                off = hw + 0.9
                x, z = bx + nx * off * side, bz + nz * off * side
                if index.clearance(x, z) > 0.5 and building_free(x, z, 0.6):
                    model('Lamp %d' % lamps, LAMP_OBJ, x, z, scale=(LAMP_SCALE,) * 3,
                          yaw=heading + (90 if LOWPOLY_PROPS else 90) + (180 if side > 0 else 0),
                          draw=LAMP_DRAW)
                    lamps += 1
            if PARKED_CARS and r['kind'] in ('street', 'local') and acc >= next_car:
                next_car += 9.0
                if rng.random() < PARKED_CAR_P:
                    sd = rng.choice((1, -1))
                    off = hw - 1.3
                    x, z = bx + nx * off * sd, bz + nz * off * sd
                    # A car must sit on its own road, away from every node.
                    if node_free(index, r, x, z):
                        model('Parked car %d' % cars, 'res/models/urban/city-car.obj', x, z, y=0.12,
                              color=rng.choice(paints),
                              yaw=heading + (0 if sd > 0 else 180), draw=CAR_DRAW,
                              collision='box')
                        cars += 1
    stats.update(trees=trees, lamps=lamps, parked_cars=cars, benches=benches)

    # ---- player + car ------------------------------------------------------
    px, pz = pcx - (BLOCK / 2 - 2), pcz
    # The plaza's west edge is a grid line; park the car on it, heading north.
    west = xs[plaza[0]]
    vx = west.at(pcz) + 2.5
    if POSE:   # a frozen measuring camera (docs/profiling.md): no walking, no looking
        x, z, heading, pitch, eye = POSE
        add('player-1', 'player', (x, 0, z), rot=(pitch, heading, 0), color=(0.15, 0.9, 0.9),
            player={'mode': 'walk', 'walkSpeed': 0, 'lookSpeed': 0, 'eyeHeight': eye,
                    'jumpSpeed': 4.5, 'canJump': False})
    else:
        add('player-1', 'player', (vx - 4.5, 0, pcz + 6), color=(0.15, 0.9, 0.9),
            player={'mode': 'walk', 'walkSpeed': 0.1, 'lookSpeed': 1, 'eyeHeight': 1.8,
                    'jumpSpeed': 4.5, 'canJump': True})
    flow = {'nextId': 4, 'nodes': [
        {'id': 1, 'type': 'OnStart', 'pos': [64, 130], 'str': '', 'num': [0, 0, 0, 0]},
        {'id': 2, 'type': 'EnterVehicle', 'pos': [400, 142], 'str': '', 'num': [0, 0, 0, 0]}],
        'links': [{'id': 3, 'from': 1, 'to': 2}]}
    car = add('ravager-1', 'vehicle', (vx, 0.5, pcz), rot=(0, 180, 0), color=(0.6, 0.6, 0.6),
              shadowMode=2, vehicle={'def': 'Ravager', 'driveable': True})
    if START_IN_CAR and not POSE:
        car['flowGraph'] = flow
    return stats


def block_centre(nodes, b):
    i, j = b
    pts = [nodes[(i, j)], nodes[(i+1, j)], nodes[(i, j+1)], nodes[(i+1, j+1)]]
    return sum(p[0] for p in pts) / 4, sum(p[1] for p in pts) / 4


building_rects = []


def building_free(x, z, pad):
    for (a, b, c, d) in building_rects:
        if a - pad < x < c + pad and b - pad < z < d + pad:
            return False
    return True


def node_free(index, road, x, z):
    """At least a node's worth of room (14 units) from every OTHER road's edge."""
    return index.clearance(x, z, 30, skip=road['name']) > 14.0


def tree_lod():
    return {'meshLod': TREE_MESH_LOD} if TREE_MESH_LOD > 0 else {}


def fill_lots(index, x0, z0, x1, z1, base):
    """Perimeter lots round a block rectangle; tall downtown, low outside."""
    count = 0
    w, d = x1 - x0, z1 - z0
    nx = max(1, int(round(w / rng.uniform(17, 23))))
    nz = max(1, int(round(d / rng.uniform(17, 23))))
    lw, ld = w / nx, d / nz
    for a in range(nx):
        for b in range(nz):
            if 0 < a < nx - 1 and 0 < b < nz - 1:
                continue        # courtyard
            cx = x0 + lw * (a + 0.5)
            cz = z0 + ld * (b + 0.5)
            fw = lw - rng.uniform(1.0, 3.0)
            fd = ld - rng.uniform(1.0, 3.0)
            if fw < 6 or fd < 6:
                continue
            corners = [(cx - fw/2, cz - fd/2), (cx + fw/2, cz - fd/2),
                       (cx - fw/2, cz + fd/2), (cx + fw/2, cz + fd/2), (cx, cz)]
            if any(index.clearance(px, pz, 40) < 1.0 for px, pz in corners):
                continue
            if not inside_ring(cx, cz, RING_WIDTH / 2 + 14):
                continue
            r = math.hypot(cx, cz)
            roll = rng.random()
            draw = BUILDING_DRAW
            if r < DOWNTOWN_RADIUS:
                tall = 1 - r / DOWNTOWN_RADIUS
                bands = 2 + min(3, int(tall * 3.2 + roll * 1.6))
                path, foot, h = 'city-highrise-%d' % bands, 8.0, rng.uniform(0.9, 1.15)
                draw = TOWER_DRAW
            elif r < MIDTOWN_RADIUS:
                if roll < 0.35:
                    path, foot, h = 'district-tower', 8.0, rng.uniform(0.8, 1.2)
                elif roll < 0.55:
                    path, foot, h = 'city-highrise-2', 8.0, rng.uniform(0.85, 1.1)
                    draw = TOWER_DRAW
                else:
                    path, foot, h = 'district-loft', 8.0, rng.uniform(0.9, 1.4)
            else:
                if roll < 0.45:
                    path, foot, h = 'district-loft', 8.0, rng.uniform(0.7, 1.1)
                else:
                    path, foot, h = 'district-workshop', 12.0, rng.uniform(0.9, 1.5)
            sx = fw / foot
            sz = fd / 8.0
            model('Building %d' % (base + count), 'res/models/urban/%s.obj' % path, cx, cz,
                  scale=(sx, h * min(1.6, max(1.0, (sx + sz) / 2)), sz), draw=draw)
            building_rects.append((cx - fw/2, cz - fd/2, cx + fw/2, cz + fd/2))
            count += 1
    return count


# ----------------------------------------------------------------------------
def apply_settings(p):
    s = p['settings']
    haze = [0.66, 0.72, 0.78]
    s.update({
        'videoSystem': 'ntsc', 'displayMode': 'progressive',
        'showFps': True, 'showMemory': True, 'showProfiler': True,
        'liveLink': False, 'liveLogic': False, 'timeMachine': False,
        'staticBatching': STATIC_BATCHING, 'occlusionCulling': OCCLUSION,
        'terrainDetail': TERRAIN_DETAIL, 'terrainViewDistance': VIEW_DISTANCE,
        'terrainLodDistance': TERRAIN_LOD,
        'terrainMaterial': 'res/materials/city-ground.mtl',
        'skyColor': haze, 'skyTopColor': [0.22, 0.42, 0.66],
        'fogEnabled': True, 'fogColor': haze, 'fogStart': FOG_START, 'fogEnd': FOG_END,
        'aoEnabled': False, 'giEnabled': False, 'modelAo': False,
        'bakedShadows': False, 'prelitAutoBake': False,
        'lightDir': [-0.45, 0.75, 0.4], 'ambient': 0.5, 'diffuse': 0.5,
        'lightColor': [1, 0.95, 0.86],
    })
    if ROAD_STREAM:
        s['roadStreamRadius'] = ROAD_STREAM
    else:
        s.pop('roadStreamRadius', None)
    for a in p.get('ambience', [])[:1]:
        a.update({'skyColor': haze, 'skyTopColor': [0.22, 0.42, 0.66],
                  'fogEnabled': True, 'fogColor': haze, 'fogStart': FOG_START,
                  'fogEnd': FOG_END, 'lightDir': [-0.45, 0.75, 0.4], 'ambient': 0.5,
                  'diffuse': 0.5, 'lightColor': [1, 0.95, 0.86]})


def main():
    global SEED
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--seed', type=int, default=SEED)
    ap.add_argument('--editor', help='tyrax-editor executable: road textures + --resave')
    ap.add_argument('--set', action='append', default=[], metavar='KNOB=VALUE',
                    help='override a knob for this run, e.g. --set KERBS=False')
    ap.add_argument('--bootstrap', action='store_true',
                    help='copy shared assets + vehicles from ../vehicle-playground')
    args = ap.parse_args()
    if args.editor:
        args.editor = str(Path(args.editor).resolve())
    SEED = args.seed
    for kv in args.set:
        k, v = kv.split('=', 1)
        if k not in globals() or not k.isupper():
            raise SystemExit('unknown knob ' + k)
        globals()[k] = eval(v, {})
    if args.bootstrap:
        bootstrap()
    make_assets()
    if args.editor:
        subprocess.run([args.editor, '--road-texture', str(ROOT), 'road-6lane', 'lanes=6',
                        'seed=6', 'centre=double', 'centre.colour=yellow'], check=True)
    stats = build()
    p = json.loads(MANIFEST.read_text(encoding='utf-8'))
    apply_settings(p)
    scene = p['scenes'][0]
    scene['terrain'] = {'width': TERRAIN_SIZE, 'depth': TERRAIN_SIZE}
    if STREAM_LAYERS:
        layers = sorted({o['layer'] for o in objects if 'layer' in o})
        scene['layers'] = []
        for name in layers:
            i, j = (int(v) for v in name.split('-')[1:])
            cx = -TERRAIN_SIZE / 2 + (i + 0.5) * DISTRICT_SIZE
            cz = -TERRAIN_SIZE / 2 + (j + 0.5) * DISTRICT_SIZE
            scene['layers'].append({'name': name, 'autoStream': True, 'streamX': cx,
                                    'streamZ': cz,
                                    'streamRadius': DISTRICT_SIZE * 0.71 + STREAM_MARGIN})
    else:
        scene.pop('layers', None)
    scene['objects'] = [o['id'] for o in objects]
    for f in OBJECTS.glob('*.json'):
        f.unlink()
    for o in objects:
        write(OBJECTS / (o['id'] + '.json'), json.dumps(o) + '\n')
    write(MANIFEST, json.dumps(p, indent=2) + '\n')
    if args.editor:
        subprocess.run([args.editor, '--resave', str(ROOT)], check=True)
    stats['objects'] = len(objects)
    print('big-city seed %d: ' % SEED + ', '.join(
        '%s %s' % (k, ('%.0f' % v) if isinstance(v, float) else v) for k, v in stats.items()))


if __name__ == '__main__':
    main()
