# Relaunch a Node's apps from its loadout

**Status:** design accepted 2026-11-27; not implemented.

The desktop user needs a named Node to bring back its usual apps in a new
session, because Window membership ends with the session and rebuilding
each Node by hand defeats the point of keeping Nodes.

| Criterion | Observable success | Proven by |
| --- | --- | --- |
| G-LOAD-1 Save | From inside a Node, save the relaunchable apps currently in it, in their visible order. Apps that cannot be relaunched are named, not silently dropped. | [F-LOAD-1](../flows/launch-node-loadout.md#f-load-1) |
| G-LOAD-2 Launch | An empty Node with a loadout offers one **Launch sequence** action. Each new window joins that Node through the existing launch marker. No running window is moved or matched by app name. | [F-LOAD-2](../flows/launch-node-loadout.md#f-load-2) |
| G-LOAD-3 Partial | A missing or failing app is reported and the rest still launch. A Node with some apps open offers only the missing entries. | [F-LOAD-3](../flows/launch-node-loadout.md#f-load-3) |
| G-LOAD-4 Lifecycle | Clearing the loadout or deleting the Node removes the record. Loadouts never store arguments, file paths, titles or window IDs. | [F-LOAD-4](../flows/launch-node-loadout.md#f-load-4) |

No session restore of window contents, no app state, no saved layout
and no automatic capture. Nodes are often created on demand, by a person
or later by an agent, so layout stays session-only as in
[Domain](../artifacts/domain.md).
