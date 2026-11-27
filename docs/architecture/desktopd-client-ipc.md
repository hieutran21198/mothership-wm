# desktopd client IPC

The local client contract uses the existing private
`$XDG_RUNTIME_DIR/mothership-desktopd.sock` Unix sequenced-packet socket.
One UTF-8 JSON object per packet, up to 16 KiB; no newline framing
or JSONL. The WM peer uses separate internal packets. The
[Domain artifact](../software-knowledges/artifacts/domain.md#local-client-contract)
owns behavior.

## Requests

Open one connection per request. Each request has a positive integer
`id`, a `method`, and an object `body`. The response echoes `id` and
has either a `body` or an `error` with `code` and `message`.

| Method | Request body | Response body |
| --- | --- | --- |
| `snapshot` | `{}` | Current snapshot |
| `shortcuts.get` | `{}` | Saved shortcut table |
| `shortcuts.set` | `{"close_window":"Q","app_grid":"A","node_settings":",","node_keys":"numbers"}` | `{}` after WM applies and desktopd saves the table; `node_keys` can also be `"function"` |
| `subscribe` | `{}` | Initial snapshot, then updates on this connection |
| `create` | `{"domain":"Dev","description":"Backend Platform"}` (description optional) | `{}` |
| `delete` | `{"domain":"Dev","force":true}` (`force` optional; false by default) | `{}` after removal; Force waits for WM to return live members to Inbox |
| `assign` | `{"session":"0123456789abcdef0123456789abcdef","window_id":7,"domain":"Dev"}` | `{}` |
| `unassign` | `{"session":"0123456789abcdef0123456789abcdef","window_id":7}` | `{}` after the WM applies the return to Inbox; already in Inbox is a no-op. The view does not change. |
| `loadout.set` | `{"domain":"Dev","entries":["com.mitchellh.ghostty.desktop"]}` | `{}` after the loadout file is committed; an empty list clears it. Up to eight desktop entry IDs; repeats allowed. Publishes `node.loadout` with the same body. See [Node loadout](../software-knowledges/flows/launch-node-loadout.md). |
| `missions.get` | `{"day":"2026-11-27"}` | `{"day":…,"lanes":[{"node":"Ops"},{"default_node":true}],"segments":[[0,start,end],…],"open":[0,start,0]}`; `merged` is set when short gaps were joined to fit one packet. See [Mission Log](../software-knowledges/flows/review-mission-log.md). |
| `missions.clear` | `{}` | `{}` after every stored day is deleted; an open segment restarts now. |
| `focus` | `{"domain":"Dev"}` | `{}` after WM acknowledgement |
| `focus.window` | `{"session":"0123456789abcdef0123456789abcdef","window_id":7}` | `{}` after the WM applies the window's current Node (or the Default Node for Inbox) with that window as the keyboard target. See [Connect to a window](../software-knowledges/flows/search-windows.md#connect-to-a-window). |
| `focus.prepare` | `{"domain":"Dev"}` | `{"ticket":42}` after WM preparation; no selection change |
| `focus.reveal` | `{"ticket":42}` | `{}` after the first clipped WM output state |
| `home` | `{}` | `{}` after WM acknowledgement of blank Home |
| `home.node` | `{}` | `{}` after WM applies the internal Default Node from Home |
| `overview` | `{}` | `{}` after clearing the app projection in WM |
| `map.show` / `map.hide` | `{}` | `{}` after updating the HUD visibility preference only |
| `layout` | `{"domain":"Dev","mode":"float"}` | `{}` after applying the mode in the currently selected Node. Use `"mode":"stage"` for Stage. With `"mode":"flow"`, optional `"restore_flow":true` restores the Node’s last acknowledged Flow orientation when leaving Stage. |
| `stack.next` | `{"domain":"Dev"}` | `{}` after advancing only the current Node's Flow or Stack |

For example, request `{"id":12,"method":"create","body":{"domain":"Dev"}}`
gets `{"id":12,"body":{}}` on success. Domain names are 1–32 ASCII
letters, digits, underscores or hyphens, as in the CLI. Creation is
idempotent for an existing name and does not alter its metadata. An optional
description is limited to 160 UTF-8 bytes of single-line plain text. No
combined assign-on-create.

Malformed requests use `invalid_request` or `invalid_body`; an unknown
method uses `unknown_method`. State rejections include `wm_unavailable`,
`stale_session`, `unknown_window`, `unknown_domain`, `domain_full`, and
`home_unavailable` for an older WM that does not support Home.
If the WM does not acknowledge after sending a view command, return
`outcome_unknown`, not a claim that the view did not change. If a committed
assignment cannot be confirmed by WM, return `assignment_view_unknown`;
refresh the snapshot to read current membership. An oversized
snapshot or full subscription limit returns `snapshot_too_large` or
`subscription_limit`; the server does not send partial snapshots.

## Snapshot and event stream

The snapshot has `revision`, `wm_ready`, `domains`, `windows`,
`selection`, and `session` when WM is ready. `map_visible` is omitted when
false. A native session starts with this preference enabled, but the HUD
draws only in Overview. It does not affect selection. Each window has `id`,
`mapped`, `domain` when assigned, and bounded `app_id` if WM has one.
`node_profiles` carries durable `domain`, optional `description`,
`created_at`, optional `last_active` and optional stable `shortcut` (1–9).
Times are Unix seconds. The
selection has `state`
(`not_selected`, `acknowledged`, or `unknown`) and includes `domain`
only for an acknowledged named Domain. Confirmed Home is
`{"state":"acknowledged","view":"home"}`. The internal Default Node is `{"state":"acknowledged","view":"home_node","mode":"flow"}`; it has no named Domain key or profile. Confirmed Overview has the same shape with `"view":"overview"` and no
Domain or mode. Named selection from a current WM adds `mode` (`flow`, `flow-horizontal`, `flow-vertical`, `tiling`,
`float` or `stack`). Mode changes use the existing selection event; Next
does not create a false selection event. A mode command names the Node it
intends to change; a stale target is rejected. Unknown and not selected
retain their existing shapes. Derive Inbox from mapped windows
without a `domain`; entries beyond the Default Node’s projection capacity
remain in the catalog and can be assigned directly.

The subscription emits one snapshot response and then objects of the form
`{"revision":8,"event":"domain.created","body":{"domain":"Dev","created_at":1795500000}}`.

| Event | Body and client action |
| --- | --- |
| `domain.created` | Node profile; add that name and metadata. |
| `domain.deleted` | `domain`; remove that name and profile. An acknowledged Force move publishes affected windows first. |
| `window.updated` | Complete window entry; replace by window ID, or insert it. |
| `window.closed` | `window_id`; remove that entry. |
| `selection.changed` | Complete selection; replace it. If it includes `last_active`, update that Node profile after the acknowledged entry. |
| `map.visibility.changed` | `visible`; update the paint-only HUD without changing selection. Repeated requests for the same state emit no event. |
| `wm.changed` | `wm_ready`, optional `session`, and complete `selection`; clear prior window entries. |

Use a different connection for commands. Each update follows the previous
revision by one. On EOF or a gap, mark the local view stale and subscribe
again. Revisions are scoped to the running daemon; no update replay. The
daemon bounds each subscriber queue and drops a slow subscriber rather
than blocking a writer or losing an event silently.


The matching WM's private text authoring accepts at most eight Window
ID/epoch pairs for a named Node; full-width decimal IDs fit within its
512-byte packet bound for both select and assignment reprojection. The WM
reports optional app metadata with `app WINDOW_ID APP_ID`, or
`app WINDOW_ID` to clear it. Metadata never changes membership.


Animated Map-only focus uses one private, single-use preparation ticket
bound to the current WM peer and acknowledged Map Overview. Regular focus,
Home, Overview or Map hide supersedes a prepared request. Reveal resolves
live membership again; the ticket does not store stale window IDs.
Read selection only from the normal snapshot/subscription, never from the
preparation response. A stale ticket returns `stale_connect`.

The matching WM advertises Inbox moves with `ready_return_inbox`. The
`inbox SERIAL ID ...` command clears the assigned flag for up to eight live
window IDs without opening a view; closed IDs are ignored. Wait for
`applied SERIAL` before removing the durable Node record.


A WM advertising `ready_shortcuts` also supports the earlier Inbox
actions. desktopd restores saved shortcuts on that connection. The
private `shortcuts ID CLOSE GRID SETTINGS NODE_FIRST` packet carries
Linux physical keycodes. WM changes all four bindings before returning
`applied ID`. Raw input and key repeat never cross this socket.


A WM advertising `ready_node_parity` supports all earlier shortcuts and
uses `home_node ID MODE` to enter Default Node. desktopd remembers Default
Node's mode in the live session like other Node views. `layout` or `next`
with an empty Domain targets Default Node only while it is the acknowledged
selection. It does not target blank Home, Overview or an inactive view.
Default Node accepts Flow and Stage modes; an older WM continues to receive
`home_node ID` and does not expose Default Node layout changes.
