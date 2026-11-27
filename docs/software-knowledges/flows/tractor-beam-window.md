# Tractor-beam a window to another Node

**Status:** keys implemented 2026-11-27 and owner accepted in a native Mothership Dev session (candidate `build-96C5L99n`) on 2026-11-27; drag implemented 2026-11-27 and owner accepted in a native Mothership Dev session (candidate `build-F0Om9ovi`) on 2026-11-27.
Supports [Move a window to another Node in one gesture](../goals/move-windows-between-nodes.md).
Design: [Fleet Ops preview, scene 4](../system/fleet-ops-preview.html).

Both paths end in the existing [assign command](use-command-center.md#f-command-2):
one live session and Window ID plus a destination. Neither selects the
destination. The WM reconciles the current projection and focus under
[active projection on assignment](../artifacts/domain.md#active-projection-on-assignment).

| Step | Executor | Operation and outcome |
| --- | --- | --- |
| Grab | WM | A primary press on a [preview tray](../system/home-panel.md#node-panel) card in a named Node or the Default Node, on any output width, is held by the WM. A short press stays a click that focuses the window. Once the card has moved 6 px and the pointer is more than 8 px above the tray, the drag becomes a beam and the WM draws the lifted card from its existing thumbnail. Lowering it back into the tray ends the beam; on wide outputs the existing group join still applies there. |
| Targets | WM → Panel | The WM sends `beam` begin (card center), move, drop and cancel over the private shell protocol (v28), not desktopd. The Panel shows the [Node switcher strip](../system/shell-design.md#node-switcher) titled **MOVE WINDOW TO** as paint only: the Default Node (when enabled) then named Nodes by name, the window's own Node marked **Current**, and Nodes with eight windows dimmed as **Full · 8/8**. Neither can be chosen. |
| Aim | Panel | The Panel hit-tests its own strip cells against the reported position, highlights one target and draws a dashed cyan beam from the card to it. Keep the rail and app geometry fixed. |
| Drop | Panel → desktopd → WM | Release on an eligible tile sends `assign`, or `unassign` for the Default Node. Release elsewhere, **Esc**, focus loss or a lost grab cancels: the strip closes and the window stays. |
| Keys | WM → Panel → desktopd → WM | **Meta + Shift + number** on a focused app window in a Node assigns it to the Node with that number key. Number 1 clears membership into Inbox. The number group follows the 1–9 / F1–F9 shortcut setting. A missing slot changes nothing and records a System notice, like a missing Win + number. |
| Settle | WM and Panel | After acknowledgement, the window leaves the current projection, the tray closes the gap and focus moves under the existing rule. With motion on, the card follows the beam into the target for about 180 ms. |

### F-BEAM-1

In Node Ops with three windows, drag the middle card up to Research and
release. The window moves to Research and stays open. Ops keeps the other
two, focus goes to a remaining window and the view stays on Ops. Enter
Research: the window is there. Repeat from the Default Node to triage an
Inbox window. A short click on a card still only focuses it.

### F-BEAM-2

Focus a window in Ops and press **Meta + Shift + 3**: it moves to the Node
labelled 3. Press **Meta + Shift + 1** in a named Node: the window returns
to Inbox. With F1–F9 selected, the same works with **Meta + Shift + F3**.
**Meta + Shift + Left/Right** keeps its reorder meaning.

### F-BEAM-3

Drag onto a **Full** Node: no highlight and release cancels. Press Esc
mid-drag, then leave the nested window mid-drag: both cancel without an
assign. Stop desktopd and drop: the card returns and a System notice
reports the failure; the window stays in Ops.

## Proposed contract changes

- Private shell protocol: beam drag begin, position and end events
  carrying the Window ID and output-local position. These are not sent
  to desktopd.
- Implemented for keys: the WM sends `node_move` (private shell protocol
  v27) with the number and window ID; the Panel submits `assign`, or the
  new [`unassign`](../../architecture/desktopd-client-ipc.md) for 1. Moving
  to the current Node, or 1 inside the Default Node, does nothing. The
  binding is fixed to the Node number group, not a separate editable action.
