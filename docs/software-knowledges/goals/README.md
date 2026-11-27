# Goals

A Goal states an outcome, not a sequence of steps or a delivery task. Update
the existing Goal if the criteria change; link it from its supporting Flows.
Each criterion names the Flow scenario that demonstrates it.

- [Open a nested Wayland compositor](boot-nested-wayland-compositor.md): complete
  for one client on COSMIC.
- [Focus one of two nested windows](focus-nested-windows.md): complete for two nested windows on COSMIC.

- [Switch the main view between Domains](focus-domain.md): design accepted for review.

- [Stream Domain state to local clients](stream-desktopd-state.md): draft.
- [Use Home to find unassigned windows](use-home-inbox.md).

- [Organize open windows from Command Center](use-command-center.md).

- [Launch Mothership Shell in COSMIC](launch-native-shell.md).
- [Open a terminal or file manager from Command Center](open-native-apps.md).
- [Find and launch installed apps from App Grid](use-app-grid.md).
- [Enter Nodes from Overview and choose a layout](view-node-layouts.md).

- [Show a configurable Center Map HUD](show-center-map.md).

- [Identify and revisit notifications](show-notifications.md).
- [Capture a screenshot](capture-screenshots.md).

Design accepted 2026-11-27; each document states what is implemented:

- [Relaunch a Node's apps from its loadout](launch-node-loadouts.md).
- [Work in a Node without interruptions](run-silent.md).
- [Keep one window in sight across Nodes](pin-comms-window.md).
- [Move a window to another Node in one gesture](move-windows-between-nodes.md).
- [Find any open window by name](find-any-window.md).
- [See where the day went](review-mission-log.md).
- [Read each Node's state from the Center Map](show-ship-status.md).

Design approved 2026-11-27, awaiting native acceptance:

- [Control sound and Bluetooth devices from the shell](control-devices.md).

## Template

```markdown
# <outcome>
<actor> needs <outcome> because <need or constraint>.

| Criterion | Observable success | Proven by |
| --- | --- | --- |
| <criterion label> | <result the actor can verify> | <Flow scenario link> |
```

Use labels that allow a reviewer to trace the criterion to its scenario.
