# Public receiver runtime: independent source review

Reviewed 2026-10-06 against the current production inc/template/engine diff. Source review only; no build, emulator, console, performance or image work was performed by this reviewer. Read together with editor-and-engine-seam-review.md. Root owns actual compiled-generation and runtime qualification.

## Concrete findings

1. New startup demotion initially erased shown[k] before the first batch rebuild had sized the shown snapshot. The producer repaired the new path to erase only when k < shown.size(). The repaired source has been inspected. Initial empty snapshots remain valid while b.dirty schedules a full rebuild.
2. Shared environment-map content reuse initially omitted receiver eligibility. A changed driver/player binding can change a reflected object's live illumination without changing its transform or dirty state. Additional reuse based on the old content key could retain old illumination indefinitely. A restricted-only eligibility key contribution was requested; final status is recorded separately when the repair arrives. Existing capture cadence is intentional and is distinct from unlimited additional reuse.

## Ownership routes

isLivePlayerReceiver validates scene-generation binding, bounds and active runtime object, then compares the actual object index to P1, active P2 and the active driven vehicle object. It does not borrow meshOwner, definition/material identity, selection, camera owner or an arbitrary clone's source role. Scene load invalidates the generation before index reuse and installs current player indices. Vehicle setup resets driver and wheel buffers for the new scene.

Main object, animated and vehicle glass submissions use their source object. Mirror, camera-feed, reflection proxy, shared and per-object probe, portal ordinary and exit-clipped submissions retain original source indices. Terrain, roads, procedural chunks, static world batches, particles and effect submissions use World. All inc direct Core submissions are inside the two wrappers; the additional template wheel submission explicitly resolves its class. No ambient mutable receiver state was introduced.

The restricted pre-secondary pass removes live receiver members from all static batches before a secondary view can consume dirty flags. objectBatchOf becomes solo and the original object is marked dirty; combined arrays rebuild through the existing dirty path. Animation illumination now has one source-identical helper with a receivesLive argument to dynLightAt, which always zeroes dl before suppression. The helper retains directional/albedo/probe/signedSH terms, with each instance owning its light colors independently of shared pose/mesh geometry. The restricted early refresh runs before secondary views, and the main animation path still refreshes before its own draws.

## Wheel buffers and packet lifetime

Restricted scenes use two receiver classes per definition. Eligibility partitions cars before filling class slots. All class containers are allocated before the first wheel submission, preventing container growth from relocating content/stamp owners while an earlier class is submitted. Each class owns vertex/ST/color arrays, slot signatures and sticky stamp/count/address state. Entry/exit or driver changes recompute class membership each draw; changed vehicle identity/inputs or slot length invalidate existing geometry through the original rules. Scene setup clears all wheel batches before source-model lifetime changes.

The shared wheel bag/color/texture descriptors are rebound synchronously and are not used as the receiver-state lifetime carrier. Distinct class backing arrays remain resident, and their light reception is passed as a typed Core call argument. Core emits current local spot/options before geometry retained/baked cache lookup. Thus current lights do not require changes to geometry-command cache keys; shared framebuffer capture keys need the separate repair above.

## Default behavior and unpriced work

All projects compile ANY_PLAYER_ONLY_DYNAMIC_LIGHTS=false; wrappers select FromInfo, scalar pickup receives true, batch demotion/early animation refresh are disabled and wheels retain one definition class. Mixed projects also select those original paths in an All scene. The extracted main animation lighting code preserves the old arithmetic and operation sequence. PipelineInfoBag defaults opt-in and existing spotLit/dynLightPick meanings remain intact.

These source facts do not prove a zero instruction/layout or ABI cost. Restricted early refresh can perform illumination for visible objects outside the main frustum and repeats helper work for main-visible animations. The two-class wheel split adds submissions/storage and may change ordinary blend order between driven and parked wheel groups. Those are intentional implementation costs needing actual qualification, not measurements supplied by this audit. Packet/ABI, native source closure, positive recipient/world activation, entry/exit and secondary-view images still belong to root's test workflow.
