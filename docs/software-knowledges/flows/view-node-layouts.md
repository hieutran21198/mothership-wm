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

Use three actual native windows in Dev. Tiling lays out all mapped windows
without overlap and is the default. Stack displays one window at a time;
Next switches to another mapped member. Float allows a window to be raised,
moved by its top handle and resized by its corner. Layout changes do not
change membership, restart windows or alter a different Node.

### F-VIEW-3

Leave Dev, enter an empty Review and return to Dev. Review stays empty and
no hidden window receives input. Dev keeps its last mode; the WM restores
an eligible previous focus or leaves app focus clear.
