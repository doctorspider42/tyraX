"""Offline retarget of Quaternius' Universal Animation Library (1 + 2, CC0)
onto the generated characters' 35-bone rig (rig.py). Authoring-time only.

    blender -b --factory-startup --python anim_retarget.py -- <out.json>
            [--ual1 <AnimationLibrary_Godot_Standard.glb>]
            [--ual2 <UAL2_Standard.glb>] --mh <mh-data dir> [--preview <dir> [--only <sheet,...>]]

Output: one JSON clip file with LOCAL bone rotations for every clip (bind
rotations are identity, so local == "rotation relative to the parent") plus
a hips offset per frame. The editor/game never sees the UAL files.

Spaces. Everything here works in glTF space: metres, +Y up, the character
facing +Z, +X the character's LEFT. That is MakeHuman's space too (just in
decimetres) and it is what both UAL files were authored in, so there is no
mirroring anywhere: UAL's ".L" / "_l" bones sit at +X in the rest pose, the
toes point at +Z - the same side and facing as ours (checked, not assumed:
see check_source_frame()). Blender imports glTF as (x, -z, y); `G` converts
Blender world matrices back.

Why "direction alignment with source twist". Our rig and UAL's disagree in
rest pose (ours is MakeHuman's A-pose with the forearms reaching forward,
UAL is a T-pose) and in where joints sit, so copying local rotations would
put the arms in the wrong place. Instead each of our bones copies the
source bone's WORLD rotation DELTA from its rest:
    W(b,t) = S(b,t) * S0(b)^-1 * C(b)
where C(b) is a fixed correction that turns OUR rest bone onto the SOURCE
rest bone. Then at every frame our bone points exactly where the source bone
points, and the twist about the bone comes from the source's motion.
C is the minimal-arc rotation between the two rest directions, except:
  * Hips: identity. The "direction" hips->first spine joint is an accident of
    where each rig puts its lumbar joint (ours leans 26 deg back, UAL's 14 deg
    forward); aligning it would tilt the whole pelvis skin ~40 deg. Both
    pelvises are upright and face +Z, so no correction is the right one.
  * Hand and fingers: two-vector alignment (direction + the knuckle line,
    middle->index). A minimal arc leaves the palm rolled by whatever twist
    the A->T swing happens to imply, which shows up as a twisted wrist
    even though the fingers point the right way.
Local: L(b,t) = W(parent,t)^-1 * W(b,t) (identity bind).

Hips translation: the source hips' world offset from its rest, scaled by
the ratio of LEG heights (hip joint above the floor), because what has to
match is "the feet stay on the floor": a crouch that drops UAL's hips 40 cm
must drop ours by the same fraction of our leg. (Ratio of the hips joints'
heights would be 2.5% off: UAL's hips joint sits below its thigh joints,
ours above.) Then floor_lock() moves the hips vertically per frame so our
lowest sole matches the source's: proportions differ too much for scaling
alone (a sprint floated 10 cm without it).

Checks printed every run, all on FK of the WRITTEN file: every mapped bone
points where its source segment points (worst angle), soles match the
source per frame, and planted feet do not slide (mm/frame vs the source).

Sampling: UAL keys every 1/30 s (all 89 animations, checked in the glb
accessors), so the scene runs at 30 fps before the import and every key is an
integer frame. Loops whose last frame repeats the first drop that frame, so
a runtime wrapping at N plays no doubled pose.
"""
import json
import math
import os
import sys

import bpy
import numpy as np
from mathutils import Matrix, Quaternion, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mhkit  # noqa: E402
import rig  # noqa: E402

# MakeHuman data (base.obj, targets/, default.mhskel): --mh <dir>, as
# fetch_sources.py lays it out in <sources>/mh-data.
MH = None
FPS = 30
SRC_FLOOR = [0.0]  # the source mesh's lowest rest vertex, set per library

# Blender world -> glTF: (x, y, z) -> (x, z, -y)
G = Matrix(((1, 0, 0), (0, 0, 1), (0, -1, 0)))
GT = G.transposed()

# ---------------------------------------------------------------------------
# Bone map. Our bone -> (UAL1 name, UAL2 name). HeadTop_End is unmapped and
# rides on Head.
MAP = {'Hips': ('DEF-hips', 'pelvis'), 'Spine': ('DEF-spine.001', 'spine_01'),
       'Spine1': ('DEF-spine.002', 'spine_02'), 'Spine2': ('DEF-spine.003', 'spine_03'),
       'Neck': ('DEF-neck', 'neck_01'), 'Head': ('DEF-head', 'Head')}
for side, a, b in (('Left', '.L', '_l'), ('Right', '.R', '_r')):
    MAP.update({
        side + 'Shoulder': ('DEF-shoulder' + a, 'clavicle' + b),
        side + 'Arm': ('DEF-upper_arm' + a, 'upperarm' + b),
        side + 'ForeArm': ('DEF-forearm' + a, 'lowerarm' + b),
        side + 'Hand': ('DEF-hand' + a, 'hand' + b),
        side + 'HandThumb1': ('DEF-thumb.01' + a, 'thumb_01' + b),
        side + 'HandThumb2': ('DEF-thumb.02' + a, 'thumb_02' + b),
        side + 'HandIndex1': ('DEF-f_index.01' + a, 'index_01' + b),
        side + 'HandIndex2': ('DEF-f_index.02' + a, 'index_02' + b),
        side + 'HandMiddle1': ('DEF-f_middle.01' + a, 'middle_01' + b),
        side + 'HandMiddle2': ('DEF-f_middle.02' + a, 'middle_02' + b),
        side + 'UpLeg': ('DEF-thigh' + a, 'thigh' + b),
        side + 'Leg': ('DEF-shin' + a, 'calf' + b),
        side + 'Foot': ('DEF-foot' + a, 'foot' + b),
        side + 'ToeBase': ('DEF-toe' + a, 'ball' + b),
    })

# The bone whose head a bone points at (same definition on both rigs).
CHILD = {'Hips': 'Spine', 'Spine': 'Spine1', 'Spine1': 'Spine2', 'Spine2': 'Neck',
         'Neck': 'Head', 'Head': 'HeadTop_End'}
for s in ('Left', 'Right'):
    CHILD.update({s + 'Shoulder': s + 'Arm', s + 'Arm': s + 'ForeArm',
                  s + 'ForeArm': s + 'Hand', s + 'Hand': s + 'HandMiddle1',
                  s + 'HandThumb1': s + 'HandThumb2', s + 'HandIndex1': s + 'HandIndex2',
                  s + 'HandMiddle1': s + 'HandMiddle2', s + 'UpLeg': s + 'Leg',
                  s + 'Leg': s + 'Foot', s + 'Foot': s + 'ToeBase'})
# Leaves: our tail joint, and the source bone whose TAIL ends the same chain.
# Our Thumb2/Index2/Middle2 each absorb two MakeHuman phalanges (rig.py), so
# they end at the fingertip: the source's third phalanx tail.
LEAF_MH = {'HandThumb2': 'finger1-3%s____tail', 'HandIndex2': 'finger2-3%s____tail',
           'HandMiddle2': 'finger3-3%s____tail', 'ToeBase': 'toe3-3%s____tail'}
LEAF_SRC = {'HandThumb2': ('DEF-thumb.03', 'thumb_03'), 'HandIndex2': ('DEF-f_index.03', 'index_03'),
            'HandMiddle2': ('DEF-f_middle.03', 'middle_03'), 'ToeBase': ('DEF-toe', 'ball')}
SIDE_SUFFIX = {'Left': ('.L', '_l', '.L'), 'Right': ('.R', '_r', '.R')}

NOSE = Vector((0, 0.06, 0.10))  # from the head joint, rest pose, both rigs

HAND_PARTS = ('Hand', 'HandIndex1', 'HandIndex2', 'HandMiddle1', 'HandMiddle2',
              'HandThumb1', 'HandThumb2')


def side_of(name):
    return 'Left' if name.startswith('Left') else 'Right' if name.startswith('Right') else None


def src_name(our, lib):
    return MAP[our][lib]


def leaf_src(our, lib):
    s = side_of(our)
    base = LEAF_SRC[our[len(s):]][lib]
    return base + SIDE_SUFFIX[s][lib]


# ---------------------------------------------------------------------------
# Our rest pose (MakeHuman average adult), metres, floor at y = 0.

def our_rest():
    A = MH
    base = mhkit.load_obj(os.path.join(A, 'base.obj')).v
    w = mhkit.macro_weights()
    m = mhkit.morph(base, {s: mhkit.load_target(os.path.join(A, 'targets', s + '.target')) for s in w}, w)
    sk = mhkit.load_skel(os.path.join(A, 'default.mhskel'))
    floor = m[:, 1].min()
    shift = np.array([0, -floor, 0])
    heads = (rig.heads(sk, m) + shift) * 0.1
    tails = {}
    for n in rig.NAMES:
        s = side_of(n)
        if s and n[len(s):] in LEAF_MH:
            j = LEAF_MH[n[len(s):]] % SIDE_SUFFIX[s][2]
            tails[n] = (sk.joint(j, m) + shift) * 0.1
    return [Vector(h) for h in heads], {k: Vector(v) for k, v in tails.items()}


def our_dir(n, heads, tails):
    i = rig.INDEX[n]
    end = tails[n] if n in tails else heads[rig.INDEX[CHILD[n]]]
    return (end - heads[i]).normalized()


# ---------------------------------------------------------------------------
# Source import + sampling

def load_source(path):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    # glTF stores seconds; the importer converts with the scene rate, so at
    # 30 fps every UAL key (1/30 s apart) lands on an integer frame.
    bpy.context.scene.render.fps = FPS
    bpy.context.scene.render.fps_base = 1
    bpy.ops.import_scene.gltf(filepath=path)
    arm = next(o for o in bpy.context.scene.objects if o.type == 'ARMATURE')
    lib = 0 if 'DEF-hips' in arm.data.bones else 1
    return arm, lib


def world_rot(arm, mat):
    """Armature-space matrix -> glTF world rotation quaternion."""
    r = (arm.matrix_world @ mat).to_3x3().normalized()
    return (G @ r @ GT).to_quaternion()


def world_pos(arm, v):
    return G @ (arm.matrix_world @ v)


def source_rest(arm, lib):
    bones = arm.data.bones
    S0, head, tail = {}, {}, {}
    for b in bones:
        S0[b.name] = world_rot(arm, b.matrix_local)
        head[b.name] = world_pos(arm, b.head_local)
        tail[b.name] = world_pos(arm, b.tail_local)
    mesh = [o for o in bpy.context.scene.objects if o.type == 'MESH' and o.parent == arm]
    floor = min((G @ (o.matrix_world @ v.co)).y for o in mesh for v in o.data.vertices) if mesh else 0.0
    return S0, head, tail, floor


def source_dir(n, lib, head, tail):
    s = side_of(n)
    if s and n[len(s):] in LEAF_SRC:
        end = tail[leaf_src(n, lib)]
    elif n == 'Head':  # our HeadTop_End is MakeHuman's head tail; UAL's Head ends there too
        end = tail[src_name(n, lib)]
    else:
        end = head[src_name(CHILD[n], lib)]
    return (end - head[src_name(n, lib)]).normalized()


def check_source_frame(head, lib, our_heads):
    """Same side, same facing - by evidence, before trusting the bone map."""
    sl = head[src_name('LeftUpLeg', lib)].x
    ol = our_heads[rig.INDEX['LeftUpLeg']].x
    toe = head[src_name('LeftToeBase', lib)].z - head[src_name('LeftFoot', lib)].z
    assert sl > 0 and ol > 0, 'left legs on different sides: src %.3f ours %.3f' % (sl, ol)
    assert toe > 0, 'source does not face +Z'
    print('  frame check: src LeftUpLeg x=%.3f ours x=%.3f, src toes ahead of ankle by %.3f (+Z)' % (sl, ol, toe))


def frame_basis(d, sec):
    """Orthonormal basis (columns) from a primary axis and a secondary hint."""
    x = d.normalized()
    z = x.cross(sec).normalized()
    y = z.cross(x)
    return Matrix((x, y, z)).transposed()


def corrections(lib, S0head, S0tail, heads, tails):
    C = {}
    for n in rig.NAMES:
        if n not in MAP:
            continue
        dt = our_dir(n, heads, tails)
        ds = source_dir(n, lib, S0head, S0tail)
        if n == 'Hips':
            C[n] = Quaternion()
        elif side_of(n) and n[len(side_of(n)):] in HAND_PARTS:
            s = side_of(n)
            lat_t = heads[rig.INDEX[s + 'HandIndex1']] - heads[rig.INDEX[s + 'HandMiddle1']]
            lat_s = S0head[src_name(s + 'HandIndex1', lib)] - S0head[src_name(s + 'HandMiddle1', lib)]
            if n[len(s):].startswith('HandThumb'):
                # The thumb lies along the knuckle line; hint with the palm normal.
                lat_t = our_dir(s + 'Hand', heads, tails).cross(lat_t)
                lat_s = source_dir(s + 'Hand', lib, S0head, S0tail).cross(lat_s)
            Mt, Ms = frame_basis(dt, lat_t), frame_basis(ds, lat_s)
            C[n] = (Ms @ Mt.transposed()).to_quaternion()
            twist = C[n].rotation_difference(dt.rotation_difference(ds)).angle
            print('    %-18s swing %5.1f deg, extra twist vs minimal arc %5.1f deg' %
                  (n, math.degrees(dt.angle(ds)), math.degrees(twist)))
        else:
            C[n] = dt.rotation_difference(ds)
    return C


def reset_pose(arm):
    for pb in arm.pose.bones:
        pb.location = (0, 0, 0)
        pb.rotation_quaternion = (1, 0, 0, 0)
        pb.rotation_euler = (0, 0, 0)
        pb.scale = (1, 1, 1)


def assign(arm, act):
    reset_pose(arm)  # channels the new action does not key would keep stale values
    arm.animation_data.action = act
    if getattr(arm.animation_data, 'action_slot', None) is None and getattr(act, 'slots', None):
        arm.animation_data.action_slot = act.slots[0]


def sample(arm, lib, act, S0, S0head, C, rest, leg_scale, keep_src):
    """Retarget one action. Returns (rot frames, hips frames, src snapshots,
    src sole points per frame: [L heel, L ball, R heel, R ball])."""
    assign(arm, act)
    f0, f1 = int(round(act.frame_range[0])), int(round(act.frame_range[1]))
    hips_src = src_name('Hips', lib)
    hips0 = S0head[hips_src]
    order = [n for n in rig.NAMES]
    S0inv = {n: S0[src_name(n, lib)].inverted() for n in MAP}
    # Our Thumb2/Index2/Middle2 each stand for TWO source phalanges (02+03).
    # Following 02's rotation alone would ignore 03's curl - a fist would
    # close only half way - so these bones are swung onto the source's
    # animated chord 02 head -> 03 tail instead.
    chords = {n: (our_dir(n, *rest), src_name(n, lib), leaf_src(n, lib)) for n in rig.NAMES
              if side_of(n) and n[len(side_of(n)):] in ('HandThumb2', 'HandIndex2', 'HandMiddle2')}
    rots, hips, snaps = [], [], []
    # The source's sole points, same definition as sole_points(): what our
    # feet are locked to and judged against.
    soles = []  # (bone, rest head, offset of the sole point from that head)
    for sd in ('Left', 'Right'):
        foot, toe = arm.data.bones[src_name(sd + 'Foot', lib)], arm.data.bones[src_name(sd + 'ToeBase', lib)]
        ankle, ball = (world_pos(arm, v) for v in (foot.head_local, toe.head_local))
        fl = lambda v: Vector((v.x, SRC_FLOOR[0], v.z))
        soles += [(foot.name, ankle, fl(ankle) - ankle), (foot.name, ankle, fl(ball) - ankle)]
    src_lo, src_dirs = [], []
    # Where each mapped source segment points, per frame - the yardstick of
    # direction_report() (same head/end definition as source_dir()).
    ends = {}
    for n in MAP:
        s = side_of(n)
        if s and n[len(s):] in LEAF_SRC:
            ends[n] = (leaf_src(n, lib), 'tail')
        elif n == 'Head':
            ends[n] = (src_name(n, lib), 'tail')
        else:
            ends[n] = (src_name(CHILD[n], lib), 'head')
    for f in range(f0, f1 + 1):
        bpy.context.scene.frame_set(f)
        W = {}
        for n in order:
            if n in MAP:
                pb = arm.pose.bones[src_name(n, lib)]
                W[n] = world_rot(arm, pb.matrix) @ S0inv[n] @ C[n]
                if n in chords:
                    dt, a, b = chords[n]
                    chord = world_pos(arm, arm.pose.bones[b].tail) - world_pos(arm, arm.pose.bones[a].head)
                    W[n] = (W[n] @ dt).rotation_difference(chord) @ W[n]
            else:
                W[n] = W[rig.BONES[rig.INDEX[n]][1]]
        L = []
        for i, n in enumerate(order):
            p = rig.PARENT[i]
            q = W[n] if p < 0 else W[order[p]].inverted() @ W[n]
            L.append(q.normalized())
        rots.append(L)
        hp = world_pos(arm, arm.pose.bones[hips_src].head)
        hips.append((hp - hips0) * leg_scale)
        pts = []
        for n, h0, off in soles:
            pb = arm.pose.bones[n]
            d = world_rot(arm, pb.matrix) @ S0[n].inverted()
            pts.append(world_pos(arm, pb.head) + d @ off - Vector((0, SRC_FLOOR[0], 0)))
        src_lo.append(pts)
        dirs = {}
        for n, (e, which) in ends.items():
            h = world_pos(arm, arm.pose.bones[src_name(n, lib)].head)
            dirs[n] = (world_pos(arm, getattr(arm.pose.bones[e], which)) - h).normalized()
        src_dirs.append(dirs)
        if keep_src:
            snap = {pb.name: (world_pos(arm, pb.head), world_pos(arm, pb.tail)) for pb in arm.pose.bones}
            hb = arm.pose.bones[src_name('Head', lib)]
            # A nose marker shows facing: the rest-pose "in front of the face"
            # offset carried by the head's world delta.
            d = world_rot(arm, hb.matrix) @ S0[hb.name].inverted()
            snap['NOSE'] = snap[hb.name][0] + d @ NOSE
            snaps.append(snap)
    # Quaternion hemisphere continuity: q and -q are the same rotation, but a
    # runtime that slerps/nlerps between keys needs them on the same side.
    for b in range(len(order)):
        if rots[0][b].w < 0:
            rots[0][b].negate()
        for t in range(1, len(rots)):
            if rots[t][b].dot(rots[t - 1][b]) < 0:
                rots[t][b].negate()
    return rots, hips, snaps, src_lo, src_dirs


def same_pose(a, b, ha, hb):
    ang = max(x.rotation_difference(y).angle for x, y in zip(a, b))
    return ang < math.radians(0.5) and (ha - hb).length < 0.002


def r(x, d):
    v = round(x, d)
    return 0.0 if v == 0 else v


# ---------------------------------------------------------------------------
# FK of OUR rig from the output JSON (used by the proof and the foot check).

def our_fk(rest, rot, hips):
    heads, tails = rest
    P, W = [None] * len(rig.NAMES), [None] * len(rig.NAMES)
    for i, n in enumerate(rig.NAMES):
        q = Quaternion((rot[i][3], rot[i][0], rot[i][1], rot[i][2]))
        p = rig.PARENT[i]
        if p < 0:
            W[i] = q
            P[i] = heads[i] + Vector(hips)
        else:
            W[i] = W[p] @ q
            P[i] = P[p] + W[p] @ (heads[i] - heads[p])
    T = {n: P[rig.INDEX[n]] + W[rig.INDEX[n]] @ (tails[n] - heads[rig.INDEX[n]]) for n in tails}
    return P, W, T


def main():
    argv = sys.argv[sys.argv.index('--') + 1:]
    out = argv[0]
    opt = {argv[i]: argv[i + 1] for i in range(1, len(argv) - 1) if argv[i].startswith('--')}
    sp = os.path.dirname(os.path.abspath(out))
    ual1 = opt.get('--ual1', os.path.join(sp, 'ual', 'universal_animation_librarystandard', 'Animation Library[Standard]',
                                          'Godot', 'AnimationLibrary_Godot_Standard.glb'))
    ual2 = opt.get('--ual2', os.path.join(sp, 'ual', 'universal_animation_library_2standard',
                                          'Universal Animation Library 2 [Standard]', 'Unreal-Godot', 'UAL2_Standard.glb'))
    preview = opt.get('--preview')
    global MH
    MH = opt.get('--mh')
    if not MH or not os.path.isdir(MH):
        sys.exit('pass --mh <dir with base.obj, targets/, default.mhskel>')

    heads, tails = our_rest()
    our_leg = heads[rig.INDEX['LeftUpLeg']].y
    hips_height = heads[0].y
    print('ours: hips %.4f m, hip joint %.4f m above the floor' % (hips_height, our_leg))

    clips, src_snaps, src_rests, src_soles, locks, scales, src_dirs = [], {}, {}, {}, {}, {}, {}
    for path in (ual1, ual2):
        arm, lib = load_source(path)
        S0, S0head, S0tail, floor = source_rest(arm, lib)
        print('%s: lib UAL%d, floor y=%.4f' % (os.path.basename(path), lib + 1, floor))
        SRC_FLOOR[0] = floor
        check_source_frame(S0head, lib, heads)
        src_leg = S0head[src_name('LeftUpLeg', lib)].y - floor
        leg_scale = our_leg / src_leg
        print('  hip joint %.4f m -> leg scale %.4f' % (src_leg, leg_scale))
        C = corrections(lib, S0head, S0tail, heads, tails)
        for n in ('Hips', 'Spine', 'Spine2', 'LeftShoulder', 'LeftArm', 'LeftForeArm', 'LeftUpLeg', 'LeftFoot'):
            print('    C %-14s %5.1f deg' % (n, math.degrees(C[n].angle)))
        src_rests[lib] = (S0head, S0tail, floor)
        for act in sorted(bpy.data.actions, key=lambda a: a.name):
            if act.name == 'A_TPose':
                continue
            keep = preview is not None and act.name in PROOF
            rots, hips, snaps, src_soles[act.name], src_dirs[act.name] = sample(arm, lib, act, S0, S0head, C, (heads, tails), leg_scale, keep)
            lock = floor_lock((heads, tails), rots, hips, src_soles[act.name], leg_scale)
            locks[act.name] = lock
            scales[act.name] = leg_scale
            loop = 'Loop' in act.name
            dropped = False
            if loop and len(rots) > 2 and same_pose(rots[0], rots[-1], hips[0], hips[-1]):
                rots, hips, snaps = rots[:-1], hips[:-1], snaps[:-1]
                src_soles[act.name] = src_soles[act.name][:-1]
                src_dirs[act.name] = src_dirs[act.name][:-1]
                locks[act.name] = lock[:-1]
                dropped = True
            clips.append({'name': act.name, 'loop': loop, 'frames': len(rots),
                          'rot': [[[r(q.x, 4), r(q.y, 4), r(q.z, 4), r(q.w, 4)] for q in fr] for fr in rots],
                          'hips': [[r(h.x, 4), r(h.y, 4), r(h.z, 4)] for h in hips]})
            if keep:
                src_snaps[act.name] = (lib, snaps)
            print('  %-24s %3d frames%s' % (act.name, len(rots), ' (dup end dropped)' if dropped else ''))

    names = [c['name'] for c in clips]
    assert len(names) == len(set(names)), 'clip name collision between UAL1 and UAL2'
    doc = {'source': 'Quaternius Universal Animation Library 1+2 (CC0)', 'fps': FPS,
           'bones': list(rig.NAMES), 'hipsHeight': r(hips_height, 4), 'clips': clips}
    with open(out, 'w') as fh:
        json.dump(doc, fh, separators=(',', ':'))
    print('wrote %s: %d clips, %d bytes' % (out, len(clips), os.path.getsize(out)))

    with open(out) as fh:
        doc = json.load(fh)  # the proof is driven by the FILE, not by memory
    foot_report(doc, (heads, tails), src_soles, locks, scales)
    direction_report(doc, (heads, tails), src_dirs)
    if preview:
        render_proof(doc, (heads, tails), src_snaps, preview, opt['--only'].split(',') if '--only' in opt else None)


def floor_lock(rest, rots, hips, src_soles, leg_scale):
    """Per frame, lift/drop our hips so our lowest sole point is exactly as
    high as the source's (scaled). Scaling the hips path alone cannot keep
    feet planted: our legs are 3.4% shorter than UAL's but our ankles sit
    at a different height, MakeHuman's splayed A-pose legs straighten onto
    UAL's vertical ones, and the instep angles differ - so a crouch floated
    6 cm and a sprint 10 cm before this. Locking the LOWEST sole (whichever
    foot is planted) is continuous (a min of continuous curves), exact on
    contact and keeps the source's clearance in the air. Returns the
    correction per frame for the report. Modifies hips in place."""
    out = []
    for t in range(len(rots)):
        rot = [[q.x, q.y, q.z, q.w] for q in rots[t]]
        ours = min(p.y for p in sole_points(rest, rot, hips[t]))
        want = min(p.y for p in src_soles[t]) * leg_scale
        hips[t].y += want - ours
        out.append(want - ours)
    return out


def sole_points(rest, rot, hips):
    """[L heel, L ball, R heel, R ball]: rest-pose points ON the floor under
    the ankle and under the ToeBase joint, carried rigidly by Foot. Toe tips
    are left out: UAL's own toes dip up to 7 cm through the floor on toe-off
    swings (Walk_Loop), which says nothing about the retarget."""
    heads, tails = rest
    P, W, T = our_fk(rest, rot, hips)
    out = []
    for s in ('Left', 'Right'):
        f, t = rig.INDEX[s + 'Foot'], rig.INDEX[s + 'ToeBase']
        for pt in (heads[f], heads[t]):
            out.append(P[f] + W[f] @ (Vector((pt.x, 0.0, pt.z)) - heads[f]))
    return out


def direction_report(doc, rest, src_dirs):
    """Every mapped bone of OURS (FK from the written file, so rounding and
    the q/-q flips are included) must point where its source segment points.
    Prints the worst angle per clip and overall."""
    heads, tails = rest
    dt = {n: our_dir(n, heads, tails) for n in MAP}
    worst = (0.0, '', '')
    for c in doc['clips']:
        cw = (0.0, '')
        for t in range(c['frames']):
            P, W, T = our_fk(rest, c['rot'][t], c['hips'][t])
            for n, d in src_dirs[c['name']][t].items():
                if n == 'Hips':  # C = identity on purpose: copies the pelvis ROTATION, not a direction
                    continue
                a = math.degrees((W[rig.INDEX[n]] @ dt[n]).angle(d, 0.0))
                cw = max(cw, (a, n))
        worst = max(worst, (cw[0], c['name'], cw[1]))
    print('bone directions vs source: worst %.3f deg (%s, %s)' % worst)


def foot_report(doc, rest, src_soles, locks, scales):
    """Two numbers per clip, from the WRITTEN file:
    lock - the vertical hips correction floor_lock() needed (how far the
           plain scaled retarget would have sunk/floated), max |.|;
    sole - lowest sole, ours minus source*scale, after rounding (should be ~0);
    slide - where the SOURCE has a sole point planted (on the floor and
           still), how fast OUR same point moves horizontally, mm/frame,
           next to the source's own."""
    print('feet: lock max|m|, sole err max|m|, slide ours/src mm/frame')
    worst_sole, worst_slide = (0.0, ''), (0.0, '')
    for c in doc['clips']:
        ss = src_soles[c['name']]
        ours = [sole_points(rest, c['rot'][t], c['hips'][t]) for t in range(c['frames'])]
        err = max(abs(min(p.y for p in ours[t]) - min(p.y for p in ss[t]) * scales[c['name']])
                  for t in range(c['frames']))
        sl_o, sl_s, n = 0.0, 0.0, 0
        for t in range(c['frames'] - 1):
            for k in range(4):
                a, b = ss[t][k], ss[t + 1][k]
                if a.y < 0.01 and b.y < 0.01 and math.hypot(b.x - a.x, b.z - a.z) < 0.004:
                    oa, ob = ours[t][k], ours[t + 1][k]
                    sl_o += math.hypot(ob.x - oa.x, ob.z - oa.z)
                    sl_s += math.hypot(b.x - a.x, b.z - a.z)
                    n += 1
        so = 1000 * sl_o / n if n else 0.0
        worst_sole = max(worst_sole, (err, c['name']))
        worst_slide = max(worst_slide, (so, c['name']))
        print('  %-24s lock %.3f  sole %.4f  slide %4.1f / %4.1f (%d)' % (
            c['name'], max(abs(x) for x in locks[c['name']]), err, so, 1000 * sl_s / n if n else 0.0, n))
    print('worst sole err %.4f m (%s), worst slide %.1f mm/frame (%s)' % (worst_sole + worst_slide))


# ---------------------------------------------------------------------------
# Visual proof: contact sheets, SOURCE (left, darker) next to OURS (right,
# driven by the written JSON) at the same frame, front view on the top row
# and the right-hand side view (figures turned 90 deg) below. Capsules, with
# the character's LEFT limbs blue and RIGHT limbs red on both, so a swapped
# side is a colour mismatch; a nose ball shows facing. Orthographic, with the
# floor drawn as a line, so sinking/floating feet read directly.

COLORS = {'C': (0.85, 0.85, 0.82), 'L': (0.2, 0.45, 1.0), 'R': (1.0, 0.25, 0.2),
          'N': (1.0, 0.8, 0.1), 'F': (0.1, 0.1, 0.1), 'T': (1.0, 1.0, 1.0)}
B4 = GT.to_4x4()  # glTF -> Blender


def side_key(name):
    n = name.lower()
    if name.startswith('Left') or n.endswith('.l') or n.endswith('_l'):
        return 'L'
    if name.startswith('Right') or n.endswith('.r') or n.endswith('_r'):
        return 'R'
    return 'C'


def radius_for(name):
    n = name.lower()
    if any(k in n for k in ('thumb', 'index', 'middle', 'ring', 'pinky', 'f_')):
        return 0.009
    if 'head' in n and 'top' not in n:
        return 0.07
    if any(k in n for k in ('hips', 'pelvis', 'spine')):
        return 0.06
    if 'hand' in n:
        return 0.016
    if 'neck' in n:
        return 0.035
    return 0.035


def our_segments(rest, rot, hips, only=None):
    heads, tails = rest
    P, W, T = our_fk(rest, rot, hips)
    segs = []
    for i, n in enumerate(rig.NAMES):
        if only and not only(n):
            continue
        if n in T:
            end = T[n]
        elif n in CHILD:
            end = P[rig.INDEX[CHILD[n]]]
        else:
            continue
        segs.append((P[i], end, radius_for(n), side_key(n)))
    h = rig.INDEX['Head']
    if not only:
        segs.append((P[h] + W[h] @ NOSE, None, 0.025, 'N'))
    return segs, P


def src_segments(snap, only=None):
    segs = []
    for name, ht in snap.items():
        if name in ('root', 'NOSE') or 'leaf' in name or (only and not only(name)):
            continue
        segs.append((ht[0], ht[1], radius_for(name), side_key(name)))
    if not only:
        segs.append((snap['NOSE'], None, 0.025, 'N'))
    return segs


def build_mesh(name, segs, mats):
    import bmesh
    bm = bmesh.new()
    for a, b, rad, key in segs:
        mi = list(COLORS).index(key)
        made = []
        if b is not None and (b - a).length > 1e-5:
            d = b - a
            m = Matrix.Translation((a + b) / 2) @ d.to_track_quat('Z', 'Y').to_matrix().to_4x4()
            made += bmesh.ops.create_cone(bm, cap_ends=True, segments=10, radius1=rad, radius2=rad * 0.8,
                                          depth=d.length, matrix=m)['verts']
            made += bmesh.ops.create_uvsphere(bm, u_segments=10, v_segments=6, radius=rad * 0.8,
                                              matrix=Matrix.Translation(b))['verts']
        made += bmesh.ops.create_uvsphere(bm, u_segments=10, v_segments=6, radius=rad,
                                          matrix=Matrix.Translation(a))['verts']
        for f in {f for v in made for f in v.link_faces}:
            f.material_index = mi
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    for m in mats:
        me.materials.append(m)
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    return ob


def add_text(body, pos, size):
    cu = bpy.data.curves.new('t', 'FONT')
    cu.body = body
    cu.size = size
    ob = bpy.data.objects.new('t', cu)
    ob.matrix_world = B4 @ Matrix.Translation(pos)
    ob.data.materials.append(bpy.data.materials['mT'])
    bpy.context.scene.collection.objects.link(ob)


def proof_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    for eng in ('BLENDER_EEVEE', 'BLENDER_EEVEE_NEXT'):
        try:
            sc.render.engine = eng
            break
        except TypeError:
            pass
    sc.view_settings.view_transform = 'Standard'
    w = bpy.data.worlds.new('w')
    w.color = (0.55, 0.57, 0.6)
    sc.world = w
    mats = []
    for k, c in COLORS.items():
        for dark in (False, True):
            m = bpy.data.materials.new('m' + k + ('d' if dark else ''))
            col = tuple(x * (0.45 if dark else 1.0) for x in c) + (1,)
            m.diffuse_color = col
            m.use_nodes = True
            bsdf = m.node_tree.nodes.get('Principled BSDF')
            bsdf.inputs['Base Color'].default_value = col
            bsdf.inputs['Roughness'].default_value = 0.7
            mats.append(m)
    sun = bpy.data.objects.new('sun', bpy.data.lights.new('sun', 'SUN'))
    sun.data.energy = 3.5
    sun.matrix_world = B4 @ Matrix.Rotation(math.radians(-35), 4, 'X') @ Matrix.Rotation(math.radians(25), 4, 'Y')
    sc.collection.objects.link(sun)
    return ([bpy.data.materials['m' + k] for k in COLORS],
            [bpy.data.materials['m' + k + 'd'] for k in COLORS])


def clear_scene():
    for ob in list(bpy.data.objects):
        if ob.type != 'LIGHT':
            bpy.data.objects.remove(ob)
    for me in list(bpy.data.meshes):
        bpy.data.meshes.remove(me)
    for cu in list(bpy.data.curves):
        bpy.data.curves.remove(cu)


def render_sheet(path, title, cells, colw, rowh, ppm, sep, labsize, floor=True, views=None, pivot=0.0):
    """cells: [(label, src_segs, our_segs)], already centred on x=z=0."""
    light, dark = MATS
    clear_scene()
    views = views or [('front', 'Y', 0.0), ('right side', 'Y', 90.0)]
    for k, (label, ssegs, osegs) in enumerate(cells):
        cx = k * colw
        for row, (vname, axis, ang) in enumerate(views):
            fy = -row * rowh
            R = Matrix.Rotation(math.radians(ang), 4, axis)
            for segs, dx, mats, tag in ((ssegs, -sep, dark, 's'), (osegs, sep, light, 'o')):
                ob = build_mesh('%s%d%d' % (tag, k, row), segs, mats)
                ob.matrix_world = B4 @ Matrix.Translation((cx + dx, fy + pivot, 0)) @ R
            if floor:
                fl = build_mesh('floor', [(Vector((-colw * 0.45, 0, 0)), Vector((colw * 0.45, 0, 0)),
                                           labsize * 0.04, 'F')], light)
                fl.matrix_world = B4 @ Matrix.Translation((cx, fy, 0))
        add_text(label, Vector((cx - colw * 0.45, -labsize * 1.4, 0.5)), labsize)
    add_text(title + '   (left: UAL source, dark | right: ours from JSON; blue = LEFT, red = RIGHT; '
             'rows: ' + ', '.join(v[0] for v in views) + ')', Vector((-colw * 0.45, rowh * 0.92, 0.5)), labsize)
    sc = bpy.context.scene
    cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam'))
    cam.data.type = 'ORTHO'
    W = len(cells) * colw
    H = len(views) * rowh + 2 * labsize  # row 0 top (title) .. last row floor plus a margin
    cam.data.ortho_scale = max(W, H)
    cam.matrix_world = B4 @ Matrix.Translation((W / 2 - colw / 2, rowh - H / 2, 10))
    cam.data.clip_end = 50
    sc.collection.objects.link(cam)
    sc.camera = cam
    sc.render.resolution_x = int(W * ppm)
    sc.render.resolution_y = int(H * ppm)
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)
    print('proof:', path)


MATS = None


def render_proof(doc, rest, src_snaps, outdir, only=None):
    """only: optional sheet names to render (clip names, or '<clip>_hand')."""
    global MATS
    os.makedirs(outdir, exist_ok=True)
    MATS = proof_scene()
    by = {c['name']: c for c in doc['clips']}
    for name in PROOF:
        if name not in src_snaps or (only and name not in only):
            continue
        lib, snaps = src_snaps[name]
        c = by[name]
        n = 6 if c['frames'] > 60 else 5
        idx = sorted({int(round(i * (c['frames'] - 1) / (n - 1))) for i in range(n)})
        cells = []
        for t in idx:
            osegs, P = our_segments(rest, c['rot'][t], c['hips'][t])
            ssegs = src_segments(snaps[t])
            cells.append(('f%d' % t, centre(ssegs, snaps[t][src_name('Hips', lib)][0]), centre(osegs, P[0])))
        render_sheet(os.path.join(outdir, name + '.png'), name, cells, 2.6, 2.4, 110, 0.55, 0.13)
    # Hand close-ups: a fist and a pistol grip must read as a grip.
    for name, side in (('Punch_Cross', 'Right'), ('Pistol_Idle_Loop', 'Right'), ('Punch_Cross', 'Left')):
        if name not in src_snaps or (only and name + '_hand' not in only):
            continue
        lib, snaps = src_snaps[name]
        c = by[name]
        sfx = SIDE_SUFFIX[side]
        ours_only = lambda n: n.startswith(side) and 'Hand' in n
        src_only = lambda n: n.lower().endswith(sfx[lib].lower()) and any(
            k in n.lower() for k in ('hand', 'thumb', 'f_', 'index', 'middle', 'ring', 'pinky'))
        idx = sorted({int(round(i * (c['frames'] - 1) / 3)) for i in range(4)})
        cells = []
        for t in idx:
            osegs, P = our_segments(rest, c['rot'][t], c['hips'][t], ours_only)
            ssegs = src_segments(snaps[t], src_only)
            hs = snaps[t][src_name(side + 'Hand', lib)][0]
            cells.append(('f%d' % t, centre(ssegs, hs, 0.0), centre(osegs, P[rig.INDEX[side + 'Hand']], 0.0)))
        render_sheet(os.path.join(outdir, '%s_hand%s.png' % (name, side[0])), '%s %s hand' % (name, side),
                     cells, 0.5, 0.3, 1000, 0.11, 0.016, floor=False,
                     views=[('front', 'Y', 0.0), ('right side', 'Y', 90.0), ('top', 'X', 90.0)], pivot=0.13)


def centre(segs, c, lift=None):
    """Move a figure so its hips sit at x=z=0, or (lift given) so a hand
    joint sits at (0, lift, 0) for the close-ups."""
    off = Vector((c.x, 0.0 if lift is None else c.y - lift, c.z))
    return [(a - off, None if b is None else b - off, r_, k) for a, b, r_, k in segs]


PROOF = ['Idle_Loop', 'Walk_Loop', 'Jog_Fwd_Loop', 'Sprint_Loop', 'Jump_Start', 'Punch_Cross',
         'Pistol_Idle_Loop', 'Sitting_Idle_Loop', 'Zombie_Walk_Fwd_Loop', 'Sword_Regular_Combo']

if __name__ == '__main__':
    main()
