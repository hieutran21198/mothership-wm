# Use Home to find unassigned windows

Tracking: [GitHub issue #5](https://github.com/hieutran21198/mothership-wm/issues/5).

A developer can return to live, unassigned windows without creating a
Domain. Home opens when desktopd and WM start a managed session.

| Criterion | Observable success | Proven by |
| --- | --- | --- |
| G-HOME-1 | Home is the initial managed view. Inbox includes mapped, unassigned windows, including windows waiting for a visible slot. | [F-HOME-1](../flows/use-home-inbox.md#f-home-1) |
| G-HOME-2 | Explicit assignment removes the window from Inbox. A new unassigned window does not interrupt a selected named Domain; an empty named Domain stays empty. | [F-HOME-2](../flows/use-home-inbox.md#f-home-2) |
| G-HOME-3 | Returning to Home leaves clients alive, restores the last eligible Home focus or clears it, and confirms Home to clients only when WM applies it. | [F-HOME-3](../flows/use-home-inbox.md#f-home-3) |

Builds on [Domain views](focus-domain.md) and [local event delivery](stream-desktopd-state.md).
No Panel, launcher, app rules, app metadata, new layout, agent seats,
Shield, sandbox or persistence.
