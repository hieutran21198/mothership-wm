# Run silent in a Node

**Status:** design accepted 2026-11-27; not implemented.
Supports [Work in a Node without interruptions](../goals/run-silent.md).
Design: [Fleet Ops preview, scenes 1 and 2](../system/fleet-ops-preview.html).

Silent running is a Panel-owned per-Node preference. It changes
notification presentation and, optionally, playback mute. It never changes
membership, focus or view selection, and desktopd receives no notice text
([notification rule](show-notifications.md#reading)).

| Step | Executor | Operation and outcome |
| --- | --- | --- |
| Set | Panel | **Node Panel Settings → Silent running: Off / Notices / Notices + audio**. Save writes `nodes.<name>.silent_running` to the shared user config, or `default_node.silent_running` for the Default Node (a Node may be named `default`). The left-wing **Silent running** button toggles between Off and the last non-Off choice and saves immediately. A toggled button keeps the cyan edge from the [mechanical button rules](../system/shell-design.md#foundations). |
| Enter | Panel | When desktopd publishes an acknowledged selection of that Node, start holding. Record the hold start time. |
| Hold | Panel notification service | Store every arriving notice as usual with its own source. Suppress toasts from all sources, including the current Node and Home · System. The Map pulse queue keeps new entries for the next Map open. |
| Mute | Panel → PulseAudio | For **Notices + audio**, mute identifiable playback streams of apps in other Nodes and Inbox, using the [Node Volume](../system/home-panel.md#node-volume) scope rules. Remember exactly which streams this action muted. New streams from other Nodes that start during the hold are muted too. Shared or unidentifiable streams are left alone. |
| Leave | Panel | On any acknowledged selection of another view, or on session end, restore only streams this hold muted and still exist. Then post one digest toast **Silent running ended · N notices held** under the destination's toast setting if N > 0. The digest is not stored as a separate log entry. |

Failure and recovery: if `pactl` is unavailable, Notices still applies and
the Settings row shows audio as unavailable. If the Panel restarts during a
hold, it cannot know which streams it muted; it logs one System notice
asking the user to check app audio rather than unmuting guesses.

### F-SILENT-1

Set **Notices** on Node Research and enter it. Send a notification from an
app in Research, one from another Node, and a Home · System notice (for
example a failed reload). No toast appears. Open the log with **Meta + N**
and **All**: all three entries are present with their original sources and
unread markers. The Node Panel button shows the toggled cyan edge.

### F-SILENT-2

Play audio in Node Ops and in Research, then enable **Notices + audio** and
enter Research. Ops is muted and Research keeps playing. Pre-mute one Ops
app before entering. Leave for Home: the other Ops stream resumes and the
pre-muted app stays muted. Repeat with Node Volume's own Node mute active.
Neither toggle unmutes the other's streams.

### F-SILENT-3

Hold five notices, then press bare Meta. One digest toast reads **Silent
running ended · 5 notices held**. Hold none and leave: no digest. With
toasts **Off**, no digest appears; the five entries remain unread.

## Proposed contract changes

- Shared user config gains a sparse `nodes` object keyed by Node name and
  a `default_node` object. An unknown or deleted Node key is ignored. R on blank Home reloads it.
- Center Map marker paint gains the cloaked state
  ([Show ship status](show-ship-status.md)).

## Decisions

- Held notices keep their Map arrival pulse and badge for the next Map
  open, as with any other unseen notice.
