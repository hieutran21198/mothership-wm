# Center Map UI contract

For visual changes, compare the [original approved assets and review checklist](center-map-references/README.md) against an actual build capture.

- Wallpaper is lowest and the WM owns application surfaces above it. In
  one Center Map HUD, paint back to front: surface/grid/frame, radar, world
  (Mothership, Nodes, names, routes and relay), minimap and input guide,
  then Node Detail.
  Command Center and the physical panel remain independent.
- The native radar uses a Wayland subsurface of the same HUD. It runs
  before world landmarks appear; it and the GTK fallback both exclude the
  entire painted minimap, including the dashed rim. No sweep crosses it.
  When the scan starts, replace the opening frame with the settled two
  contours; do not retain both frame snapshots.
- The shared Shell layout keeps the painted Map bottom **16 logical px**
  above the top of the [Home Panel](home-panel.md). Map top/sides and the
  dock anchor keep their existing positions. The minimap follows the new
  lower-right corner; there is no notch or panel overlap. Use the same
  frame for Cairo/GSK, grid, frost, intro/radar and WM input. Camera anchors
  and projection do not depend on the dock footprint.
- **Map Settings** opens with a bare comma in the normal interactive Map.
  Use the [approved English layout](center-map-references/Mothership-Map-Settings.png):
  a top-right analog glass panel with separate 45° corner strokes and Camera and Display
  sections. Its right inset matches the minimap's 18 logical px. Keep
  another 18 logical px between their painted edges; scroll the settings
  body if height is limited and keep the footer reachable. Settings does
  not move the camera or pan, zoom, or connect through the panel.
  On every open, slide Settings in from the right and back out on close
  with the same 250 ms ease-in-out as Home Panel Settings. Skip the slide
  if Map animation is Off or GTK animations are disabled. Reset previews
  defaults; Cancel or Esc rolls back; only Save persists.
  Return to the same Map and its keyboard controls without replaying entry.
- Center Map combines a mechanical outer bezel with an analog display
  inside it. Keep the wallpaper visible through the display in both Clear
  and Frosted modes; the grid, markers and scan are projected over it, not
  painted on an opaque backplate. Paint the Map surface, two continuous thin
  45° frame strokes and the stationary grid
  first. The approved visual adds light cyan junction points: static, not a hover effect.
  Keep the darker inner steel contour parallel to the outer edge, with a
  5 logical px normal inset; the outer stroke owns the frame shape.
  Use the bundled Chakra Petch shell family for Map labels by default,
  with existing single-family and size theme overrides. Project Mothership, Node spacecraft and labels above the grid. Pan or zoom
  changes world-marker projection and icon scale only. Label size, grid, frame and
  wallpaper do not move. Keep paint and hit targets inside the frame.
- Default markers are compact top-down ships with dark filled planes. Selecting
  a Node highlights its icon and label, draws the dashed connection, and opens
  one Node Detail card. Hover does none of these; a blank click clears the
  selection. **Enter** or the card's spacecraft icon invokes Connect; the normal
  Command Center remains a separate explicit entry point.
  During Map entry, use a transparent focus carrier only: the intermediate
  desktopd Overview acknowledgement must not paint the Command Center or
  Overview selector for a frame.
- Use WASD, wheel and blank-space drag only in Map View. After a small
  movement threshold, drag moves the map 1:1 with the pointer at any zoom.
  Start only on the empty interior, not on a marker, label, Detail,
  minimap or frame. A stationary click keeps its existing behavior. Release, leaving
  the frame or losing focus ends drag immediately, with no inertia or
  click-through. Space centers Mothership within the visible frame without
  changing zoom, clamped by world bounds. M toggles Map View; Q, Esc and
  bare Meta also leave Map for blank Home. Esc first dismisses a modal or
  Arcade. App typing stays with the app. The
  active Map has no shortcut banner or Done button. The small lower-left
  **W/A/S/D, primary-button, wheel, Q, Esc, Space, M, and Meta** guide is
  paint-only and does not
  consume clicks or reserve a target. Put grab before scroll and keep the
  outer control silhouettes equally inset from the left and right inner
  housing strokes. The guide appears after the radar pass.
  Show active keys only while held in Map, pulse wheel only for accepted
  zoom, and highlight grab from a primary press on eligible map background
  until release or cancel. Leaving the outer window, using Command Center,
  or closing the Map clears highlights. Leaving the outer window also
  clears held motion. Returning restores Map keys
  and wheel without closing, reopening or replaying the Map.
- Show the real Node name, optional description, Running time and Last active
  when available, and icons for identifiable mapped member apps in one opaque
  card with paired chamfered contours. Running time and Last active share one
  row with clock and pulse icons. Show “No windows open” only when live
  membership is known empty. Do not show dummy fields, agent slots, or
  notification and volume controls in the card.
- Anchor Detail to the right of the selected marker and its label. Fall back
  to the left, below, or above if the card does not fit. Keep its painted rim
  inside the Map and clear of the minimap. Pan/zoom reprojects the anchor.
  World content must not show through or receive card clicks. Hide Detail
  during the radar, Arcade, and Connect relay; Disconnect restores selection
  without opening it. Click the selected Node again to reopen it.

## Visual invariants

- Use one retained layer stack in one HUD, in the order above. Check both
  radar backends against the same minimap boundary, including its dashed
  rim. The minimap is paint-only; it must not select covered Nodes.
- Give each layer one render owner in each animation phase or backend.
  Retire the outgoing retained paint when switching owners, in the same
  state transition that enables the replacement. Stopping its updates is
  not retirement; the last snapshot can remain visible.
- Treat outer keyboard or pointer focus as input eligibility, not view
  selection. Cancel held actions when focus leaves; resume only fresh
  input when it returns, without restarting the open Map.
- Use the approved [fleet marker sheet](center-map-references/Mothership-fleet-markers-draft.png):
  Mothership has a broad dual-engine hull and the default Node a narrower,
  taller-proportioned single-engine hull. Both are viewed from above and
  use angular cyan steel lines and dark filled planes without an idle ring
  or glow. Their proportions stay distinct at every zoom. A selected Node
  adds a brighter outside silhouette and label; custom Node SVGs retain
  their existing artwork, scaling and independent focus outline. Check
  actual painted contours and retain idle contrast over light and dark wallpaper.
- Use the same [shell typeface and role weights](../../../internal/shellfonts/fonts/chakra-petch/README.md)
  across Mothership-owned text; external applications keep their own fonts.
  Mothership, numbered Node names and unnumbered Node names share the
  configured Map font and baseline. Focus can change ink or weight, not
  size. Keep map names on one line;
  pan and zoom must not cause ellipses or wrap. The painted Map edge clips
  the resulting ink.
- Keep a visible route gap to the real painted endpoint contours, including
  the focus stroke. Intersect the route ray with the padded ship hull or
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
- Key 1 is reserved for Default Node, which remains off the Map. Named
  Node keys 2–9 match their labels. The key is the saved named-Node slot
  plus one; saved records do not change. Named Nodes beyond the first
  eight shortcut slots remain available on the Map and in the command
  palette. Deleting another Node does not shift assigned keys.
- In Map View, unmodified top-row keys use these same numbers. Empty
  slots do nothing. Unmodified numbers outside Map View remain app or
  input keys.
- In a focused Mothership session, Win + top-row 1–9 jumps to the same
  numbered Node from Home, the Map, or an app. A missing slot (including
  Win+1 when Default Node is disabled) keeps the view and focus unchanged and records a System notification for the
  current view. Its toast follows the notification settings. An outer
  compositor can reserve the chord before it reaches Mothership.

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
  Previewing or rolling back Outer margin moves the minimap with the
  frame immediately, even if the camera has not moved.
  Do not notch the Map, move the panel, or add controls. The minimap has
  its own opaque cool-navy layer and uses paired 45°
  contour, solid corners and dashed straight runs.
- Inside, draw 45° Node diamonds, a simplified broad Mothership
  silhouette, and a thin viewport outline. Project Nodes and the virtual
  origin with the same fixed world transform; panning and zooming change
  the viewport indicator, not those anchors. Keep the ship legible if a
  Node coincides with it. No interior grid, labels, or buttons.
  Open and close it with Center Map, independently of Command Center.
  It does not receive input or control the camera.

## Fleet arcade trial

- Bare **G** starts the game only from active Center Map, with no command,
  settings or editable input open. **Escape** leaves the game and returns
  to the same Map pose and Node selection. The dock and frame do not move;
  the game owns only the opaque inner playfield and replaces the Map
  guide and world overlays. Home, applications and the App Grid do not
  acquire a G binding.
- **WASD** moves the fleet in screen axes. The pointer aims through 360°
  and all three ships turn together, keeping their wing mounts in formation.
  Aim follows the latest pointer inside the arcade, never a global pointer
  stream or mouse grab. Leaving the frame hides the reticle and retains the
  last heading; keyboard focus loss pauses the game and clears held inputs.
- **Q** fires both wing weapons on each ship; **E** fires one wide, brief
  pulse from Mothership only. Holding **left click** also fires the six
  wing weapons; pressing **right click** fires plasma. These buttons
  only act inside the focused arcade field and do not intercept normal
  Map clicks or other apps. Keyboard and mouse holds are independent;
  leaving the field ends mouse firing. The plasma meter displays the existing
  cooldown and each struck enemy counts once. Hull damage tints only
  Mothership; gentle banking does not alter the aim vector.
- Edge chevrons warn of enemies entering from all four sides. Chickens
  drop temporary yolk patches that make steering slippery; pigs lob slow
  mud that briefly reduces speed on contact; dogs throw a bone that
  returns to its owner and can damage only once per throw. Defeating the
  wave advances the counter. Q starts over after game over. Nothing is
  saved or sent to desktopd.
- Use the [Space Ops draft](center-map-references/Mothership-Space-Ops-draft.png)
  for the suited-animal silhouette, four-sided warnings, and aim cue. The
  concept's right-side profile, arrows, and illustrated counters are not
  game UI. The [Q/E draft](center-map-references/Mothership-Fleet-Arcade-QE-draft.png)
  still owns the three-ship weapon roles.


See [Center Map flow](../flows/show-center-map.md) for focus and view changes.
