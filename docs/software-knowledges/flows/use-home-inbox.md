# Open Home and use Inbox

Supports [Use Home to find unassigned windows](../goals/use-home-inbox.md).
Home is a view, not a Domain. [Domain](../artifacts/domain.md#home-and-inbox)
owns assignments; WM owns visible slots and device focus.

| Step | Executor | Operation and outcome |
| --- | --- | --- |
| Start | WM → desktopd | Enable Home in the managed session before accepting child apps. WM readiness confirms the initial Home view. Standalone WM remains unchanged. |
| Map | WM → desktopd | An unassigned mapped window is in Inbox. When Home is open, show up to two eligible windows in fixed slots by ascending Window ID. All eligible IDs remain listed and assignable. |
| Assign | Local client → desktopd → WM | Validate the live reference and assign it explicitly. Remove it from Inbox immediately when WM applies the membership change and fill a free Home slot. Leave any named view and focus unchanged. |
| Select | Local client → desktopd → WM | The separate Home command switches the projection without moving or restarting apps. WM restores the last eligible Home keyboard target, or clears it; only its acknowledgement confirms selection. |

### F-HOME-1

**Given** a managed session, **when** three unassigned windows map, **then**
Home is confirmed and the fixed slots show the first two by ID. The third
is still listed as mapped and unassigned; it enters a visible slot when one
of the earlier windows closes or leaves Inbox. Covers G-HOME-1.

### F-HOME-2

**Given** Home shows an unassigned window, **when** it is assigned to a
named Domain, **then** Home stays selected and the window leaves Inbox.
When a new unassigned window maps while an empty named Domain is shown,
it neither appears nor takes focus there. Opening Home reveals the new
window without an automatic assignment. A Domain literally named `Home`
is distinct from the Home view. Covers G-HOME-2.

### F-HOME-3

**Given** an eligible Home window last held focus, **when** the user leaves
Home and comes back, **then** the same window can receive keys without a new
click. If it no longer qualifies, focus is restored to the next eligible
remembered Home window or stays clear. No input falls back from a named
Domain into Home. If applying Home is not acknowledged, report the selection
outcome as unknown. Covers G-HOME-3.
