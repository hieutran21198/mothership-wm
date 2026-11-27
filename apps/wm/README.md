# Window manager

Go owns startup and explicit backend selection. The default cgo path uses
the wlroots nested Wayland backend. The opt-in `--native` path uses the
local DRM and input seat and currently publishes one output. The [first slice](../../docs/software-knowledges/goals/boot-nested-wayland-compositor.md)
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

## Presentation timing experiments

The compositor publishes `wp_presentation` version 2. Set
`MOTHERSHIP_EXPERIMENT_PRESENTATION=0` before launch for a baseline
without presentation feedback.

## Explicit sync

The compositor automatically publishes `wp_linux_drm_syncobj_manager_v1`
when the renderer has a DRM fd and both the renderer and backend support
timelines. Check the startup log and the client registry to confirm support.

With explicit sync, the normal window surface uses wlroots'
acquire and release points. The strip, overflow picker, and keyboard
projection share a compositor-owned GPU thumbnail for each visible window.
Each copy waits on the client's acquire point and registers GPU completion
with its release point. The cache is bounded to 640 × 400 pixels per image
and refreshed when the source contents or viewport changes. If a safe copy
cannot be made, the preview falls back instead of sampling the client buffer.
Stage scale transitions and Map video frost still use their fallback for
explicit-sync sources. The Node App Grid and Notification Log can sample
explicit-sync windows through the bounded [scene-glass capture](../../docs/architecture/shell-scene-glass.md).

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

### Shell popup regression check

In a disposable full Mothership session, open Home Settings with comma.
Pause the pointer over **Panel width**; no tooltip should appear. Click **+**,
select **All Nodes** under **Show toasts**, and save. Reopen Settings and
verify that both changes persisted. Open the Notification Log and use its
Node filter to exercise a real layer popup. Repeat at 100% and 150%.
For a freeze, launch the test session with `WAYLAND_DEBUG=client`.
Each command-center layer popup must receive `xdg_popup.configure`
after its initial commit. A button event in the trace alone does not
prove that GTK processed it.

### App file chooser check

The session launcher finds `xdg-desktop-portal` and its GTK backend through
their installed D-Bus service files under `XDG_DATA_DIRS`. It uses `gdbus` to
wait for them on the private application bus. Use the
[file chooser scenarios](../../docs/software-knowledges/flows/open-app-file-dialog.md)
to verify dialog placement and focus with real clients.

## Flow and window chrome

Managed Nodes default to Flow. Check the [layout and frame behavior](../../docs/software-knowledges/flows/view-node-layouts.md#f-view-2)
and the [native visual review checklist](../../docs/software-knowledges/system/window-frame.md#review).
For a manual axis, from the nested runtime use
`bin/mothership-desktopd layout Dev flow-vertical`, replacing `Dev` with the
active Node. Use `flow-horizontal` or `flow` for horizontal or automatic.
If COSMIC handles Alt+Tab before the nested output can see it, use **Next
window** from Command Center or `bin/mothership-desktopd next Dev`.

Win + top-row 1–9 routes to the displayed Node shortcut while the nested
output has keyboard focus. COSMIC also binds these chords to workspaces.
A chord reserved by the parent cannot reach the nested WM; the nested
session does not change host key bindings.

## Two-window focus check

Use the same child socket command to start two separate disposable instances of
one native Wayland app that visibly responds to typing. Do not use an existing
single-instance app that forwards requests to its already-running process.
The WM reports `WINDOW_MAPPED=<id> SLOT=<slot>` in the launch terminal. Check
two distinct IDs and two side-by-side windows. Click A, type harmless text,
then click B and do the same. Close B while focused and check that the next
key goes to A. Keep the host shell and clipboard out of the test.
