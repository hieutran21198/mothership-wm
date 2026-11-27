# Find any open window by name

**Status:** implemented 2026-11-27; owner accepted in a native Mothership Dev session (candidate `build-96C5L99n`) on 2026-11-27.

The desktop user needs to reach a window without remembering which Node
holds it, because Nodes hide their windows and a session can hold dozens.

| Criterion | Observable success | Proven by |
| --- | --- | --- |
| G-SCAN-1 Search | `windows <query>` in Slash lists live mapped windows from every Node and Inbox whose title, app name or app ID matches, labelled with the Node. | [F-SCAN-1](../flows/search-windows.md#f-scan-1) |
| G-SCAN-2 Go | Enter connects to that window's Node and gives that exact window keyboard focus. | [F-SCAN-2](../flows/search-windows.md#f-scan-2) |
| G-SCAN-3 Stale | A window that closes or moves after the list was taken produces a clear error or the new Node, never focus on a different window. | [F-SCAN-3](../flows/search-windows.md#f-scan-3) |
| G-SCAN-4 Private | Window titles stay on the WM–Panel Wayland path. They are not sent to desktopd, logged or saved. | [F-SCAN-1](../flows/search-windows.md#f-scan-1) |
