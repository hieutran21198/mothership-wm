# Stream Domain state to local clients

**Draft for owner review.** This Goal does not require Panel or a GUI.

A developer can use a local test client to observe Domain state and submit
commands without polling or reading WM internals.

| Criterion | Observable success | Proven by |
| --- | --- | --- |
| G-DESKTOPD-STREAM-1 | A client gets one initial snapshot, then ordered updates without missing a change at subscription time. | [F-DESKTOPD-STREAM-1](../flows/stream-desktopd-state.md#f-desktopd-stream-1) |
| G-DESKTOPD-STREAM-2 | Requests return responses correlated by ID. A Domain selection is confirmed only when WM acknowledges it. | [F-DESKTOPD-STREAM-2](../flows/stream-desktopd-state.md#f-desktopd-stream-2) |
| G-DESKTOPD-STREAM-3 | A slow or disconnected client does not block commands or WM lifecycle updates and can recover with a fresh snapshot. | [F-DESKTOPD-STREAM-3](../flows/stream-desktopd-state.md#f-desktopd-stream-3) |

Builds on [switching Domain views](focus-domain.md). No Panel, toolkit,
public API, persistence or replay log.
