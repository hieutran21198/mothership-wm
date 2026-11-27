# Save and launch a Node loadout

**Status:** implemented 2026-11-27 (Launch on entry added later the same day and owner accepted in a native Mothership Dev session (candidate `build-F0Om9ovi`) on 2026-11-27); owner accepted in a native Mothership Dev session (candidate `build-xb1Qmf4O`) on 2026-11-27.
Supports [Relaunch a Node's apps from its loadout](../goals/launch-node-loadouts.md).
Design: [Fleet Ops preview, scene 2](../system/fleet-ops-preview.html).

A loadout is an ordered list of up to eight desktop entry IDs. It saves no
layout: layout stays session-only, because Nodes are often created on demand. It describes what to launch, not which
windows belong to the Node. Membership still comes from the
[launch marker](../system/app-grid.md) and explicit assignment
([DOM-1](../artifacts/domain.md)).

| Step | Executor | Operation and outcome |
| --- | --- | --- |
| Save | Panel → desktopd | **Node Panel Settings → Loadout → Save current apps** (named Nodes only) resolves each live mapped member, in the order the windows opened, and sends `loadout.set`. desktopd validates the Node and entry IDs, commits the file and publishes `node.loadout`. The row shows the saved count and a one-line result. **Clear** sends an empty list. |
| Resolve | Panel | Use the entry recorded when a Mothership launch attached the window (App Grid, Slash or a loadout, this session). Otherwise accept the one installed entry whose ID or `StartupWMClass` equals the app ID. Anything else is named in the result as **Not relaunchable**. |
| Offer | Panel | In a named Node whose loadout has missing entries, an analog button sits in free preview-lane space: **Launch sequence · N apps** at the lane start in an empty Node, **Launch missing · N** after the previews otherwise. It hides when the lane has no room; Slash still works. Map Node Detail shows **Loadout · N apps**. |
| Launch | Panel → launcher | A click on the offer or Slash `nodes launch <name>` launches the missing entries in loadout order with the App Grid launch policy and a launch marker for that Node. Each launch waits up to 3 s for its window before the next starts. A second activation while a sequence runs is ignored; the offer reads **Launching…**. |
| Join | Panel → desktopd → WM | A marked window is assigned to the requesting Node exactly as an App Grid launch. Windows usually join in loadout order because of the pacing. The Node keeps its current session layout (Flow on first entry). |
| Report | Panel | When every launch has attached or timed out (12 s each), log one Node · System **Loadout incomplete** notice naming entries that were not installed, failed to start, did not join, or did not fit. Successful launches are silent. |

Failure and recovery: an uninstalled entry is skipped and reported. A Node
already at eight members stops the sequence and reports the remainder.
Launching never moves an existing window and never closes one. Leaving the
Node mid-sequence keeps launching; late windows join the hidden Node
without taking focus ([app-grid rule](../system/app-grid.md)).

### F-LOAD-1

In a named Node with Ghostty and COSMIC Files launched from App Grid and
one window started outside Mothership, choose **Save current apps**. The
confirmation names the outside window as Not relaunchable. The saved record
lists the two entry IDs in the order the windows opened and nothing else.
Restart desktopd; the record loads unchanged.

### F-LOAD-2

Start a new session and enter that Node. The lane shows **Launch sequence ·
2 apps**. Click it. Both apps open in the Node in order, the first gets
focus, and no Inbox window or other Node changes. Repeat from Slash with
`nodes launch <name>` from Home: the apps join the hidden Node without
taking focus and the view stays on Home.

### F-LOAD-3

Uninstall one saved app and launch: the other opens and one notice names
the missing entry. With one saved app already running in the Node, the
action reads **Launch missing · 1**. Fill the Node to eight windows first:
the sequence stops and reports the entries it did not launch.

### F-LOAD-4

Choose **Clear**; the offer disappears and the record file is removed.
Save again, then Force Delete the Node: its loadout is removed with it. Inspect the
stored file: it contains only entry IDs and timestamps.

## Contract

- desktopd stores `$XDG_STATE_HOME/mothership-wm/loadouts/<Node>.json`
  (`entries`, `saved_at`), outside the `nodes/` directory whose loader
  rejects unknown records, bounded at 4 KiB. Deleting a Node removes it. A
  malformed file is reported and ignored. Layout stays session-only.
- [`loadout.set`](../../architecture/desktopd-client-ipc.md) replaces or
  (with an empty list) clears a Node's loadout; snapshots carry it in each
  Node profile and the `node.loadout` event announces changes.
- Slash: `nodes launch <name>`.

## Decisions

- **Launch on entry:** an opt-in per-Node **Launch on entry** row in Node
  Panel Settings, saved as `nodes.<name>.launch_on_entry` in the shared
  user config (Off removes it). An acknowledged entry into an empty named
  Node with a loadout starts the sequence once per entry. Default Off.
- **Enter** in an empty Node is not bound: the empty Node does not give
  the Panel keyboard focus. Use the offer or Slash.
