# Center Map rendering and input

Status: proposed for architecture review. This note records the runtime
architecture, not evidence of native-session acceptance.

The Panel owns a 10,240 × 5,760 logical map with fixed Node anchors. WASD
moves only the Node projection through the fixed 45° frame and grid. The
wheel zooms the Node positions and icons around the cursor
within bounds; text keeps its font size. Off applies immediately; Linear
and Ease animate briefly on the frame clock. Nodes and their annotations
are clipped and drawn above the grid. The entry effect paints a transient frame and then a clipped radar gradient
once, using monotonic elapsed frame time and keeping the grid retained.
Only the logical frame width can lengthen the radar duration within its cap;
device scale and submission cadence do not affect its timeline. The
radar uses a private paint-only Wayland subsurface when available. The
compositor damages only the old and new bounds of the moving strip; the
static grid and frame stay in GTK. The middle strip is immutable, and
temporary edge/lock buffers are bounded and reused only after compositor
release. If the small buffers or required protocol are unavailable, the
same timeline uses the in-HUD GTK renderer. The
annotation layer then reveals all visible Nodes on one timeline: icon scale,
leader stroke, clipped stationary label. The label paint is cached during its
reveal; no Node polls or separate timers extend it. Panning or zooming never selects a
Node. Fly is normal workspace selection after an acknowledgement.

The nested WM owns applications, geometry, seat and input. From back to
front it composites a solid fallback, session wallpaper, applications,
Center Map, and Command Center. Wallpaper and Map are paint-only. An
explicit Command Center → Map View click acquires the shell keyboard
target and retains it only while entering the acknowledged Overview. A
private Wayland protocol routes vertical wheel inside the mapped Map's
chamfered frame to that focused shell, not outside it or over a control.
The same protocol carries completed left clicks on empty canvas only while
Map View owns that keyboard target. Panel applies Node and card hit
testing in logical coordinates, independent of app routing. There is no
passive pointer stream, pointer grab or new global shortcut.
A blank Home has no app keyboard target, so the focused nested shell can
handle unmodified M/H without a global shortcut. M toggles Map View, H
returns to blank Home, and Esc closes only the profile when one is open.
Neither app keyboard routing nor shell text input is intercepted. Closing
the Map or losing outer focus stops movement; returning to Home leaves
prior apps running without showing or focusing them.

Use GTK's native Wayland stack and OpenGL ES through EGL for the retained
grid and wallpaper and optional static Frost. The wallpaper fits the
viewport with an aspect-preserving cover or contain transform. Camera
changes do not invalidate it; WASD does not redraw the fixed grid or frame.
Wheel updates also leave the grid texture and phase unchanged. The physical nested buffers
follow the parent scale without changing logical geometry. The optional
Render FPS counts changed Node samples while the Map is open; it is not
display presentation.

Frosted caches a bounded downsample of the wallpaper in the GPU context,
aligned with Clear and clipped to the same outline. It never samples apps,
icons or text and does not rerun on movement. If GLES is unavailable, use
Clear with the normal Map; no dynamic CPU blur or second renderer.

See the [use flow](../software-knowledges/flows/show-center-map.md) and
[native guide](../../apps/panel/CENTER_MAP.md). For instrumentation and A/B checks,
use the [render performance runbook](../runbooks/render-performance.md).
