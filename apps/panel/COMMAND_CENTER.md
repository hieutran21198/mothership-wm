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
singleton inside Mothership, not an app in Home. The old Command Center icon is not shown on the [Home Panel](../../docs/software-knowledges/system/home-panel.md). Use **Hide** or Escape to close it.
On blank Home, **/** opens a focused Node search; Enter uses an exact or
unique-prefix match, and Escape closes it without leaving Home.
Use **A** for the separate [App Grid](../../docs/software-knowledges/system/app-grid.md).
Use **W** from blank Home to enter the [Default Node](../../docs/software-knowledges/flows/use-home-inbox.md)
and tap **Meta** without another key to return to Home. W in an app is normal
typing. Disable entry and launch foregrounding with `"home": {"default_node": false}`
in the same sparse config and R on blank Home. No apps are closed.

Open the Home Panel's **Settings** icon to preview and save its width.
The optional override is `"home_panel": {"width": 960}` inside
`~/config/.mothership/config.json`. Defaults stay in the app, and **R**
reloads the configuration on blank Home only. Center Map settings are
a separate page in the same UI.

Create a named Node, select a live Window ID and a destination, then
**Attach / Move**. Use **Connect** or **Home** separately. **Hide**
or Escape during text entry releases input back to the current view. Use **Overview** to leave the current view and enter a Node from its
position on the grid. The **Home** action returns to the blank shell. In a Node,
choose **Stack**, **Float** or **Flow**. Use **Next window** in Stack; drag
a Float window's top bar to move it or its bottom-right grip to resize it.
See [Reorder windows in Flow](../../docs/software-knowledges/flows/view-node-layouts.md#f-view-6)
for the keyboard shortcuts that move a focused window in its current Node.
Closing the
managed WM also closes its nested Command Center.

Use **Screenshot** to open the [snipping overlay](../../docs/software-knowledges/flows/capture-screenshot.md).
In a Node, **Win + /** opens Slash. Enter `screenshot` there.

**Terminal** starts an installed Ghostty with single-instance mode off.
**File Manager** starts installed COSMIC Files without daemonizing.
Neither is installed by this project. Each opens in Inbox under the internal Default Node. Launching from blank
Home brings that stack forward when enabled. From a named Node it does not
change the current view. These actions do not run arbitrary commands. The apps remain ordinary
Linux apps with access to your normal files, not sandboxes.

To add another disposable native application for a test, use the
`MOTHERSHIP_WAYLAND` absolute socket path printed by the launcher:

```sh
env -u WAYLAND_SOCKET -u DISPLAY WAYLAND_DISPLAY=YOUR_PRINTED_PATH YOUR_TEST_APP
```

Do not export the child's socket to the host session. The launch command
does not open apps automatically.

### Create a Node from Slash

At Home, enter `/ nodes new <name>`. Enter creates the Node and opens it
after desktopd saves it. The Command Center form still creates without
changing views.

### Delete a Node from Slash

At Home, enter `/ nodes delete <name>` to remove an empty Node or
`/ nodes force-delete <name>` to return its windows to Default Node first.
Enter on the named result displays a confirmation; press Enter again to
confirm or Esc to cancel. Tab only completes. Both actions require that
the Node is no longer active. See the [deletion rules](../../docs/software-knowledges/artifacts/domain.md#delete-a-named-node).

App Grid opens a new Brave window in the Node that was selected at launch.
Brave reuses its private nested profile, so existing Brave windows stay in
their own Nodes. Use Home as before to open a Brave window in Default Node.
