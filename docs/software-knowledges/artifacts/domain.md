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
