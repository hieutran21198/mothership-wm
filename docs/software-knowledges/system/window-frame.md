# Flow window frame

Use the approved analog **Open Corners** variant for individual Flow and
Stage frames. App content and the application glyph in the concept are
illustrative, not part of the compositor frame. The earlier
[Flight Deck concept](Mothership-Window-Frame-Flight-Deck.png) is not the
current visual target. See the [shell design system](shell-design.md)
for the analog material.

- Show four short 45° corner marks over a dark translucent bezel. For the
  focused app, brighten and thicken the corners and add short cyan dashes along the
  straight edges. Center the dashes on each edge and leave a gap at each
  corner arm. Focus keeps the frame geometry and app size fixed. Group
  members use shorter, quieter marks so the group shell stays legible.
- Left-align the title on the flat header. Ellipsize it before the action
  icons. Do not draw placeholder agent badges.
- Reuse the existing **Float**, **Maximize/Restore**, **Minimize** and
  **Close** actions where the current layout supports them. Keep the
  header controls flat and keep their hit areas clear of the app.
  Float changes only this window's placement. Maximize overrides placement
  until Restore. Close sends the native app close request. The WM owns
  chrome hit targets; app areas keep their ordinary pointer events.
- In a named Node or Default Node, **Win + Q** requests the same close
  for the keyboard-focused window. Do not force-kill the app, close its
  other windows, or handle the shortcut while an overlay owns focus.
  A held key sends one request.
- Keep four logical pixels of frame-owned padding between the chrome
  and client content on every edge. Use the resulting inner rectangle
  for the client's requested size, placement and clip, including during
  Maximize and Restore. The separator and controls never paint over
  the client area or intercept input inside it.
- Retain the frame separately from app pixels. Crop it with the workspace
  reveal; do not resize the app for animation frames. Geometry is logical
  and chrome is rasterized at the output scale. Client-drawn UI stays intact.

## Review

1. Capture at least two real native clients in one Flow Node, one focused
   and one unfocused. Switch focus and compare both frames at matched zoom.
   Verify that the brighter 45° corners and separated edge dashes follow
   the focused app. Check title position, flat controls and clearance at
   100% and 150%. In a group, check that the dashes identify the focused member.
2. Record each client's actual configure sizes. Scroll the strip and use
   Next or nested Alt+Tab; these actions may translate the strip or raise
   Float, but must not send new sizes or leak the shortcut to the app.
3. Float one window, move and resize it, attach or close another window,
   then return the float. Check live membership, its original order,
   absence of a slot gap, unwrapped axis, Maximize/Restore and Close.
4. Compare landscape, square and portrait with auto axis, plus a manual
   override. Leave and re-enter the Node. Verify that hidden Nodes never
   receive input and report separately if the parent owns Alt+Tab.
5. In Flow, Stage and Default Node, use **Win + Q** with a disposable app
   focused. Verify that only that window receives the close request. With
   App Grid focused, the same chord must not close the app underneath.

See [Node layouts](../flows/view-node-layouts.md#f-view-2) for input routing
and compatibility behavior; record runtime evidence separately from the spec.


## App decorations

When a Wayland client negotiates xdg-decoration and Mothership draws the
outer Flow or Stage frame, select server-side decorations so the client
can remove its duplicate window border and window controls. If Mothership
does not draw a frame, keep client-side decorations. Do not crop client
pixels, alter application settings, or hide in-app controls and toolbars.
A client that does not negotiate decorations, or puts its title bar inside
its content, can still draw that header.

In a private test session, open a client that negotiates decorations and
verify that the outer controls work, while its own toolbar or menu still
does. Verify the no-frame fallback and an application that draws its own
header without protocol negotiation.

## Default Node

The internal [Default Node](../flows/use-home-inbox.md) uses the same
Flow and Stage frames as named Nodes. It retains Inbox membership and
starts in Flow. Its Layout row, focus controls and maximize/restore rules
are the same as a named Node in the same layout. Review both projections
at 100% and 150%.
