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
accept pointer input. The last acknowledged named Node is marked in amber
in Overview. Choosing a Node enters its workspace normally, without a map
or background flight.

Session wallpaper is a separate paint-only surface under apps, the HUD and
Command Center. It covers the canvas without stretching in Home, Overview
and Node workspaces, and remains when the HUD is hidden.

Theme keys are optional. Restart to reload. `background`, `grid`, `text`
and `stroke` use `#RRGGBB`. The `opacity` fraction (0–1) applies only to
the glass tint, not the grid, symbols or labels. Default and sample opacity
are 0. The border defaults to 24 logical px inside the viewport on every side,
with 12×12 logical px 45° corners. Grid, markers, leaders and labels
normally keep 12 logical px from the painted border, measured normal to
each edge.
The grid pitch is 44 logical px at 100% zoom and it has no coordinate ticks or labels.
Use a separate minimum 16 logical px gap between future independent
components on this same layer. That is not the spacing inside a Node callout
and does not change the separate Command Center layer. `border_color` accepts `#RRGGBB` or `#RRGGBBAA` (default
`#6F91AC9F`, about 62% opacity). `stroke` colors normal
Node leaders, not the amber focus indicator. `icons` maps exact,
case-sensitive Node names (the current app's Node identity) to local SVG
files. Relative asset paths resolve beside the JSON. Omit a Node for a
diamond; an unreadable or unsupported icon logs a warning and falls back
to the diamond. An unreadable or malformed JSON uses defaults; individual
invalid colors and opacity fall back without dropping the other valid keys.
`font_family` names a single local system font (default `Sans`), and
`font_size` is 8–28 logical px (default 13). An unavailable family falls
back to Sans without resetting a valid size. Only Center Map labels use
these settings. No Node descriptions are invented.

## Navigate the Map

Open **Command Center → Map View**. Hold W/A/S/D to
pan. Escape or Done leaves the Map for the wallpaper-only Overview (the
Command Center entry remains); it does not restore or close apps. Open
**Command Center → Map View** there to re-enter. Focus loss stops panning
immediately. Diagonal panning
uses the same total speed as single-axis movement. Pan does not select Nodes.
The frame and grid stay fixed; the Node positions and labels move with the
map camera. **Fly to Node** is ordinary workspace selection after
acknowledgement. Fly does not animate the Map or wallpaper.

Without hardware GLES the wallpaper remains Clear and Node labels still work.
See [rendering and input decisions](../../docs/architecture/center-map-rendering.md).

While **Map View** is active, vertical wheel **inside the 45° frame** zooms
Node positions, icons and grid pitch around the pointer (65–225%). Labels
keep their font size and move with their icons. The wallpaper and frame stay fixed.
Off zooms immediately; Linear and Ease in/out apply each wheel step over a
short, interruptible transition. WASD pans the Nodes under
the anchored grid at the selected screen speed. No drag/grab is added;
wheel outside Map navigation follows the ordinary pointer target.

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
This changes only WASD, not Fly.

**Camera animation** controls WASD and wheel movement, not workspace selection:

| Setting | Hold/release WASD | Wheel zoom |
| --- | --- | --- |
| Off (0) | Immediate steady motion / immediate stop | Instant |
| Linear (1, default) | Immediate steady motion / immediate stop | Linear, about 90 ms |
| Ease in/out (2) | 140 ms acceleration, steady hold, 180 ms coast | Ease in/out, about 90 ms |

The settings JSON uses `"animation": 0`, `1`, or `2`. Missing or invalid
values default to Linear without changing other settings. Escape and focus
loss cancel movement immediately.

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
