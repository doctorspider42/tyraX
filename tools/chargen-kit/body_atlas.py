"""Pack retained UV islands and freshly unwrapped skin into a single PS2 atlas."""

import io
import numpy as np
from PIL import Image
from scipy.ndimage import distance_transform_edt, map_coordinates
import xatlas

from studio_gear import bindings


def unwrap(pos, faces, retained_uv):
    # Keep the face's existing paint parametrization. Unwrap the body on its
    # welded geometry, then pack both sets of islands together at game scale.
    atlas = xatlas.Atlas()
    used, tri = np.unique(faces, return_inverse=True)
    atlas.add_mesh(pos[used], tri.reshape(-1, 3).astype("uint32"))
    opt = xatlas.PackOptions()
    opt.resolution = 512
    opt.padding = 3
    atlas.generate(pack_options=opt)
    _, indices, uv = atlas[0]
    pack = xatlas.Atlas()
    for corners in (retained_uv, uv[indices] * 0.76):
        coords, indices = np.unique(corners.reshape(-1, 2), axis=0, return_inverse=True)
        pack.add_uv_mesh(coords, indices.reshape(-1, 3).astype("uint32"))
    pack.generate(pack_options=opt)
    result = []
    for mesh in (0, 1):
        _, indices, uv = pack[mesh]
        result.append(uv[indices])
    return np.concatenate(result)


def paint_map(uv, out_pos, source_uv, keep, source_pos, source_tri, skin):
    # Resolve the donor UV independently for each texel. Interpolating three
    # old UVs across a new face would read through unrelated islands at seams.
    size = 512
    hit = np.full((size, size), -1, int)
    bary = np.zeros((size, size, 3))
    xy = uv.copy()
    xy[:, :, 1] = 1 - xy[:, :, 1]
    for i, (a, b, c) in enumerate(xy * size - 0.5):
        u, v = b - a, c - a
        det = u[0] * v[1] - v[0] * u[1]
        if abs(det) < 1e-7:
            continue
        l, t = np.maximum(0, np.ceil(np.minimum(np.minimum(a, b), c)).astype(int))
        r, bt = np.minimum(
            size, np.floor(np.maximum(np.maximum(a, b), c)).astype(int) + 1
        )
        yy, xx = np.mgrid[t:bt, l:r]
        dx, dy = xx - a[0], yy - a[1]
        wb = (dx * v[1] - dy * v[0]) / det
        wc = (u[0] * dy - u[1] * dx) / det
        w = np.stack([1 - wb - wc, wb, wc], axis=-1)
        inside = w.min(2) >= -1e-5
        hit[t:bt, l:r][inside] = i
        bary[t:bt, l:r][inside] = w[inside]
    gather = np.zeros((size, size, 2))
    retained_uv = source_uv[keep]
    retained = (hit >= 0) & (hit < len(retained_uv))
    gather[retained] = np.einsum(
        "ij,ijk->ik", bary[retained], retained_uv[hit[retained]]
    )
    authored = hit >= len(retained_uv)
    points = np.einsum("ij,ijk->ik", bary[authored], out_pos[hit[authored]])
    donor, weights = bindings(points, source_pos, source_tri, skin)
    gather[authored] = np.einsum("ij,ijk->ik", weights, source_uv[donor])
    iy, ix = distance_transform_edt(
        hit < 0, return_distances=False, return_indices=True
    )
    gather = gather[iy, ix]
    return np.stack(((1 - gather[:, :, 1]) * size - 0.5, gather[:, :, 0] * size - 0.5))


def transfer_layers(chunks, prefix, mapping):
    for key in list(chunks):
        start = "img/" + prefix
        if not key.startswith(start):
            continue
        layer = key[len(start) :].split("/")[0]
        if layer not in ("skin", "eye", "iris", "class", "ao", "mask", "brow", "lash"):
            continue
        source = Image.open(io.BytesIO(chunks[key][1]))
        data = np.array(source)
        if data.ndim == 2:
            data = data[:, :, None]
        out = np.stack(
            [
                map_coordinates(
                    channel, mapping, order=0 if layer == "class" else 1, mode="nearest"
                )
                for channel in data.transpose(2, 0, 1)
            ],
            axis=-1,
        )
        img = Image.fromarray(out.squeeze())
        buf = io.BytesIO()
        img.save(buf, source.format, quality=96)
        payload = buf.getvalue()
        chunks[key] = (4, payload, len(payload))
