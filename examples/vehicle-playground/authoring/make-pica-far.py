"""Build the Pica Turbo's FAR model: a hand-built low-poly twin of the Pica
(make-pica.py) for cars that are far away or that nobody drives.

Run through Blender, AFTER make-pica.py (it reads that model's texture):

    blender -b --factory-startup --python make-pica-far.py -- [--full GLB]
            [--out GLB] [--preview DIR]

Writes res/models/pica-far.glb and, with --preview, the full and far models
rendered from the same cameras plus a contact sheet (pica-far-sheet.png).

The same recipe as make-ravager-far.py (carkit.far_main does the work): the
full model's own section() sampled on 16 stations instead of ~32 and on 8 of
its 12 character lines (keel, rocker, flank, belt, sill, rail, glass edge,
crown - make-pica.py FAR_LINES); the recessed grille and tail panel kept, so
the full model's lamps part (never hidden) still sits in front of them; the
bumpers as two-segment bars; octagonal wheels with the painted wheel face. It
wears the full model's atlas byte for byte - make-pica.py paints the windows,
the lamp lenses and the wheel face for it - so it is ONE material, one submit,
no VRAM of its own.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import carkit as K  # noqa: E402

if __name__ == "__main__":
    K.far_main(K.load_car("make-pica.py"))
