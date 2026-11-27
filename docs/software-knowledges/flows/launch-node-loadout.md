# Save and launch a Node loadout

**Status:** design accepted 2026-11-27; not implemented.
Supports [Relaunch a Node's apps from its loadout](../goals/launch-node-loadouts.md).
Design: [Fleet Ops preview, scene 2](../system/fleet-ops-preview.html).

A loadout is an ordered list of up to eight desktop entry IDs. It saves no
layout: layout stays session-only, because Nodes are often created on demand. It describes what to launch, not which
windows belong to the Node. Membership still comes from the
[launch marker](../system/app-grid.md) and explicit assignment
([DOM-1](../artifacts/domain.md)).

| Step | Executor | Operation and outcome |
| --- | --- | --- |
| Save | Panel → desktopd | **Node Panel Settings → Loadout → Save current apps** sends the Node name and, per live member in visible order, the desktop entry ID the Panel resolved. desktopd validates the Node and entry IDs and commits the record atomically. |
| Resolve | Panel | Use the entry recorded with the launch marker when Mothership launched the window. Otherwise accept a unique installed entry whose ID or `StartupWMClass` equals the app ID. Anything else is listed as **Not relaunchable** in the confirmation. |
| Offer | Panel | When a named Node is entered, compare its live members' resolved entries to the loadout. If entries are missing, show **Launch sequence · N apps** (empty Node) or **Launch missing · N** in the preview lane's empty space. Map Node Detail shows **Loadout · N apps**. |
| Launch | Panel → launcher | **Enter** in an empty Node, a click on the action, or Slash `nodes launch <name>` launches each missing entry in loadout order with the App Grid launch policy and a launch marker for this Node. Ignore repeated activation while a sequence runs. |
| Join | WM → desktopd | A marked window joins the requesting Node as today. The WM places joining windows in loadout order and appends late or unmarked ones. The Node keeps its current session layout (Flow on first entry). |
| Report | Panel | After about 10 s or when every entry has mapped, log one Node · System notice naming entries that failed, were uninstalled or did not map. Successful launches are silent. |

Failure and recovery: an uninstalled entry is skipped and reported. A Node
already at eight members stops the sequence and reports the remainder.
Launching never moves an existing window and never closes one. Leaving the
Node mid-sequence keeps launching; late windows join the hidden Node
without taking focus ([app-grid rule](../system/app-grid.md)).

### F-LOAD-1

In a named Node with Ghostty and COSMIC Files launched from App Grid and
one window started outside Mothership, choose **Save current apps**. The
confirmation names the outside window as Not relaunchable. The saved record
lists the two entry IDs in visible order and nothing else.
Restart desktopd; the record loads unchanged.

### F-LOAD-2

Start a new session and enter that Node. The lane shows **Launch sequence ·
2 apps**. Press Enter. Both apps open in the Node in order, the first gets
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

## Proposed contract changes

- desktopd stores `$XDG_STATE_HOME/mothership-wm/loadouts/<Node>.json`,
  outside the `nodes/` directory whose loader rejects unknown records.
  Bound the file at 4 KiB. This extends the durable data listed in
  [Domain](../artifacts/domain.md); update DOM-7 so deletion removes the
  loadout. Layout stays session-only.
- New desktopd methods: `loadout.get`, `loadout.set`, `loadout.clear`.
  Publish a `loadout_changed` event with the entry count only.
- Slash: `nodes launch <name>`.

## Decisions

- **Launch on entry:** an opt-in per-Node toggle that starts the sequence
  automatically on an empty Node. Default Off.
