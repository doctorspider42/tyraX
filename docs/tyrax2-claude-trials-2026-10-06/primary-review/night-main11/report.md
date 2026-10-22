# Night-only main-role submissions: source audit

Read-only review; no build, hardware, runtime mutation or new measurement. The parent supplied emulator cold counts: main role 38 day /49 night, other78/84, total entered118/135, AO0/emissive0/env2. These aggregates are not per-object execution traces.

The concrete visibility change is in `game/src/scripts/district_mood.cpp:29-34`: it matches the current scene stable ID hash against DISTRICT_NIGHT_OBJECTS and assigns `ctx.objects[i].visible = night`. This executes once per mood or scene-generation change. The first11 entries in `inc/scripts/district_data.hpp` resolve to scene0 rows111..121. They are eight illuminated window boxes and three garage trim/blade boxes. This is authored night-only geometry, independent of dynamic lighting.

| Scene0 row | Authored ID | Name | Material |
|---|---|---|---|
| 111 | district632d34ec | Night window -17 3.4 | 2 |
| 112 | districtd4f46781 | Night window -17 8.2 | 2 |
| 113 | district578a4884 | Night window -17 13 | 2 |
| 114 | districtf89c6a4e | Night window -17 17.8 | 2 |
| 115 | district9b893ecf | Night window 17 3.4 | 2 |
| 116 | district680e250f | Night window 17 8.2 | 2 |
| 117 | districtee693d5e | Night window 17 13 | 2 |
| 118 | district1614dfd4 | Night window 17 17.8 | 2 |
| 119 | district9635ea45 | Night garage trim 3.0 | 3 |
| 120 | district135040f7 | Night garage trim 5.6 | 3 |
| 121 | district29658260 | Night garage blade | 4 |

All11 generated rows have type0(box), model-1, collision2(no collision), drawDistance0(unlimited), batchStatic0, dynLit0, impostorDistance0. Geometry rebuild (`game_collision.gen.cpp:1148`) assigns one part for non-model objects, including these boxes. They bypass static batching by authored batchStatic0 (`:2170`), so the solo-path object loop is the relevant consumer.

The main-role annotation is around the original per-part call at `game_physics.gen.cpp:2035-2040`: bag present, not translucent, not LOD-hidden; its earlier object visibility gate excludes day-hidden dressing. Main role is a call-family label, not a count of imported models: these primitives are included. Different light colors or directional lighting bags do not independently create additional calls in this loop. AO, emissive and environment passes are separately annotated.

Thus the exact source supplies11 individually submitted night-only primitive parts, an exact-cardinality explanation consistent with the observed main-role +11. This does not prove per-object cold acceptance: coarse frustum, active/visibility, geometry readiness and per-part consumer early returns still matter, and the current aggregate census does not record object IDs. Do not claim the +11 was individually attributed by the runtime. No source-driven assertion that Other+6 has the same cause is made.

A day/night comparison with all effects disabled still need not have equal geometry: these visibility assignments remain. An effects-off test that leaves them intact therefore cannot isolate the engine lighting path as equal workload. Source pin JSON records exact reviewed files and row mapping. No current performance cost is inferred.
