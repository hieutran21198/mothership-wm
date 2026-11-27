# Pin a comms window

**Status:** implemented 2026-11-27; owner accepted in a native Mothership Dev session (candidate `build-F0Om9ovi`) on 2026-11-27.
Supports [Keep one window in sight across Nodes](../goals/pin-comms-window.md).
Design: [Fleet Ops preview, scene 3](../system/fleet-ops-preview.html).

The WM owns the pin: its state, pixels, input region and frame scheduling.
Pin state is session-only, keyed by the session window ID. There is one
pin per session in this slice; pinning another window replaces it.

| Step | Executor | Operation and outcome |
| --- | --- | --- |
| Pin | WM | **Meta + P** in a Node or the Default Node toggles the pin for the focused app window. The chord is fixed in this slice. |
| Show | WM | Whenever the window's projection is not the current view, draw a scene buffer from the window's existing top-level buffer, like the [preview tray](../system/home-panel.md#node-panel). Default to the bottom-right, 320 logical px wide at the source aspect (max 240 px high), 16 px above the rail and 18 px from the edge. The box includes a 22 px label strip above the thumbnail. Hide it in its own Node, Overview and the Center Map, and while the screenshot tool is open. Shell overlays (Slash, App Grid, Settings, Command Center, logs) are drawn above it rather than hiding it. |
| Frame | WM | Keep sending frame callbacks to the pinned surface while its Node is hidden, at the output rate. This is the only hidden surface that receives them. Never configure a new size. A missing buffer shows a neutral placeholder. |
| Label | Panel | `pin_state` (private shell protocol v28) carries the box, title and app ID. The Panel paints **Node · App — title** and an × in the label strip on a paint-only `mothership-pin-label` layer; it takes no input. Titles stay on the Wayland path. |
| Activate | WM → Panel → desktopd → WM | A short click on the pin sends `pin_activate`; the Panel [connects to the window](search-windows.md#connect-to-a-window). The click is consumed and never reaches the app or moves focus. |
| Move | WM | Dragging the pin past an 8 px threshold moves it. Release snaps to the nearest of four corners, keeping clear of the toast area (top-right) and rail. The corner is kept for the session. |
| Release | WM | A short click on the × (right end of the label strip), **Meta + P** on the pinned window, close/destroy or session end removes the pin and stops extra frame callbacks. Unmap hides it until the window maps again. |

### F-PIN-1

Pin a video playing in Node Ops, then enter Research and go Home. The pin
shows the moving video in both views, at the same client size as before.
Check with `WAYLAND_DEBUG` that no configure with a new size is sent.
Return to Ops: the pin disappears and the window is shown normally.

### F-PIN-2

From Research, click the pin. Ops opens and the pinned window receives the
next key, even if another Ops window was last focused. Move the window to
Node Review first and click again: Review opens.

### F-PIN-3

Type in a Research app while the pin updates: keys still reach the app.
Open Map, Slash and App Grid: the pin is hidden and reappears after each
closes. Drag the pin to every corner at 100% and 150% scale: it snaps clear
of the rail and toasts, and a short click without drag activates instead.

### F-PIN-4

Close the pinned window from its own Node: the pin is gone in every view.
Pin a window, pin a second one: the first pin is replaced. Restart the
session: no pin is restored.

## Contract

- Private shell protocol v28: `pin_state` (box, title, app ID; width 0
  when hidden) and `pin_activate`. No desktopd change: activation uses
  `focus.window`.
- Approved exception (2026-11-27) to the Node Panel rule that hidden apps
  are never repainted: exactly one pinned surface keeps frame callbacks.
- **Esc** cancels a pin drag, like any other pointer drag.

## Decisions

- Pin only with **Meta + P** in this slice; no `+N` grid or Alt+Tab entry.
  The chord is not yet in the Keyboard Shortcuts editor.
- No frame-rate cap unless native power checks show a cost.
