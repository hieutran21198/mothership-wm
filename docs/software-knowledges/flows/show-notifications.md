# Read notifications by Node

## Ingress

The Panel service receives Mothership notices and direct desktop
notifications from owned apps on one private D-Bus for the nested session.
It never replaces the host's notification service or monitors the host bus.
If the private service cannot start, owned launches retain their unavailable
bus and internal Mothership notices still work. Portal forwarding and app
notification actions are not supported in this slice.

Mothership notices have an explicit source. For app notices, resolve the
credential PID against the current WM session's mapped window list. Several
windows of one PID must agree on the Node. A mapped window with no named
Node belongs to Default Node. Missing or conflicting matches use **From the
Void**.
Do not use focus or the displayed app name as evidence. Store the source
when the notification first arrives; moving a window does not relabel old
entries. Replacements for the same live sender/ID update the existing entry.

## Reading

- **Show toasts** in [Home Settings](../system/home-panel.md) is
  **Current Node** by default. **All Nodes** also shows notices from other
  Nodes, Home and From the Void. **Off** never shows a toast. Save stores
  `home_panel.show_toasts` as `current_node`, `all_nodes` or `off`.
- The top-right toast uses local wallpaper blur and thin analog corner marks,
  and shows **Node · App** or **Home · System** above the title. It does not
  acquire keyboard focus. Only its dismiss control joins the input region.
  Its footprint is 336 × 104 logical px, reduced in width only to fit
  the viewport. The source and title each use one line; the body uses
  at most two. End-ellipsize long text. The log wraps descriptions and
  collapses them to three lines until expanded.
- Toggle the analog Notification Log from the rightmost physical dock bell
  or **N** on focused blank Home. **Q**, **N**, or **Esc** closes it;
  show these three keycaps in that order in the log footer without captions.
  Clicking outside the log dismisses it without activating a control
  behind it. The separate toast dismiss button keeps its own action.
  Align the panel's horizontal center to the bell where the viewport allows,
  clamping to keep it readable. Leave a 16 logical px gap above the dock.
  Fade the log in over 180 ms and out over 140 ms like the
  [App Grid](../system/app-grid.md), without sliding or scaling it. Release
  its input at the start of the exit; a fast reopen starts from the current
  opacity. Skip the fade when animations are off or another shell view or
  Node change takes over.
  **All** shows separate groups, newest first in each group. Filter by Node,
  Default Node, Home or From the Void on Home. Inside a Node, the bell or
  **Meta + N** opens the same log locked to that Node without a filter
  dropdown, including in Default Node. **Meta + N** takes precedence over
  a custom shortcut on the same chord inside a Node. **Mark all read**
  clears unread indicators only in the displayed scope without deleting
  rows. Closing the toast or log is not a read.
  When both are open, each close button dismisses only its own surface.
  **Clear** immediately removes notifications in the displayed scope, with
  no confirmation. With **All** selected, it removes the entire history.
  Keep other groups and their read state intact. Disable Clear for an empty view.
  Show **No notifications yet.** for an empty log, or **No notifications
  for this Node.** when the selected group is empty.
- Open with focus on the first visible record. **W** or **A** moves to the
  previous record and **S** or **D** to the next, in visible order, without
  wrapping at the ends. Scroll to keep the focused record in view. Use a
  narrow dark scrollbar track and a restrained cyan thumb with brighter
  hover and drag states. Show it only when the list overflows. Wheel and
  touchpad scrolling stay available.
- **Space** toggles the focused description. **Enter** marks the focused
  record read. **Backspace** deletes it and moves focus to the next visible
  record or, at the end, the previous one. **Shift + Enter** marks all
  records in the displayed scope read. **Shift + Backspace** directly deletes
  all records in that scope. Home with **All** selected includes every group.
  Handle these keys only while the log owns keyboard focus. Each action
  runs once per press. Show the shortcuts in the log footer. On deletion
  failure, report the error and keep the records.
  Use 10 logical px of vertical padding per side, 6 px between entries,
  and 16 px between Node groups. Place the unread marker and title on
  the first line. Keep the timestamp and source icon on that same line,
  with the icon farthest right. The body starts under the title and can
  use the remaining width below the timestamp and icon. Hovering a
  row adds four analog corners, smaller and thinner than the outer log
  frame, and a darker local frosted layer behind
  its text. This visual state does not open, select, or mark an entry read.
- Keep the latest 200 entries or the file-size bound, whichever is smaller,
  in the user's private local state file. No notification text is sent to
  desktopd. If the state file cannot be written, the current in-memory log
  remains usable and the shell logs the write error.

## Map feedback

On opening Center Map, after the opening radar, one short lavender pulse
per newly notifying visible Node travels toward Mothership. The head is
brighter than its fading, gently bent tail. A tiny ring at Mothership fades
on arrival; the Node badge remains until read. Send only one packet at a
time. Do not move the camera, recolor the cyan route, replay old entries,
or draw over the minimap. With motion off, show only the badge.
