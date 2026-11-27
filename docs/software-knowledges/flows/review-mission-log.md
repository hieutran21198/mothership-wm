# Record and review the Mission Log

**Status:** design accepted 2026-11-27; not implemented.
Supports [See where the day went](../goals/review-mission-log.md).
Design: [Fleet Ops preview, scenes 1 and 6](../system/fleet-ops-preview.html).

desktopd records segments because it already publishes a selection only
after the WM acknowledges it. Last active keeps its existing meaning in
[Domain](../artifacts/domain.md).

| Step | Executor | Operation and outcome |
| --- | --- | --- |
| Open segment | desktopd | On an acknowledged selection of a named Node or the Default Node, open a segment with the Node name, or a `default_node: true` flag instead of a name, and the start time. |
| Close segment | desktopd | Close it on the next acknowledged selection, WM disconnect, desktopd shutdown or a confirmed lock. Split a segment at local midnight. Drop segments under 5 s. |
| Persist | desktopd | Append closed segments to `$XDG_STATE_HOME/mothership-wm/missions/YYYY-MM-DD.jsonl`. Delete files older than 30 days at start-up. A write failure keeps the in-memory day and is logged once. |
| Read | Panel → desktopd | `missions.get {day}` returns the day's segments plus the open one. The Panel subscribes to selection events to extend the live bar; it does not poll. |
| Show | Panel | Slash `missions` or **L** on focused blank Home opens the analog Mission Log above the dock. One lane per Node with Node glyph and name, a 24-hour axis with ticks every three hours, cyan segment bars, a brighter cap on the live segment and a total per Node. A/D (or Left/Right) changes day; Esc, L or Q closes. |
| Detail | Panel | Center Map [Node Detail](../system/center-map-ui.md) adds a **Today** row with that Node's total when it is above zero. |
| Clear | Panel → desktopd | **Clear history** asks for confirmation, then deletes every stored day and the in-memory history. The open segment restarts now. |

### F-MISSION-1

Enter Ops for one minute, go Home for one minute, enter the Default Node
for one minute, then open Map and Overview. The log shows one Ops segment
and one Default segment, each about a minute, and nothing for Home, Map
or Overview. Kill desktopd mid-segment and restart: the closed segment up
to the last durable point is present and no negative or overlapping
segment appears.

### F-MISSION-2

Open the log on Home with Ops active earlier and Research now: Research's
bar grows while open. Press A: yesterday's file is shown; D returns. A
day with no data shows **No missions logged**. Map Detail for Ops shows
**Today · 1m**.

### F-MISSION-3

Delete Node Ops: its lane remains, labelled **Ops (deleted)** in muted ink.
Inspect the files: only Node names and Unix times. Set the clock 31 days
ahead and restart: the old file is gone. Clear history: the log is empty
and the files are deleted.

## Proposed contract changes

- New durable desktopd state and the read-only `missions.get` and
  `missions.clear` methods.
- Home keyboard guide gains **L**; Slash gains `missions`.

## Decisions

- **Idle** is a second slice: stop counting after 5 min without input,
  using a coarse idle begin/end signal from the WM to desktopd with no
  pointer data. The first slice counts time in view.

## Open questions
- Whether lock is observable from the shell's existing logind check.
