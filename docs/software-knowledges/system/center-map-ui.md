# Center Map UI contract

- The fixed wallpaper is the lowest layer. The nested WM owns application
  surfaces above it. The shell overlay paints the Center Map; Command Center
  and the physical panel remain independent.
- Paint the Map surface, thin frame with equal 45° cuts, and stationary grid
  first. The approved visual adds light cyan junction points: static, not a hover effect.
  Project Node diamonds, leaders and labels above them. Pan or zoom changes
  only the Node projection and icon scale. Label size, grid, frame and
  wallpaper do not move. Keep paint and hit targets inside the frame.
- Default markers are quiet hollow diamonds. Selecting a Node highlights
  its icon and label and opens one anchored Node Detail; hover does neither.
  Use 45° card corners, paired parallel contours, solid corner runs and
  dashed long rim runs. Card updates do not repaint the backdrop.
- Show the real Node name, optional description, available Running time and
  Last active, and icons for identifiable mapped member apps. Use a generic
  icon only for a known app ID with no icon. Show “No windows open” only
  when live membership is known empty. Missing fields leave no dummy
  content. Outside click, Escape or loss of Map View dismisses the card.
  Fly is a separate, acknowledged workspace entry.
- Use WASD and wheel only in Map View. Escape dismisses the card first.
  The current shortcut design uses M to toggle Map View and H for blank
  Home only when the shell owns keys. App typing stays with the app.
- Agent, notification and volume badges in the static Node Detail study
  are outside V1. The mock data and its artwork do not imply integration.

## Command Center minimap (design only)

- In the approved preview, the minimap floats above the existing Center
  Map, aligned bottom-right with equal painted clearance from both edges.
  Do not notch the Map, move the panel, or add controls. The minimap has
  its own opaque cool-navy layer and matches the Detail's paired 45°
  contour, solid corners and dashed straight runs.
- Inside, draw only 45° Node diamonds and a thin viewport outline from the
  same Node and camera coordinates. No interior grid, dots, labels, or buttons.
  Show it only with Command Center. The combined preview is static; minimap
  implementation and animation are outside Node Detail V1.


See [Center Map flow](../flows/show-center-map.md) for focus and view changes.
