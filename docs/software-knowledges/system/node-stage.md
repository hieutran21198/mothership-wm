# Node Stage layout

Flow stays the default layout. In a named Node or Default Node, open Node
Panel Settings and choose **Layout → Stage** or **Flow**. The choice takes
effect immediately and the Node keeps it for this session. Keep the selected
window if it is still eligible. Returning to Flow restores its orientation,
order, and window sizes.

- Stage presents one standalone window or one [tiled group](../flows/view-node-layouts.md#f-view-5)
  in the work area above the Node Panel. One member of the presenting group
  holds keyboard focus. Selecting another member keeps the group in place
  and moves focus directly to that window.
- Use the same 1024 × 560 logical pixel minimum as Flow for tiled groups.
  Below either dimension, Stage presents the selected member on its own
  and retains the group for when the output fits it again.
- Switch from the [preview tray](home-panel.md#node-panel) or commit
  Alt+Tab. On a switch between standalone windows with motion enabled, scale
  the outgoing window toward its thumbnail, then scale the incoming window
  up from its thumbnail. Both thumbnails keep their tray slots during the
  switch. Ease both phases in and out using retained buffers. Apply client
  sizes for the final layout and keep the moving view out of input routing.
  Rapid switches redirect to the latest target. With Motion Off, apply the
  final layout immediately.

See [Stage switching](../flows/view-node-layouts.md#f-view-4) for the
runtime checks.
