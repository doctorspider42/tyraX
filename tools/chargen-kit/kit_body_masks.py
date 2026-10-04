"""Face-paint regions, defined by MakeHuman detail targets (data only).

A target's per-vertex displacement, normalized, is a soft mask of the region
it shapes: "lip volume" moves exactly the lips, "cheek volume" the cheeks.
kit_body.py bakes these into the atlas; fetch_sources.py downloads them.

name -> (targets, gamma)
"""

MASKS = {
    'lips': (['mouth/mouth-upperlip-volume-incr', 'mouth/mouth-lowerlip-volume-incr'], 0.8),
    'eyeshadow': (['eyes/l-eye-eyefold-down', 'eyes/r-eye-eyefold-down'], 1.2),
    'cheeks': (['cheek/l-cheek-volume-incr', 'cheek/r-cheek-volume-incr'], 0.9),
    # Not neck-double: that target reaches the collarbones, and the stubble
    # came out as a rash down to the chest with a hard edge at the atlas seam.
    'stubble': (['chin/chin-width-incr', 'chin/chin-height-incr', 'chin/chin-prominent-incr',
                 'chin/chin-bones-incr', 'mouth/mouth-philtrum-volume-incr',
                 'mouth/mouth-laugh-lines-in'], 0.5),
}

MASK_TARGETS = sorted({t for ts, _ in MASKS.values() for t in ts})
