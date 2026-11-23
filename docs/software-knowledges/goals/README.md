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

## Template

```markdown
# <outcome>
<actor> needs <outcome> because <need or constraint>.

| Criterion | Observable success | Proven by |
| --- | --- | --- |
| <criterion label> | <result the actor can verify> | <Flow scenario link> |
```

Use labels that allow a reviewer to trace the criterion to its scenario.
