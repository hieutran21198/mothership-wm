# Window manager

Go owns startup and the parent selection; a cgo adapter uses the explicit
wlroots Wayland backend. The [first slice](../../docs/software-knowledges/goals/boot-nested-wayland-compositor.md)
is one nested output and one xdg-shell client. The [focus slice](../../docs/software-knowledges/goals/focus-nested-windows.md)
adds two fixed side-by-side slots and one local human seat. Camera movement,
general layout and the Panel are outside this slice. Start without desktopd
for standalone two-window behavior. Use [desktopd](../../services/desktopd/README.md)
with `--desktopd` for Domain views.

## Build and check

Run these from the repository root:

```sh
devenv shell -- task wm:test
devenv shell --profile wm-native -- task wm:build
```

The default check tests Go startup policy only. The native profile selects
wlroots 0.20.2 from the locked package set and uses the `wlroots-0.20` pkg-config
module. It also provides the cgo compiler, libwayland and Wayland protocol
headers. The native task generates private parent bindings from those pinned
protocols and builds `bin/mothership-wm`.

The parent window keeps size and input in logical coordinates. Where the
parent offers fractional-scale and viewporter, the WM uses its preferred
scale for physical buffers and informs nested clients via Wayland so they
can draw at that resolution. Without both protocols, the nested output
keeps its 1× fallback. No host scale setting is modified.

## Manual acceptance

Only run this in a session approved for the test. Start from a terminal in
the existing COSMIC session and leave that session running. Run
`bin/mothership-wm` to open the nested output. It prints a child `WAYLAND_DISPLAY` after submitting the
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

## Two-window focus check

Use the same child socket command to start two separate disposable instances of
one native Wayland app that visibly responds to typing. Do not use an existing
single-instance app that forwards requests to its already-running process.
The WM reports `WINDOW_MAPPED=<id> SLOT=<slot>` in the launch terminal. Check
two distinct IDs and two side-by-side windows. Click A, type harmless text,
then click B and do the same. Close B while focused and check that the next
key goes to A. Keep the host shell and clipboard out of the test.
