# Open a nested Wayland compositor

**Draft for owner review.** This is the first executable WM slice, not the
whole [compositor milestone](../../architecture/README.md#first-milestones).

A developer needs to see a real Wayland client render inside Mothership WM
without replacing or changing their current desktop session. The WM owns this
slice; it does not require the Panel, desktopd, flow membership, or an agent.

| Criterion | Observable success | Proven by |
| --- | --- | --- |
| G-WM-BOOT-1 | The WM opens one nested output under Niri with its own local Wayland socket, distinct from the parent's socket. | [F-WM-BOOT-1](../flows/boot-nested-wayland-compositor.md#f-wm-boot-1) |
| G-WM-BOOT-2 | One real xdg-shell client connects to the nested socket and its content is visibly rendered inside that output. | [F-WM-BOOT-1](../flows/boot-nested-wayland-compositor.md#f-wm-boot-1) |
| G-WM-BOOT-3 | Stopping the WM releases its socket and output, while the same parent session keeps running. | [F-WM-BOOT-2](../flows/boot-nested-wayland-compositor.md#f-wm-boot-2) |
| G-WM-BOOT-4 | A missing parent, incompatible backend, or unusable nested output fails with an actionable reason; the WM does not fall back to a direct desktop or another backend. | [F-WM-BOOT-3](../flows/boot-nested-wayland-compositor.md#f-wm-boot-3) |

Two-client focus and pan, input routing, and native GPU measurements are
outside this slice. COSMIC may later be used for an explicitly authorized
smoke test. It does not replace the Niri acceptance check.
