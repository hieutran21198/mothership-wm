# Create a Node, move a window and Fly

Supports [the Command Center Goal](../goals/use-command-center.md). The
Command Center reads live state from desktopd. WM owns visible windows
and keyboard input; the UI itself is not assignable to a Node.

| Step | Part | Action |
| --- | --- | --- |
| Read | Command Center | Subscribe to the private snapshot and updates. The current view is confirmed only by desktopd state. |
| Create | Command Center → desktopd | Create an empty Node. The Command Center form leaves the current selection unchanged. Slash `/ nodes new` enters the new Node after successful creation and WM acknowledgement. |
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

## Delete a Node

See the
[Domain deletion rules](../artifacts/domain.md#delete-a-named-node).
At Home, use `/ nodes delete` or `/ nodes force-delete`. The service guard
can take manager ownership from a future agent lifecycle integration;
the current product has no live agent lifecycle provider.

| Step | Executor | Operation and outcome |
| --- | --- | --- |
| Confirm | Shell | Show the exact Node name. For Force Delete, state that its windows move to Default Node and stay open. Cancel changes nothing. |
| Check | desktopd | Recheck the current view, live membership and agent management at execution. If a manager blocks deletion, identify the manager so the user can detach it or end its session. Force does not bypass this check. |
| Move | desktopd → WM | For Force only, clear membership for all live members into Inbox. Wait for WM acknowledgement if the move affects its projection. Do not delete the Node if the move fails or its outcome is unknown. |
| Delete | desktopd | Persist the removal before reporting success and publish the updated Node catalog. On failure, do not report successful deletion. |
| Notify | Shell | After a successful Delete or Force Delete from Slash, log one Home · System notification naming the deleted Node. Show the toast only when the current notification setting permits it. |

### F-COMMAND-4

Release an empty user-created Node with no attached manager, then confirm
Delete. The Node disappears and stays absent after restart. Other Node
shortcuts do not change and existing notifications remain in the log.
The reserved Mothership and Default Node cannot be deleted.

### F-COMMAND-5

With a mapped and an unmapped window in a released Node, normal Delete
is rejected. Force Delete preserves both window IDs and moves them to
Inbox; mapped windows appear in Default Node subject to its normal
projection limit. If the move fails or is unconfirmed, the Node remains.

### F-COMMAND-6

With an agent still managing a Node, both Delete and Force Delete are
rejected, even if it is empty. The agent is named and neither it nor the
windows are stopped or moved. After the user explicitly detaches or ends
the manager, check the remaining deletion conditions again.

## Slash in a Node

Press **Win + /** in any Node, including Default Node. The palette uses
the same entry and completion as Home. It offers **Apps**, **Notifications**
for the current Node, and **Settings** for that Node. Node management, Map,
Display, and global Shortcuts stay on Home. Typing a Home-only command in
a Node does not make it available.

Launch an app in the current Node without changing Nodes. Press Esc or
click outside the palette to close it and return keyboard focus to the
active window. Switching views closes the palette instead of changing the
scope of a command being typed. A plain slash in an app remains app input.
