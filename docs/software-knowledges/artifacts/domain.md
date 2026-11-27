# Domain

Serving part: [desktopd](../../../services/desktopd/README.md).
A Domain is a named group of live [Windows](window.md) for the main view. It
is not an app, process, input seat, or grant of access. desktopd owns
membership and resolves Domain commands. WM owns Window identity, actual
visible surfaces, pointer hit-testing and the human seat’s keyboard target. A
Domain does not own seats.

| Rule | Invariant or allowed change |
| --- | --- |
| DOM-1 | An explicit mapping assigns one live `(WM session, Window ID)` to one Domain. Reassigning removes the old membership. App ID, PID and title do not imply ownership. A Domain may be empty. A named Node allows up to eight live members, including temporarily unmapped windows. |
| DOM-2 | Destroy or session end removes old refs. Unmap changes visibility, not membership. Reopening creates a new unassigned ref. Old-session and closed IDs cannot be assigned. |
| DOM-3 | `FocusDomain` names an existing Domain and resolves its members. Only WM applies the visible set and determines the eligible keyboard target. Membership grants no authority to send input. |

Only Node name, optional plain-text description, creation time, latest
acknowledged workspace entry, optional Map shortcut slot and optional
[loadout](../flows/launch-node-loadout.md) (desktop entry IDs to relaunch,
never membership) are durable, local to the current user.
Creation time is set once. Last active advances only when WM acknowledges an
explicit entry into that named workspace, including an empty Node; layout,
assignment, hover and profile changes do not affect it. Node age and activity
can be absent for a client that has no reliable source. Window IDs,
membership, layouts and focus remain session-only. App IDs are display
metadata, not an assignment rule. See the [FocusDomain Flow](../flows/focus-domain.md).

## Active projection on assignment

Assignment changes one existing Window’s membership without selecting the
destination. If it changes the already-selected named projection, WM
reconciles that projection and acknowledges it. Keep the eligible current
target, or send the next key to a remaining visible window if the old
target left; clear focus if none remains. Never fall back across views.

## Home and Inbox

Home is the initial managed view, not a named Domain or a reservation of
the name `Home`. Inbox is derived from the current session: a window must
be mapped, live, and without Domain membership. Explicit assignment removes
it from Inbox. Unmap or destruction also makes a window ineligible.

The Default Node is the reserved foreground projection of Inbox, not stored
in the named Domain catalog. Its detail is available from the existing
[Mothership marker](../system/center-map-ui.md#default-node-detail), which
keeps its fixed position on the Map and minimap.
Blank Home and the active Default Node are distinct views; the latter has
the normal stack capacity (eight projected members). Overflow remains live
and assignable. WM owns its private position and focus history; it never
falls back to Inbox while a named Domain is active.

## Delete a named Node

| Rule | Invariant or allowed change |
| --- | --- |
| DOM-4 | Delete only a user-created named Node after leaving its active view. The Mothership marker and Default Node are not deletable. Normal Delete requires no live Window members, including unmapped members. |
| DOM-5 | An agent still attached as a manager blocks both Delete and Force Delete, even when the Node has no windows. End or detach that manager explicitly. Deleting a Node never stops or transfers an agent. |
| DOM-6 | Force Delete can move live Window members to Default Node by clearing their named membership into the Inbox. Preserve the existing Window IDs and clients; do not open Default Node or close apps. Inbox overflow and unmapped members remain live under the existing projection rules. Delete the Node only after the move succeeds. |
| DOM-7 | Successful deletion removes the durable Node record and its loadout, frees its optional shortcut slot without renumbering other Nodes, and preserves existing notification history. |

See [Delete a Node](../flows/use-command-center.md#delete-a-node).

## Local client contract

Desktop clients can subscribe over [desktopd’s private Unix socket](../../architecture/desktopd-client-ipc.md).
desktopd owns the snapshot: Domain names, live Window refs with mapping and
membership, WM availability, and the current WM session’s selection state
(not selected, last acknowledged, or unknown). An acknowledgement describes
the applied projection, not the current keyboard target.

Register the subscription and capture its initial snapshot at the same
revision boundary. Send the snapshot first, then push ordered updates
from the next revision on the same connection, with no gap between them.
On disconnect or a missing revision, the client marks its view stale and
subscribes again for a fresh snapshot. A slow or stalled subscriber cannot
block WM or desktopd commands: keep its queue bounded and disconnect it
rather than silently dropping an update.

Reuse the existing create, assign and select commands. `unassign` returns
one window to Inbox and `focus.window` selects the Node that currently
holds one window; neither infers membership from app ID. Validate the same
live session and Window ID for all clients. Publish a new selection as
acknowledged only after WM applies it.

[Node layouts and Overview](../flows/view-node-layouts.md) do not change
membership. Overview has no app projection and is not Home or a Domain.
