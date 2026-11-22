# Focus one of two nested windows

**Implemented.** COSMIC rendering and isolated protocol routing passed;
physical click-through on COSMIC has not been exercised.

A developer needs to operate two disposable Wayland windows inside one nested
COSMIC session, even when they belong to the same app, and continue using the
remaining window when the focused one closes.

| Criterion | Observable success | Proven by |
| --- | --- | --- |
| G-WM-FOCUS-1 | Two independently visible windows with the same app ID have distinct IDs in the nested session. | [F-WM-FOCUS-1](../flows/focus-nested-windows.md#f-wm-focus-1) |
| G-WM-FOCUS-2 | Clicking A and then B changes keyboard focus; each window receives only the test input meant for it. | [F-WM-FOCUS-2](../flows/focus-nested-windows.md#f-wm-focus-2) |
| G-WM-FOCUS-3 | When B closes while focused and A is still live, A receives the next keyboard event. | [F-WM-FOCUS-3](../flows/focus-nested-windows.md#f-wm-focus-3) |

Builds on [one-client nested boot](boot-nested-wayland-compositor.md). This
slice does not cover camera pan or zoom, general window layout, Panel,
desktopd, cross-session identity or direct DRM.

See [verification](../flows/focus-nested-windows.md#verification) for the checks and limits.

Tracking: [GitHub issue #1](https://github.com/hieutran21198/mothership-wm/issues/1).
