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
then clipped stationary label. The label paint is cached during its
reveal; no Node polls or separate timers extend it. Panning or zooming never selects a
Node. Fly is normal workspace selection after an acknowledgement.

The nested WM owns applications, geometry, seat and input. From back to
front it composites a solid fallback, session wallpaper, applications,
Center Map, and Command Center. Wallpaper and Map are paint-only. An
explicit Command Center → Map View click acquires the shell keyboard
target and retains it only while entering the acknowledged Overview. A
private Wayland protocol routes vertical wheel inside the mapped Map's
chamfered frame to that focused shell, not outside it or over a control.
The same private protocol carries scoped left-button sequences that start
in otherwise untargeted Overview space while Map View owns the keyboard.
Panel applies current marker, label and overlay hit tests. Only an
empty interior press can become a drag after a movement threshold; translate
by the inverse zoom to keep the map under the pointer. The WM ends routing
on focus loss or target change and consumes a cancelled sequence's release.
There is no passive Map motion stream outside arcade or any cross-window
or global grab. Arcade opts into passive pointer coordinates on the same
private Wayland object only while focused in the active Map. The nested
seat supplies logical coordinates, and the simulation consumes the latest
aim on its existing frame clock, not a redraw per mouse event.
A blank Home has no app keyboard target, so the focused nested shell can
handle unmodified M/H without a global shortcut. M toggles Map View, H
returns to blank Home, and Esc clears the selected Node. While requesting
Map View, the focus carrier stays transparent; Overview is not a menu.
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

Fleet arcade replaces the static Map content within the same frame, not
the wallpaper or dock. Its chamfered field is one immutable texture at
the actual device scale, rebuilt on arcade entry or a changed allocation
or scale. Dynamic sprites, shots, and effects use a rectangular clip
inside the corner cuts. They never turn the entire static field into a
per-frame alpha mask. Oversize fields or allocation failure use retained
Cairo for the field alone. Resize does not reset the simulation.

See the [use flow](../software-knowledges/flows/show-center-map.md) and
[native guide](../../apps/panel/CENTER_MAP.md). For instrumentation and A/B checks,
use the [render performance runbook](../runbooks/render-performance.md).
