# Native Command Center

See the [goal](../../docs/software-knowledges/goals/use-command-center.md) and
[one-command launch](../../README.md#run-inside-cosmic).

```sh
devenv shell --profile panel-native -- task panel:build
```

Use the WM and desktopd [local run instructions](../../services/desktopd/README.md).
When the managed WM reports its child socket, use the same runtime directory
and run `bin/mothership-command-center --nested YOUR_CHILD_SOCKET`. Do not
replace your parent `WAYLAND_DISPLAY` in that terminal. The Center is a
singleton bottom-right overlay inside Mothership, not an app in Home.
Click **Command Center** to open its controls; close it with **Hide** or Escape.
The transparent area around the entry lets input reach the app below it.

Create a named Node, select a live Window ID and a destination, then
**Attach / Move**. Use **Fly to Node** or **Home** separately. **Hide**
or Escape during text entry releases input back to the current view. Use **Overview** to leave the current view and enter a Node from its
position on the grid. **Home / Inbox** is a separate destination. In a Node,
choose **Stack**, **Float** or **Tiling**. Use **Next window** in Stack; drag
a Float window's top bar to move it or its bottom-right grip to resize it.
Closing the
managed WM also closes its nested Command Center.

**Terminal** starts an installed Ghostty with single-instance mode off.
**File Manager** starts installed COSMIC Files without daemonizing.
Neither is installed by this project. Each opens in Home / Inbox; use the
existing Attach / Move control if you want it in a Node. These actions do
not change your view or run arbitrary commands. The apps remain ordinary
Linux apps with access to your normal files, not sandboxes.

To add another disposable native application for a test, use the
`MOTHERSHIP_WAYLAND` absolute socket path printed by the launcher:

```sh
env -u WAYLAND_SOCKET -u DISPLAY WAYLAND_DISPLAY=YOUR_PRINTED_PATH YOUR_TEST_APP
```

Do not export the child's socket to the host session. The launch command
does not open apps automatically.
