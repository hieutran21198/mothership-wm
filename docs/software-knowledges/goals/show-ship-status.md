# Read each Node's state from the Center Map

**Status:** design accepted 2026-11-27; not implemented.

The desktop user needs to see at a glance which Nodes are making sound or
running silent, without connecting to each one. The unread badge and arrival
pulse from [Identify and revisit notifications](show-notifications.md)
already cover new notices.

| Criterion | Observable success | Proven by |
| --- | --- | --- |
| G-SHIP-1 Comms | A Node with an identifiable playing stream shows comms arcs on its ship while the Map is open. A muted Node shows them struck through. | [F-SHIP-1](../flows/show-ship-status.md#f-ship-1) |
| G-SHIP-2 Cloak | A Node with Silent running shows a cloaked ship whose label keeps full contrast. | [F-SHIP-2](../flows/show-ship-status.md#f-ship-2) |
| G-SHIP-3 Detail | Node Detail lists the loadout size and today's time when they exist. | [F-SHIP-3](../flows/show-ship-status.md#f-ship-3) |
| G-SHIP-4 Calm | Status costs nothing while the Map is closed, honours Motion Off and never moves the camera or changes selection. | [F-SHIP-1](../flows/show-ship-status.md#f-ship-1) |
