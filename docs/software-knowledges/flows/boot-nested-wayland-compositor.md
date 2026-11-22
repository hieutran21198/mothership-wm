# Boot the WM inside a Wayland session

**Draft for owner review.** Supports [Open a nested Wayland compositor](../goals/boot-nested-wayland-compositor.md).
The developer starts the WM as an ordinary nested app under a running Niri
session and runs a separate real xdg-shell test client. The parent's active
session and configuration stay in place.

| Step | Executor | Operation and outcome |
| --- | --- | --- |
| Start | Developer → WM | Select the nested Wayland backend and supply a usable parent display. Fail if it cannot be used; do not infer another backend. |
| Listen | WM | Create one nested output and a private local server socket distinct from the parent. Report the child socket only after both are ready. |
| Map | Developer → client → WM | Point only the test client at the child socket. After the WM sends the xdg-shell configure, the client acknowledges it and commits a buffer. The WM maps and presents that buffer on the nested output. |
| Stop | Developer → WM | End the test run normally. Release the child connections, socket, and nested output without stopping or reconfiguring the parent. |

State is local to the WM; there is no database transaction or cross-service
contract. If startup stops partway through, release resources already acquired.
Never fall back to a direct DRM/seat backend, X11, or headless mode. A detached
client that never maps is not startup acceptance.

### F-WM-BOOT-1

**Given** a running Niri parent and a compatible nested backend, **when** the
developer starts the WM and connects an xdg-shell client only to its new socket,
**then** the parent and child sockets differ and the test client's content
appears inside the nested output rather than as a separate parent window.
Covers G-WM-BOOT-1 and G-WM-BOOT-2.

### F-WM-BOOT-2

**Given** the nested output is displaying the client, **when** the developer
stops the WM, **then** the child socket is removed, the nested output closes,
and the same parent remains usable. Covers G-WM-BOOT-3.

### F-WM-BOOT-3

**Given** no usable parent display, an incompatible nested backend, or an
output that cannot initialize, **when** the developer starts the WM,
**then** it exits nonzero with the failed prerequisite and leaves no child
socket or output. It does not attempt to take over the desktop.
Covers G-WM-BOOT-4.

## Build prerequisite to confirm

Before native implementation, verify the Go pin and cgo compiler, select a
compatible wlroots release in the project shell, and build against that release's
versioned pkg-config module, matching headers and libraries, and required
libwayland development libraries. If the build generates protocol code, also
use the shell's Wayland scanner and stable xdg-shell XML. The repository does
not yet pin a wlroots ABI; confirm the Wayland backend and its API against
the selected release before writing the binding. Do not add a runtime
autodetection fallback to compensate for missing or mismatched packages.
