# Switch the main view between Domains

**Design accepted.** Delivery is tracked in [issue #3](https://github.com/hieutran21198/mothership-wm/issues/3).

A developer can switch between groups of applications in one nested COSMIC
session, then return to the same live windows. Focus selects the Domain's view,
not an app, and does not grant another process input control.

| Criterion | Observable success | Proven by |
| --- | --- | --- |
| G-WM-DOMAIN-1 | Two Domains hold explicitly assigned live windows; same-app windows can belong to different Domains. | [F-WM-DOMAIN-1](../flows/focus-domain.md#f-wm-domain-1) |
| G-WM-DOMAIN-2 | Switching shows the destination's existing windows without restarting them. WM restores the most recently focused eligible window there. | [F-WM-DOMAIN-2](../flows/focus-domain.md#f-wm-domain-2) |
| G-WM-DOMAIN-3 | Hidden windows receive no new input. An empty Domain can be shown and has no keyboard target. | [F-WM-DOMAIN-3](../flows/focus-domain.md#f-wm-domain-3) |
| G-WM-DOMAIN-4 | A destroyed window loses its membership and focus history. A reopened window does not inherit either. | [F-WM-DOMAIN-4](../flows/focus-domain.md#f-wm-domain-4) |

Builds on [two-window focus](focus-nested-windows.md). No Panel, app-level
focus, agent, Shield, sandbox, task Flow, tunnel, pan/zoom or general layout.
