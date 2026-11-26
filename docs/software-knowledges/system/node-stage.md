# Node Stage layout

Flow stays the default layout. In a named Node, open Node Panel Settings
and choose **Layout → Stage** or **Flow**. The choice takes effect
immediately and is remembered for the Node in this session. Switching does
not reassign or restart any window. Keep the selected window when it is
still eligible. Returning to Flow restores its orientation, order, and
window sizes.

- Stage shows one focused window, on both wide and narrow outputs. It
  fills the work area above the Node Panel, inside the usual frame and
  content padding. There is no companion pane or Stage pin button.
- The tray and its overflow grid contain only windows that are off Stage.
  The active window has no duplicate thumbnail. All windows still take
  part in Alt+Tab.
- When focus changes, the old window scales down to its slot in the tray.
  Then the selected window leaves its slot and scales up to fill Stage.
  Ease both phases in and out. Retain the painted buffers for the moving
  view; do not request new client sizes or warp the image. The outgoing
  view does not receive input. Rapid switches change the destination
  without queuing each selection. With motion disabled, swap immediately.
- Holding Alt previews off-Stage windows from their tray slots. Cycling
  back to the active window does not draw a second copy. Releasing Alt
  commits the switch; Escape cancels and restores the tray position.

Check on a wide output with at least three native windows. Switch using
the tray and Alt+Tab. Verify that no two application windows are visible
above the tray at once. Check that the active thumbnail is absent, the
scaling starts and ends at the corresponding slot, and the outgoing
window cannot receive input.
Repeat on a narrow output and with Motion Off. Close the focused window
and check that the next eligible window takes its place. Switch to Flow
and back without closing the other apps, and verify that a different Node
keeps its own layout.
