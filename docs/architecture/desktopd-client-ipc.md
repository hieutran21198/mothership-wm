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
| `subscribe` | `{}` | Initial snapshot, then updates on this connection |
| `create` | `{"domain":"Dev","description":"Backend Platform"}` (description optional) | `{}` |
| `assign` | `{"session":"0123456789abcdef0123456789abcdef","window_id":7,"domain":"Dev"}` | `{}` |
| `focus` | `{"domain":"Dev"}` | `{}` after WM acknowledgement |
| `home` | `{}` | `{}` after WM acknowledgement |
| `overview` | `{}` | `{}` after clearing the app projection in WM |
| `map.show` / `map.hide` | `{}` | `{}` after updating the HUD visibility preference only |
| `layout` | `{"domain":"Dev","mode":"float"}` | `{}` after applying the mode in the currently selected Node |
| `stack.next` | `{"domain":"Dev"}` | `{}` after advancing only the current Node's Stack |

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
`created_at` and optional `last_active`. Times are Unix seconds. The
selection has `state`
(`not_selected`, `acknowledged`, or `unknown`) and includes `domain`
only for an acknowledged named Domain. Confirmed Home is
`{"state":"acknowledged","view":"home"}`. Confirmed Overview has the same shape with `"view":"overview"` and no
Domain or mode. Named selection from a current WM adds `mode` (`tiling`,
`float` or `stack`). Mode changes use the existing selection event; Next
does not create a false selection event. A mode command names the Node it
intends to change; a stale target is rejected. Unknown and not selected
retain their existing shapes. Derive Inbox from mapped windows
without a `domain`; any beyond the first two by ID wait for a Home slot,
but remain in the catalog and can be assigned directly.

The subscription emits one snapshot response and then objects of the form
`{"revision":8,"event":"domain.created","body":{"domain":"Dev","created_at":1795500000}}`.

| Event | Body and client action |
| --- | --- |
| `domain.created` | Node profile; add that name and metadata. |
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
