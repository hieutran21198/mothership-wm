# Boot the WM inside a Wayland session

**Accepted scope: one nested client on COSMIC.** Supports [Open a nested Wayland compositor](../goals/boot-nested-wayland-compositor.md).
The developer starts the WM as an ordinary nested app under a running COSMIC
session and runs a separate real xdg-shell test client. The parent's active
session and configuration stay in place.

| Step | Executor | Operation and outcome |
| --- | --- | --- |
| Start | Developer → WM | Select the nested Wayland backend and supply a usable parent display. Fail if it cannot be used; do not infer another backend. |
| Listen | WM | Create one nested output and a private local server socket distinct from the parent. Report the child socket only after both are ready. |
| Map | Developer → client → WM | Point only the test client at the child socket. After the WM sends the xdg-shell configure, the client acknowledges it and commits a buffer. The WM maps and presents that buffer on the nested output. |
| Stop | Developer → WM | End the test run normally. Release the child connections, socket, and nested output without stopping or reconfiguring the parent. |

State is local to the WM; there is no database transaction or cross-service
contract. Follow the [Desktop Session rules](../artifacts/desktop-session.md) for
ownership, readiness and teardown. A child socket or frame callback alone
does not prove the client's pixels appeared in the parent window.

### F-WM-BOOT-1

**Given** a running COSMIC parent and a compatible nested backend, **when** the
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

## Verification

The COSMIC run checked F-WM-BOOT-1 and F-WM-BOOT-2 with one disposable native
client. A capture of only the nested window matched its four-color marker;
after teardown the child socket was gone and the same parent answered a
Wayland roundtrip. The isolated startup-cancel test covered a stalled Unix
peer. F-WM-BOOT-3 states the expected failure behavior, not a claim that
every unavailable backend or output failure was exercised.

See [WM build and test instructions](../../../apps/wm/README.md#build-and-check)
for the current pinned native environment.
