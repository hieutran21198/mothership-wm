# Record and review the Mission Log

**Status:** implemented 2026-11-27 (idle added later the same day and owner accepted in a native Mothership Dev session (candidate `build-F0Om9ovi`) on 2026-11-27); owner accepted in a native Mothership Dev session (candidate `build-jk5prr29`) on 2026-11-27.
Supports [See where the day went](../goals/review-mission-log.md).
Design: [Fleet Ops preview, scenes 1 and 6](../system/fleet-ops-preview.html).

desktopd records segments because it already publishes a selection only
after the WM acknowledges it. Last active keeps its existing meaning in
[Domain](../artifacts/domain.md).

| Step | Executor | Operation and outcome |
| --- | --- | --- |
| Open segment | desktopd | On an acknowledged selection of a named Node or the Default Node, open a segment with the Node name (or the Default Node flag) and the start time. A layout change in the same Node keeps the segment. |
| Close segment | desktopd | Close it on the next acknowledged selection of another view (Home, Overview and the Map close it), an unknown outcome, WM disconnect or desktopd shutdown. Split a segment at local midnight. Drop segments under 5 s. |
| Idle | WM → desktopd | After 5 min without key, pointer or scroll input the WM sends `idle <seconds since the last input>` once; the next input sends `active 1`. desktopd ends the open segment at the last input and starts a new one for the current view when input returns. No input data crosses the socket. A locked screen has no input, so it stops counting the same way. |
| Persist | desktopd | Append closed segments to `$XDG_STATE_HOME/mothership-wm/missions/YYYY-MM-DD.jsonl`. Checkpoint the open segment to `open.json` every 30 s; the next start closes it at that checkpoint. Delete days older than 30 days at start-up. A write failure keeps the segment in memory and is logged once. |
| Read | Panel → desktopd | [`missions.get`](../../architecture/desktopd-client-ipc.md) returns a day as lanes and `[lane, start, end]` triples plus the open segment. A day too large for one packet joins short gaps within a lane. The overlay extends the live bar locally; it does not poll. Map Detail totals refresh when the acknowledged view changes. |
| Show | Panel | **L** on focused blank Home or Slash `missions` opens the analog Mission Log above the dock. One lane per Node, longest total first: a diamond (hollow for the Default Node) and name, a 24-hour axis with ticks every three hours, cyan bars, a bright cap on the live segment and a total. Deleted Nodes keep their lane in muted ink as **Name (deleted)**. A/D or Left/Right changes day within 30 days; Esc, L or Q closes; a click outside dismisses it. The Home keyboard guide adds **L** and hides while the log is open. |
| Detail | Panel | Center Map [Node Detail](../system/center-map-ui.md) adds a **Today** row with that Node's total, including the open visit, when it is above zero. |
| Clear | Panel → desktopd | **Clear history** arms on the first press (**Press again to clear**, 4 s) and `missions.clear` on the second deletes every stored day. The open segment restarts now. |

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

## Contract

- New durable desktopd state under `missions/` (only Node names and Unix
  times) and the `missions.get` and `missions.clear` methods.
- Home keyboard guide gains **L**; Slash gains `missions`.
- Stored as JSON Lines, one file per local day, without SQLite: a day is
  the only query and the format keeps the module dependency-free.

## Decisions

- Lock is covered by idle: no input for 5 min ends the segment at the
  last input. A separate lock signal is not needed for counting.
