# Selecting objects in the viewport

Click a visible object to select it, or choose its name in the scene list; both
routes show the same yellow bounding box in the viewport.

![Waystone selected in the grove](img/selection-bounds.png)

The box encloses the object's actual bounds in world space (AABB), including its
position, rotation and scale. Models whose origin is at the ground get a box
around their whole height, rather than a unit cube buried at their feet. When
an impostor is visible, its selected camera-facing card is included too. Local
and collaborator selection outlines remain visible through scene geometry.

Picking static models first tests this box, then prioritizes a hit on the
visible mesh or selected impostor capture. Texture alpha below 128 does not count
as a solid hit. Clicking empty space inside a model's AABB still selects it when
there is no direct surface hit along that ray. A five-pixel grab
margin helps with small objects; areas and procedural volumes rank last.
Repeated clicks at the same position cycle through overlapping candidates.
Hidden layers and generated procedural chunks remain excluded.

This is editor selection only: collision, snapping surfaces and PS2 rendering
are unchanged. Animated model selection uses its existing cached model bounds;
this is not per-frame skinned-triangle picking. Asset reloads invalidate cached
pick meshes and alpha masks; painting a texture invalidates its pick mask too.
