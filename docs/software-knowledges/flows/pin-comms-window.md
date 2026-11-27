# Pin windows across Nodes

**Implemented** after design approval on 2026-11-27. Mothership Dev session
acceptance is pending.
The earlier single-pin behavior was accepted on candidate `build-F0Om9ovi`.

Supports [Keep windows in sight across Nodes](../goals/pin-comms-window.md).
The WM owns the ordered stack, keyed by session window ID. The Panel paints
the labels and logs new pins. Pin membership and the stack's corner last
for the session.

| Step | Executor | Operation and outcome |
| --- | --- | --- |
| Pin | WM | **Meta + P** in a named Node or the Default Node toggles only the focused app window. A new pin goes at the growing end of the stack. |
| Identify | WM | Each [window frame](../system/window-frame.md) keeps the amber pin mark while that window is pinned, including when another app has focus. |
| Announce | WM → Panel | On each addition, the Panel logs one Node · System **Window pinned** notice that names the app and title and explains how to unpin. The notice follows [Show toasts and Silent running](show-notifications.md). It can arrive while the pin is hidden in its source Node. Layout changes, label replays and unpinning are silent. |
| Show | WM | Stack up to three of the newest eligible previews that fit. Omit windows in the current Node and temporarily unmapped windows. Keep their order for when they return. With no eligible pins, the stack disappears. In Overview, Center Map and the screenshot tool, hide the stack. Draw ordinary shell overlays above it and give those overlays pointer priority. |
| Frame | WM | Draw each visible preview from the app's existing buffer, as in the [preview tray](../system/home-panel.md#node-panel). Continue frame callbacks only for pinned windows with visible previews whose app surfaces are hidden. Preserve client size. A missing buffer shows a neutral placeholder. |
| Label | Panel | Paint **Node · App — title** and **×** in each header or compact row. Keep the action visible and ellipsize the label as needed. The paint-only layer receives no input. |
| Overflow | WM | Show **+N more** when eligible pins exceed the live preview capacity. Open a bounded list of eligible pins at the same anchor in place of the live previews. Scroll to reach additional rows. **Show previews**, **Esc** or clicking outside dismisses the list. |
| Hover | WM | Fade a cyan contour in when the pointer enters a live card or compact row and out when it leaves. Use the preview tray's 165 ms easing and apply the contour immediately when system animations are disabled. Hover keeps the stack geometry and app focus fixed. |
| Activate | WM → Panel → desktopd → WM | A short click on a preview or compact row starts [Connect to a window](search-windows.md#connect-to-a-window). Switch to the window's current Node and focus that exact window. The click is consumed by the stack. The pin stays in the stack. |
| Move | WM | Drag a card header to move the whole stack. Movement starts past 8 logical px. Release snaps to the nearest of four corners. **Esc** cancels the drag. |
| Release | WM | Click **×** or press **Meta + P** on the pinned window to remove that pin. Destroying a window removes its pin. Ending the session clears every pin. |

Use the [shell's analog treatment and 45° chamfer recipe](../system/shell-design.md)
for a cool dark surface and one thin contour. At the default bottom-right
anchor, grow upward, 18 logical px from the side and 16 px above the rail.
Grow inward from the selected corner and keep clear of the rail and top-right
toast area. Target a 320 px column, 8 px gaps, 5 px cuts, 24 px preview headers
and 32 px compact rows. Center each image in its preview at the source
aspect with a maximum image height of 240 px. All dimensions are logical
pixels.

### F-PIN-1

Pin live windows in at least two Nodes, including a playing video. Work in
another Node and then go to blank Home. Check that the eligible thumbnails
are live, with the newest at the stack's growing end. Return to a source Node
and verify that only its cards disappear and that they return in order on
leaving. Compare client configure sizes and frame callbacks before pinning,
while the previews are shown and while the stack is hidden.

### F-PIN-2

In a stack containing two windows with the same app ID, click one preview
from another Node and type. The selected window receives the next key even
if its sibling was last focused. Move it to a different Node and activate
it from an overflow row. Verify that the destination follows the window
and that the pin remains. Type in an unrelated app while the previews update
to confirm that they leave keyboard focus with the current app.

### F-PIN-3

At 100% and 150% output scale, capture the full viewport and a crop of the
stack at the same zoom. With at least three eligible pins, add enough pins
to overflow and repeat on a shorter output. Check the 45° cuts, label
legibility, gaps and clearance from the rail and toast area. Scroll the
compact list, dismiss it with **Esc** and by clicking outside, and check
that windows listed only as metadata receive no extra frame callbacks.
Hover across cards, compact rows and gaps. Check that only the item under
the pointer lights up, that the contour fades on exit, and that the stack
stays still and typing still reaches the current app. Scroll under a
stationary pointer and check that the contour follows the visible row.
Repeat with system animations disabled and check that the contour changes
immediately.
Drag a header to all four corners and cancel one drag. Check that an
ordinary shell overlay above the stack receives its own clicks. Verify
that Overview, Center Map and screenshot mode hide the stack.

### F-PIN-4

Pin two framed windows in a Node and switch focus between them. The amber
stripes stay on both windows while the cyan focus dashes follow focus.
Check that each addition logs one **Window pinned** notice. Unpin one with
**Meta + P**, then unpin another from outside its Node with **×**. Only the
chosen pin disappears each time. The second action leaves the source
window open and focus where it was. Unpinning is silent. Close one pinned window
and verify that its card and overflow row are gone. Temporarily unmap and
remap another and check that it returns in order. Restart the session and
verify that the stack is empty.

## Contract

- Private shell protocol v30. `pin_stack_label` carries a session window ID,
  a label rectangle, title and app ID. A zero width removes its visible
  label. `pin_stack_control` carries the disclosure rectangle, overflow
  count and expanded state. A zero width hides the control.
- `pin_stack_added` carries the new session window ID, title and app ID.
  The WM emits it only when a window enters the stack. It can resend visible
  labels and the control independently of notices. The Panel paints on
  `mothership-pin-label`.
- Activation uses `pin_activate` and the existing `focus.window` operation.
  Window pixels and pointer handling stay in the WM.
