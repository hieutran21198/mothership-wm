# Keep windows in sight across Nodes

**Implemented** after design approval on 2026-11-27. Mothership Dev session
acceptance is pending.

The desktop user needs to watch several live windows, such as a call and
a build log, while working in other Nodes and keep each window in its
source Node.

| Criterion | Observable success | Proven by |
| --- | --- | --- |
| G-PIN-1 Pin | **Meta + P** toggles the focused window's pin independently. Read-only thumbnails update in other Nodes, Default Node and blank Home. | [F-PIN-1](../flows/pin-comms-window.md#f-pin-1) |
| G-PIN-2 Return | Selecting a pin switches to its current Node and focuses that exact window. | [F-PIN-2](../flows/pin-comms-window.md#f-pin-2) |
| G-PIN-3 Out of the way | The stack fits at a chosen corner, keeps overflow reachable and takes no app focus on its own. Pins disappear in their source Node and keep their order. | [F-PIN-1](../flows/pin-comms-window.md#f-pin-1), [F-PIN-3](../flows/pin-comms-window.md#f-pin-3) |
| G-PIN-4 Release | Unpinning or closing a window removes its pin. Ending the session clears the stack. Each window keeps its Node membership and client size. | [F-PIN-1](../flows/pin-comms-window.md#f-pin-1), [F-PIN-4](../flows/pin-comms-window.md#f-pin-4) |
