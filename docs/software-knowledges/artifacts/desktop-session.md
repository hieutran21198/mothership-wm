# Desktop Session

Serving part: [WM](../../../apps/wm/README.md). A Desktop Session is the
lifetime of one nested WM server. It owns its client-side connection to the
parent, its nested output, its own server socket and the clients connected
there. It does not own the parent session, parent socket, or host settings.
Panel state, task coordination, flow membership, and agent access are outside
this artifact.

| Rule | Invariant or allowed change |
| --- | --- |
| DS-1 | Startup selects a usable Wayland parent explicitly. The parent socket and nested server socket are distinct. A failed prerequisite does not trigger a fallback to direct DRM/seat, X11 or headless mode. |
| DS-2 | The WM reports `WAYLAND_DISPLAY` only once its nested output has submitted the initial buffer and its child socket is bound. This is server readiness, not proof that a client mapped, rendered or appeared on the parent. |
| DS-3 | A canceled startup must not announce readiness if cancellation wins before serving. It can interrupt only its own private connection to the parent to unblock startup, never the parent's listener. Once serving, stop goes through the WM event loop for orderly teardown. |
| DS-4 | Failure or shutdown releases resources the WM acquired. Successful teardown disconnects its clients, removes its server socket and closes its output. The parent is neither stopped nor reconfigured. |

No new service API is defined here. The Wayland client/parent interactions and
checks belong to the [nested boot Flow](../flows/boot-nested-wayland-compositor.md).
COSMIC rendering and teardown were verified. Startup cancellation was verified
separately against a disposable unresponsive Unix peer, not a real hung
desktop.
