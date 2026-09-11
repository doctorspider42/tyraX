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
