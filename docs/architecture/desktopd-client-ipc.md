# desktopd client IPC

The local client contract uses the existing private
`$XDG_RUNTIME_DIR/mothership-desktopd.sock` Unix sequenced-packet socket.
One UTF-8 JSON object per packet, up to 4096 bytes; no newline framing
or JSONL. The WM peer’s separate internal packets are unchanged. The
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
| `create` | `{"domain":"Dev"}` | `{}` |
| `assign` | `{"session":"0123456789abcdef0123456789abcdef","window_id":7,"domain":"Dev"}` | `{}` |
| `focus` | `{"domain":"Dev"}` | `{}` after WM acknowledgement |

For example, request `{"id":12,"method":"create","body":{"domain":"Dev"}}`
gets `{"id":12,"body":{}}` on success. Domain names are 1–32 ASCII
letters, digits, underscores or hyphens, as in the CLI. Creation is
idempotent for an existing name. No description or combined assign-on-create.

Malformed requests use `invalid_request` or `invalid_body`; an unknown
method uses `unknown_method`. State rejections include `wm_unavailable`,
`stale_session`, `unknown_window`, `unknown_domain`, and `domain_full`.
If the WM does not acknowledge after sending FocusDomain, return
`outcome_unknown`, not a claim that the view did not change. An oversized
snapshot or full subscription limit returns `snapshot_too_large` or
`subscription_limit`; the server does not send partial snapshots.

## Snapshot and event stream

The snapshot has `revision`, `wm_ready`, `domains`, `windows`,
`selection`, and `session` when WM is ready. Each window has `id`,
`mapped`, and `domain` when assigned. The selection has `state`
(`not_selected`, `acknowledged`, or `unknown`) and includes `domain`
only when acknowledged.

The subscription emits one snapshot response and then objects of the form
`{"revision":8,"event":"domain.created","body":{"domain":"Dev"}}`.

| Event | Body and client action |
| --- | --- |
| `domain.created` | `domain`; add that name. |
| `window.updated` | Complete window entry; replace by window ID, or insert it. |
| `window.closed` | `window_id`; remove that entry. |
| `selection.changed` | Complete selection; replace it. |
| `wm.changed` | `wm_ready`, optional `session`, and complete `selection`; clear prior window entries. |

Use a different connection for commands. Each update follows the previous
revision by one. On EOF or a gap, mark the local view stale and subscribe
again. Revisions are scoped to the running daemon; no update replay. The
daemon bounds each subscriber queue and drops a slow subscriber rather
than blocking a writer or losing an event silently.
