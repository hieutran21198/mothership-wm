# Native Command Center

See the [goal](../../docs/software-knowledges/goals/use-command-center.md).

```sh
devenv shell --profile panel-native -- task panel:build
```

Use the WM and desktopd [local run instructions](../../services/desktopd/README.md).
When the managed WM reports its child socket, use the same runtime directory
and run `bin/mothership-command-center --nested YOUR_CHILD_SOCKET`. Do not
replace your parent `WAYLAND_DISPLAY` in that terminal. The Center is a
singleton bottom-right overlay inside Mothership, not an app in Home.

Create a named Node, select a live Window ID and a destination, then
**Attach / Move**. Use **Fly to Node** or **Home** separately. **Return keys**
or Escape releases text entry back to the current view. Closing the
managed WM also closes its nested Command Center.
