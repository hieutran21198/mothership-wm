# Mothership shell design system

Mothership is a spacecraft interface over an ordinary Linux desktop. Its
hardware and display layers give the shell its identity without taking space
or focus from the apps or restyling them. The design direction and references
are approved. This is not approval of the final mockup or native implementation.

## Foundations

Use these named roles across shell components rather than choosing a new
shape or palette for each piece.

| Role | Reusable rule |
| --- | --- |
| **Edge / shoulder** | Keep uncut corners square. Use the shared chamfer recipe for cuts on shell hardware, buttons, display bezels, and Map edges. Do not apply it to native app surfaces. |
| **Surface / metal** | Use one rigid matte graphite body. Express CNC-machined depth with one narrow light-catching top lip and a darker bevel face immediately below. Use the same contour for the lip and inset seam. Keep their light direction and weight consistent. No chrome on the top or sides, thick glow, rounded rails, separate pods or protruding keycaps. |
| **Surface / glass** | Use cool-tinted translucent glass for the Map and a darker tinted inset face for readouts. Share the angular edge language with the metal without making either display look like another metal slab. Keep labels and guides sharp; blur only the backdrop. |
| **Ink / text / signal** | Use high-contrast primary labels, quieter descriptions and a restrained cool accent for meaningful readouts and active states. Keep text and icons distinct from the edge treatments; don't rely on color alone to identify an action or status. |
| **Space / clearance** | Cut a recess only for a named receiving component. Size and position the pocket and occupant together, with a common orientation and axis as appropriate. Make the occupant visibly use its pocket. Keep positive, visually even clearance between independent contours so they never touch, join, or overlap. Preserve usable viewport space and readable controls. |

### Shared chamfer recipe

1. Use one straight 45° diagonal with zero radius and sharp endpoints.
   Verify equal absolute x and y runs against the upper-left Command
   Center cutout where available. Mirror counterpart cuts, not just their
   decoration. Round only when the owner expressly requests it.
2. Size local corner cuts deliberately and keep them small for their role.
   Match the angle across components, not their cut lengths. An exposed
   rail corner, a viewport edge, and the reflected center recess are
   different boundaries. A layout transition must not set the size of
   exposed terminal cuts or require cutting an uncut edge.
3. Build the inset by shifting each outer edge along its perpendicular
   by the same distance and intersecting adjacent shifted lines. This
   keeps the bevel parallel through the junctions. Use one thin seam or
   highlight with sharp endpoints, not layered strokes to patch a corner.
4. Stop a fitted seam where the two facing surfaces stop. Do not wrap
   around an exposed end or leave an overhanging spur. When there is no
   mating edge, trim the end cleanly or use its own short inward chamfer.

### Penpot reference at 1440 × 900

These measured examples describe the current study, not hard-coded tokens or
layout positions for other viewport sizes. Preserve the relationships above
when adapting them.

| Component | Reference values |
| --- | --- |
| Panel rail | The Command Center reference cut is 8 × 8 px (45°). The study rail uses 24 × 24 px breaks around the center and shoulders that run level to both screen edges. It borrows the navy depth from **Dev · Orbital cockpit**, with a 1.2 px `#58818E` rim painted inside its contour and a thin inset echo following a 14 px perpendicular offset. |
| Map glass | Four 8 × 8 px exposed corner cuts. Rail breaks reflect across viewport y = 816.5 px; the Map lower side and center are y = 808 and 784 px, respectively. Tint `#20384B` at 23%, 1 px `#648B9F` edge at 35%, backdrop blur 8 in Penpot. |
| Telemetry surround | A 440 × 39 px display with 7 × 7 px chamfers, in a pocket with 13 px normal painted-edge clearance at the top, bottom, and all four corresponding diagonals. Outer `#546A76` at 80%, inset `#18252E` at 98%. |

## Components

### Panel rail and controls

- One continuous rail along the bottom edge, with two raised ends joined by a
  broad recessed center. Run the outer shoulders level to the screen edges;
  do not add unoccupied dips or ornamental cuts at the ends. Keep the metal
  itself clear of extra controls and readouts. The open space bounded by the rail and Map holds the separate
  CPU/RAM display. Let the planet and Map remain visible.
- Each shoulder holds one legible, nearly flush inset control. The working
  study has **Command Center** left and **Toggle View** right; placement is
  for review. Use a distinct label and recognizable icon for each. For any
  interactive design, show discernible keyboard focus and toggle state.
- **Toggle View** shows or hides only the Map. It is not Command Center and
  must not share its visual state or action.

### Telemetry

- Center one CPU/RAM display in the pocket formed by the mirrored rail
  and Map contours. Size it with the pocket so it visibly uses the space
  while leaving equal clearance at the straight and corresponding angled
  edges. Derive the pocket by moving the display bezel's outer edge lines
  outward by the same perpendicular distance and intersecting them. Keep
  the Map and rail reflected; the open side transitions need no sidewall.
  Keep paint, including strokes, inside the measured boundaries. The
  display stays on its own layer, not mounted in the metal. Use small straight corner
  cuts and a thin cool graphite seam. No rounded capsule, metal block,
  or extra controls.
- Show individual CPU core bars, a RAM used/total meter, and clear NET
  download and upload rates with units. Keep all three legible in the same
  bezel; omit miniature plots if they crowd the readouts. The eight bars,
  `7.8 / 16 GiB`, and NET rates are illustrative, not live values.

### Overview Map

- Use a predominantly rectangular glass backplate with small chamfers
  on the exposed corners. Reflect the rail’s shoulder-to-recess profile
  across a horizontal axis
  in the opening. Keep the same break x coordinates; for each rail vertex
  use `yMap = 2 * yAxis - yRail`. The rail drops toward the center while
  the Map lower edge rises into a notch. Stop the profile at the existing
  Map sides and keep the glass, telemetry and rail clear of each other.
- Keep the planet behind and outside the Map. The Map owns its glass,
  crisp horizontal and vertical guides, and Node groups as separate layers.
  A Map restyle must not reposition Nodes or change guide and app content.

### Center Map

The [Center Map UI contract](center-map-ui.md) owns the Map, Node Detail,
and the design-only minimap. Use the shell-wide rules above for the shared
material and chamfer treatment.

### Node callout

- Preserve three rows: title, short purpose description, and open-app icons.
  Show an empty state when no apps are open; do not invent app icons.
- Preview is read-only and never Fly; Fly is an explicit view switch. Do
  not take app focus merely because an overlay or preview appears. The
  [Node layout](../flows/view-node-layouts.md) and [Command Center](../flows/use-command-center.md)
  flows own current behavior.

## Approved references

| Source | Borrow | Avoid |
| --- | --- | --- |
| [Rocinante screen graphics in Rhys Yorke's interview](https://www.pushing-pixels.org/2021/09/04/the-art-and-craft-of-screen-graphics-interview-with-rhys-yorke.html) | Keep the physical frame distinct from the display and the world behind it. | Do not copy screen readouts, fictional telemetry or exact artwork. |
| [NASA Discovery Flight Deck Panels](https://science.nasa.gov/3d-resources/discovery-flight-deck-panels/) | Make separate controls read as one installed assembly. | Do not borrow the control density, mechanical switches or legends. |
| [teenage engineering TX-6](https://teenage.engineering/products/tx-6) | Use the compact CNC aluminum construction as a reference for matte machined metal and a light-catching chamfer. | Do not copy the mixer layout, knobs, raised controls or branding. |

Sources guide the design. They are not approved mockups or a component kit.

## Agent review

Compare **Overview · Orbital cockpit** (closed) and
**Overview · Command Center mở** (open) in the editable
[Penpot Shells file](https://penpot.home.arpa/?screen=workspace&file-id=05c6f3cc-5317-814a-8008-fcf08be704a1&page-id=05c6f3cc-5317-814a-8008-fcf08be704a2).
Treat it as the current visual study, not authority over these rules or proof
of runtime behavior. Do not revive the top/side frame, rounded Optical Sill,
or separate raised keys. The Ops marker crop is not approved.

1. **Full viewport:** Check that the main scene still dominates. Confirm the
   planet, independent layers, Node positions and rows stay intact outside
   the requested change. On open and closed states, verify that only the
   open Map reserves a fitted minimap pocket and the rail doesn't move.
2. **Detail crops:** Compare affected corners at the same zoom. Measure
   equal absolute x and y runs for the reference cut and 45° corners,
   small cut sizes for their roles, and constant perpendicular spacing
   through the inset. Check one clean line, sharp endpoints, and square
   uncut corners. Check equal break x coordinates and mirrored distances
   for the Map recess. Check that the named display uses the pocket and
   measure the same perpendicular painted-edge gap at the top, bottom and
   all four matching diagonals. Do not use bounding boxes or stroke
   centerlines as the measurement. Flag unavailable measurements.
3. **Controls and data:** Read the labels at viewport scale. Verify one
   control per shoulder, separate functions, legible data and the Node
   empty state. Flag mock data and any design deviation.
4. **Handoff:** Show the viewport plus the relevant crop with what changed.
   Get owner approval before native work. If implemented later, favor
   static shell-owned material and a preblurred shell backdrop over live
   app-surface blur or capture; avoid idle redraw. A mockup proves neither
   behavior nor performance.
