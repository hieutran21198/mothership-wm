# Window manager

Go owns startup and the parent selection; a cgo adapter uses the explicit
wlroots Wayland backend. The [first slice](../../docs/software-knowledges/goals/boot-nested-wayland-compositor.md)
is one nested output and one real xdg-shell client. Two-window focus/pan,
keyboard or pointer routing, the Panel, and desktopd are not part of this slice.

## Build and check

Run these from the repository root:

```sh
devenv shell -- task wm:test
devenv shell --profile wm-native -- task wm:build
```

The default check tests Go startup policy only. The native profile selects
wlroots 0.20.2 from the locked package set and uses the `wlroots-0.20` pkg-config
module. It also provides the cgo compiler, libwayland and Wayland protocol
headers. The native build produces `bin/mothership-wm`.

## Manual acceptance

Only run this in a session approved for the test. For Niri acceptance, start
from a terminal in Niri and leave it running. Run `bin/mothership-wm` to open
the nested output. It prints a child `WAYLAND_DISPLAY` after submitting the
initial output buffer. A different parent can be named with
`--parent <socket-name-or-absolute-path>`; the backend never switches to
DRM, X11, or headless mode.

In a separate terminal, start one disposable native xdg-shell client with
`env -u WAYLAND_SOCKET -u DISPLAY WAYLAND_DISPLAY=<reported-child-socket> <client>`.
This avoids an inherited Wayland fd or X11 fallback. Do not export the child
socket to the parent session. Verify that the client's content is visible
inside the nested output, then stop Mothership with Ctrl+C and verify that
the child socket disappears and the same parent is still usable. Follow the
[Flow scenarios](../../docs/software-knowledges/flows/boot-nested-wayland-compositor.md)
for the failure check. A successful CLI test is not native acceptance.
