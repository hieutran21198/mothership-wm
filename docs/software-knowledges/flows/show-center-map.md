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

In Command Center, choose Map View or press M from blank Home. The Panel waits for Overview
and Map visibility and uses the existing shell keyboard target. WASD pans
only Nodes and labels with the configured speed. Wheel inside the frame
zooms around the cursor and scales the icons, not the label font, grid, frame or wallpaper. M, H or Done cancels movement and hides the Map, leaving the wallpaper
and Command Center entry without reopening apps. Escape dismisses a
profile only; it does not close the Map. Focus loss stops movement. Outside Map
View these controls keep their ordinary routing. Fly requests the named
workspace and reveals it after acknowledgement, without map movement.

### F-MAP-5

While Map View owns the existing shell input, click a Node marker or name.
The Panel shows its profile beside the projected anchor and keeps the card
inside the frame. Clicking a different Node replaces it; clicking empty
space, including the margin, dismisses it. It also closes when its Node
scrolls out of view or Map View loses focus. Fly in the profile uses the
existing named-workspace request and acknowledgement. Node selection does
not itself request a workspace switch or change Last active. Render the
real name and optional description. Running time is Node age (now minus
creation time), not window uptime. Last active is the last acknowledged
entry into that named workspace, not layout or window events. Omit missing
fields. Group mapped members with matching app IDs into one recognizable
icon per app. Omit unknown IDs and show the empty line only when live
membership is known empty. See the [Center Map UI contract](../system/center-map-ui.md).
