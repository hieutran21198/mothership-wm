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
or Escape during text entry releases input back to the current view. Closing the
managed WM also closes its nested Command Center.


To add a disposable native application for a test, use the
`MOTHERSHIP_WAYLAND` absolute socket path printed by the launcher:

```sh
env -u WAYLAND_SOCKET -u DISPLAY WAYLAND_DISPLAY=YOUR_PRINTED_PATH YOUR_TEST_APP
```

Do not export the child's socket to the host session. The launch command
does not start applications for you.
