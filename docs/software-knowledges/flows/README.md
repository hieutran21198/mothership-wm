# Flows

A Flow owns sequence, handoffs and failure behavior. Add or update it when those
promises change. Name who executes each step and where work commits locally;
a cross-part call does not imply a shared transaction. Link the receiving
Artifact for its operation, payload and errors rather than duplicating them.

- [Boot the WM inside a Wayland session](boot-nested-wayland-compositor.md).
- [Route input between two nested windows](focus-nested-windows.md): isolated protocol acceptance.

- [Focus a Domain](focus-domain.md): design accepted local command and WM handoff.

- [Subscribe to desktopd state](stream-desktopd-state.md): draft.
- [Open Home and use Inbox](use-home-inbox.md).

- [Create a Node, move a window and Fly](use-command-center.md).

- [Launch and close a nested Shell](launch-native-shell.md).
- [Open native apps inside the nested session](open-native-apps.md).
- [Browse and launch from App Grid](use-app-grid.md).
- [Navigate Overview and lay out a Node](view-node-layouts.md).

- [Show and hide the Center Map](show-center-map.md).

- [Read notifications by Node](show-notifications.md).
- [Select and export a screenshot](capture-screenshot.md).

Design accepted 2026-11-27; each document states what is implemented. See the
[Fleet Ops preview](../system/fleet-ops-preview.html):

- [Save and launch a Node loadout](launch-node-loadout.md).
- [Run silent in a Node](run-silent.md).
- [Pin a comms window](pin-comms-window.md).
- [Tractor-beam a window to another Node](tractor-beam-window.md).
- [Search windows and connect to one](search-windows.md).
- [Record and review the Mission Log](review-mission-log.md).
- [Show ship status on the Center Map](show-ship-status.md).

Design approved 2026-11-27, awaiting native acceptance:

- [Control devices from the Systems board](control-devices.md).

## Template

```markdown
# <journey>
Trigger and preconditions: <what starts this flow and when>
Participants and outcome: <actors/parts; observable result>
Local transaction or state change: <owner and boundary, if applicable>
Failure and recovery: <rejection, partial result or retry, if applicable>

| Step | Executor | Operation and outcome |
| --- | --- | --- |
| <step> | <responsible part> | <operation; result; contract link if needed> |

| Scenario | Given | When | Then | Covers |
| --- | --- | --- | --- | --- |
| <scenario label> | <starting facts> | <trigger> | <observable result> | <criteria/rules> |
```

The scenario is expected behavior, not a claim an acceptance check ran.
