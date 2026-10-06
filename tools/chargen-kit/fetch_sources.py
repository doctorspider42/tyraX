"""Downloads every CC0 source the Character Generator kit is built from.

    python fetch_sources.py <sources_dir>

Nothing here is needed to BUILD or RUN the editor - the kit it produces is
committed (resources/chargen-kit.bin). This is for regenerating that kit.

What it fetches, all CC0 1.0:
  mh-data/     MakeHuman's reference mesh, rig, weights and macro targets
               (github.com/makehumancommunity/makehuman, makehuman/data -
               the DATA directory; the AGPL program is not used)
  targets/     the MakeHuman detail targets the kit's sliders and face-paint
               masks use (same repository)
  sys/         MakeHuman "system assets" pack (proxies, eyes, brows, lashes,
               skins, system clothes and hair)
  packs/<n>/   MakeHuman community asset packs: only the *_cc0 builds, named
               by catalog.py
  ual/         Quaternius' Universal Animation Library 1 + 2 (Standard)

The MakeHuman asset packs are served from files2.makehumancommunity.org, which
throttles each connection hard; they are fetched in parallel byte ranges.
"""
import concurrent.futures as cf
import os
import sys
import urllib.request
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from catalog import CATALOG  # noqa: E402
import mhkit  # noqa: E402

MH_RAW = 'https://raw.githubusercontent.com/makehumancommunity/makehuman/master/makehuman/data'
PACK = 'https://files2.makehumancommunity.org/asset_packs/{0}/{0}_cc0.zip'
UAL = ['https://opengameart.org/sites/default/files/universal_animation_librarystandard.zip',
       'https://opengameart.org/sites/default/files/universal_animation_library_2standard.zip']
CHUNK = 4 * 1024 * 1024


def get(url, path):
    if os.path.exists(path) and os.path.getsize(path) > 0:
        return
    os.makedirs(os.path.dirname(path), exist_ok=True)
    for attempt in range(5):
        try:
            data = urllib.request.urlopen(url, timeout=120).read()
            with open(path + '.part', 'wb') as f:
                f.write(data)
            os.replace(path + '.part', path)
            return
        except Exception as e:  # noqa: BLE001 - retried, then raised
            err = e
    raise IOError('%s: %s' % (url, err))


def get_ranged(url, path, pool):
    """One big file in parallel 4 MB ranges (the pack server throttles per
    connection to ~70 KB/s)."""
    if os.path.exists(path) and zipfile.is_zipfile(path):
        return
    req = urllib.request.Request(url, method='HEAD')
    total = int(urllib.request.urlopen(req, timeout=60).headers['Content-Length'])
    parts = []

    def one(a, b):
        r = urllib.request.Request(url, headers={'Range': 'bytes=%d-%d' % (a, b)})
        for attempt in range(8):
            try:
                d = urllib.request.urlopen(r, timeout=180).read()
                if len(d) == b - a + 1:
                    return d
            except Exception:  # noqa: BLE001
                pass
        raise IOError('range %d-%d of %s' % (a, b, url))

    for a in range(0, total, CHUNK):
        parts.append(pool.submit(one, a, min(a + CHUNK, total) - 1))
    with open(path + '.part', 'wb') as f:
        for p in parts:
            f.write(p.result())
    os.replace(path + '.part', path)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    # Imported late: build_kit pulls in PIL, which the fetch does not need.
    from build_kit import SLIDERS, expand
    from kit_body_masks import MASK_TARGETS

    jobs = []
    with cf.ThreadPoolExecutor(24) as pool:
        mh = os.path.join(out, 'mh-data')
        for rel, dst in (('3dobjs/base.obj', 'base.obj'), ('rigs/default.mhskel', 'default.mhskel'),
                         ('rigs/default_weights.mhw', 'default_weights.mhw')):
            jobs.append(pool.submit(get, MH_RAW + '/' + rel, os.path.join(mh, dst)))
        for stem in mhkit.macro_stems():
            jobs.append(pool.submit(get, '%s/targets/macrodetails/%s.target' % (MH_RAW, stem),
                                    os.path.join(mh, 'targets', stem + '.target')))
        detail = set(MASK_TARGETS) | set(mhkit.breast_stems())
        for s in SLIDERS:
            detail.update(expand(s[3]) + expand(s[4]))
        for t in sorted(detail):
            jobs.append(pool.submit(get, '%s/targets/%s.target' % (MH_RAW, t),
                                    os.path.join(out, 'targets', t + '.target')))
        for j in jobs:
            j.result()
        print('MakeHuman data: %d files' % len(jobs))

        packs = {'makehuman_system_assets': 'sys'}
        for c in CATALOG:
            if c[4].startswith('packs:'):
                packs[c[4].split(':', 1)[1].split('/')[0]] = None
        for name, dest in packs.items():
            z = os.path.join(out, 'zips', name + '.zip')
            os.makedirs(os.path.dirname(z), exist_ok=True)
            get_ranged(PACK.format(name), z, pool)
            target = os.path.join(out, dest or os.path.join('packs', name))
            if not os.path.isdir(target):
                zipfile.ZipFile(z).extractall(target)
            print('pack', name)
        for url in UAL:
            z = os.path.join(out, 'zips', os.path.basename(url))
            get(url, z)
            target = os.path.join(out, 'ual', os.path.basename(url)[:-4])
            if not os.path.isdir(target):
                zipfile.ZipFile(z).extractall(target)
        print('UAL')


if __name__ == '__main__':
    main()
