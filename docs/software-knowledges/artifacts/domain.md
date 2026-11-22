# Domain

Serving part: [desktopd](../../../services/desktopd/README.md).
A Domain is a named group of live [Windows](window.md) for the main view. It
is not an app, process, input seat, or grant of access. desktopd owns
membership and resolves Domain commands. WM owns Window identity, actual
visible surfaces, pointer hit-testing and the human seat’s keyboard target. A
Domain does not own seats.

| Rule | Invariant or allowed change |
| --- | --- |
| DOM-1 | An explicit mapping assigns one live `(WM session, Window ID)` to one Domain. Reassigning removes the old membership. App ID, PID and title do not imply ownership. A Domain may be empty. This slice allows two live members for its fixed slots. |
| DOM-2 | Destroy or session end removes old refs. Unmap changes visibility, not membership. Reopening creates a new unassigned ref. Old-session and closed IDs cannot be assigned. |
| DOM-3 | `FocusDomain` names an existing Domain and resolves its members. Only WM applies the visible set and determines the eligible keyboard target. Membership grants no authority to send input. |

For this slice membership is in memory only. No persistence, app rules,
agent or public API. See the [FocusDomain Flow](../flows/focus-domain.md).

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

Reuse the existing create, assign and select commands. Validate the same
live session and Window ID for all clients. Publish a new selection as
acknowledged only after WM applies it.
