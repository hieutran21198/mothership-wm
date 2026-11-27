# Open an app file chooser

See [Open and save files from an app](../goals/open-app-file-dialogs.md).

The user starts an open, save, or folder-selection action from a Wayland app
running in Mothership.

| Step | Executor | Operation and outcome |
| --- | --- | --- |
| 1 | App | Request a chooser on the Mothership session bus or open a toolkit dialog. |
| 2 | File chooser portal and backend | For a portal request, open the chooser on the session's Wayland display and return the selected paths or cancellation to the app. |
| 3 | WM | For a chooser with a valid Wayland parent, show it above the parent in the parent's current layout, give the chooser keyboard focus, and restore focus to the parent on close if it is still visible. The chooser shares the parent's projection and consumes no layout slot. |

Mothership runs the GTK file chooser portal on its private application bus
and Wayland display. COSMIC Terminal opens its embedded COSMIC Files chooser
without a parent handle. The WM associates it only with a terminal on the
same Wayland connection, using the focused terminal or the only matching
terminal. A window with no identifiable parent follows normal Inbox behavior.

## F-CHOOSER-1

Launch Brave and COSMIC Terminal in independent test sessions. Open and cancel
Brave's file picker, and verify a second request returns the selected file.
In COSMIC Terminal, open the color scheme import chooser and verify that
selecting a valid scheme imports it. Repeat in the nested and native sessions.

## F-CHOOSER-2

In a named Node, open a chooser with a Wayland parent in Flow and Stage.
Verify that it accepts input above its app, follows the app when switching away
and back, and leaves the parent's layout and focus intact after cancellation.
Repeat with the Node's layout slots full.
