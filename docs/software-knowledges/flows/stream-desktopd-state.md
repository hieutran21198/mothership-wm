# Subscribe to desktopd state

**Draft for owner review.** Supports the [local client Goal](../goals/stream-desktopd-state.md).
A test client connects to a running desktopd over the private socket.
[Domain](../artifacts/domain.md#local-client-contract) owns the state;
[desktopd client IPC](../../architecture/desktopd-client-ipc.md) defines
the envelopes and bodies.

| Step | Executor | Operation and outcome |
| --- | --- | --- |
| Subscribe | Test client → desktopd | Send `{"id":1,"method":"subscribe","body":{}}` as one packet. desktopd captures the snapshot and registers the subscriber at one revision boundary. |
| Submit | CLI or test client → desktopd | Use a separate request connection. For example, `mothership-desktopd create Dev` submits a create request. desktopd validates and returns the matching response ID. |
| Publish | desktopd → Test client | Push subsequent revisions in order. Only acknowledge a new selected Domain after WM applies it; lifecycle and membership changes do not imply selection. |
| Recover | Test client → desktopd | On disconnection or a missing revision, mark the view stale and subscribe again. Replace the local view with the new snapshot; do not replay commands. |

### F-DESKTOPD-STREAM-1

**Given** a legitimate mutation races with subscribing, **when** the first
response and later events are read, **then** the change appears in the
snapshot or the next ordered update, not neither or both. Covers
G-DESKTOPD-STREAM-1.

### F-DESKTOPD-STREAM-2

**Given** a request ID and an existing Domain, **when** the client focuses
it, **then** no new selection is confirmed before WM acknowledges. The
response carries that ID. Lost acknowledgement produces an unknown
outcome, not success. Covers G-DESKTOPD-STREAM-2.

### F-DESKTOPD-STREAM-3

**Given** one subscriber stops consuming and a second stays responsive,
**when** the first queue fills during a WM lifecycle change, **then** the
second gets its update, commands remain usable, and the dropped subscriber
can reconnect for a fresh snapshot. Covers G-DESKTOPD-STREAM-3.
