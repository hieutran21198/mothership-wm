# Open native apps inside the nested session

Supports [Open a terminal or file manager from Command Center](../goals/open-native-apps.md).

### F-APPS-1

The user clicks one of two fixed Shell actions. The Shell starts the
installed application as a distinct process on the exact child Wayland
socket, without an inherited connection, host activation, or X11 fallback.
The WM and desktopd observe its real Window lifecycle through the existing
path. Process start alone does not confirm that the app mapped.

### F-APPS-2

With a named Node selected, open each app. The new Windows are unassigned
in Home / Inbox, not in the selected Node. That Node and its keyboard
destination stay in place. The user can select each new Window ID in the
existing Attach / Move flow; launching is not assigning or flying.

### F-APPS-3

Without the supported executable or a live child socket, the Shell reports
which app cannot open and does not fall back to the host. On session exit
the Shell stops and reaps its own processes without terminating an
already-running external instance.
