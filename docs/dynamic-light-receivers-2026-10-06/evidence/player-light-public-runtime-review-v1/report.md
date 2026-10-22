# Public receiver option runtime source draft

Source-only implementation; no native/editor build, emulator or console run was performed by this agent. Parent owns documentation, compiled validation and release. These pins identify a review snapshot, not immutable production release authority.

## Contract and routing

PipelineInfoBag.dynamicLightReceive defaults true. StaPipCore::render accepts a synchronous typed FromInfo/Suppress argument; a suppressed bag skips dynamic-light picking and explicitly uses the existing disabled spot descriptor, including camera fallback. Existing spotLit=false remains disabled. The shared descriptor is never temporarily mutated. Geometry/program retained caches consume freshly emitted per-object light/options before replay; this source contract was independently checked by pmu_completion_review. No VU or packet layout change is asserted from this source inspection.

Both TerrainGame header templates declare the ownership and draw helpers. isLivePlayerReceiver validates current scene binding generation, bounds and active object, P1 binding, enabled/active P2 binding and the active driven vehicle's actual object. Vehicle-less generation substitutes no vehicle field access. All resolved scene policy short-circuits ownership with ANY_PLAYER_ONLY_DYNAMIC_LIGHTS and PLAYER_ONLY_DYNAMIC_LIGHTS. Load clears binding generation and marks it current while reseeding live controller indices.

Main static/animated parts, AO/emissive/env overlays, reflection proxies, shared/object environment captures, mirrors, CCTV and portal destination/clipped/animated paths carry actual original object identity. Procedural/terrain/roads/static merged batches and existing no-light effects route World. Portal exit scratch retains caller identity explicitly. Vehicle glass carries vehicles_[vi].object.

Normal-lit and animated scalar pickup receives explicit live eligibility and zeroes dl before early return, retaining probe/sun/directional/albedo arithmetic. Animated light-color math was extracted into fillAnimLightColors; restricted policy refreshes it before secondary views to avoid stale player/World ambient after ownership change, without reskinning. Existing All mode retains main-only timing of this refresh.

Restricted updateDynLitObjects removes a newly live member from existing World static batches before secondary consumers can clear dirty, marks original geometry dirty and marks the remaining batch dirty. All policy performs no such membership work. Original batch routing and dirty rebuild behavior remains otherwise intact.

Wheel draw iterates independent World/live receiver classes only in restricted scenes, retaining per-definition source/ST/transform/content/stamp logic. Full mode has one class and does not classify vehicles. Class buffers own their slots and are allocated before any submit to avoid moving content/stamp owners under queued DMA. Entry/exit selects vehicles_[vi].object against actual driver identity; a moved vehicle slot is already invalidated by existing slot.vehicle matching. A typed Core override consumes class reception without shared batchInfoBag mutation. Scene setup retains its existing wheel storage clear and fence order.

## Review limitations

Generated compilation/extraction, positive scalar animation/light fixtures, wheel entry/exit and class changes, inactive P2, secondary-view images, all-mode regressions and hardware feature timing require parent qualification. No optimization gain follows from source completion. Spawn pool indices cannot become live merely by copying player model identity; no general possession API was introduced. Controller indices are seeded from authored slots and spawned indices are allocated beyond authored scene slots in the existing runtime.

## Secondary retained image key repair

The shared environment capture content key now includes actual reflected-owner receiver eligibility only under Players policy, so driver/P2 changes invalidate retained pixels even when transforms and dirty flags are unchanged. All policy leaves the original key unchanged. Mirrors, ray-traced mirrors, camera feeds and portals render every qualifying invocation; object probes also capture each valid invocation, preserving their existing too-close/invalid-intersection fallback contract. No new capture cadence or waits were added.
