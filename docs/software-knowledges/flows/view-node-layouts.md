# Navigate Overview and lay out a Node

Supports [Enter Nodes from Overview and choose a layout](../goals/view-node-layouts.md).

Shell displays Nodes and requests explicit actions. desktopd resolves named
Nodes and membership, remembers the acknowledged mode for each Node in the
live session and publishes selection only after WM acknowledgment. WM owns
visibility, geometry, pointer hit testing and keyboard focus.

### F-VIEW-1

Leave Dev for Overview. All app surfaces become ineligible for input.
Overview places Nodes in stable positions as they are first seen in this
Shell session. Enter Dev or Review explicitly; Home remains the distinct,
unchanged view for unassigned windows.

### F-VIEW-2

Use three actual native windows in Dev. Flow is the initial layout. It puts
members on one non-wrapping strip in stable order, horizontal in landscape
and square outputs, vertical in portrait. The layout commands
`flow-horizontal` and `flow-vertical` override the axis for this Node's live
session independently; `flow` restores automatic orientation.

Wheel on empty workspace or window chrome pans along the strip without
changing app size or focus. Wheel over app content remains app input.
If the parent delivers Alt+Tab, switch only within the active Node. A tap
returns to the previously used eligible window. Further Tab presses with
Alt held step through the captured recent order and project the selected
thumbnail above the carousel, without opening the `+N` grid. Shift reverses
the order. Escape or loss of parent keyboard focus cancels and restores
the carousel offset. Releasing Alt ends the projection and commits at most
one focus change. The app windows remain unchanged until commit.
The preview-tray `+N` button opens a separate grid of offscreen window
previews even if the parent intercepts Alt+Tab. Click a thumbnail to focus
that exact window. Commit in Flow centers the selected member without resizing it.
Clicking another Flow window or its preview uses the same transition. A new
focus redirects from the current slide position; manual wheel scrolling
cancels it. Longer moves have more time, while short hops stay quick.
Reduced motion centers immediately. For an independent Float,
raise it without moving the strip.

When an app requests Minimize, WM hides its main window and keeps its
preview, membership and geometry. It leaves the Flow strip without leaving
a gap. In Stage, minimizing the active window shows the most recently
used non-minimized window, or leaves Stage empty. Click the preview or
commit Alt+Tab to restore it. Use the Minimize button on the Mothership
frame for a standalone window. Scale the window towards the tray when
minimizing and back from the tray when restoring. Respect reduced motion.
Alt+Tab can restore even the only window in a Node; cancel does not
restore it. This also applies to Default Node.

The [native frame](../system/window-frame.md) controls individual windows.
A float keeps its stable slot but occupies no strip space; returning it
reinserts it in current order. Float bounds are separate from Flow bounds.
Maximize is explicit and restores the previous mode and bounds. With motion
enabled, the frame and app content scale together between their current
bounds and the workspace edge above the rail. A second click on the moving
Maximize/Restore control reverses from the drawn bounds. The app receives
pointer input again when its live frame returns. With reduced motion or an
unavailable capture, apply the final placement immediately. Changes do not
restart apps or change membership. Float or Return slides from the
current painted position to the destination when the positions differ.
If needed, request the final client size once; the slide only translates
the compositor scene. The older explicit Stack, all-
Float and Tiling modes remain compatible; Flow does not depend on Tiling.

Repeat Maximize and Restore in a named Node and Default Node at 100% and
150%. Try a strip window and a moved, resized Float. With a client that
draws its own title bar, repeat using its Maximize and Restore controls.
Check reduced motion,
a second click during the transition, and a client that delays its resize.
Check the protocol trace for one final size request per toggle. Close or
minimize the window, change Node or layout, and resize the output during
motion. Verify that the captured frame clears and pointer input follows
the live frame.

### F-VIEW-3

Leave Dev, enter an empty Review and return to Dev. Review stays empty and
no hidden window receives input. Dev keeps its last mode; the WM restores
an eligible previous focus or leaves app focus clear.

### F-VIEW-4

Open three native Wayland windows and choose Stage in Node Panel
Settings. Click another preview. Check that both thumbnails keep their
slots and the focus marker follows the selected window. Scroll until a
thumbnail is fully clipped and verify that it appears in `+N`, including
when its window is presenting.

Tap Alt+Tab to switch. Start another gesture and press Tab repeatedly
with Alt held until the selection returns to the presenting window. Check
that its hologram uses its own tray thumbnail, with the app focus and size
held until commit. Release Alt on the presenting window to dismiss the
hologram without a Stage transition. Repeat with Shift to reverse, and
test that Escape and loss of parent keyboard focus restore the prior tray
position. Use a session that delivers Alt+Tab to the WM.

Check the [Stage layout](../system/node-stage.md) at 100% and 150% scale.
Capture the full viewport and matching tray crops for the switch and the
hologram. Repeat with Motion Off and a compact output. Close the focused
window and check that the next eligible window takes its place. Return to
Flow and check that the apps keep their Node membership, Flow sizes, and
order. Repeat in Default Node and check that each Node keeps its own layout.

### F-VIEW-5

On a wide output in Flow or Stage, drag a tray thumbnail onto a second
thumbnail. In Flow, you can also drag the frame's title rail on the desktop.
The potential group shows focus corners while the pointer rests on a valid
target. At 300 ms the corners brighten. Release to join, or leave the target
to cancel. A group accepts up to four windows. Use two columns, a large left
tile and two right tiles, or a 2×2 grid for two, three, or four members
respectively. Keep groups local to the Node and the compositor session.
Both layouts retain stored groups on compact outputs and present their
members individually.

A tiled group occupies one stop on the Flow strip or the main area on
Stage. Its members share an outline in the preview tray. Hover enlarges
the shared tile. Alt+Tab selects an individual window for keyboard focus
and commits to its group as a unit. Flow centers the group when switching
to it. A focus change within the group leaves its tiles in place. Animate
the joining app in Flow using a preview and apply the final size when
needed. Respect reduced motion.

Use the split-layout icon on the Group frame or right-click its tray
thumbnail to open Group Settings. The frame control anchors the settings
popover to its icon. Edit Split and Tiling Gap, or choose Ungroup. The
minimize dash on the Group frame hides the whole group and keeps its tray
entry. Click a member or commit Alt+Tab to restore the group with its split
and gap. Groups stay with their Node across view changes and
minimize/restore. Individual Minimize, Maximize and Float are unavailable
in a tiled group. Close affects only that window. If one window remains,
the WM dissolves the group and treats the remaining window as standalone.

The Group frame uses one thin chamfered perimeter. Draw the Black Ops One
heading and the two icons directly over the scene. The scene stays visible
through the space around and between member frames. Leave 12 logical pixels
between the group header or perimeter and the individual window frames.
Tiling Gap changes the space between those frames. Keep client content
rectangular and unblurred. Use fixed, separate hit targets for the two icons.

With real Wayland clients on Stage, join two thumbnails and check that
selecting either member moves focus without moving the tiles. Minimize,
restore, adjust the split and gap, then ungroup. Verify that a compact
output shows one member and that widening it restores the layout. Capture
the viewport and matching header crops in Flow and Stage at 100% and 150%
scale. Check that the scene shows through the header, edges, and gutters.
Check the icon hit targets and popover anchor
against the live scene.

### F-VIEW-6

Focus an app in a named Node or Default Node in Flow. For a standalone
window, press **Meta + Shift + Left/Right** in horizontal Flow or
**Meta + Shift + Up/Down** in vertical Flow. Each press swaps with the
adjacent standalone Flow stop. A group boundary or the end of the strip
stops the move. Minimized, floating and maximized windows are ineligible.
Holding a key makes one move. A shell overlay that owns keyboard focus
keeps control of its keys.

Inside a tiled group, **Meta + Shift + Arrow** swaps with a neighbor in
that screen direction within the same group. The group keeps its place
and its tile geometry. Each window takes its destination tile's bounds.
The preview tiles swap inside the shared tray outline. In compact Flow,
where group members appear as individual windows, use the Flow axis and
swap only adjacent windows with the same group membership.

The WM commits one order for the desktop and preview tray without changing
membership, app focus or Alt+Tab history. The Node keeps this order when
reopened or switched to Stage and back. Temporarily unmapped windows keep
their position. A closed or reassigned window leaves the order. New members
join at the end. The [Node Panel](../system/home-panel.md#node-panel) defines
the synchronized swap motion.

Use three native Wayland windows of different sizes in a disposable nested
session. Focus B and start with A, B, C. Move B toward C and verify A, C, B
on both surfaces and that typing still goes to B. Reverse while moving,
hold the arrow, try both ends and repeat in vertical Flow and Default Node.
Open another Node and return, switch to Stage and back, and temporarily unmap
and remap a member. Verify focus, order and membership in each case.

Group B and C beside standalone A. Attempt to swap A into the group and
try an arrow pointing outside the group from B or C. Verify that both
surfaces keep their order. Swap B and C within the group. Verify that the
group stays in place, the windows trade tiles on both surfaces, and focus
follows the same window. Repeat with group members shown individually in
compact Flow and verify that group boundaries still stop swaps.

Record the desktop and tray in one capture at 100% and 150% scale, with
matching detail crops before, during and after a standalone and a group
swap. Repeat with a clipped preview, reduced motion and a window closing
mid-swap. Check that clicks follow the visible frame or card during motion
and that `+N` counts only hidden cards. Use a Wayland trace to verify that
the handled arrow does not reach the app. A standalone swap sends no new
client sizes. Within a group, send a final size only when the destination
tile requires it. Animation frames send no size requests. Record native
hardware frame pacing separately using the
[render runbook](../../runbooks/render-performance.md).
