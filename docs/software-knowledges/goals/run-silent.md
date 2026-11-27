# Work in a Node without interruptions

**Status:** design accepted 2026-11-27; not implemented.

The desktop user needs a Node where nothing interrupts deep work, so
pop-ups and sound from elsewhere wait without being lost. The default
**Current Node** toast setting already hides other Nodes' toasts; it
does not silence the current Node, Home · System notices or audio.

| Criterion | Observable success | Proven by |
| --- | --- | --- |
| G-SILENT-1 Hold | While a Node with Silent running is in view, no toast appears from any source. Every notice still enters the Notification Log. | [F-SILENT-1](../flows/run-silent.md#f-silent-1) |
| G-SILENT-2 Audio | With **Notices + audio**, playback from apps in other Nodes is muted on entry. On leaving, only streams Silent running muted are restored. | [F-SILENT-2](../flows/run-silent.md#f-silent-2) |
| G-SILENT-3 Digest | Leaving the Node shows one digest of how many notices were held, under the destination's toast setting. | [F-SILENT-3](../flows/run-silent.md#f-silent-3) |
| G-SILENT-4 Visible | The mode is visible on the Node Panel and as a cloaked ship on the Center Map. | [F-SILENT-1](../flows/run-silent.md#f-silent-1), [F-SHIP-2](../flows/show-ship-status.md#f-ship-2) |

Not a system-wide Do Not Disturb, no schedule, and no effect on the host desktop.
