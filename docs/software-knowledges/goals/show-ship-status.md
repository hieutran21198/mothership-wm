# Read each Node's state from the Center Map

**Status:** G-SHIP-2 (cloak) implemented 2026-11-27 and owner accepted in a native Mothership Dev session (candidate `build-zvH5676i`) on 2026-11-27; G-SHIP-3 (Detail rows) implemented with loadouts and the Mission Log; Comms (G-SHIP-1) implemented 2026-11-27 and owner accepted in a native Mothership Dev session (candidate `build-kuuGdEco`) on 2026-11-27; Music (G-SHIP-5) design approved by the owner 2026-11-27 and implemented the same day, awaiting native acceptance.

The desktop user needs to see at a glance which Nodes are making sound or
running silent, without connecting to each one. The unread badge and arrival
pulse from [Identify and revisit notifications](show-notifications.md)
already cover new notices.

| Criterion | Observable success | Proven by |
| --- | --- | --- |
| G-SHIP-1 Comms | A Node with an identifiable playing stream that is not music shows comms arcs on its ship while the Map is open. A muted Node shows them struck through. | [F-SHIP-1](../flows/show-ship-status.md#f-ship-1) |
| G-SHIP-2 Cloak | A Node with Silent running shows a cloaked ship whose label keeps full contrast. | [F-SHIP-2](../flows/show-ship-status.md#f-ship-2) |
| G-SHIP-3 Detail | Node Detail lists the loadout size and today's time when they exist. | [F-SHIP-3](../flows/show-ship-status.md#f-ship-3) |
| G-SHIP-5 Music | A Node playing music shows a music zone around its ship instead of comms arcs: waves fade out from the hull on the beat inside a fixed zone, with a spectrum halo around the hull. Its name, neighbours and clicks are unaffected. Muted shows a struck, still zone; paused shows nothing. | [F-SHIP-4](../flows/show-ship-status.md#f-ship-4) |
| G-SHIP-4 Calm | Status costs nothing while the Map is closed, honours Motion Off and never moves the camera or changes selection. | [F-SHIP-1](../flows/show-ship-status.md#f-ship-1) |
