# Move a window to another Node in one gesture

**Status:** G-BEAM-2 and G-BEAM-3 for keys implemented 2026-11-27 and owner accepted in a native Mothership Dev session (candidate `build-96C5L99n`) on 2026-11-27; drag (G-BEAM-1) not implemented.

The desktop user needs to send a window to the Node where it belongs while
looking at it, because the Command Center form asks for a Window ID and a
destination and breaks the flow of work.

| Criterion | Observable success | Proven by |
| --- | --- | --- |
| G-BEAM-1 Drag | Dragging a preview card up out of the Node Panel shows the Node targets. Dropping on one moves that exact window there without changing the current view. | [F-BEAM-1](../flows/tractor-beam-window.md#f-beam-1) |
| G-BEAM-2 Keys | **Meta + Shift + 1–9** moves the focused window to the matching numbered Node. **1** returns it to Inbox. | [F-BEAM-2](../flows/tractor-beam-window.md#f-beam-2) |
| G-BEAM-3 Safe | Cancel, a full Node or a failed assign leaves the window where it was, focused as before. | [F-BEAM-3](../flows/tractor-beam-window.md#f-beam-3) |

Dragging from the Center Map Node Detail is a later slice.
