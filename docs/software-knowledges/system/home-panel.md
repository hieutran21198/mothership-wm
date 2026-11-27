# Home Panel

One bottom-centered GTK panel on Home and Center Map. It shares Mothership's
existing shell window and is not a separate window manager, Node panel or
application. The shell retains only the visible dock and open-control input
regions; the rest of the desktop remains available to its current view.

## Geometry

The Home dock defaults to **840 × 62 logical px**. The shared
[shell sizing rule](shell-design.md#shell-sizing-and-mounted-modules)
separates the base shell from mounted readouts. `home_panel.width`
changes the rail width, not the button faces or their positions within the two wings. The centered telemetry tray fits the available span up to
**600 logical px**; the extra width does not stretch the readouts. Fit the
whole dock to the logical viewport with symmetric side clearance. Only when
its two fixed wings cannot fit, compact every action equally. Do not clip a
control to preserve a preferred width.

| Part | Normal-width recipe |
| --- | --- |
| Rail | One opaque graphite silhouette with paired, parallel thin strokes. Each cut has equal horizontal and vertical runs. Keep the positive end and seam gaps from the [shell design system](shell-design.md). |
| Actions | Two equal wings: Map, App Grid and Reload on the left; Hangar, Audio, Lock and Notification Log on the right. Do not add a global Settings button. All button faces share a size, up to 44 × 36 logical px. Mirror the 13 px outer inset and use 8 px between buttons at normal widths. Compact faces and gaps equally at narrow widths. Extra space stays toward the tray; the telemetry center never shifts. |
| Readout | 51 px high, with a matched upper/lower edge and quiet ink. Use the same near-black blue base as the Node recess. Separate Health, CPU, RAM, NET, and time/date; the display does not acquire focus. |
| Map gap | The Home Panel anchor does not move. The Center Map ends 16 logical px above the painted top of the panel, at every supported width. |

Use the approved [Map](home-panel-references/Map.svg) and [App Grid](home-panel-references/App-Grid.svg) glyphs. Keep button labels accessible but not printed on the dock. Do not show hover tooltips. The rightmost action opens the [Notification Log](../flows/show-notifications.md).
The [System Tray](../flows/use-system-tray.md) appears immediately before
the bell on Home and in Nodes, only while apps register tray items. Focus, hover and
the active Map state are distinct outlined states; the wallpaper, settled Map bounds and workspace positions do not change
with dock width. The dock stays outside the Map. Slash search is a separate
Home mode and is never shown over an open Map.

### Node Hangar

Keep telemetry centered in the dock recess. Do not show a second row of
ships or a parking pocket in the dock. The Hangar button in the right
wing reveals a mechanical 3×3 hangar above it with up to nine named
Nodes. Empty bays use a subdued single edge and a short, muted amber-and-black
diagonal strip in place of the occupied bay's blue landing line; no
labels or actions, including when all nine bays are empty. Click a ship to
play a short takeoff and Connect directly. Escape or the same button
closes the Hangar. Do not open it
outside blank Home or if its panel cannot fit.

## Home Panel Settings

Follow the [approved layout](home-panel-references/home-panel-settings.png).
Center the double-chamfered popup above the live telemetry tray with a 16
logical px gap. Match the painted tray width, including its 600 px cap;
when the tray is narrower than the readable popup minimum, keep the
form at that minimum, capped by the logical viewport. The contents may
scroll on a short output while the footer remains available.
The English form groups panel width, Keyboard guide, and Cursor size
under Layout, and video playback under Wallpaper. Cursor size ranges
from 16 to 64 logical px (default 24). Save updates the Mothership
pointer immediately and keeps the value for the next session; Cancel
leaves it unchanged. The cursor theme may use its closest image size.
This does not change the host desktop or mouse sensitivity. Keep the Map closed while Settings owns input:
`M` and the dock Map button do not navigate away or discard edits.
Slide up on every open and down on every close, with a 250 ms ease-in-out.
Skip motion when GTK animations are disabled. The tray and dock stay put.
Save applies the draft; Cancel or Escape rolls back unsaved changes. The
Show toasts choices are inline controls and do not open a separate menu.
The **Display** link appears only in native mode. It opens a compact board
above the same telemetry tray with monitor identity, Resolution, Refresh
rate, Scale, Cancel and Apply. See the [native Display flow](../flows/use-native-dev-session.md#change-one-native-display).

### Keyboard Shortcuts

Open **Keyboard Shortcuts** next to **Display**. The editor changes the
Win key bindings for Close focused window, Node App Grid, and Node
Settings, and switches the Node group between 1–9 and F1–F9. These use
physical key positions. Single-key Home and Map controls, Alt+Tab, and
session recovery keys do not change. Defaults only edits the draft.

**Save** sends the new table to desktopd. It rejects conflicts, waits for
WM to apply the entire table, and then writes `shortcuts` in the shared
configuration. It restores the prior table if saving fails. No logout
is needed between edits. Cancel discards the unsaved draft. The Node key
guide reads the saved App Grid and Node Settings bindings.

### Node Volume

The Node speaker button opens an analog glass board above that button,
styled like the Notification Log. Each app in this Node with an
identifiable playback stream has one row: app name, volume, and a mute
button. Shared or unidentifiable streams are excluded rather than
changing their audio in another Node. Home's output-volume readout is
separate. Escape, the close button, clicking outside, or leaving the
Node closes the board.

The icon by the Node name toggles temporary mute for that Node. It does
not change any volume level. When toggled off, restore only streams the
toggle muted, and keep pre-muted apps muted. Per-app mute editing is
disabled while the Node is muted. Scope uses the current desktopd
membership and the playback process; it never changes the default sink.
The controls require a local PulseAudio-compatible playback server
(for example PipeWire Pulse) and `pactl`.

## Home keyboard guide

Show a small, noninteractive Map-style key strip above the dock on blank
Home. Align its left edge with the dock, not the viewport, and keep a 16
logical px vertical gap. Render only the **A**, **M**, **W**, **/**, **,**,
**R**, and **N** keycaps, without text captions. Omit **W** if Default Node is disabled. Wrap at
narrow widths, and hide it when Map, Settings, Slash, App Grid or Command
Center is open. The guide is on by default. Save **Keyboard guide** under
`home_panel.show_keyboard_guide`; Cancel leaves the old value. This setting
never changes the actual shortcuts or the guides in other views.

## Actions and readouts

| Action | Expected behavior |
| --- | --- |
| Map / Command Center | The existing acknowledged Map actions and Command Center. Opening the latter in Map suspends map-specific input until it closes; outside the dock or open controls the previous view keeps its input behavior. |
| Reload | Same explicit validation as Home **R**. No filesystem watcher. It is unavailable outside blank Home. |
| Audio | Read-only level/mute state of the default PipeWire output via `wpctl`; unavailable state without it. No mixer or volume mutation in this slice. |
| Panel Settings | Use the contextual comma shortcut rather than a dock button. Preview width in logical px and set **Video wallpaper**. Cancel restores width and leaves video unchanged. Turning video off uses the bundled image and keeps the selected MP4 path. Map Settings remain separate. |
| Lock | Request lock for the shell's active local Wayland login, only when the same login can be validated. Report success only if logind acknowledges `LockedHint`. If it is not available or confirmed, say so; no in-app imitation. |
| `,` | Toggles Panel Settings on focused blank Home. In Center Map the same key addresses Map Settings. Editable fields keep the literal comma. |
| App Grid / `A` | Open the [App Grid](app-grid.md) on focused blank Home. The dock button also closes it; the button is unavailable in Map and Settings. |
| `W` | Enter the internal [Default Node](../flows/use-home-inbox.md) from blank Home when enabled. |
| `N` | Toggle the Notification Log on focused blank Home. Retain normal typing in Slash and App Grid. |
| `/` | Open the Slash palette on focused blank Home. It is not a global shortcut or a terminal. |

## Slash palette

The same palette opens in a Node with **Win + /**. See the
[Node command scope](../flows/use-command-center.md#slash-in-a-node).

Center the input and analog dropdown together above the dock, with the same
width up to 480 logical px. Open the dropdown upward. Use spacious 48 px
rows, app icons, and smaller analog corners on the selected row. Show no
heading or redundant caption. Keep **↑**, **↓**, **Tab**, **Enter** and
**Esc** as bare keycaps in a separate footer. Scroll the bounded list with
the wheel or move the selection with the arrow keys.

- An empty prompt lists `apps`, `nodes`, `windows`, `maps`,
  `notifications` and `settings`. Complete a module to show its available
  commands. `windows <query>` searches live windows in every Node, also
  from a Node; see [Search windows](../flows/search-windows.md).
- `apps <query>` searches the installed catalog and launches with the App
  Grid's launch rules; `apps` alone lists the catalog. It never runs
  arbitrary shell text.
- `nodes connect <name>` focuses an existing named Node. `nodes new
  <name>` creates and enters a Node. `nodes delete <name>` and `nodes
  force-delete <name>` show confirmation before submission and report the
  service result. Force delete moves open windows to Default Node.
- A module with one action has no `open` sub-command: `maps` opens
  Center Map, `notifications` the Notification Log and `screenshot` the
  capture tool. `settings`, `settings display` and
  `settings shortcuts` open the same panes as their Home controls without
  changing or saving a setting. Display controls require a native session;
  nested Display shows this restriction and keeps controls disabled.
- Click or Tab completes only. Enter on `maps`, `notifications`,
  `screenshot` or `settings` opens it; Enter on `apps`, `nodes` or an
  incomplete command fills the next stage. Enter on an app or Node result launches or connects;
  Enter on a selected view command opens it. While Connect or New is in
  progress, keep the command visible and ignore repeated submissions.
  Close after the acknowledged Node selection. On failure, show the
  desktop service error and keep the typed command editable. Esc dismisses
  the palette without cancelling a submitted request. Clicking outside both
  the entry and dropdown also dismisses it without activating the dock or
  background underneath. Leave key handling to the input method while it
  composes text.

Sample physical CPU cores (combine SMT siblings when topology is available)
and RAM from kernel counters once per second. Display
RAM as total less available. NET is traffic in decimal Mb/s on the physical
IPv4 default interface with the lowest metric, not capacity or per-app
internet usage. Show unavailable or warming counters as a dash, and keep zero
a valid sample. Show local 24-hour time and day/month below it. Refresh only
the telemetry widget from samples, never use animation ticks for readings.

### Health

The [approved visual](home-panel-references/home-health.png) puts an unmarked
26 px ring with a glowing center before CPU. It fits the existing telemetry
height. Hide the lamp if the telemetry area is narrower than 156 logical px.
Slash opens above the tray; the lamp stays visible and keeps its state. Keep the wing positions and remaining readouts unchanged
except for giving up horizontal space inside the telemetry tray.

Health summarizes host CPU/RAM load, not system faults, storage, NET or GPU.
Use raw aggregate CPU counter deltas across approximately 10 seconds and
RAM used divided by total, where used excludes available memory. Initial
thresholds are 95%/99% CPU for amber/red, and 90%/97% RAM sustained for
five seconds. Either source can raise the level; red takes precedence.
Only show green if both readings are known and no warning qualifies.
Otherwise, when there is no qualifying warning and either reading is
missing or warming up, use gray. Expose the status and readings to assistive technology without a tooltip.

The ring stays fixed. The center pulses twice per cycle at 2.10 s green,
1.50 s amber or 1.05 s red regardless of the source. Gray is static.
Sampling keeps the existing one-second cadence; only the lamp widget is
invalidated by animation. Stop animation when hidden or when GTK animations
are disabled.

## Visual review

Open the original [840 px Home](home-panel-references/home-840.png),
[960 px Map off/on](home-panel-references/home-960-map.png) and
[slash concept](home-panel-references/home-slash.png). Latest Map off/on
reference owns the six actions including Lock; Home/Slash imagery predates
that button. The former dock-mounted WASD is superseded by the instruction
inside [Center Map](center-map-ui.md). Reference telemetry is illustrative only.

Use the same native build state at 100% and 150% scale. Compare each wing at
both widths, on-screen action bounds, end clearance, six equal faces, visible
diagonals and the distinct twin strokes. Measure the shortest gap from painted
edges. At 840 and 960 px, measure the Map-to-panel spacing at 100% and
150%; confirm the dock occupies its normal anchor and the telemetry does
not shift when the Map opens. Shrink the viewport and show all six
controls remaining accessible without a clipped right wing. Capture slash
under focus and test Escape, actual name selection and app typing. Verify width
Cancel/Save, restart and Home R preserve unrelated config. Test lock handling
with a fixture or explicitly approved login; do not lock a working desktop
just for automated evidence.

## Node Panel

Use the [approved three-zone concept](home-panel-references/node-panel.png)
with the Home lower-gap correction. A selected named Node or active Default
Node uses the same bottom anchor,
rail height and width preference as Home. The left wing holds mechanical
icon controls, the right wing holds the physical Volume and notification buttons,
and a single [recessed display](home-panel-references/node-panel-recess.png) fills the center. The outer panel is
mechanical hardware; the preview tray and controls inside it are one small
analog screen. Use display ink and tint inside, not mechanical button depth. The outer double contour climbs from each wing directly
into the raised center with one 45° shoulder; do not put a V-notch or
independent tray outline at the join. Keep the 45° corner cuts. The
[approved shadow detail](home-panel-references/node-panel-soft-recess.png)
uses a soft, low-contrast shadow inside all four edges and around their
chamfers. Use a near-black blue base for the recessed display. The upper
shadow is only slightly stronger. Keep the bottom
reflection faint and do not trace the cavity with an extra outline or
move the existing outer seam. Home and Center Map do not
host this preview strip.

The compositor owns preview pixels. It uses only the active Node's mapped
windows and their existing top-level buffers, without requesting new sizes
or repainting hidden apps for the strip. A missing buffer uses a neutral
placeholder. Thumbnails start at the left inset of the preview area and use stable
horizontal cells with tight vertical clearance. Unused space stays on the
right; growing gaps extend to the right without recentering the row.
When the fixed-size row exceeds its lane, clip it at the lane edges.
Wheel or trackpad scroll over the preview lane moves only that row
horizontally. App content, telemetry, and the rail do not move. A new
Node starts at the left edge. Hover enlarges the
target and, by a smaller amount, each immediate neighbor. **Hover zoom** in
Node Panel Settings sets 100–300% (default 100%) for the fully hovered card,
saved under `node_panel.hover_zoom_percent`. At 100% the card fills the
lane height; above it the card rises over the rail from the same baseline,
stays in front of its neighbors, and the gaps widen with it. Hover is
still tracked only over the lane. The visible gaps
expand in proportion to the lift on either side, with one easing curve for
size and spacing. Feet stay on the same baseline. Hover uses stable cells; clicks follow
the clipped, painted preview, not empty gaps.
The rail never moves, and the compositor stops scheduling
extra frames when the transition settles. Click focuses that window using
the Node's normal focus policy. Hover alone does not acquire keyboard focus. On focus change, scroll only
enough to reveal the selected preview; leave its position in the order
unchanged.

In [Stage](node-stage.md), keep every window in the tray, including the
presenting window or group. The focus marker moves between the existing
cells as focus changes. Apply the usual clipping and `+N` rules.

During a [Flow reorder](../flows/view-node-layouts.md#f-view-6), slide the
desktop frames and tray cards on the same compositor frames, easing over
about 190 ms. The tray stays horizontal in both Flow orientations. Keep the
focus corners and underline attached to the moving card. Within a group,
swap the member previews inside the shared outline. Keep the rail fixed
and scroll the tray only enough to keep the focused card visible.
Use the clipped, painted card positions for clicks and overflow throughout
the move. A new swap starts from the positions currently drawn. With reduced
motion, apply both positions immediately.

When some windows are outside the preview lane, show a `+N` button
at its right end, centered on the resting preview row. Use a flat analog
button with a high-contrast label, not a mechanical press. Use one soft
shadow around its edge for focus or the open state. Count windows with no visible thumbnail and only show
those windows in the thumbnail grid. Do not duplicate even a partially
visible thumbnail. Use up to three columns in the available width.
Show only window previews without headers or visible text; preserve the
window title as an accessible name. Click a preview to focus it. Use
W/A/S/D to move between cells or Enter to focus. Click outside or press
Escape to dismiss without activating the underlying window.

The [approved level projection](home-panel-references/node-alt-tab-projection.svg)
belongs to Alt+Tab, not the `+N` grid. A quick swap does not start it.
On further Tab presses while holding Alt, scroll the carousel only
far enough to reveal the selection. Project a straight, read-only copy of
the selected preview above the rail, including when its window already
occupies Stage. For a shared group tile, project the group and mark the
selected member. The compositor draws the projection directly from the
source thumbnail buffers without resizing the app or reading pixels back.
Use four softly shaded faces joining the corresponding four corners.
The frame edges stay level and are limited to the output. Open the light
and fade the image in over about 150 ms. Allow about 120 ms for a selection
change, from the last painted position, with no animation queue.
Only start the projection when the source thumbnail is visible.
**Preview zoom** in Node Panel Settings sets 100–300% (default 150%).
Save it under `node_panel.preview_zoom_percent`. At 100% leave only the
selection marker. With system motion off, place the preview immediately.
Ending the gesture clears all projection paint before focus changes.
Escape or loss of parent focus restores the prior carousel offset.
Mark the focused preview with four thin cyan corners and a short underline.
Crossfade the markers between previews in 150 ms without changing their
size or spacing. Clip markers to the same horizontal bounds as previews.
If animations are disabled, the same state change is immediate.

The left wing's fourth control, **Silent running**, latches with the
toggled cyan edge while the current Node runs silent. Node Panel Settings
adds a **Silent running: Off / Notices / Notices + audio** row. See
[Run silent in a Node](../flows/run-silent.md).

Node Panel Settings exposes the same **Flow / Stage** layout row in Default
Node and in a named Node. The compositor applies the choice before desktopd
confirms it. Each active projection retains its own choice for the live
session; changing Default Node does not change any named Node.

Node Panel Settings saves the independent preference
`node_panel.show_keyboard_guide` (default true). The guide displays `Meta`,
`Alt + Tab`, `Meta + Tab`, `Meta + A`, `Meta + N`, and `Meta + ,`, one
keycap per action. [Meta + Tab](../flows/view-node-layouts.md#f-view-7)
opens the Node switcher. Meta+A opens App Grid in the active Node without
requiring pointer focus on the panel. **Meta + N** opens the
[Notification Log](../flows/show-notifications.md#reading) for that Node.
A bare Meta tap returns to Home. Hide it when Node Settings or
App Grid is open. The guide does not create bindings or receive input. Use **Meta + ,** to
open Node Panel Settings from the focused app. Plain comma remains app
input in a Node. Use the same paired mechanical **Off / On** controls
as Home for Keyboard guide and Telemetry.

The recessed display shows no Node name or ship glyph; the preview lane
runs to the mechanical Volume and notification buttons. Keep the readouts vertically centered without a name row.

Telemetry in Node Panel Settings defaults off and is saved under
`node_panel.show_telemetry`. The readout shares the Home sampler.
The [shell sizing rule](shell-design.md#shell-sizing-and-mounted-modules)
applies: Telemetry fits in the left part of the center bay, while app
previews occupy the right. Reserve a readable minimum for previews; at
narrow widths, or when Telemetry is off, return its space to previews.
Opening Node Panel Settings hides the interactive previews, but keeps the
enabled telemetry readout in its usual position and at its usual width.
It still follows the normal narrow-width rule.
An inset divider separates the two displays. Do not add a floating frame
or a support foot. The outer rail, controls, and client area stay fixed. The Node bell starts Notification Log on the current Node filter,
including an empty Node; Home opens it on All. Default Node windows use
their own notification category, separate from Home system notices.
