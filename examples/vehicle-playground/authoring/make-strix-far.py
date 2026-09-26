"""Build the Strix V12's FAR model: a hand-built low-poly twin of the Strix
(make-strix.py) for cars that are far away or that nobody drives.

Run through Blender, AFTER make-strix.py (it reads that model's texture):

    blender -b --factory-startup --python make-strix-far.py -- [--full GLB]
            [--out GLB] [--preview DIR]

Writes res/models/strix-far.glb and, with --preview, the full and far models
rendered from the same cameras plus a contact sheet (strix-far-sheet.png).

The same recipe as make-ravager-far.py (carkit.far_main does the work): the
full model's own section() on 17 stations and 8 of its 12 character lines
(make-strix.py FAR_LINES), the recessed nose and tail panel kept for the full
model's lamps part, the wing blade and its posts, octagonal wheels with the
painted five-spoke face, and the full model's atlas byte for byte - one
material, one submit, no VRAM of its own. The side intake, the louvres, the
windows and the lamp lenses are the texture's.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import carkit as K  # noqa: E402

if __name__ == "__main__":
    K.far_main(K.load_car("make-strix.py"))
