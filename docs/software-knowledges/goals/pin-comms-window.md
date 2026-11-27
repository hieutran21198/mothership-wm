# Keep one window in sight across Nodes

**Status:** implemented 2026-11-27; owner accepted in a native Mothership Dev session (candidate `build-F0Om9ovi`) on 2026-11-27.

The desktop user needs to watch one live window, such as a call, build log
or player, while working in other Nodes, without moving it into each Node.

| Criterion | Observable success | Proven by |
| --- | --- | --- |
| G-PIN-1 Pin | **Meta + P** pins the focused window. A live, read-only thumbnail appears in other Nodes, Default Node and Home, and keeps updating. | [F-PIN-1](../flows/pin-comms-window.md#f-pin-1) |
| G-PIN-2 Return | Clicking the pin connects to the window's Node and focuses that window. | [F-PIN-2](../flows/pin-comms-window.md#f-pin-2) |
| G-PIN-3 Out of the way | The pin never takes keyboard focus or forwards input to the app. It hides in its own Node, the Map and exclusive shell overlays, and can sit in any corner. | [F-PIN-3](../flows/pin-comms-window.md#f-pin-3) |
| G-PIN-4 Release | Unpinning, closing the window or ending the session removes the pin. Membership and the app's size never change. | [F-PIN-4](../flows/pin-comms-window.md#f-pin-4) |
