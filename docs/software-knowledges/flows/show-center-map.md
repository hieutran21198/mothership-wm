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
positions. The canvas has separate global passes for tint/grid and the chamfered border,
then Node symbols and their labels. Only the tint honors the configured
opacity, which defaults to 0. The grid has no coordinate rulers. The font style is local to map labels. Missing assets and invalid
styles preserve a usable view.

### F-MAP-3

In Command Center → Map settings, change border color, outer margin or
inner padding, or select Clear/Frosted and the camera animation. Frost is
clipped to the Map outline
and only affects the wallpaper below apps; it is off while the Map is hidden.
A valid change redraws without a view/focus request.
Cancel restores the previous appearance. Save patches only changed keys in
`center_map` in the [canonical user configuration](../../../apps/panel/CENTER_MAP.md#user-configuration);
it preserves wallpaper and other modules. A failed write leaves the
form visible with an error. On launch or on **R** from blank Home with
Command Center closed, the saved patch applies over defaults in code.
An invalid manual reload keeps the running configuration, view and camera.
External edits are not watched, and R does not reload config in Map View,
Command Center, Settings or apps.

### F-MAP-4

In Command Center, choose Map View or press M from blank Home. The Panel waits for Overview
and Map visibility and uses the existing shell keyboard target. WASD pans
only Nodes and labels with the configured speed. Wheel inside the frame
zooms around the cursor and scales the icons, not the label font, grid, frame
or wallpaper. Drag from blank space inside the frame pulls the map with the
pointer after a movement threshold. Release, leaving the frame or focus loss
stops drag. Space centers Mothership at the current zoom, clamped by the
world bounds. Q, Escape, M or a bare Meta tap hides Map for blank Home
without reopening apps. Escape first dismisses a modal or Arcade. Focus
loss stops movement. Outside Map
View these controls keep their ordinary routing. Connect requests the named
workspace and reveals it after acknowledgement, without map movement.

### F-MAP-5

While Map View owns the existing shell input, click a Node marker or name.
The Panel highlights that Node and shows its dashed Mothership route without
opening a dialog or changing the workspace. A different Node replaces the
selection. A blank click clears it. Enter connects the selected Node
through the existing acknowledgement. Number keys 1–9 connect to their own
slot, and every visible Node can be selected and connected without a number.
Explicit Disconnect returns to the previous camera and last connected selection.
See the [Center Map UI contract](../system/center-map-ui.md).

Click Mothership to inspect its [Default Node detail](../system/center-map-ui.md#default-node-detail).
Enter or Connect requests the same view as key 1 through desktopd. The Panel
uses the live Inbox membership and refreshes the card as windows change.
