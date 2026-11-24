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
If the parent delivers Alt+Tab, cycle only within the active Node and
commit one focus change when Alt is released. Use **Next window** if the
parent intercepts that key. Commit centers a strip member without resizing
it; for an independent Float, raise it without moving the strip.

The [native frame](../system/window-frame.md) controls individual windows.
A float keeps its stable slot but occupies no strip space; returning it
reinserts it in current order. Float bounds are separate from Flow bounds.
Maximize is explicit and restores the previous mode and bounds. Changes
do not restart apps or change membership. The older explicit Stack, all-
Float and Tiling modes remain compatible; Flow does not depend on Tiling.

### F-VIEW-3

Leave Dev, enter an empty Review and return to Dev. Review stays empty and
no hidden window receives input. Dev keeps its last mode; the WM restores
an eligible previous focus or leaves app focus clear.
