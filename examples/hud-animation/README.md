# Animated HUD

This example is a compact, controller-driven tour of TyraX's animated HUD:
smoothed and segmented bars, looped motion, show/hide transitions and one-shot
effects. Open **Tools > UI Editor** to inspect the three bars and two text
elements, or open the player's Flow Graph to see every interaction.

## Controls

- **Cross** damages the health bar to 30 and flashes it.
- **Circle** heals the health bar to 100 and bounces it.
- **Triangle** toggles the stamina bar through its slide transition.
- **Square** bounces the title.
- **R1 / L1** drains and restores the stamina bar.

The health bar follows the `health` save value, demonstrating that **Set HUD
Bar** updates the stored value and the display together. The stamina and keys
bars are node-driven: stamina eases between values while the five-segment keys
strip starts at three filled segments. The title breathes, the help text pulses,
the stamina bar bobs, and the keys wobble; all four loops use the same formula
in the editor preview and the generated game.

Build with `tyrax-editor --build examples/hud-animation`, then run it from the
editor or with the generated launcher.

Generated object values live in `src/gen/scene_objects.gen.cpp`;
`inc/scene_data.hpp` keeps stable declarations. Counts and object IDs live in
the same data file, so ordinary moves, color edits, additions and removals can
rebuild it alone. Changes to features or derived tables can still rebuild consumers.

Generated game methods are split between `src/terrain_game.cpp` and the
`src/gen/game_*.gen.cpp` subsystems, with shared inline helpers/state in
`inc/game_runtime.gen.hpp`. Header changes can compile these units in parallel.
The main file remains user-ownable; generated subsystem files refresh on build.


Hardware timeline instrumentation matches the 1.170 coarse/detail emitter.
Use the [capture and observer-control guide](../../docs/hardware-profiler.md)
when comparing performance; traces add measurable work.

This example explicitly requests `framePipeline: true` (TyraX2). Its authored interlaced output preserves the existing scan mode and quality; ordinary interlaced output is eligible, unlike true field rendering. True field rendering, BLSS, unlimited triple buffering and unavailable queue memory fall back safely. This request does not guarantee 60 FPS. See [TyraX2](../../docs/tyrax2.md).

Generated runtime snapshot refreshed for [dynamic light receivers](../../docs/dynamic-light-receivers.md). This example retains the default **All objects** policy; Players and driven vehicle is an optional project or scene setting.
