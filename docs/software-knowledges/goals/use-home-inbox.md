# Use Home to find unassigned windows

Tracking: [GitHub issue #5](https://github.com/hieutran21198/mothership-wm/issues/5).

A developer can return to live, unassigned windows without creating a
Domain. The shell opens on blank Home. The internal Default Node provides the same framed layouts as named Nodes for unassigned windows.

| Criterion | Observable success | Proven by |
| --- | --- | --- |
| G-HOME-1 | Home initially owns the shell. W enters the internal framed Flow or Stage view without adding a Node to the map. | [F-HOME-1](../flows/use-home-inbox.md#f-home-1) |
| G-HOME-2 | Explicit assignment removes the window from Inbox. A new unassigned window does not interrupt a selected named Domain; an empty named Domain stays empty. | [F-HOME-2](../flows/use-home-inbox.md#f-home-2) |
| G-HOME-3 | Meta returns to Home without closing apps. Re-entering restores the selected layout, window order and eligible focus. WM acknowledgement confirms the change. | [F-HOME-3](../flows/use-home-inbox.md#f-home-3) |

Builds on [Domain views](focus-domain.md) and [local event delivery](stream-desktopd-state.md).
No named Node changes, new general switcher, agent seats, new app sandbox or persistent grouping.
