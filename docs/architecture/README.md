# Architecture

## Scope

Build an original Go WM and native shell. GTK4 and wlroots are system
libraries, not the ownership boundary for our application policy. Driftwm
is a reference, not a fork. No 3D space, browser adapter, external broker,
event sourcing, or one daemon per domain in the first milestone.

## Boundaries

| Part | Owns | Connects over |
| --- | --- | --- |
| WM | Live windows, input, focus, window geometry and output composition | Wayland to clients; a local control socket to desktopd |
| Panel | Dock, palette, Map camera and UI state | Wayland layer-shell for placement; local control to desktopd |
| desktopd | Domain names, live Window membership and resolution of Domain commands | Local control socket to WM; no Wayland dependency |

Commands ask for work. Events describe completed state changes. A reconnect
takes a snapshot before consuming updates. Window IDs are scoped to a WM
session; neither PID nor title is a durable key. Frame timing and pixel buffers stay on the drawing path; desktopd does not
receive pointer motion. See [Center Map rendering and input](center-map-rendering.md)
for the scoped Wayland wheel route. A panel or desktopd failure must
not stop basic window management.

## First milestones

1. Toolchain: enter the pinned devenv shell and run `task versions`.
2. Panel: prove dock/overlay placement and focus restoration on Niri.
3. Compositor: launch a nested session, open two real Wayland windows,
   focus and pan without the panel or desktopd.
4. Flow: assign two same-app windows to different flows and confirm moving
   one does not change membership. Test restart/reconnect separately.
5. Native runtime: only after nested acceptance, measure frame pacing in a
   separate native session; nested results do not prove native GPU behavior.
