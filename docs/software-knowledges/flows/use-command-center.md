# Create a Node, move a window and Fly

Supports [the Command Center Goal](../goals/use-command-center.md). The
Command Center reads live state from desktopd. WM owns visible windows
and keyboard input; the UI itself is not assignable to a Node.

| Step | Part | Action |
| --- | --- | --- |
| Read | Command Center | Subscribe to the private snapshot and updates. The current view is confirmed only by desktopd state. |
| Create | Command Center → desktopd | Submit an empty Node name without selecting it. |
| Attach or move | Command Center → desktopd → WM | Submit one live session and Window ID and a target Node. Reconcile the current view if affected, without selecting the destination. Confirm after the WM applies the change. |
| Fly or Home | Command Center → desktopd → WM | Explicitly select the named Node or Home. Return input to the eligible destination window or clear focus. |

### F-COMMAND-1

With two live same-app windows in Home, create Dev and Review while Home
remains selected. Select each distinct Window ID and attach both to Dev.
A stale ID is rejected, not replaced by another window.

### F-COMMAND-2

With A and B in Dev and B focused, move B to Review without Fly. Dev stays
selected; B loses visibility and input; the next key goes to A. Moving
the last eligible focused window clears input. Do not report success if
the WM projection is not acknowledged.

### F-COMMAND-3

Fly to Review and confirm it contains the same B and its in-window state.
Use Home separately to return. No command restarts a window. When the UI
is dismissed it does not retain keyboard focus.
