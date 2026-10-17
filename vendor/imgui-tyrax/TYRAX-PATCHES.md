# Dear ImGui, as patched for TyraX

This directory is [Dear ImGui](https://github.com/ocornut/imgui) — the
`docking` branch at `b334d19b667958ed970000073644d911fae17e57` (1.92.9) — kept
**in the repository** rather than fetched by `setup.sh` / `setup.ps1`, so the
editor can carry small patches to it. Only the files the editor compiles are
here (the core, the GLFW and OpenGL 3 backends, `imconfig.h`, the `imstb_*`
headers and the license); `docs/`, `examples/` and `misc/` are not.

Every change is marked with a `// TYRAX:` comment, so `grep -n TYRAX` lists
them all and an upgrade is: copy the new upstream files over these, then re-apply
each marked hunk.

| Patch | Files | What and why |
| --- | --- | --- |
| `ImGuiStyle::DockNodeRounding` | `imgui.h`, `imgui.cpp` (style ctor, `ScaleAllSizes`, docked window background in `Begin`, `DockContextEndFrame`, `DockNodeUpdateTabBar` title bar) | When > 0, every dock node is drawn as its own rounded card: its tab bar rounded on top, its window background at the bottom. Upstream only rounds the corners that touch the dockspace's host window, which with gaps between the panels (`DockingSeparatorSize`) leaves square cards with rounded outer corners. 0 = upstream behaviour. Set by `theme::apply` (src/theme.cpp). |
| `ImGuiStyle::TabPillInset` | `imgui.h`, `imgui.cpp` (style ctor, `ScaleAllSizes`), `imgui_widgets.cpp` (`TabItemBackground`, `TabItemLabelAndCloseButton`) | When > 0, a tab is drawn as a rounded pill inset this far from the tab bar's top and bottom (all four corners rounded; nothing at all for a fully transparent tab colour), and an unselected tab's label - unless hovered - is drawn in `ImGuiCol_TextDisabled`. Stock ImGui rounds only a tab's top corners and has no per-state label colour, so "the selected tab is a filled pill, the others dim text" needs both. Tab *buttons* (`ImGuiTabItemFlags_Button`) keep the upstream shape. 0 = upstream behaviour. Set by `theme::applyMetrics` (src/theme.cpp). |
| Popups over a panel's OS window | `imgui.cpp` (`Begin`: "Late create viewport"; `WindowSelectViewport`: the merge branch; a forward declaration of `IsViewportAbove`) | With multi-viewports on (`src/multiviewport.hpp`), a popup, menu or tooltip that a platform window above its host viewport would cover gets an OS window of its own - stock ImGui keeps it in the host (it fits there), where the panel's OS window hides it (the logo menu opened under a straddling Procedural window was invisible and its clicks went to the panel). An open popup that owns an OS window is not merged back into its host until it next appears (merging flip-flopped against the rule above every frame). Relies on the application honouring `ParentViewport` (`ImGuiBackendFlags_HasParentViewport`), which TyraX does in `mvp::install`. No effect with viewports off. |
