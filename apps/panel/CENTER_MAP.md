# Center Map

**Center Map** shows a thin 45° frame, a stationary grid and live
Node labels in Overview. Only Nodes move when the map pans; the wallpaper,
frame and app windows do not. Node anchors are fixed in map space and their
paint is clipped inside the frame. **Command Center → Map settings** keeps
Clear/Frosted, spacing, camera animation and the optional debug readout.
The sample theme is optional:

```sh
MOTHERSHIP_MAP_THEME="$PWD/apps/panel/themes/orbital.json" devenv shell --offline -- task mothership:run
```

The launcher prints `MOTHERSHIP_WAYLAND=/…/mothership-…/wayland-…`. In a second
terminal, use the containing directory as the runtime directory **for these
commands only**. Replace the example path with the actual one:

```sh
export XDG_RUNTIME_DIR=/run/user/1000/mothership-EXAMPLE
./bin/mothership-desktopd create Dev
./bin/mothership-desktopd create Review
./bin/mothership-desktopd focus Review
./bin/mothership-desktopd overview
./bin/mothership-desktopd map show
./bin/mothership-desktopd map hide
./bin/mothership-desktopd map show
```

Overview is an explicit view selection; `map show` and `map hide` are
idempotent presentation controls. They do not select Overview, move Nodes
or windows, or request keyboard focus. They control the HUD only in
Overview; the visibility preference starts enabled. The Map does not draw over an app workspace and does not
become a GTK pointer target. The nested WM routes Map View input to the shell. The last acknowledged named Node is marked in amber
in Overview. Choosing a Node enters its workspace normally, without a map
or background flight.

Session wallpaper is a separate paint-only surface under apps, the HUD and
Command Center. It covers the canvas without stretching in Home, Overview
and Node workspaces, and remains when the HUD is hidden.

Theme keys are optional. Restart to reload. `background`, `grid`, `text`
and `stroke` use `#RRGGBB`. The `opacity` fraction (0–1) applies only to
the glass tint, not the grid, symbols or labels. Default and sample opacity
are 0. The border defaults to 24 logical px inside the viewport on every side,
with a 1.5 logical px outline and 12×12 logical px 45° corners. Grid, markers and labels
normally keep 12 logical px from the painted border, measured normal to
each edge.
The grid pitch is 44 logical px at every zoom and it has no coordinate ticks or labels.
Use a separate minimum 16 logical px gap between future independent
components on this same layer. That is not the spacing inside a Node callout
and does not change the separate Command Center layer. `border_color` accepts `#RRGGBB` or `#RRGGBBAA` (default
`#7895A5`). The inner steel line follows the border color and opacity at a 5 logical px perpendicular inset. The former `stroke` key is accepted for compatibility but no longer styles a Node leader. `icons` maps exact,
case-sensitive Node names (the current app's Node identity) to local SVG
files. Relative asset paths resolve beside the JSON. Omit a Node for a
hollow diamond marker; an unreadable or unsupported icon logs a warning
and falls back to that marker. An unreadable or malformed JSON uses defaults; individual
invalid colors and opacity fall back without dropping the other valid keys.
`font_family` names a single local system font (default bold `Chakra Petch`, with a system monospace fallback), and
`font_size` is 8–28 logical px (default 13). An unavailable family falls
back to Sans without resetting a valid size. Only Center Map labels use
these settings. No Node descriptions are invented.

## Navigate the Map

Open **Command Center → Map View** or press **M** from a blank Home.
Hold W/A/S/D to pan. **M** toggles the map; **M** or **H** leaves
the Map for blank Home, without reopening or closing apps. **Esc** only
dismisses an open Node profile. **H** also returns to blank Home when Command
Center owns the keyboard from a Node. These shortcuts do not intercept
modified keys, typing in apps, or text input in Command Center. Focus loss
stops panning immediately. Diagonal panning
uses the same total speed as single-axis movement. Pan does not select Nodes.
The frame and grid stay fixed; the Node positions and labels move with the
map camera. **Connect** prepares the real workspace without changing focus. The signal
travels out; after the WM confirms readiness, the monitor returns and the
Map retracts as the real workspace opens from the center. Apps use their
existing buffers, and late apps appear when ready. Off and the 1–9 shortcuts
skip this flourish. The Map camera and wallpaper do not move.

Without hardware GLES the wallpaper remains Clear and Node labels still work.
See [rendering and input decisions](../../docs/architecture/center-map-rendering.md).

While **Map View** is active, vertical wheel **inside the 45° frame** zooms
Node positions and icons around the pointer (65–225%). Labels
keep their font size and move with their icons. The wallpaper, frame and grid stay fixed.
Off zooms immediately; Linear and Ease in/out apply each wheel step over a
short, interruptible transition. WASD pans the Nodes under
the anchored grid at the selected screen speed. Hold the left button on
empty space inside the frame and move to pull the Nodes with the pointer.
A still click dismisses the open Detail as before. Drag never starts over
a marker, name, Detail, minimap or frame. Release, leaving the frame or
losing focus stops it without drift. Wheel outside Map navigation follows
the ordinary pointer target.

Opening Map View in Linear or Ease in/out expands a centered line into
the chamfered frame (200 ms), then runs one left-to-right sweep over the
fixed grid (450 ms, up to 560 ms on a wider logical Map). Timing uses the
frame clock and the Map width in logical pixels, not the monitor scale.
After the sweep, visible Node icons grow in place together (90 ms), then
the names reveal left to right without moving (100 ms). Off shows the full Map immediately; closing cancels the sequence.

### Node Quick Profile

Click a Node marker or its name to show one compact profile. Clicking a
different Node switches it; clicking empty Map space or its margin dismisses
it. The default marker gains a separate cyan rim and a brighter hollow
diamond when selected. Its center and the gap between rings stay clear.
A custom SVG keeps its artwork and only gains an outer selection outline.
The map label brightens until the profile closes. The profile stays beside its Node through pan and zoom, but closes if
the Node leaves the frame. Text and controls keep their screen size.
The Connect button uses the existing acknowledged workspace switch;
opening a profile never switches workspaces. The Map shows a dashed
Home-to-Node route for its selection. Press an assigned number (1–9) in
Map View to connect; a still-unassigned number does nothing. Nodes without
numbers remain available with the mouse or through Command Center.
Choose Disconnect from a named workspace to return to the previous Map
pan and zoom with the last connected Node selected. The card can show a real
description, age since creation (Running time), last acknowledged entry
(Last active), and distinct mapped member-app icons. Missing data stays
hidden; the card reports an empty Node only if live membership is known.
No agent, notification, or volume UI ships here. See the
[Center Map UI contract](../../docs/software-knowledges/system/center-map-ui.md).

## Center Map Settings

Open **Command Center → Map settings**. Set the border in RGB or RGBA hex,
the outer margin (viewport to border), and the inner padding (painted border
to contents). **Surface** is **Clear** by default; **Frosted** softens only
the static wallpaper behind the chamfered Map, below app windows. Text,
icons, grid and apps stay sharp. Hiding the Map restores the clear
wallpaper. Values preview immediately when the Map is visible. **Save**
applies and keeps them across sessions; **Cancel** restores the
last saved values. A save failure leaves the form open.

**WASD speed** has minus/plus steps of 10 percentage points, from 50%
to 200%; 100% is 530.4 logical screen px/s. Change the displayed percentage,
then **Save** to use it on the next movement without restarting. **Cancel**
restores the saved speed. The optional `pan_speed_percent` integer in the
Map theme sets the default (100 if absent or invalid). Save writes an
override to `mothership-wm/center-map.json` under `$XDG_CONFIG_HOME`;
**Reset** and Save remove that override so future theme defaults apply.
This changes only WASD, not Connect.

**Camera animation** controls WASD and wheel movement, not workspace selection:

| Setting | Hold/release WASD | Wheel zoom |
| --- | --- | --- |
| Off (0) | Immediate steady motion / immediate stop | Instant |
| Linear (1, default) | Immediate steady motion / immediate stop | Linear, about 90 ms |
| Ease in/out (2) | 140 ms acceleration, steady hold, 180 ms coast | Ease in/out, about 90 ms |

The settings JSON uses `"animation": 0`, `1`, or `2`. Missing or invalid
values default to Linear without changing other settings. Closing the Map or
losing keyboard focus cancels movement immediately.

**Show FPS** is off by default. While the Map is open it displays **Render
FPS**, which counts changed Node-layer render samples, not display
presentation or the monitor refresh rate. The readout has no input target
and does not create a continuous redraw.

These overrides use `mothership-wm/center-map.json` under
`$XDG_CONFIG_HOME` (normally `~/.config`). Only these overrides
are written; the theme JSON, font, icons, tint and wallpaper file are
not modified. Unset or invalid overrides retain their defaults from
the theme and map layout. If the selected wallpaper is missing, the procedural fallback stays clear.
Spacing uses logical px (outer 0–96, inner 0–64)
and is capped for extremely small viewports without rewriting preferences.

## Map bounds

The independent Node plane is **10,240 × 5,760 logical px**: four by four
2560 × 1440 reference viewports. Camera travel is ±3840 × ±2160 at that
reference size and clamps against actual viewport dimensions. The
wallpaper uses fixed screen-space cover (or configured Contain). A map pan
does not invalidate the wallpaper, frame or grid.

## Session wallpaper

The bundled frost-ring PNG is the default. For a separate configuration,
copy [themes/wallpaper.json](themes/wallpaper.json) and set
`MOTHERSHIP_WALLPAPER_CONFIG` to the config path before launching:

```sh
MOTHERSHIP_WALLPAPER_CONFIG="$PWD/apps/panel/themes/wallpaper.json" ./bin/mothership
```

`path` is a local PNG path, relative to this JSON or absolute. Omit it to
use the bundled image. `mode` is `cover` (fill the canvas, crop if needed)
or `contain` (show the whole image with dark bars if needed). Both center
the image without stretching. Mode defaults to `cover` if absent or invalid.
A missing or malformed configuration uses the bundled image; an unreadable or
invalid selected PNG falls back to the bundled image (procedural only if unavailable).
The selected PNG is limited to 4096 px on either axis. Wallpaper
loads once on launch and is never applied to COSMIC itself.
