"""The generated characters' rig: 35 Mixamo-named bones over MakeHuman's 163,
plus 5 face bones (jaw, eyes, upper lids) that the game animates.

Each bone takes its HEAD from a MakeHuman joint (a cube of base-mesh vertices,
so the skeleton follows every morph for free) and absorbs the skin weights of
the MakeHuman bones listed in `mh` - plus, by the ancestor walk in
`collapse_map`, every MakeHuman bone below those that is not claimed itself.

Fingers are cut down to what an animation can actually show at this budget:
the thumb and the index finger get two bones each, and the middle, ring and
little fingers SHARE two ("Middle" drives all three), which is enough for a
fist, a grip, a pointing hand and a relaxed hand.
"""

L, R = '.L', '.R'

# (name, parent, head joint, [MakeHuman bones whose weights fold in here])
BONES = [
    ('Hips', None, 'spine05____head', ['spine05', 'root', 'pelvis.L', 'pelvis.R']),
    ('Spine', 'Hips', 'spine04____head', ['spine04']),
    ('Spine1', 'Spine', 'spine03____head', ['spine03']),
    ('Spine2', 'Spine1', 'spine02____head', ['spine02', 'spine01', 'breast.L', 'breast.R']),
    ('Neck', 'Spine2', 'neck01____head', ['neck01', 'neck02', 'neck03']),
    ('Head', 'Neck', 'head____head', ['head']),
    ('HeadTop_End', 'Head', 'head____tail', []),
]
for side, s in (('Left', L), ('Right', R)):
    BONES += [
        (side + 'Shoulder', 'Spine2', 'clavicle' + s + '____head', ['clavicle' + s, 'shoulder01' + s]),
        (side + 'Arm', side + 'Shoulder', 'upperarm01' + s + '____head', ['upperarm01' + s, 'upperarm02' + s]),
        (side + 'ForeArm', side + 'Arm', 'lowerarm01' + s + '____head', ['lowerarm01' + s, 'lowerarm02' + s]),
        (side + 'Hand', side + 'ForeArm', 'wrist' + s + '____head',
         ['wrist' + s] + ['metacarpal%d%s' % (i, s) for i in range(1, 5)]),
        (side + 'HandThumb1', side + 'Hand', 'finger1-1' + s + '____head', ['finger1-1' + s]),
        (side + 'HandThumb2', side + 'HandThumb1', 'finger1-2' + s + '____head', ['finger1-2' + s, 'finger1-3' + s]),
        (side + 'HandIndex1', side + 'Hand', 'finger2-1' + s + '____head', ['finger2-1' + s]),
        (side + 'HandIndex2', side + 'HandIndex1', 'finger2-2' + s + '____head', ['finger2-2' + s, 'finger2-3' + s]),
        (side + 'HandMiddle1', side + 'Hand', 'finger3-1' + s + '____head',
         ['finger3-1' + s, 'finger4-1' + s, 'finger5-1' + s]),
        (side + 'HandMiddle2', side + 'HandMiddle1', 'finger3-2' + s + '____head',
         ['finger3-2' + s, 'finger3-3' + s, 'finger4-2' + s, 'finger4-3' + s, 'finger5-2' + s, 'finger5-3' + s]),
    ]
for side, s in (('Left', L), ('Right', R)):
    BONES += [
        (side + 'UpLeg', 'Hips', 'upperleg01' + s + '____head', ['upperleg01' + s, 'upperleg02' + s]),
        (side + 'Leg', side + 'UpLeg', 'lowerleg01' + s + '____head', ['lowerleg01' + s, 'lowerleg02' + s]),
        (side + 'Foot', side + 'Leg', 'foot' + s + '____head', ['foot' + s]),
        # The ball of the foot: every toe chain folds in here.
        (side + 'ToeBase', side + 'Foot', 'foot' + s + '____tail',
         ['toe%d-%d%s' % (t, k, s) for t in range(1, 6) for k in range(1, 4)]),
    ]

# The face, APPENDED so every bone above keeps its index (anims.json and every
# stored clip are indexed by bone; build_kit.py pads clips made before these
# existed with the bind pose). No clip animates them: the game drives them -
# blinks, eyes that follow the player, a jaw that talks (docs/character-
# generator.md, "A living face"). The lids pivot on the EYE's centre, so a
# closing lid slides over the eyeball instead of swinging through it.
BONES += [
    ('Jaw', 'Head', 'jaw____head', ['jaw']),  # the lower lip and tongue hang below
    ('LeftEye', 'Head', 'eye.L____head', ['eye.L']),
    ('RightEye', 'Head', 'eye.R____head', ['eye.R']),
    ('LeftEyelid', 'Head', 'eye.L____head', ['orbicularis03.L']),
    ('RightEyelid', 'Head', 'eye.R____head', ['orbicularis03.R']),
]
FACE = ['Jaw', 'LeftEye', 'RightEye', 'LeftEyelid', 'RightEyelid']

NAMES = [b[0] for b in BONES]
INDEX = {n: i for i, n in enumerate(NAMES)}
PARENT = [INDEX[b[1]] if b[1] else -1 for b in BONES]
PREFIX = 'mixamorig:'


def collapse_map(mhskel_bones):
    """MakeHuman bone -> our bone index, by explicit claim then ancestor walk."""
    claim = {}
    for i, b in enumerate(BONES):
        for m in b[3]:
            claim[m] = i
    out = {}
    for m in mhskel_bones:
        cur = m
        while cur is not None and cur not in claim:
            cur = mhskel_bones[cur]['parent']
        out[m] = claim[cur] if cur is not None else 0
    return out


def heads(skel, base):
    import numpy as np
    return np.array([skel.joint(b[2], base) for b in BONES])
