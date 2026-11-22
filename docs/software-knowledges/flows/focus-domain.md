# Focus a Domain

**Design accepted.** Supports [Switch the main view between Domains](../goals/focus-domain.md).
The caller is a local test command, not Panel. [Domain](../artifacts/domain.md)
owns membership; WM owns the render/input transition.

| Step | Executor | Operation and outcome |
| --- | --- | --- |
| Discover | WM → desktopd | Register the empty initial session before serving clients, then send ordered create/map/unmap/close updates on the same local connection. |
| Assign | Developer → desktopd | Assign a live session-scoped Window ID to one Domain. Reject stale or unknown refs rather than matching by app. Assignment alone does not switch the view; use FocusDomain to apply it. |
| Resolve | Developer → desktopd | `FocusDomain` resolves the destination's members and sends that set to WM. An unknown Domain or unavailable WM leaves the view unchanged. |
| Apply | WM | In one local event-loop transition disable non-members for input and rendering, show still-mapped members, restore the latest eligible target from destination focus history or clear focus. Acknowledge only after applying. |

When switching, WM invalidates old targets or grabs and does not replay held
input into the destination. After application, newly delivered input cannot
reach hidden windows. No pixel buffers or pointer events go over the control
socket. If desktopd disconnects, input to the last applied visible set still
works; automatic reconnect is outside this slice. Standalone WM behavior is
unchanged until a Domain view is applied.

### F-WM-DOMAIN-1

**Given** three live native windows including two with the same app ID,
**when** A1 and A2 are assigned to Domain A and B1 to B, **then** each
unique window ref has one membership. Same app ID does not transfer
ownership. Covers G-WM-DOMAIN-1 and DOM-1.

### F-WM-DOMAIN-2

**Given** A1 and then A2 received focus in A, **when** switching to B and
back to A, **then** only the destination is visible, A1/A2 keep their IDs
and in-window state, and A2 receives the next key without a click. Use at
most two visible slots in each test Domain. Covers G-WM-DOMAIN-2.

### F-WM-DOMAIN-3

**Given** B1 has no saved focus and A2 was focused, **when** switching to
B, **then** keys do not go to A or B1 until B1 is clicked, and hit-testing
cannot reach A. If B1 then unmaps while A still has live windows, selecting
B shows an empty view and clears keyboard focus; it never falls back to A.
B1 stays assigned and a same-ID remap makes it eligible again. After B1 is
destroyed, empty B has no pointer or keyboard target. Compare event counters,
not just visuals. Covers G-WM-DOMAIN-3 and DOM-2.

### F-WM-DOMAIN-4

**Given** A2 was last focused and A1 is still eligible, **when** A2 is
destroyed while B is active and a new A2′ opens, **then** A2 is removed,
A2′ starts with a new unassigned ref, and returning to A restores A1. A
same-ID remap after unmap retains membership but only becomes eligible
when mapped again. Covers G-WM-DOMAIN-4 and DOM-2.

Use a private local control socket. If the WM connection ends after a command
was sent but before acknowledgement, return an unknown outcome rather than
claiming that the view did not change.
