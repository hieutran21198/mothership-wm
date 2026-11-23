# Show and hide the Center Map

Supports [Show a configurable Center Map HUD](../goals/show-center-map.md).
See the [local panel guide](../../../apps/panel/CENTER_MAP.md) for commands
and theme input.

The default shows the framed Center Map in Overview. `map.hide` hides it
without changing the view or wallpaper; `map.show` restores it. The debug
readout counts motion-layer rendering, not presented frames.

### F-MAP-1

With a live session, call `map.show` or `map.hide` on the existing desktopd
socket. The service records the single session visibility preference.
It publishes a revisioned change only when the preference changes, and
includes visibility in a fresh snapshot. Panel consumes the preference;
WM does not receive a view or focus command. The HUD is paint-only and
never becomes a pointer or keyboard target. The separate, paint-only session
wallpaper stays below windows and is not affected by these commands.
Show/hide is separate from
the [Overview selection flow](view-node-layouts.md).

### F-MAP-2

Start Panel with a theme and show the HUD. Read only the existing live
Node names and Overview slots. After adding a Node whose name sorts earlier,
the already assigned positions remain; hide and show do not allocate new
positions. The canvas has separate global passes for tint/grid and the single chamfered border,
Node symbols, then leaders and labels. Only the tint honors the configured
opacity, which defaults to 0. The grid has no coordinate rulers. The font style is local to map labels. Missing assets and invalid
styles preserve a usable view.

### F-MAP-3

In Command Center → Map settings, change border color, outer margin or
inner padding, or select Clear/Frosted and the camera animation. Frost is
clipped to the Map outline
and only affects the wallpaper below apps; it is off while the Map is hidden.
A valid change redraws without a view/focus request.
Cancel restores the previous appearance. Save persists only those overrides
locally; the theme and wallpaper stay separate. A failed write leaves the
form visible with an error. The next session uses the saved values or its
defaults when the override is invalid.

### F-MAP-4

In Command Center, choose Map View. Panel requests and waits for Overview
and Map visibility and uses the existing shell keyboard target. WASD pans
only Nodes and labels with the configured speed. Wheel inside the frame
zooms around the cursor and scales the icons and grid pitch, not the label
font, frame or wallpaper. Escape or Done cancels movement, releases
keyboard focus, and hides the Map, leaving the wallpaper and Command Center
entry without reopening any apps. Focus loss stops movement. Outside Map
View these controls keep their ordinary routing. Fly requests the named
workspace and reveals it after acknowledgement, without map movement.
