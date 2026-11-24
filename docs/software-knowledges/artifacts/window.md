# Window

Serving part: [WM](../../../apps/wm/README.md).
A Window is one live, addressable xdg-shell toplevel in a
[Desktop Session](desktop-session.md). Its identity is not the app's identity.
The artifact owns that identity and eligibility for input. It does not own
desktop session lifetime, flow membership, layout or camera policy.

| Rule | Invariant or allowed change |
| --- | --- |
| WIN-1 | WM assigns distinct session-scoped IDs to live windows. app ID, process ID and title are not window IDs. Closing a window removes it as an input target. |
| WIN-2 | A mapped window can receive pointer events when it is the hit target. Clicking a live target selects it for keyboard focus; keys are not broadcast to the other window. |
| WIN-3 | Closing or unmapping the focused target clears its focus before more keys are routed. With one eligible window left, that window receives focus; with none, focus stays clear. Ordering among multiple targets is outside this rule. |

The optional xdg-shell app ID is client-supplied display metadata. WM
publishes its initial value and changes; the Panel may use a known ID for
icon lookup. Missing or unusable app IDs never block a window or change
ownership. Closing the window removes the metadata with its ID.

For a Domain view, only mapped members of the current view are eligible.
The WM restores that view’s most recently focused eligible window, or clears
keyboard focus if none remains. See [Focus a Domain](../flows/focus-domain.md).
Focus is a local keyboard destination, not a grant of control. No public API
or cross-session persistence is specified. See the [focus Flow](../flows/focus-nested-windows.md)
for checks and limits.
