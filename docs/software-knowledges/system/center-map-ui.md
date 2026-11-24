# Center Map UI contract

For visual changes, compare the [original approved assets and review checklist](center-map-references/README.md) against an actual build capture.

- Wallpaper is lowest and the WM owns application surfaces above it. In
  one Center Map HUD, paint back to front: surface/grid/frame, radar, world
  (Mothership, Nodes, names, routes and relay), minimap, then opaque Detail.
  Command Center and the physical panel remain independent.
- The native radar uses a Wayland subsurface of the same HUD. It runs
  before world/Detail appear; it and the GTK fallback both exclude the
  entire painted minimap, including the dashed rim. No sweep crosses it.
  When the scan starts, replace the opening frame with the settled two
  contours; do not retain both frame snapshots.
- Paint the Map surface, two continuous thin 45° frame strokes and the stationary grid
  first. The approved visual adds light cyan junction points: static, not a hover effect.
  Keep the darker inner steel contour parallel to the outer edge, with a
  5 logical px normal inset; the outer stroke owns the frame shape.
  Use the bundled Chakra Petch shell family for Map labels by default,
  with existing single-family and size theme overrides. Project Node diamonds and labels above the grid. Pan or zoom changes
  only the Node projection and icon scale. Label size, grid, frame and
  wallpaper do not move. Keep paint and hit targets inside the frame.
- Default markers are quiet hollow diamonds. Selecting a Node highlights
  its icon and label and opens one anchored Node Detail; hover does neither.
  Use 45° card corners, paired parallel contours, solid corner runs and
  dashed long rim runs. Card updates do not repaint the backdrop.
- Show the real Node name, optional description, available Running time and
  Last active, and icons for identifiable mapped member apps. Render
  Running time (clock) and Last active (pulse) on one baseline with a
  thin separator only when both values exist; do not wrap or invent data. Use a generic
  icon only for a known app ID with no icon. Show “No windows open” only
  when live membership is known empty. Missing fields leave no dummy
  content. Outside click, Escape or loss of Map View dismisses the card.
  Place the card to the right of the selected Node and its label. Follow the
  projected anchor and flip to the left or another in-bounds side only when
  the right side does not fit. Paint Detail above Node symbols, names, routes
  and relay glyphs. The card consumes clicks before covered Nodes do.
  The spacecraft icon is the separate Connect action. It uses the same
  acknowledged workspace entry as the Command Center's labeled action.
- Use WASD, wheel and blank-space drag only in Map View. After a small
  movement threshold, drag moves the map 1:1 with the pointer at any zoom.
  Start only on the empty interior, not on a marker, label, Detail, minimap
  or frame. A stationary click keeps its existing behavior. Release, leaving
  the frame or losing focus ends drag immediately, with no inertia or
  click-through. Escape cancels a drag and dismisses an open card.
  The current shortcut design uses M to toggle Map View and H for blank
  Home only when the shell owns keys. App typing stays with the app. The
  active Map has no shortcut banner, Done button or reserved pointer region.
  Leaving the outer window clears held motion. Returning restores Map keys
  and wheel without closing, reopening or replaying the Map.
- Agent, notification and volume badges in the static Node Detail study
  are outside V1. The mock data and its artwork do not imply integration.

## Visual invariants

- Use one retained layer stack in one HUD, in the order above. Check both
  radar backends against the same minimap boundary, including its dashed
  rim. The Detail is opaque and higher than world content and minimap;
  do not draw through it or send clicks to a Node it covers.
- Give each layer one render owner in each animation phase or backend.
  Retire the outgoing retained paint when switching owners, in the same
  state transition that enables the replacement. Stopping its updates is
  not retirement; the last snapshot can remain visible.
- Treat outer keyboard or pointer focus as input eligibility, not view
  selection. Cancel held actions when focus leaves; resume only fresh
  input when it returns, without restarting the open Map.
- Mothership and the stock Node diamond use the same base logical extent
  multiplied by map zoom. Keep the extra Node focus outline outside that
  base; do not compare the decorated focus ring to the base icon. Check the
  painted contour, not an SVG or widget bounding box. Keep the idle Node
  readable over light and dark wallpaper without looking selected.
- Use the same [shell typeface and role weights](../../../apps/panel/assets/fonts/chakra-petch/README.md)
  across Mothership-owned text; external applications keep their own fonts.
  Mothership, numbered Node names and unnumbered Node names share the
  configured Map font and baseline. Focus can change ink or weight, not
  size. The Detail has its own text hierarchy. Keep map names on one line;
  pan and zoom must not cause ellipses or wrap. The painted Map edge clips
  the resulting ink.
- Keep a visible route gap to the real painted endpoint contours, including
  the focus stroke. Intersect the route ray with the circle, diamond or
  custom focus contour, accounting for line/stroke widths and diagonal
  approaches. Use the same endpoints for dashed and returning-solid
  segments; the relay glyphs must not cover the endpoint markers.
- Review actual pixels at rest and during interaction. Check two zoom or
  output scales, both pan directions, clipping at a real edge, light/dark
  backgrounds, covered clicks and cancel/close/reopen. For a phase handoff,
  capture opening, scan and settled pixels on first entry and reopen, on
  the native path and its fallback. Do not conflate renderer evidence
  with outer presentation timing; see [the render runbook](../../runbooks/render-performance.md).

## Connect and Disconnect

- Mothership is a virtual route origin at a fixed coordinate
  in the map's world. Pan and zoom project it just like Nodes. A small,
  non-interactive mark denotes the origin inside the Map; it is not a
  Node, a separate viewport control, or part of the frame. The dashed route
  and both relay glyphs share this coordinate and clip to Map content.
  Selection does not switch workspace. With motion enabled, only the
  dash pattern loops at constant speed from Mothership to the selected
  Node while the dashed route is visible in the active Map. Its projection
  and endpoint gaps do not change. Pan/zoom do not restart the phase.
  Pause on focus loss (resume from the same phase), occlusion or if the
  route leaves view. Stop on deselection, Map close or relay takeover.
  Off never starts the effect.
- Animated Connect asks the WM to prepare without projecting the target or
  changing focus. Draw the signal outward. The monitor returns and the line
  becomes solid only after preparation is acknowledged. After it returns,
  retract the retained Map around viewport center as the WM expands a
  centered clip over the real workspace. Do not capture the workspace or
  reconfigure clients for animation frames. Keep the wallpaper and stored
  map camera still; late clients join normally when they map.
- Disconnect returns to the same Map pan and zoom, with the most recently
  connected Node selected and the route dashed again. Do not animate a
  flying icon when returning.
- Switching directly from Node A to B uses the same acknowledgement.
  A rejected switch leaves A selected. If WM fails to acknowledge or
  disconnects, the result is unknown; do not render B as connected.
- In Map View, unmodified top-row keys 1–9 connect to the displayed Node
  with that slot. Empty slots do nothing; no slot shifts when another Node
  disappears. Slots are stored with the Node. Nodes without a slot remain
  available by mouse and in Command Center; numbering does not cap
  creation. Inputs and apps keep their keys; no global bind is added.

The outbound signal takes about 150 ms, then waits without redrawing if
preparation is late. The monitor returns in about 180 ms. The WM
acknowledges selection after committing the first clipped output state.
The approximately 200 ms reveal does not scale app buffers. The WM
blocks input to apps until the clip is fully open. A failed prepare,
stale ticket or cancellation before reveal leaves the Map selected;
once reveal is sent, the acknowledged projection is authoritative.
Off, number keys, an off-screen target or a connection outside Map View
use the direct path. Preview remains separate and read-only.

## Center Map minimap

- The minimap floats above the existing Center
  Map, aligned bottom-right with equal painted clearance from both edges.
  Do not notch the Map, move the panel, or add controls. The minimap has
  its own opaque cool-navy layer and matches the Detail's paired 45°
  contour, solid corners and dashed straight runs.
- Inside, draw only 45° Node diamonds and a thin viewport outline from the
  same Node and camera coordinates. No interior grid, dots, labels, or buttons.
  Open and close it with Center Map, independently of Command Center.
  It does not receive input or control the camera.


See [Center Map flow](../flows/show-center-map.md) for focus and view changes.
