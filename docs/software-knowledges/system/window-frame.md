# Flow window frame

Compare the live build against the approved [Window Frame reference](window-frame-reference.png).
Read the original pixels and separate focus styling from the reference's
illustrative agent and app content.

- In Flow, the WM frames the actual app buffer. The frame has two parallel
  contours and straight 45° cuts. Focus changes ink and contrast, not the size
  or position of the frame, the app or its controls.
- Put the title above the rail, to the left after the chamfer when there is
  no real agent group. Center it in the available gap only when a future
  integration supplies a real group. V1 draws no agent badges or placeholders.
- Recess **Float**, **Maximize/Restore** and **Close** in a raised pocket at the
  right with even internal clearances. Inset the whole pocket so a visible
  horizontal run separates its outer shoulder from the frame's main chamfer.
  Keep a visible painted gap between the inner pocket outline and all
  three button outlines, including focused and active states. Leave a
  second clear gap below the complete buttons, before the continuous
  rail-to-app seam. Paint stays within the chrome without shrinking hit targets
  or overlaying app pixels. Float moves only this window in
  or out of the strip. Maximize is explicit and overrides placement until Restore.
  Close is the native app close request, not process termination. The WM
  owns frame hit targets; app areas keep their ordinary pointer events.
- Retain the frame separately from app pixels. Crop it with the workspace
  reveal; do not resize the app for animation frames. Geometry is logical
  and chrome is rasterized at the output scale. Client-drawn UI stays intact.

## Review

1. Capture at least two real native clients in one Flow Node, one focused
   and one not. Compare the same frame corners and right pocket at matched
   zoom, verify title position and measure even spacing and parallel cuts.
2. Record each client's actual configure sizes. Scroll the strip and use
   Next or nested Alt+Tab; these actions may translate the strip or raise
   Float, but must not send new sizes or leak the shortcut to the app.
3. Float one window, move and resize it, attach or close another window,
   then return the float. Check live membership, its original order,
   absence of a slot gap, unwrapped axis, Maximize/Restore and Close.
4. Compare landscape, square and portrait with auto axis, plus a manual
   override. Leave and re-enter the Node. Verify that hidden Nodes never
   receive input and report separately if the parent owns Alt+Tab.

See [Node layouts](../flows/view-node-layouts.md#f-view-2) for input routing
and compatibility behavior; record runtime evidence separately from the spec.
