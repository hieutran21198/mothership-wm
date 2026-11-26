# Center Map

**Center Map** shows a thin 45° frame, a stationary grid and live
Node labels in Overview. Only Nodes move when the map pans; the wallpaper,
frame and app windows do not. Node anchors are fixed in map space and their
paint is clipped inside the frame. **Command Center → Map settings** keeps
Clear/Frosted, spacing, camera animation and the optional debug readout.
Use [the user configuration](#user-configuration) to set preferences.
The [sample theme](themes/orbital.json) is a developer fixture.

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

Theme keys are optional in `center_map`. Apply external edits from blank Home as described below. `background`, `grid`, `text`
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
single-engine spacecraft marker; an unreadable or unsupported icon logs a warning
and falls back to that marker. An unreadable or malformed JSON uses defaults; individual
invalid colors and opacity fall back without dropping the other valid keys.
`font_family` names a single local system font (default `Black Ops One`, falling back to the shell body font), and
`font_size` is 8–28 logical px (default 13). An unavailable family falls
back to Sans without resetting a valid size. Only Center Map labels use
these settings. No Node descriptions are invented.

## Navigate the Map

Open **Command Center → Map View** or press **M** from a blank Home.
Hold W/A/S/D to pan. **Space** centers Mothership in the Map at the current
zoom, or stops at the nearest legal camera position. **M** toggles the Map.
**Q**, **Esc**, **M**, or a bare **Meta** tap leaves Map for blank Home
without reopening apps. Esc first dismisses a modal or Arcade. These shortcuts do not intercept
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
A still click on blank Map clears selection. Drag never starts over a
marker, name, minimap or frame. Release, leaving the frame or
losing focus stops it without drift. Wheel outside Map navigation follows
the ordinary pointer target.

Opening Map View in Linear or Ease in/out expands a centered line into
the chamfered frame (200 ms), then runs one left-to-right sweep over the
fixed grid (450 ms, up to 560 ms on a wider logical Map). Timing uses the
frame clock and the Map width in logical pixels, not the monitor scale.
After the sweep, visible Node icons grow in place together (90 ms), then
the names reveal left to right without moving (100 ms). Off shows the full Map immediately; closing cancels the sequence.

### Select and connect to Nodes

Click a Node marker or its name to select it. Its cyan selection outline,
name and dashed Mothership route confirm the selection. The Node Detail
card opens near the marker with its available description, activity, and
live app icons. Click blank Map to clear selection and dismiss the card
without closing the Map.

Press **Enter** or click the spacecraft icon in Node Detail to connect. Press **1–9** to connect
directly to a numbered Node; an empty slot does nothing. Nodes beyond nine
are still available by clicking them then pressing Enter, or through
Command Center. Choose **Disconnect** from a named workspace to return to
the same Map pan/zoom with the last connected Node selected, no card.
See the [Center Map UI contract](../../docs/software-knowledges/system/center-map-ui.md).

## Center Map Settings

Press **,** in Center Map, or open **Command Center → Map settings**.
Escape or Cancel restores saved options without closing the Map. Reset
previews defaults; Save applies the settings. Set the border in RGB or RGBA hex,
the outer margin (viewport to border), and the inner padding (painted border
to contents). **Glass** is **Clear** by default. **Frosted** softens only the wallpaper
behind the chamfered Map, below app windows. For video, the compositor
reuses the existing wallpaper texture and blurs a reduced-size surface
on the GPU, falling back to Clear when unavailable. Text, icons, grid
and apps stay sharp. Hiding the Map stops the video blur. Values preview immediately when the Map is visible. **Save**
applies and keeps them across sessions; **Cancel** restores the
last saved values. A save failure leaves the form open.

**WASD speed** has minus/plus steps of 10 percentage points, from 50%
to 200%; 100% is 530.4 logical screen px/s. Change the displayed percentage,
then **Save** to use it on the next movement without restarting. **Cancel**
restores the saved speed. `center_map.pan_speed_percent` overrides the
code default of 100 or the speed supplied by an explicit developer theme.
**Reset** and Save remove the user override.
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

Save patches the changed keys in the canonical `center_map` section.
It does not rewrite the meaning of other preferences or change wallpaper assets,
font files, or developer fixtures. Unset or out-of-range values fall back to
the defaults.
Spacing uses logical px (outer 0–96, inner 0–64)
and is capped for extremely small viewports without rewriting preferences.

## Map bounds

The independent Node plane is **10,240 × 5,760 logical px**: four by four
2560 × 1440 reference viewports. Camera travel is ±3840 × ±2160 at that
reference size and clamps against actual viewport dimensions. The
wallpaper uses fixed screen-space cover (or configured Contain). A map pan
does not invalidate the wallpaper, frame or grid.

The lower-left Map guide only shows inputs; it is not a set of buttons.
It includes Q, Esc, Space, M and Meta when the full guide fits. It highlights
held WASD and Space, valid wheel zoom, and a held primary press on
blank Map. The dock stays 16 logical px below the Map's painted edge.

## User configuration

The [Home Panel](../../docs/software-knowledges/system/home-panel.md) uses the
same file with an independent optional `home_panel.width` setting.

Keep preferences in **`~/config/.mothership/config.json`**. Modules and
keys use lowercase snake case; `center_map` and `wallpaper` are separate
top-level modules. Include only overrides. Missing sections and keys use
defaults in code. For example:

```json
{
  "center_map": {
    "opacity": 0,
    "animation": 0,
    "frosted": false,
    "pan_speed_percent": 120
  },
  "wallpaper": {
    "path": "assets/my-wallpaper.png",
    "mode": "contain"
  }
}
```

Zero and false are valid where supported. Save in Map settings writes
only to this file, including on first use. It does not alter other module
fields. If the file is malformed or managed through a symlink, UI Save
reports an error instead of replacing it. Startup never creates it.
From **blank Home** with Command Center closed, press **R** to reread the
file and its configured SVG and wallpaper assets. It does not restart the
session, reselect a workspace, or reset the Map camera. A rejected
reload keeps the running configuration and logs the error. Saving a file
does not reload it automatically. R is not a shortcut in Map View,
Command Center, Settings, or apps. Runtime sockets and desktopd's saved
Node data stay separate from preferences.

Compatibility: an explicit `MOTHERSHIP_MAP_THEME` or
`MOTHERSHIP_WALLPAPER_CONFIG` supplies a launch-specific baseline for
that module; keys in the canonical file take precedence. Those developer
paths and their relative assets are never copied or written by Settings.
Before the canonical file exists, the app also accepts legacy settings at
`$XDG_CONFIG_HOME/mothership-wm/center-map.json` (normally under
`~/.config`). The first Settings Save carries the legacy overrides into
the canonical file and leaves the old file unchanged. If you create the new
file yourself, include any old overrides you still want: missing keys never
fall back to the old settings file once a canonical file exists.

## Session wallpaper

Set `wallpaper.path` to a local PNG or H.264 MP4, relative to the
configuration file's directory or absolute. Leave it empty to use the
bundled Frost Ring. `wallpaper.mode` is `cover` (default; crop to fill) or
`contain` (fit without stretching). PNG is limited to 4096 px per axis;
MP4 is limited to 4096 × 2160.

An MP4 loops silently on the existing paint-only wallpaper layer.
In **Home Panel Settings → Video wallpaper**, off displays the bundled PNG
without changing the selected MP4 path or fit mode. Existing MP4 settings
remain enabled if no switch value has been saved; the JSON key is
`wallpaper.video_enabled`. Enable video only if its performance trade-off
is acceptable, especially at 4K.

The panel loads the GStreamer runtime on demand and prefers NVDEC if present,
with H.264 software decoding as a fallback. The normal devenv launch
provides the plugins. Video does not feed the static wallpaper blur cache;
the Map overlays clear glass instead. A corrupt file on Home **R** leaves
the previous wallpaper running. A startup or playback failure falls back
to static PNG. This affects only Mothership, not the COSMIC wallpaper.
