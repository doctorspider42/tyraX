"""Refine the jaw, cheek, clavicle and shoulder loops with shared edge stencils."""

from collections import defaultdict
import numpy as np


def refine(chunks, prefix, level):
    def read(name, kind, cols=1):
        return np.frombuffer(chunks[prefix + name][1], kind).reshape(-1, cols)

    def store(name, data):
        data = np.ascontiguousarray(data)
        chunks[prefix + name] = (
            {"f": 0, "i": 1, "u": 2}[data.dtype.kind],
            data.tobytes(),
            data.size,
        )

    pos = read("mesh/pos", "<f4", 3)
    tri = read("mesh/tri", "<i4", 3)
    parts = read("mesh/part", "u1").ravel()
    uv = read("mesh/uv", "<f4", 6).reshape(-1, 3, 2)
    neighbors = defaultdict(list)
    for face in tri:
        for i in range(3):
            edge = tuple(sorted((face[i], face[(i + 1) % 3])))
            neighbors[edge].append(face[(i + 2) % 3])
    indices = {}
    stencils = []
    for edge, opposite in neighbors.items():
        a, b = edge
        x, y, z = (pos[a] + pos[b]) * 0.5
        on_face = y > 0.637 and z > 0.055
        on_shoulder = 0.42 < y < 0.63 and abs(x) < 0.226
        threshold = {"hero": 0.030, "standard": 0.027, "crowd": 0.053}[level]
        if not (on_face or on_shoulder) or np.linalg.norm(pos[a] - pos[b]) < threshold:
            continue
        indices[edge] = len(pos) + len(stencils)
        # A smooth Loop midpoint away from open boundaries. Each attribute,
        # including every sparse morph, uses the very same interpolation.
        c, d = opposite if len(opposite) == 2 else edge
        stencils.append((a, b, c, d))
    if not stencils:
        return
    parents = np.asarray(stencils)
    coeff = np.array([0.375, 0.375, 0.125, 0.125], np.float32)

    def extend(values):
        extra = np.einsum("ijk,j->ik", values[parents], coeff)
        return np.concatenate([values, extra])

    faces = []
    coords = []
    labels = []
    for ids, tex, part in zip(tri, uv, parts):
        mids = [
            indices.get(tuple(sorted((ids[i], ids[(i + 1) % 3])))) for i in range(3)
        ]
        count = sum(v is not None for v in mids)
        ids = list(ids) + mids
        local = np.vstack([tex, (tex + np.roll(tex, -1, axis=0)) * 0.5])
        if count == 0:
            pattern = [(0, 1, 2)]
        elif count == 3:
            pattern = [(0, 3, 5), (3, 1, 4), (5, 4, 2), (3, 4, 5)]
        elif count == 1:
            i = next(k for k, v in enumerate(mids) if v is not None)
            j, k = (i + 1) % 3, (i + 2) % 3
            pattern = [(i, 3 + i, k), (3 + i, j, k)]
        else:
            i = mids.index(None)
            j, k = (i + 1) % 3, (i + 2) % 3
            pattern = [(k, 3 + k, 3 + j), (i, j, 3 + j), (i, 3 + j, 3 + k)]
        for corners in pattern:
            faces.append([ids[k] for k in corners])
            coords.append(local[list(corners)])
            labels.append(part)
    joints = read("mesh/joints", "u1", 4)
    weights = read("mesh/weights", "u1", 4)
    full = np.zeros((len(pos), int(joints.max()) + 1), np.float32)
    for j, w in zip(joints.T, weights.T):
        np.add.at(full, (np.arange(len(pos)), j), w)
    extra = extend(full)[len(pos) :]
    top = np.argsort(-extra, axis=1)[:, :4]
    mass = np.take_along_axis(extra, top, axis=1)
    quant = np.rint(mass * 255 / mass.sum(axis=1, keepdims=True)).astype(int)
    quant[:, 0] += 255 - quant.sum(axis=1)
    store("mesh/joints", np.vstack((joints, top)).astype("u1"))
    store("mesh/weights", np.vstack((weights, quant)).astype("u1"))
    store("mesh/pos", extend(pos))
    store("mesh/tri", np.array(faces, "<i4"))
    store("mesh/uv", np.array(coords, "<f4"))
    store("mesh/part", np.array(labels, "u1"))

    for key in list(chunks):
        if not (key.startswith(prefix + "t/") and key.endswith("/idx")):
            continue
        base = key[len(prefix) : -3]
        idx = read(base + "idx", "<i4").ravel()
        delta = read(base + "d", "<i2", 3)
        full = np.zeros_like(pos)
        full[idx] = delta
        extra = np.rint(extend(full)[len(pos) :]).astype("<i2")
        changed = np.flatnonzero(np.any(extra != 0, axis=1))
        store(base + "idx", np.r_[idx, len(pos) + changed].astype("<i4"))
        chunks[prefix + base + "d"] = (
            3,
            np.vstack((delta, extra[changed])).tobytes(),
            3 * (len(idx) + len(changed)),
        )
